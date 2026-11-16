#include <jni.h>
#include <string>
#include <EGL/egl.h>
#include <android/native_window_jni.h>
#include "Log.h"
#include "util/ErrorCount.h"
#include "util/NativeLog.h"
#include "util/OrientationBridge.h"
#include "EventDispatch.h"
#include "CanvasApp.h"

#undef LOG_TAG
#define LOG_TAG "CanvasDemo"

static canvas::CanvasApp* s_app = nullptr;
static JavaVM* g_jvm = nullptr;
static jclass g_mainActivityClass = nullptr;
static int s_width = 0, s_height = 0;

static JNIEnv* getJniEnv() {
    if (!g_jvm) return nullptr;
    JNIEnv* env = nullptr;
    int ret = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (ret == JNI_EDETACHED) return nullptr;
    return env;
}

static const char* androidGetOrientationType() {
    JNIEnv* env = getJniEnv();
    if (!env || !g_mainActivityClass) return "portrait-primary";
    jmethodID mid = env->GetStaticMethodID(g_mainActivityClass, "getOrientationType", "()Ljava/lang/String;");
    if (!mid) return "portrait-primary";
    jstring str = (jstring)env->CallStaticObjectMethod(g_mainActivityClass, mid);
    if (!str) return "portrait-primary";
    const char* cstr = env->GetStringUTFChars(str, nullptr);
    if (!cstr) return "portrait-primary";
    static thread_local std::string s_type;
    s_type = cstr;
    env->ReleaseStringUTFChars(str, cstr);
    env->DeleteLocalRef(str);
    return s_type.c_str();
}

static int androidGetOrientationAngle() {
    JNIEnv* env = getJniEnv();
    if (!env || !g_mainActivityClass) return 0;
    jmethodID mid = env->GetStaticMethodID(g_mainActivityClass, "getOrientationAngle", "()I");
    if (!mid) return 0;
    return env->CallStaticIntMethod(g_mainActivityClass, mid);
}

static bool androidLockOrientation(const char* orientation) {
    JNIEnv* env = getJniEnv();
    if (!env || !g_mainActivityClass) return false;
    jmethodID mid = env->GetStaticMethodID(g_mainActivityClass, "lockOrientation", "(Ljava/lang/String;)Z");
    if (!mid) return false;
    jstring orientStr = env->NewStringUTF(orientation);
    bool ok = env->CallStaticBooleanMethod(g_mainActivityClass, mid, orientStr);
    env->DeleteLocalRef(orientStr);
    return ok;
}

static void androidUnlockOrientation() {
    JNIEnv* env = getJniEnv();
    if (!env || !g_mainActivityClass) return;
    jmethodID mid = env->GetStaticMethodID(g_mainActivityClass, "unlockOrientation", "()V");
    if (!mid) return;
    env->CallStaticVoidMethod(g_mainActivityClass, mid);
}

static void initOrientationBridge() {
    canvas::g_getOrientationType = androidGetOrientationType;
    canvas::g_getOrientationAngle = androidGetOrientationAngle;
    canvas::g_lockOrientation = androidLockOrientation;
    canvas::g_unlockOrientation = androidUnlockOrientation;
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;

    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK && env) {
        jclass local = env->FindClass("com/example/canvasdemo/MainActivity");
        if (local) {
            g_mainActivityClass = (jclass)env->NewGlobalRef(local);
            env->DeleteLocalRef(local);
        }
    }

    initOrientationBridge();
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_canvasdemo_MainActivity_nativeInit(JNIEnv* env, jclass clazz,
                                                    jstring jsCode, jint width, jint height) {
    delete s_app;
    s_app = new canvas::CanvasApp(width, height);

    const char* code = env->GetStringUTFChars(jsCode, nullptr);
    if (!code) return;
    s_app->runScript(std::string(code), false);
    env->ReleaseStringUTFChars(jsCode, code);
}

static ANativeWindow* s_pendingWindow = nullptr;

