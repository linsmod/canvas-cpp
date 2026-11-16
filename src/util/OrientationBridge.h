#pragma once

namespace canvas {

using GetOrientationTypeFn = const char* (*)();
using GetOrientationAngleFn = int (*)();
using LockOrientationFn = bool (*)(const char*);
using UnlockOrientationFn = void (*)();

extern GetOrientationTypeFn g_getOrientationType;
extern GetOrientationAngleFn g_getOrientationAngle;
extern LockOrientationFn g_lockOrientation;
extern UnlockOrientationFn g_unlockOrientation;

const char* currentOrientationType();
int currentOrientationAngle();
bool lockOrientation(const char* orientation);
void unlockOrientation();

} // namespace canvas
