#include "PlatformFileSystem.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace canvas {

PlatformFileSystem::PlatformFileSystem(std::string baseDir)
    : m_baseDir(std::move(baseDir))
{
}

std::string PlatformFileSystem::resolve(const std::string& path) const
{
    if (path.empty()) return m_baseDir;
    if (!path.empty() && path[0] == '/') return path;

    if (m_baseDir.empty()) return path;
    if (m_baseDir.back() == '/' || m_baseDir.back() == '\\') return m_baseDir + path;
    return m_baseDir + "/" + path;
}

std::string PlatformFileSystem::readFile(const std::string& path) const
{
    const std::string fullPath = resolve(path);
    std::ifstream stream(fullPath.c_str(), std::ios::in | std::ios::binary);
    if (!stream) return std::string();

    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

bool PlatformFileSystem::exists(const std::string& path) const
{
    const std::string fullPath = resolve(path);
    std::FILE* file = std::fopen(fullPath.c_str(), "rb");
    if (!file) return false;
    std::fclose(file);
    return true;
}

PlatformFileSystem* createPlatformFileSystem(const std::string& baseDir)
{
    return new PlatformFileSystem(baseDir);
}

} // namespace canvas
