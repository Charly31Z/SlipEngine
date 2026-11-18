#pragma once

#ifndef SLIP_SCRIPT_INDEXER_H
#define SLIP_SCRIPT_INDEXER_H

#include "SlipEngine/core/SlipExport.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

enum class SymbolType {
    EngineFunction,
    EngineClass,
    EngineProperty,
    GlobalFunction,
    ModuleFunction,
    Variable
};

struct ScriptSymbol {
    std::string name;
    std::string displayName;   // e.g. "Slip.Input.GetAxis(axisName)"
    std::string signature;     // e.g. "(axisName: string) -> number"
    std::string description;   // Tooltip / doc
    std::string sourceFile;    // "" if engine, or "assets/scripts/player.lua"
    int line = 1;              // 1-based line in source file
    SymbolType type = SymbolType::GlobalFunction;

    std::string GetTypeBadge() const {
        switch (type) {
            case SymbolType::EngineFunction: return "[Motor]";
            case SymbolType::EngineClass:    return "[Clase]";
            case SymbolType::EngineProperty: return "[Prop]";
            case SymbolType::GlobalFunction: return "[Global]";
            case SymbolType::ModuleFunction: return "[Modulo]";
            case SymbolType::Variable:       return "[Var]";
            default:                         return "[Simb]";
        }
    }
};

class SlipScriptIndexer {
public:
    static SlipScriptIndexer& Get();

    // Reindexa todos los scripts en la ruta de assets del proyecto
    void RefreshIndex(const std::string& assetsPath);

    // Parsea un script individual (útil tras guardar o editar en memoria)
    void IndexScript(const std::string& filePath, const std::string& content);

    // Búsqueda de autocompletado dado un prefijo o contexto
    std::vector<ScriptSymbol> QuerySymbols(const std::string& prefix, const std::string& currentContext = "") const;

    // Buscar definición para 'Ir a Definición' (F12)
    const ScriptSymbol* FindDefinition(const std::string& symbolName) const;

    // Registro global de nombres únicos de scripts en el proyecto
    bool IsScriptNameUnique(const std::string& scriptName, const std::string& currentFilePath = "") const;
    std::string FindScriptPathByName(const std::string& scriptName) const;
    void RegisterScriptFile(const std::string& filePath);
    void UnregisterScriptFile(const std::string& filePath);
    const std::unordered_map<std::string, std::string>& GetScriptFileRegistry() const { return m_ScriptFileRegistry; }

    const std::vector<ScriptSymbol>& GetAllSymbols() const { return m_AllSymbols; }

private:
    SlipScriptIndexer();
    void RegisterEngineSymbols();

    std::vector<ScriptSymbol> m_EngineSymbols;
    std::unordered_map<std::string, std::vector<ScriptSymbol>> m_FileSymbols; // Por archivo
    std::unordered_map<std::string, std::string> m_ScriptFileRegistry; // Nombre base (lowercase) -> ruta completa
    std::vector<ScriptSymbol> m_AllSymbols; // Cacheados para búsqueda rápida

    inline static SlipScriptIndexer* s_Instance = nullptr;
};

#endif // !SLIP_SCRIPT_INDEXER_H

