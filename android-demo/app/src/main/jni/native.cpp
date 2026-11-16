)

target_include_directories(canvasdemo PRIVATE
    ${CANVAS_CPP_DIR}/include
    ${CANVAS_CPP_DIR}/src
    ${QUICKJS_DIR}
    ${ANDROID_INCLUDE_DIR}
    ${ANDROID_INCLUDE_DIR}/nanovg
)

# libcanvas.a already contains nanovg, nanosvg, QuickJS symbols.
# We only need to link canvas + system libs.
target_link_libraries(canvasdemo
    ${ANDROID_LIB_DIR}/libcanvas.a
    EGL
    GLESv3
    log
    android
)
add_compile_options(-Wno-c99-designator)

target_compile_features(canvasdemo PUBLIC cxx_std_17)

# Suppress "unknown attribute 'dllexport'" warnings from NDK headers
target_compile_options(canvasdemo PRIVATE -Wno-ignored-attributes)