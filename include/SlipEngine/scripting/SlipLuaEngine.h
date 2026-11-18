#pragma once

#ifndef SLIP_LUA_ENGINE_H
#define SLIP_LUA_ENGINE_H

#include "SlipEngine/core/SlipExport.h"
#include <string>
#include <memory>
#include <functional>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

class SlipEntity;
class TransformComponent;
class RigidBodyComponent;

class SLIPENGINE_API SlipLuaEngine {
public:
    static SlipLuaEngine& Get();

    void Init();
    void Shutdown();
    void ResetState();

    sol::state& GetState() { return m_Lua; }

    // Ejecución segura de archivos o strings
    bool ExecuteFile(const std::string& filePath);
    bool ExecuteString(const std::string& scriptCode, const std::string& chunkName = "chunk");

    // Configura la ruta base para paquetes 'require'
    void SetAssetsDirectory(const std::string& assetsPath);
    const std::string& GetAssetsDirectory() const { return m_AssetsDirectory; }

    // Enlace de subsistemas del motor (Transform, Entity, Input, RigidBody, Console)
    void RegisterEngineBindings();

    // Invocación segura de funciones Lua
    template<typename... Args>
    sol::protected_function_result SafeCall(sol::protected_function fn, Args&&... args) {
        if (!fn.valid()) {
            return sol::protected_function_result();
        }
        auto result = fn(std::forward<Args>(args)...);
        if (!result.valid()) {
            sol::error err = result;
            ReportError(err.what());
        }
        return result;
    }

    void ReportError(const std::string& errorMessage);

private:
    SlipLuaEngine();
    ~SlipLuaEngine();

    sol::state m_Lua;
    std::string m_AssetsDirectory;
    bool m_Initialized = false;

    inline static SlipLuaEngine* s_Instance = nullptr;
};

#endif // !SLIP_LUA_ENGINE_H