extern "C" JNIEXPORT void JNICALL
Java_com_example_canvasdemo_MainActivity_nativeInitModule(JNIEnv* env, jclass clazz,
                                                           jstring jsfile, jstring basedir,
                                                           jint width, jint height) {
    delete s_app;
    s_app = new canvas::CanvasApp(width, height);

    ANativeWindow* win = s_pendingWindow;
    s_pendingWindow = nullptr;

    const char* path = env->GetStringUTFChars(jsfile, nullptr);
    if (!path) return;
    const char* dir = env->GetStringUTFChars(basedir, nullptr);
    if (!dir) return;

    s_app->SetBaseDir(std::string(dir));

    if (win) {
        LOGI("[JNI] nativeInitModule: calling initEGL %dx%d", width, height);
        s_app->initEGL(width, height);
    }
    if (s_pendingWindow) {
        ANativeWindow_release(s_pendingWindow);
        s_pendingWindow = nullptr;
    }

    LOGI("[JNI] nativeInitModule: runModule path=%s dir=%s eglReady=%d",
         path, dir, s_app->isEGLReady() ? 1 : 0);
    s_app->runModule(std::string(path));
    LOGI("[JNI] nativeInitModule: runModule done");

    env->ReleaseStringUTFChars(jsfile, path);
    env->ReleaseStringUTFChars(basedir, dir);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_canvasdemo_MainActivity_nativeSetSurface(JNIEnv* env, jclass clazz,
                                                           jobject surface, jint width, jint height) {
    if (!s_app) {
        LOGE("[JNI] nativeSetSurface: s_app is NULL");
        return;
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        LOGE("[JNI] nativeSetSurface: ANativeWindow_fromSurface failed");
        return;
    }
    LOGI("[JNI] nativeSetSurface window=%p %dx%d", (void*)window, width, height);

    s_width = width;
    s_height = height;
    s_app->setSurface(window, width, height);
    ANativeWindow_release(window);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_canvasdemo_MainActivity_nativeResize(JNIEnv* env, jclass clazz,
                                                      jint width, jint height) {
    if (!s_app) return;
    s_width = width;
    s_height = height;
    s_app->resize(width, height);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_canvasdemo_MainActivity_nativeUpdate(JNIEnv* env, jclass clazz) {
    if (!s_app) return JNI_FALSE;
    s_app->TickScript();
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_canvasdemo_MainActivity_nativeRender(JNIEnv* env, jclass clazz) {
    if (!s_app) return;
    s_app->renderFrame();
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_canvasdemo_MainActivity_nativeTouchEvent(JNIEnv* env, jclass clazz,
                                                          jstring type, jint x, jint y,
                                                          jint button, jint buttons) {
    if (!s_app) return;
    const char* ctype = env->GetStringUTFChars(type, nullptr);
    if (!ctype) return;
    s_app->dispatchMouseEvent(ctype, x, y, button, buttons);
    env->ReleaseStringUTFChars(type, ctype);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_canvasdemo_MainActivity_nativeVisibilityEvent(JNIEnv* env, jclass clazz,
                                                              jboolean hidden) {
    if (!s_app) return;
    s_app->dispatchVisibilityChanged(hidden == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_canvasdemo_MainActivity_nativeFocusEvent(JNIEnv* env, jclass clazz,
                                                          jboolean focused) {
    if (!s_app) return;
    s_app->dispatchWindowFocusEvent(focused == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_canvasdemo_MainActivity_nativeWheelEvent(JNIEnv* env, jclass clazz,
                                                          jint x, jint y, jint deltaY,
                                                          jboolean ctrlKey, jboolean shiftKey,
                                                          jboolean altKey, jboolean metaKey) {
    if (!s_app) return;
    s_app->dispatchWheelEvent("wheel", x, y, deltaY,
        ctrlKey == JNI_TRUE, shiftKey == JNI_TRUE,
        altKey == JNI_TRUE, metaKey == JNI_TRUE);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_example_canvasdemo_MainActivity_nativeGetAndResetErrorCount(JNIEnv* env, jclass clazz) {
    return canvas::g_jsErrorCount.exchange(0);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_canvasdemo_MainActivity_nativeLastLogEntry(JNIEnv* env, jclass clazz) {
    return env->NewStringUTF("");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_canvasdemo_MainActivity_nativeLastNativeLog(JNIEnv* env, jclass clazz) {
    return env->NewStringUTF(canvas::NativeLog::pop().c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_canvasdemo_MainActivity_nativeSurfaceDestroyed(JNIEnv* env, jclass clazz) {
    if (!s_app) return;
    s_app->destroySurface();
    s_app->teardownEGL();
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_canvasdemo_MainActivity_nativeDestroy(JNIEnv* env, jclass clazz) {
    delete s_app;
    s_app = nullptr;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_canvasdemo_MainActivity_nativeFireOrientationChange(JNIEnv* env, jclass clazz) {
    if (!s_app) return;
    s_app->dispatchOrientationChanged();
}