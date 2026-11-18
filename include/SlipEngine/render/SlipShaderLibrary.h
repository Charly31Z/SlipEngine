#pragma once

#ifndef SLIP_SHADER_LIBRARY_H
#define SLIP_SHADER_LIBRARY_H

#include "SlipEngine.h"
#include "SlipShader.h"
#include <string>
#include <unordered_map>
#include <memory>

class SLIPENGINE_API SlipShaderLibrary {
public:
    static void Init();
    static void Clean();
    static void Reset();
    static SlipShader* Get(const std::string& name);
    static SlipShader* LoadOrGet(const std::string& name);
    static bool HasShader(const std::string& name);

private:
    static std::unordered_map<std::string, std::unique_ptr<SlipShader>> s_Shaders;
    static bool s_Initialized;

    static void LoadBuiltinShaders();
};

#endif // !SLIP_SHADER_LIBRARY_H

