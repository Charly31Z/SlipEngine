#pragma once

#ifndef SLIP_SPIRV_COMPILER_H
#define SLIP_SPIRV_COMPILER_H

#include "SlipEngine/core/SlipExport.h"
#include <cstdint>
#include <string>
#include <vector>

enum class SpirvShaderStage : int {
    Vertex = 0,
    Fragment = 1,
    Compute = 2
};

struct SLIPENGINE_API SpirvCompileResult {
    bool success = false;
    bool loadedFromCache = false;
    std::vector<uint32_t> spirv;
    std::string errorLog;
    std::string cachePath;
};

class SLIPENGINE_API SlipSpirvCompiler {
public:
    static SpirvCompileResult CompileGLSL(
        const std::string& glslSource,
        SpirvShaderStage stage,
        const std::string& debugName = "shader",
        bool useDiskCache = true,
        const std::string& cacheDir = "cache/spirv"
    );
};

#endif // !SLIP_SPIRV_COMPILER_H
