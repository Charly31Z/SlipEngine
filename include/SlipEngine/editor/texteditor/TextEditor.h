#pragma once

#ifndef SLIP_TEXT_EDITOR_H
#define SLIP_TEXT_EDITOR_H

#include "SlipEngine/core/SlipExport.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <imgui.h>

class SlipTextEditor {
public:
    enum class PaletteId {
        Dark,
        Light,
        RetroBlue
    };

    SlipTextEditor();
    ~SlipTextEditor();

    void SetText(const std::string& text);
    std::string GetText() const;

    void SetReadOnly(bool readOnly) { m_ReadOnly = readOnly; }
    bool IsReadOnly() const { return m_ReadOnly; }

    bool IsModified() const { return m_Modified; }
    void SetModified(bool modified) { m_Modified = modified; }

    void SetCursorPosition(int line, int column);
    void GetCursorPosition(int& line, int& column) const { line = m_CursorLine; column = m_CursorCol; }

    void Render(const char* title, const ImVec2& size = ImVec2(0, 0), bool border = false);

    // Callbacks de eventos
    using AutocompleteCallback = std::function<void(const std::string& wordPrefix, ImVec2 screenPos)>;
    void SetAutocompleteTriggerCallback(AutocompleteCallback cb) { m_OnAutocompleteTrigger = cb; }

    using GoToDefinitionCallback = std::function<void(const std::string& word)>;
    void SetGoToDefinitionCallback(GoToDefinitionCallback cb) { m_OnGoToDefinition = cb; }

    using DismissCallback = std::function<void()>;
    void SetDismissAutocompleteCallback(DismissCallback cb) { m_OnDismissAutocomplete = cb; }

    void DismissAutocomplete();

    // Inserción de palabra desde el autocompletado
    void InsertCompletion(const std::string& text);

    std::string GetCurrentWord() const;

    // Manejo de Selección
    bool HasSelection() const;
    void ClearSelection();
    void SelectAll();
    std::string GetSelectedText() const;
    void DeleteSelection();

    // Portapapeles (Copiar, Cortar, Pegar)
    void Copy();
    void Cut();
    void Paste();
    void PasteText(const std::string& text);

    void EnsureCursorVisible() { m_RequestScrollToCursor = true; }
    float GetLineHeight() const { return m_LineHeight; }

private:
    struct Line {
        std::string text;
    };

    void HandleKeyboardInputs();
    void InsertCharacter(char c);
    void Backspace();
    void DeleteKey();
    void InsertNewLine();
    void ClampCursor();

    float GetColumnX(const std::string& line, int col) const;
    int GetColumnFromX(const std::string& line, float relX) const;
    void GetSelectionRange(int& startLine, int& startCol, int& endLine, int& endCol) const;

    std::vector<Line> m_Lines;
    int m_CursorLine = 0;
    int m_CursorCol = 0;

    int m_SelectionStartLine = -1;
    int m_SelectionStartCol = -1;
    int m_SelectionEndLine = -1;
    int m_SelectionEndCol = -1;
    bool m_HasSelection = false;
    bool m_IsDraggingMouse = false;

    bool m_ReadOnly = false;
    bool m_Modified = false;
    float m_LineHeight = 18.0f;
    float m_CharWidth = 8.0f;
    bool m_RequestScrollToCursor = false;

    // Detección de doble y triple clic estilo VS Code
    float m_LastClickTime = -1.0f;
    int m_ClickCount = 0;
    int m_LastClickLine = -1;
    int m_LastClickCol = -1;

    AutocompleteCallback m_OnAutocompleteTrigger;
    GoToDefinitionCallback m_OnGoToDefinition;
    DismissCallback m_OnDismissAutocomplete;

    bool m_AutocompleteSuppressed = false;
    std::string m_LastDismissedWord;

    // Paleta de colores
    ImVec4 m_ColorKeyword = ImVec4(0.86f, 0.44f, 0.84f, 1.0f);     // Violeta
    ImVec4 m_ColorEngine = ImVec4(0.35f, 0.75f, 1.0f, 1.0f);       // Celeste brillante
    ImVec4 m_ColorString = ImVec4(0.85f, 0.65f, 0.45f, 1.0f);      // Ocre/Naranja
    ImVec4 m_ColorNumber = ImVec4(0.70f, 0.85f, 0.55f, 1.0f);      // Verde claro
    ImVec4 m_ColorComment = ImVec4(0.45f, 0.55f, 0.45f, 1.0f);     // Verde grisáceo
    ImVec4 m_ColorText = ImVec4(0.92f, 0.92f, 0.92f, 1.0f);        // Blanco hueso
    ImVec4 m_ColorLineNum = ImVec4(0.40f, 0.40f, 0.45f, 1.0f);     // Gris oscuro
    ImVec4 m_ColorCursor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);         // Blanco
    ImVec4 m_ColorSelection = ImVec4(0.24f, 0.44f, 0.72f, 0.55f);  // Azul selección tipo VS Code
};

#endif // !SLIP_TEXT_EDITOR_H

