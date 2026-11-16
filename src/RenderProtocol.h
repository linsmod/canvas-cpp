#pragma once

#include <future>
#include <vector>

namespace canvas {

class CanvasRenderingContext2d;

struct CompositeCommand {
    CanvasRenderingContext2d* context = nullptr;
    int dstX = 0, dstY = 0, dstW = 0, dstH = 0;
    int srcX = 0, srcY = 0, srcW = 0, srcH = 0;
    int targetFbo = 0;
    int targetW = 0, targetH = 0;
    int zOrder = 0;
    int dirtyX = 0, dirtyY = 0, dirtyW = 0, dirtyH = 0;
};

enum class GPUCommandType { CreateFBO, DeleteFBO, ReadPixels, CleanupCanvas };

struct CreateFboResult {
    int fboId = 0;
    int texId = 0;
    int rbId = 0;
    bool success = false;
};

struct GPUCommand {
    GPUCommandType type = GPUCommandType::CleanupCanvas;
    int width = 0, height = 0;
    int fboId = 0, texId = 0, rbId = 0;
    CanvasRenderingContext2d* readCtx = nullptr;
    int readX = 0, readY = 0, readW = 0, readH = 0, readFbo = 0;
    CanvasRenderingContext2d* cleanupCtx = nullptr;
    std::promise<CreateFboResult> createPromise;
    std::promise<bool> simplePromise;
    std::promise<std::vector<unsigned char>> readPromise;
};

extern std::vector<CompositeCommand> g_renderCommands;

} // namespace canvas
