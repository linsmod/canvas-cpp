cmake_minimum_required(VERSION 3.16)
project(canvas-cpp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

set(NANOVG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/nanovg" CACHE PATH "NanoVG source root")
set(QUICKJS_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/quickjs" CACHE PATH "Directory containing libquickjs.a")
set(QUICKJS_INCLUDE_DIR "${QUICKJS_LIB_DIR}" CACHE PATH "QuickJS headers")

if(NOT EXISTS "${NANOVG_DIR}/src/nanovg.c")
    message(FATAL_ERROR "Set NANOVG_DIR to a NanoVG checkout containing src/nanovg.c")
endif()
if(NOT EXISTS "${QUICKJS_LIB_DIR}/libquickjs.a")
    message(FATAL_ERROR "Set QUICKJS_LIB_DIR to a directory containing libquickjs.a")
endif()

set(SHIM_JS "${CMAKE_CURRENT_SOURCE_DIR}/src/browser_shim.js")
set(SHIM_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated/browser_shim_generated.cpp")

add_custom_command(
    OUTPUT "${SHIM_GENERATED}"
    COMMAND ${CMAKE_COMMAND}
        -DSHIM_INPUT=${SHIM_JS}
        -DSHIM_OUTPUT=${SHIM_GENERATED}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/src/cmake/GenerateShim.cmake"
    DEPENDS "${SHIM_JS}" "${CMAKE_CURRENT_SOURCE_DIR}/src/cmake/GenerateShim.cmake"
    COMMENT "Generating browser_shim_generated.cpp"
    VERBATIM
)
add_custom_target(generate_browser_shim DEPENDS "${SHIM_GENERATED}")

set(CANVAS_SOURCES
    src/Bindings.cpp
    src/DOMBindings.cpp
    src/ColorParser.cpp
    src/Context.h
    src/CanvasApp.cpp
    src/CanvasAppBase.cpp
    src/CanvasRenderingContext2d.cpp
    src/DisplayList.cpp
    src/DOMClasses.cpp
    src/EventClasses.cpp
    src/EventDispatch.cpp
    src/ImageData.cpp
    src/Layout.cpp
    src/NanoVG_GL.cpp
    src/Orientation.cpp
    src/Perf.cpp
    src/PosixFileSystem.cpp
    src/PlatformFileSystem_Linux.cpp
    src/RenderNode.cpp
    src/RenderThread.cpp
    src/Script.cpp
    src/State.cpp
    src/Style.cpp
    src/Tiler.cpp
    src/util/CondVar.cpp
    src/util/JSError.cpp
    src/util/Mutex.cpp
    src/util/NativeLog.cpp
    src/util/OrientationBridge.cpp
    src/util/Thread.cpp
    src/util/Timer.cpp
    src/util/ErrorCount.h
    src/Log.h
    src/RenderProtocol.h
    "${SHIM_GENERATED}"
)

list(REMOVE_DUPLICATES CANVAS_SOURCES)

set(CANVAS_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${NANOVG_DIR}/src"
    "${QUICKJS_INCLUDE_DIR}"
)

if(ANDROID)
    pkg_check_modules(CAIRO REQUIRED cairo)
    set(GL_LIBRARIES GLESv3 EGL android log)
    list(APPEND CANVAS_SOURCES src/Canvas.cpp "${NANOVG_DIR}/src/nanovg.c")
    list(APPEND CANVAS_INCLUDES ${CAIRO_INCLUDE_DIRS})
    set(CANVAS_DEFINITIONS NANOVG_GLES3)
else()
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(GL REQUIRED gl)
    pkg_check_modules(CAIRO REQUIRED cairo)
    list(APPEND CANVAS_SOURCES "${NANOVG_DIR}/src/nanovg.c" src/PlatformScreen_SDL2.cpp)
    list(APPEND CANVAS_INCLUDES ${SDL2_INCLUDE_DIRS} ${GL_INCLUDE_DIRS} ${CAIRO_INCLUDE_DIRS})
    set(CANVAS_DEFINITIONS NANOVG_GL3 GL_GLEXT_PROTOTYPES)
    set(PLATFORM_LINK_LIBRARIES ${SDL2_LIBRARIES} ${GL_LIBRARIES} ${CAIRO_LIBRARIES})
endif()

add_library(canvas STATIC ${CANVAS_SOURCES})
add_dependencies(canvas generate_browser_shim)
target_include_directories(canvas PUBLIC ${CANVAS_INCLUDES})
target_compile_definitions(canvas PUBLIC ${CANVAS_DEFINITIONS})
target_link_libraries(canvas PUBLIC
    "${QUICKJS_LIB_DIR}/libquickjs.a"
    Threads::Threads
    ${PLATFORM_LINK_LIBRARIES}
    dl m z
)

if(NOT ANDROID)
    add_executable(demo examples/Demo.cpp)
    target_link_libraries(demo PRIVATE canvas)
endif()
