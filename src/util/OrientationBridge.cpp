#include "OrientationBridge.h"

namespace canvas {

namespace {

const char* defaultOrientationType()
{
    return "portrait-primary";
}

int defaultOrientationAngle()
{
    return 0;
}

bool defaultLockOrientation(const char*)
{
    return false;
}

void defaultUnlockOrientation()
{
}

} // namespace

GetOrientationTypeFn g_getOrientationType = &defaultOrientationType;
GetOrientationAngleFn g_getOrientationAngle = &defaultOrientationAngle;
LockOrientationFn g_lockOrientation = &defaultLockOrientation;
UnlockOrientationFn g_unlockOrientation = &defaultUnlockOrientation;

const char* currentOrientationType()
{
    return g_getOrientationType ? g_getOrientationType() : "portrait-primary";
}

int currentOrientationAngle()
{
    return g_getOrientationAngle ? g_getOrientationAngle() : 0;
}

bool lockOrientation(const char* orientation)
{
    return g_lockOrientation ? g_lockOrientation(orientation) : false;
}

void unlockOrientation()
{
    if (g_unlockOrientation) g_unlockOrientation();
}

} // namespace canvas
