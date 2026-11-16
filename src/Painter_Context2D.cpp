static bool getImageDataFromJS(JSContext* ctx, JSValueConst obj, int& width, int& height, std::vector<uint8_t>& rgbaData) {
    JSValue wVal = JS_GetPropertyStr(ctx, obj, "width");
    JSValue hVal = JS_GetPropertyStr(ctx, obj, "height");
    JSValue dataVal = JS_GetPropertyStr(ctx, obj, "data");
    
    if (!JS_IsNumber(wVal) || !JS_IsNumber(hVal) || !JS_IsObject(dataVal)) {
        JS_FreeValue(ctx, wVal); JS_FreeValue(ctx, hVal); JS_FreeValue(ctx, dataVal);
        return false;
    }
    
    int32_t w, h;
    JS_ToInt32(ctx, &w, wVal);
    JS_ToInt32(ctx, &h, hVal);
    width = w; height = h;
    
    // 获取 Uint8ClampedArray 的缓冲区
    JSValue bufferVal = JS_GetPropertyStr(ctx, dataVal, "buffer");
    if (!JS_IsObject(bufferVal)) {
        JS_FreeValue(ctx, wVal); JS_FreeValue(ctx, hVal); JS_FreeValue(ctx, dataVal);
        JS_FreeValue(ctx, bufferVal);
        return false;
    }
    size_t bufSize;
    uint8_t* bufData = JS_GetArrayBuffer(ctx, &bufSize, bufferVal);
    if (!bufData || bufSize < (size_t)(width * height * 4)) {
        JS_FreeValue(ctx, wVal); JS_FreeValue(ctx, hVal); JS_FreeValue(ctx, dataVal);
        JS_FreeValue(ctx, bufferVal);
        return false;
    }
    rgbaData.assign(bufData, bufData + width * height * 4);
    
    JS_FreeValue(ctx, wVal); JS_FreeValue(ctx, hVal); JS_FreeValue(ctx, dataVal);
    JS_FreeValue(ctx, bufferVal);
    return true;
}