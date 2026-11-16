#pragma once

#include <string>

namespace canvas {

class PlatformFileSystem {
public:
    explicit PlatformFileSystem(std::string baseDir = std::string());
    virtual ~PlatformFileSystem() = default;

    virtual std::string readFile(const std::string& path) const;
    virtual bool exists(const std::string& path) const;

    const std::string& baseDir() const { return m_baseDir; }

protected:
    std::string resolve(const std::string& path) const;

private:
    std::string m_baseDir;
};

PlatformFileSystem* createPlatformFileSystem(const std::string& baseDir);

} // namespace canvas
