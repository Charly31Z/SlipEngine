#include "SlipEngine/render/vulkan/SlipSpirvCompiler.h"
#include "SlipEngine/tools/SlipFileSystem.h"

#include <shaderc/shaderc.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace {

uint64_t ComputeFnv1a64(const void* data, size_t size, uint64_t seed = 14695981039346656037ULL) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint64_t h = seed;
    for (size_t i = 0; i < size; ++i) {
        h ^= static_cast<uint64_t>(bytes[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

std::string MakeCacheFilePath(const std::string& cacheDir, const std::string& glslSource, SpirvShaderStage stage) {
    uint64_t h = ComputeFnv1a64(glslSource.data(), glslSource.size());
    uint32_t stageTag = static_cast<uint32_t>(stage);
    h = ComputeFnv1a64(&stageTag, sizeof(stageTag), h);

    std::ostringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << h << "_s" << stageTag << ".spv";
    return SlipFileSystem::CombinePath(cacheDir, ss.str());
}

shaderc_shader_kind ToShadercKind(SpirvShaderStage stage) {
    switch (stage) {
        case SpirvShaderStage::Vertex:   return shaderc_glsl_vertex_shader;
        case SpirvShaderStage::Fragment: return shaderc_glsl_fragment_shader;
        case SpirvShaderStage::Compute:  return shaderc_glsl_compute_shader;
        default:                         return shaderc_glsl_compute_shader;
    }
}

bool TryReadCachedSpirv(const std::string& path, std::vector<uint32_t>& outSpirv) {
    if (path.empty() || !std::filesystem::exists(path)) {
        return false;
    }

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        return false;
    }

    std::streamsize byteSize = in.tellg();
    if (byteSize <= 0 || (byteSize % static_cast<std::streamsize>(sizeof(uint32_t))) != 0) {
        return false;
    }

    in.seekg(0, std::ios::beg);
    outSpirv.resize(static_cast<size_t>(byteSize) / sizeof(uint32_t));
    if (!in.read(reinterpret_cast<char*>(outSpirv.data()), byteSize)) {
        outSpirv.clear();
        return false;
    }

    // Validate SPIR-V magic number (0x07230203)
    if (outSpirv.empty() || outSpirv[0] != 0x07230203u) {
        outSpirv.clear();
        return false;
    }

    return true;
}

void WriteCachedSpirv(const std::string& path, const std::vector<uint32_t>& spirv) {
    if (path.empty() || spirv.empty()) return;
    std::error_code ec;
    std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path()) {
        std::filesystem::create_directories(fsPath.parent_path(), ec);
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return;
    out.write(reinterpret_cast<const char*>(spirv.data()), static_cast<std::streamsize>(spirv.size() * sizeof(uint32_t)));
}

} // namespace

SpirvCompileResult SlipSpirvCompiler::CompileGLSL(
    const std::string& glslSource,
    SpirvShaderStage stage,
    const std::string& debugName,
    bool useDiskCache,
    const std::string& cacheDir)
{
    SpirvCompileResult result;
    if (glslSource.empty()) {
        result.errorLog = "GLSL source is empty";
        return result;
    }

    if (useDiskCache && !cacheDir.empty()) {
        result.cachePath = MakeCacheFilePath(cacheDir, glslSource, stage);
        if (TryReadCachedSpirv(result.cachePath, result.spirv)) {
            result.success = true;
            result.loadedFromCache = true;
            return result;
        }
    }

    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetTargetSpirv(shaderc_spirv_version_1_6);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);

    shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(
        glslSource,
        ToShadercKind(stage),
        debugName.c_str(),
        options
    );

    if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
        result.errorLog = module.GetErrorMessage();
        std::cerr << "[SlipSpirvCompiler] Error compilando '" << debugName << "':\n" << result.errorLog << std::endl;
        return result;
    }

    result.spirv.assign(module.cbegin(), module.cend());
    if (result.spirv.empty() || result.spirv[0] != 0x07230203u) {
        result.errorLog = "SPIR-V output is invalid";
        result.spirv.clear();
        return result;
    }

    if (useDiskCache && !result.cachePath.empty()) {
        WriteCachedSpirv(result.cachePath, result.spirv);
    }

    result.success = true;
    result.loadedFromCache = false;
    return result;
}
