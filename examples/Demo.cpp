/**
 * Copyright (c) 2010 Daniel Wiberg
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "SDL_keycode.h"
#include "SDL_pixels.h"
#include "SDL_video.h"
#include <SDL.h>

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <cstring>
#include <Log.h>
#include "CanvasApp.h"
#include "Script.h"
using namespace canvas;


static int mouseStateToJS(){
   SDL_PumpEvents();
   int mouseState = SDL_GetMouseState(NULL, NULL);
    int buttonsMask = 0;
    if (mouseState & SDL_BUTTON_LMASK) buttonsMask |= 1;   // 左键
    if (mouseState & SDL_BUTTON_RMASK) buttonsMask |= 2;   // 右键
    if (mouseState & SDL_BUTTON_MMASK) buttonsMask |= 4;   // 中键
    return buttonsMask;
}
static void SyncInnerOuter(SDL_Window* window,CanvasApp* app){
   // 2. 获取尺寸
   int innerW, innerH;
   SDL_GetWindowSize(window, &innerW, &innerH);
   
   int outerW, outerH;
   int top = 0, left = 0, bottom = 0, right = 0;
   
   // 【核心修改 2】: 修正外围尺寸的数学计算公式（内部尺寸 + 两边边框）
   if (SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right) == 0) {
      outerW = innerW + left + right;  // 宽度 = 客户区 + 左边框 + 右边框
      outerH = innerH + top + bottom;  // 高度 = 客户区 + 上边框(含标题栏) + 下边框
   }
   else {
      // 如果极个别系统依然获取失败，采用 inner 尺寸安全兜底
      outerW = innerW;
      outerH = innerH;
   }
   LOGI("[SyncInnerOuter] inner=%dx%d outer=%dx%d", innerW, innerH, outerW, outerH);
   app->setSize(innerW, innerH, outerW, outerH);
}
/// Map SDL keycode to a JS key string (returns a static string)
static std::string sdlKeyToJS(SDL_Keycode sym)
{
   switch (sym)
   {
      case SDLK_ESCAPE: return "Escape";
      case SDLK_RETURN: return "Enter";
      case SDLK_TAB: return "Tab";
      case SDLK_BACKSPACE: return "Backspace";
      case SDLK_DELETE: return "Delete";
      case SDLK_UP: return "ArrowUp";
      case SDLK_DOWN: return "ArrowDown";
      case SDLK_LEFT: return "ArrowLeft";
      case SDLK_RIGHT: return "ArrowRight";
      case SDLK_HOME: return "Home";
      case SDLK_END: return "End";
      case SDLK_PAGEUP: return "PageUp";
      case SDLK_PAGEDOWN: return "PageDown";
      case SDLK_LSHIFT: return "Shift";
      case SDLK_RSHIFT: return "Shift";
      case SDLK_LCTRL: return "Control";
      case SDLK_RCTRL: return "Control";
      case SDLK_LALT: return "Alt";
      case SDLK_RALT: return "Alt";
      case SDLK_SPACE: return " ";
      default:
      {
         if (sym >= 32 && sym <= 126)
            return std::string(1, (char)sym);
         return "Unidentified";
      }
   }
}

/// Map SDL mouse button to JS button number
static int sdlButtonToJS(Uint8 button)
{
   switch (button)
   {
      case SDL_BUTTON_LEFT: return 0;
      case SDL_BUTTON_MIDDLE: return 1;
      case SDL_BUTTON_RIGHT: return 2;
      default: return 0;
   }
}

/// Simple function that tries to locate the Demo.js file
std::string locateDemoJavaScript()
{
   std::string  paths[3] = {
      "examples/Demo.js",
      "../examples/Demo.js",
      "../../examples/Demo.js"
   };
   
   for (int i = 0; i < 3; ++i)
   {
      std::ifstream file(paths[i].c_str());
      if (file.is_open())
         return paths[i];
   }
   
   return "";
}

int main(int argc, char * argv[])
{
   bool running = true;
   bool minimized = false;
   bool useModule = false;
   std::string scriptFile;
   std::string entryDir = "";
   int winW = 1024, winH = 768;
   
   if (argc < 2)
   {
      std::cout << "Usage: demo [--module] [-i <image-path>] [-s <width> <height>] <javascript>" << std::endl;
      std::cout << "  --module   Load as ES module (supports import/export)" << std::endl;
      std::cout << "  -s W H     Set canvas/window size (default 800 500)" << std::endl;
      return 0;
   }
   
   // if (SDL_Init(SDL_INIT_VIDEO) < 0)
   // {
   //    std::cerr << "Could not initialize SDL: " << SDL_GetError() << std::endl;
   //    return 1;
   // }

   // Parse arguments
   for (int i = 1; i < argc; ++i)
   {
      std::string arg(argv[i]);
      if (arg == "--module")
      {
         useModule = true;
      }
      else if (arg == "-s" && (i + 2) < argc)
      {
         winW = std::atoi(argv[++i]);
         winH = std::atoi(argv[++i]);
      }
      else
      {
         scriptFile = arg;
      }
   }

   if (scriptFile.empty())
   {
      std::cerr << "Error: no script file specified" << std::endl;
      SDL_Quit();
      return 1;
   }

   // Compute entry directory for module resolution
   if (!scriptFile.empty()) {
      size_t slash = scriptFile.find_last_of("/\\");
      if (slash != std::string::npos){
            entryDir = scriptFile.substr(0, slash);
            scriptFile = scriptFile.substr(slash+1);
      }
   }

   // Create the window with OpenGL (SDL2)
   SDL_Window * window = SDL_CreateWindow("Canvas++ Demo",
      SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      winW, winH, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
   if (!window)
   {
      std::cerr << "Could not create window: " << SDL_GetError() << std::endl;
      SDL_Quit();
      return 1;
   }

   // Set GL attributes
   SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
   SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
   SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

   SDL_GLContext glContext = SDL_GL_CreateContext(window);
   if (!glContext)
   {
      std::cerr << "Could not create GL context: " << SDL_GetError() << std::endl;
      SDL_DestroyWindow(window);
      SDL_Quit();
      return 1;
   }
   SDL_GL_SetSwapInterval(1); // vsync

   // Create the canvas
   CanvasApp * app = new CanvasApp(window, glContext, winW, winH, winW, winH);
   
   // Enter the application loop

   SDL_ShowWindow(window);
   SyncInnerOuter(window, app);
   // app->RenderFrame();
      // Load script (module or global)
   if (useModule)
   {
      std::cout << "Loading ES module: " << scriptFile << " (entryDir: " << entryDir << ")" << std::endl;
      app->SetBaseDir(entryDir);
      app->runModule(scriptFile);
   }
   else
   {
      app->SetBaseDir(entryDir);
      app->runScript(scriptFile,1);
   }

   int lastMouseX = -1, lastMouseY = -1;
   bool mouseInside = false;
   Uint32 lastFpsTime = SDL_GetTicks();
   int frameCount = 0;
   int fps = 0;
   std::vector<Uint32> prevPixels(winW * winH, 0);
   // 渲染计时控制
   const Uint32 TARGET_FRAME_TIME = 1000 / 60; // 16.666 ms
   Uint32 lastRenderTime = SDL_GetTicks();
   while (running)
   {
      SDL_Event event;
      int hasEvent = 0;
      while (SDL_PollEvent(&event))
      {
         hasEvent= 1;
         switch (event.type)
         {
             case SDL_QUIT:
                running = false;
                break;
             case SDL_WINDOWEVENT:
                 if (event.window.event == SDL_WINDOWEVENT_RESIZED)
                 {
                    int newW = event.window.data1;
                    int newH = event.window.data2;
                    if (newW > 0 && newH > 0)
                    {
                       SyncInnerOuter(window, app);
                       winW = newW;
                       winH = newH;
                        prevPixels.assign(winW * winH, 0);
                    }
                 }
                 else if (event.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    minimized = true;
                    app->dispatchVisibilityChanged(true);
                 }
                 else if (event.window.event == SDL_WINDOWEVENT_RESTORED) {
                    minimized = false;
                    app->dispatchVisibilityChanged(false);
                 }
                 else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                    app->dispatchWindowFocusEvent(true);
                 else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                    app->dispatchWindowFocusEvent(false);
                 break;
              case SDL_MOUSEMOTION:{
               //  LOGI("[SDL] MOUSEMOTION x=%d y=%d winSize=%dx%d", event.motion.x, event.motion.y, winW, winH);
                // 检查鼠标是否在窗口内
                bool isInside = (event.motion.x >= 0 && event.motion.x < winW &&
                               event.motion.y >= 0 && event.motion.y < winH);
               
                // 处理进入/离开事件
                if (!mouseInside && isInside) {
                   LOGI("[SDL] mouseenter x=%d y=%d", event.motion.x, event.motion.y);
                   app->dispatchMouseEvent("mouseenter", event.motion.x, event.motion.y,0,0);
                   mouseInside = true;
                } else if (mouseInside && !isInside) {
                   LOGI("[SDL] mouseleave x=%d y=%d", event.motion.x, event.motion.y);
                   app->dispatchMouseEvent("mouseleave", event.motion.x, event.motion.y,0,0);
                   mouseInside = false;
                }
               // 更新最后有效坐标
               if (isInside) {
                  lastMouseX = event.motion.x;
                  lastMouseY = event.motion.y;
               }
               app->dispatchMouseEvent("mousemove", event.motion.x, event.motion.y,0,mouseStateToJS());
               }
               break;
             case SDL_MOUSEBUTTONDOWN:
                LOGI("[SDL] MOUSEBUTTONDOWN x=%d y=%d button=%d", event.button.x, event.button.y, event.button.button);
                app->dispatchMouseEvent("mousedown", event.button.x, event.button.y,
                   sdlButtonToJS(event.button.button),mouseStateToJS());
                break;
             case SDL_MOUSEBUTTONUP:
                LOGI("[SDL] MOUSEBUTTONUP x=%d y=%d button=%d", event.button.x, event.button.y, event.button.button);
                app->dispatchMouseEvent("mouseup", event.button.x, event.button.y,
                   sdlButtonToJS(event.button.button),mouseStateToJS());
                break;
            case SDL_MOUSEWHEEL:
               {
                  int x, y;
                  SDL_GetMouseState(&x, &y);
                  SDL_Keymod mod = SDL_GetModState();
                  app->dispatchWheelEvent("wheel", x, y,
                  event.wheel.y * -120,  // 标准化为典型delta值
                  (mod & KMOD_CTRL) != 0,
                     (mod & KMOD_SHIFT) != 0,
                     (mod & KMOD_ALT) != 0,
                  (mod & KMOD_GUI) != 0);
               }
               break;
            case SDL_KEYDOWN:
               {
                  std::string key = sdlKeyToJS(event.key.keysym.sym);
                  SDL_Keymod mod = SDL_GetModState();
                  if (key == "Escape")
                  {
                     running = false;
                     break;
                  }
                  app->dispatchKeyEvent("keydown", key.c_str(),
                     (mod & KMOD_CTRL) != 0,
                     (mod & KMOD_SHIFT) != 0,
                     (mod & KMOD_ALT) != 0,
                  (mod & KMOD_GUI) != 0);
               }
               break;
            case SDL_KEYUP:
               {
                  std::string key = sdlKeyToJS(event.key.keysym.sym);
                  SDL_Keymod mod = SDL_GetModState();
                  app->dispatchKeyEvent("keyup", key.c_str(),
                     (mod & KMOD_CTRL) != 0,
                     (mod & KMOD_SHIFT) != 0,
                     (mod & KMOD_ALT) != 0,
                  (mod & KMOD_GUI) != 0);
               }
               break;
         }
      }
      
      if (minimized)
      {
         SDL_Delay(100);
         continue;
      }
      auto t0 = std::chrono::steady_clock::now();
      app->TickScript();
      auto t1 = std::chrono::steady_clock::now();
      app->RenderFrame();
      auto t2 = std::chrono::steady_clock::now();
      Uint32 now = SDL_GetTicks();
      lastRenderTime = now;
       frameCount++;
      {
         auto tickUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
         auto renderUs = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
          fprintf(stderr, "[TIMING] fc=%d tick=%ldus render=%ldus\n", frameCount, tickUs, renderUs);
      }
      // 帧率控制通过垂直同步或自适应延迟实现
      // SDL_Delay(10);
      if (now - lastFpsTime >= 1000)
      {
         fps = frameCount;
         frameCount = 0;
         lastFpsTime = now;
         char title[64];
         snprintf(title, sizeof(title), "Canvas++ Demo - %d FPS", fps);
         // SDL_SetWindowTitle(window, title);
      }
   }
   
   delete app;
   SDL_DestroyWindow(window);
   SDL_Quit();
   
   return 0;
}
