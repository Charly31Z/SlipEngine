#include "SlipEngine/editor/texteditor/TextEditor.h"
#include <imgui_internal.h>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <cmath>

static const std::unordered_set<std::string> s_LuaKeywords = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
    "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then",
    "true", "until", "while"
};

static const std::unordered_set<std::string> s_EngineKeywords = {
    "Slip", "Input", "Time", "Scene", "Entity", "Transform", "TransformComponent",
    "RigidBody", "RigidBodyComponent", "Vec2", "Vec3", "OnStart", "OnUpdate",
    "self", "this", "Translate", "Rotate", "AddForce", "AddImpulse",
    "SetLinearVelocity", "GetLinearVelocity", "GetName", "SetName",
    "GetTransform", "GetRigidBody"
};

SlipTextEditor::SlipTextEditor() {
    m_Lines.push_back({ "" });
}

SlipTextEditor::~SlipTextEditor() = default;

void SlipTextEditor::SetText(const std::string& text) {
    m_Lines.clear();
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        m_Lines.push_back({ line });
    }
    if (m_Lines.empty()) {
        m_Lines.push_back({ "" });
    }
    m_CursorLine = 0;
    m_CursorCol = 0;
    m_Modified = false;
    ClearSelection();
    ClampCursor();
}

std::string SlipTextEditor::GetText() const {
    std::string result;
    for (size_t i = 0; i < m_Lines.size(); ++i) {
        result += m_Lines[i].text;
        if (i + 1 < m_Lines.size()) {
            result += "\n";
        }
    }
    return result;
}

void SlipTextEditor::SetCursorPosition(int line, int column) {
    m_CursorLine = line - 1; // Convertir 1-based a 0-based
    m_CursorCol = column;
    ClearSelection();
    ClampCursor();
}

void SlipTextEditor::ClampCursor() {
    if (m_CursorLine < 0) m_CursorLine = 0;
    if (m_CursorLine >= static_cast<int>(m_Lines.size())) {
        m_CursorLine = static_cast<int>(m_Lines.size()) - 1;
    }
    if (m_CursorCol < 0) m_CursorCol = 0;
    int lineLen = static_cast<int>(m_Lines[m_CursorLine].text.size());
    if (m_CursorCol > lineLen) {
        m_CursorCol = lineLen;
    }
}

float SlipTextEditor::GetColumnX(const std::string& line, int col) const {
    if (col <= 0 || line.empty()) return 0.0f;
    int clampedCol = (col > static_cast<int>(line.size())) ? static_cast<int>(line.size()) : col;
    ImFont* font = ImGui::GetFont();
    float fontSize = ImGui::GetFontSize();
    if (!font) {
        return clampedCol * m_CharWidth;
    }
    return font->CalcTextSizeA(fontSize, FLT_MAX, -1.0f, line.c_str(), line.c_str() + clampedCol).x;
}

int SlipTextEditor::GetColumnFromX(const std::string& line, float relX) const {
    if (relX <= 0.0f || line.empty()) return 0;
    int lineLen = static_cast<int>(line.size());
    float totalW = GetColumnX(line, lineLen);
    if (relX >= totalW) return lineLen;

    for (int col = 0; col < lineLen; ++col) {
        float x0 = GetColumnX(line, col);
        float x1 = GetColumnX(line, col + 1);
        float mid = (x0 + x1) * 0.5f;
        if (relX < mid) {
            return col;
        }
    }
    return lineLen;
}

bool SlipTextEditor::HasSelection() const {
    return m_HasSelection && (m_SelectionStartLine != m_SelectionEndLine || m_SelectionStartCol != m_SelectionEndCol);
}

void SlipTextEditor::ClearSelection() {
    m_HasSelection = false;
    m_SelectionStartLine = -1;
    m_SelectionStartCol = -1;
    m_SelectionEndLine = -1;
    m_SelectionEndCol = -1;
}

void SlipTextEditor::GetSelectionRange(int& startLine, int& startCol, int& endLine, int& endCol) const {
    if (m_SelectionStartLine < m_SelectionEndLine ||
       (m_SelectionStartLine == m_SelectionEndLine && m_SelectionStartCol <= m_SelectionEndCol)) {
        startLine = m_SelectionStartLine;
        startCol = m_SelectionStartCol;
        endLine = m_SelectionEndLine;
        endCol = m_SelectionEndCol;
    } else {
        startLine = m_SelectionEndLine;
        startCol = m_SelectionEndCol;
        endLine = m_SelectionStartLine;
        endCol = m_SelectionStartCol;
    }
}

void SlipTextEditor::SelectAll() {
    if (m_Lines.empty()) return;
    m_SelectionStartLine = 0;
    m_SelectionStartCol = 0;
    m_SelectionEndLine = static_cast<int>(m_Lines.size()) - 1;
    m_SelectionEndCol = static_cast<int>(m_Lines[m_SelectionEndLine].text.size());
    m_HasSelection = true;
    m_CursorLine = m_SelectionEndLine;
    m_CursorCol = m_SelectionEndCol;
}

std::string SlipTextEditor::GetSelectedText() const {
    if (!HasSelection()) return "";
    int sLine, sCol, eLine, eCol;
    GetSelectionRange(sLine, sCol, eLine, eCol);

    if (sLine == eLine) {
        const std::string& l = m_Lines[sLine].text;
        int len = eCol - sCol;
        if (len <= 0) return "";
        return l.substr(sCol, len);
    }

    std::string res;
    res += m_Lines[sLine].text.substr(sCol) + "\n";
    for (int l = sLine + 1; l < eLine; ++l) {
        res += m_Lines[l].text + "\n";
    }
    res += m_Lines[eLine].text.substr(0, eCol);
    return res;
}

void SlipTextEditor::DeleteSelection() {
    if (!HasSelection() || m_ReadOnly) return;
    int sLine, sCol, eLine, eCol;
    GetSelectionRange(sLine, sCol, eLine, eCol);

    if (sLine == eLine) {
        m_Lines[sLine].text.erase(sCol, eCol - sCol);
    } else {
        std::string head = m_Lines[sLine].text.substr(0, sCol);
        std::string tail = m_Lines[eLine].text.substr(eCol);
        m_Lines[sLine].text = head + tail;
        m_Lines.erase(m_Lines.begin() + sLine + 1, m_Lines.begin() + eLine + 1);
    }
    m_CursorLine = sLine;
    m_CursorCol = sCol;
    ClearSelection();
    m_Modified = true;
    ClampCursor();
}

std::string SlipTextEditor::GetCurrentWord() const {
    if (m_CursorLine < 0 || m_CursorLine >= static_cast<int>(m_Lines.size())) return "";
    const std::string& line = m_Lines[m_CursorLine].text;
    if (line.empty()) return "";

    int start = m_CursorCol - 1;
    while (start >= 0 && (std::isalnum(static_cast<unsigned char>(line[start])) || line[start] == '_' || line[start] == '.' || line[start] == ':')) {
        start--;
    }
    start++;

    int end = m_CursorCol;
    while (end < static_cast<int>(line.size()) && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_' || line[end] == '.' || line[end] == ':')) {
        end++;
    }

    if (start < end && start >= 0 && end <= static_cast<int>(line.size())) {
        return line.substr(start, end - start);
    }
    return "";
}

void SlipTextEditor::InsertCompletion(const std::string& text) {
    if (m_ReadOnly || m_CursorLine < 0 || m_CursorLine >= static_cast<int>(m_Lines.size())) return;
    if (HasSelection()) DeleteSelection();

    std::string& line = m_Lines[m_CursorLine].text;

    // Buscar inicio de la palabra actual
    int start = m_CursorCol - 1;
    bool replaceDots = (text.find('.') != std::string::npos || text.find(':') != std::string::npos);
    while (start >= 0) {
        char ch = line[start];
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
            start--;
        } else if (replaceDots && (ch == '.' || ch == ':')) {
            start--;
        } else {
            break;
        }
    }
    start++;

    // Reemplazar prefijo escrito por el símbolo completado
    if (start <= m_CursorCol) {
        line.erase(start, m_CursorCol - start);
    }
    line.insert(start, text);
    m_CursorCol = start + static_cast<int>(text.size());
    m_Modified = true;
    DismissAutocomplete();
    EnsureCursorVisible();
}

void SlipTextEditor::Copy() {
    if (HasSelection()) {
        ImGui::SetClipboardText(GetSelectedText().c_str());
    }
}

void SlipTextEditor::Cut() {
    if (m_ReadOnly) return;
    if (HasSelection()) {
        ImGui::SetClipboardText(GetSelectedText().c_str());
        DeleteSelection();
        m_Modified = true;
        EnsureCursorVisible();
    }
}

void SlipTextEditor::Paste() {
    if (m_ReadOnly) return;
    const char* clip = ImGui::GetClipboardText();
    if (clip && clip[0] != '\0') {
        PasteText(clip);
    }
}

static std::string TrimRightWhitespace(const std::string& s) {
    size_t end = s.find_last_not_of(" \t\r\n");
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

void SlipTextEditor::PasteText(const std::string& text) {
    if (m_ReadOnly || text.empty()) return;

    // Si el documento completo está vacío o solo contiene líneas en blanco/espacios,
    // o si todo el contenido está seleccionado, reemplazamos limpiamente
    bool allEmpty = true;
    for (const auto& l : m_Lines) {
        if (!TrimRightWhitespace(l.text).empty()) {
            allEmpty = false;
            break;
        }
    }

    if (allEmpty) {
        // Eliminar posibles saltos de línea espurios al principio del portapapeles
        size_t start = 0;
        while (start < text.size() && (text[start] == '\r' || text[start] == '\n')) {
            start++;
        }
        std::string clean = (start > 0) ? text.substr(start) : text;
        SetText(clean);
        m_CursorLine = static_cast<int>(m_Lines.size()) - 1;
        m_CursorCol = static_cast<int>(m_Lines.back().text.size());
        m_Modified = true;
        DismissAutocomplete();
        EnsureCursorVisible();
        return;
    }

    if (HasSelection()) {
        DeleteSelection();
    }
    ClampCursor();

    // Normalizar y dividir en líneas (evita agregar tabuladores automáticos escalonados)
    std::vector<std::string> pasteLines;
    std::string cur;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            continue;
        }
        if (text[i] == '\n') {
            pasteLines.push_back(cur);
            cur.clear();
        } else {
            cur += text[i];
        }
    }
    pasteLines.push_back(cur);

    if (pasteLines.size() == 1) {
        m_Lines[m_CursorLine].text.insert(m_CursorCol, pasteLines[0]);
        m_CursorCol += static_cast<int>(pasteLines[0].size());
    } else {
        std::string currentLineText = m_Lines[m_CursorLine].text;
        std::string prefix = currentLineText.substr(0, m_CursorCol);
        std::string suffix = currentLineText.substr(m_CursorCol);

        m_Lines[m_CursorLine].text = prefix + pasteLines[0];

        for (size_t i = 1; i < pasteLines.size() - 1; ++i) {
            m_Lines.insert(m_Lines.begin() + m_CursorLine + i, { pasteLines[i] });
        }

        size_t lastIdx = pasteLines.size() - 1;
        std::string lastCombined = pasteLines[lastIdx] + suffix;
        m_Lines.insert(m_Lines.begin() + m_CursorLine + lastIdx, { lastCombined });

        m_CursorLine += static_cast<int>(lastIdx);
        m_CursorCol = static_cast<int>(pasteLines[lastIdx].size());
    }

    m_Modified = true;
    DismissAutocomplete();
    EnsureCursorVisible();
}

void SlipTextEditor::DismissAutocomplete() {
    m_AutocompleteSuppressed = true;
    m_LastDismissedWord = GetCurrentWord();
    if (m_OnDismissAutocomplete) {
        m_OnDismissAutocomplete();
    }
}

void SlipTextEditor::InsertCharacter(char c) {
    if (m_ReadOnly) return;
    if (HasSelection()) DeleteSelection();
    ClampCursor();
    m_Lines[m_CursorLine].text.insert(m_CursorCol, 1, c);
    m_CursorCol++;
    m_Modified = true;
}

void SlipTextEditor::Backspace() {
    if (m_ReadOnly) return;
    if (HasSelection()) {
        DeleteSelection();
        return;
    }
    ClampCursor();
    if (m_CursorCol > 0) {
        m_Lines[m_CursorLine].text.erase(m_CursorCol - 1, 1);
        m_CursorCol--;
        m_Modified = true;
    } else if (m_CursorLine > 0) {
        int prevLineLen = static_cast<int>(m_Lines[m_CursorLine - 1].text.size());
        m_Lines[m_CursorLine - 1].text += m_Lines[m_CursorLine].text;
        m_Lines.erase(m_Lines.begin() + m_CursorLine);
        m_CursorLine--;
        m_CursorCol = prevLineLen;
        m_Modified = true;
    }
}

void SlipTextEditor::DeleteKey() {
    if (m_ReadOnly) return;
    if (HasSelection()) {
        DeleteSelection();
        return;
    }
    ClampCursor();
    int lineLen = static_cast<int>(m_Lines[m_CursorLine].text.size());
    if (m_CursorCol < lineLen) {
        m_Lines[m_CursorLine].text.erase(m_CursorCol, 1);
        m_Modified = true;
    } else if (m_CursorLine + 1 < static_cast<int>(m_Lines.size())) {
        m_Lines[m_CursorLine].text += m_Lines[m_CursorLine + 1].text;
        m_Lines.erase(m_Lines.begin() + m_CursorLine + 1);
        m_Modified = true;
    }
}

static bool EndsWithLuaToken(const std::string& str, const std::string& token) {
    if (str == token) return true;
    if (str.size() > token.size()) {
        size_t pos = str.size() - token.size();
        if (str.substr(pos) == token) {
            char before = str[pos - 1];
            if (std::isspace(static_cast<unsigned char>(before)) || before == ')' || before == ']' || before == '}' || before == ':') {
                return true;
            }
        }
    }
    return false;
}

static bool CheckOpensLuaBlock(const std::string& trimmed, std::string& closingToken) {
    if (trimmed.empty()) return false;

    // Si ya termina en end o until, no abre bloque nuevo
    if (EndsWithLuaToken(trimmed, "end") || EndsWithLuaToken(trimmed, "until")) {
        return false;
    }

    // 1. function (ej: function Foo(), local function Foo(), function(), MyScript:Foo())
    if (trimmed.rfind("function", 0) == 0 ||
        trimmed.rfind("local function", 0) == 0 ||
        trimmed.find("function(") != std::string::npos ||
        trimmed.find("function (") != std::string::npos ||
        EndsWithLuaToken(trimmed, "function")) {
        closingToken = "end";
        return true;
    }

    // 2. if ... then / elseif ... then
    if (EndsWithLuaToken(trimmed, "then")) {
        closingToken = "end";
        return true;
    }

    // 3. while ... do / for ... do
    if (EndsWithLuaToken(trimmed, "do")) {
        closingToken = "end";
        return true;
    }

    // 4. repeat ... until
    if (trimmed == "repeat" || EndsWithLuaToken(trimmed, "repeat")) {
        closingToken = "until ";
        return true;
    }

    // 5. Apertura de tabla con {
    if (trimmed.back() == '{') {
        closingToken = "}";
        return true;
    }

    return false;
}

void SlipTextEditor::InsertNewLine() {
    if (m_ReadOnly) return;
    if (HasSelection()) DeleteSelection();
    ClampCursor();

    std::string current = m_Lines[m_CursorLine].text;
    std::string lineBefore = current.substr(0, m_CursorCol);
    std::string remainder = current.substr(m_CursorCol);
    m_Lines[m_CursorLine].text = lineBefore;

    // Auto-indent: contar espacios al inicio de la línea actual
    size_t baseIndent = 0;
    while (baseIndent < lineBefore.size() && lineBefore[baseIndent] == ' ') {
        baseIndent++;
    }
    std::string baseIndentStr(baseIndent, ' ');

    std::string trimmedBefore = TrimRightWhitespace(lineBefore);
    std::string trimmedRemainder = TrimRightWhitespace(remainder);
    std::string closingToken;

    bool opensBlock = CheckOpensLuaBlock(trimmedBefore, closingToken);

    if (opensBlock) {
        std::string innerIndentStr = baseIndentStr + "    ";

        // Si la parte derecha ya tenía el cierre (ej: "function() end" y dio Enter en medio)
        if (!trimmedRemainder.empty() && trimmedRemainder.find(closingToken) != std::string::npos) {
            m_Lines.insert(m_Lines.begin() + m_CursorLine + 1, { innerIndentStr });
            m_Lines.insert(m_Lines.begin() + m_CursorLine + 2, { baseIndentStr + trimmedRemainder });
        } else {
            // Predicción inteligente estilo Roblox Studio / VS Code:
            // Insertar línea del medio indentada y colocar el cierre predicho automáticamente en la siguiente
            m_Lines.insert(m_Lines.begin() + m_CursorLine + 1, { innerIndentStr + remainder });
            m_Lines.insert(m_Lines.begin() + m_CursorLine + 2, { baseIndentStr + closingToken });
        }

        m_CursorLine++;
        m_CursorCol = static_cast<int>(innerIndentStr.size());
    } else {
        // Indentación normal
        m_Lines.insert(m_Lines.begin() + m_CursorLine + 1, { baseIndentStr + remainder });
        m_CursorLine++;
        m_CursorCol = static_cast<int>(baseIndent);
    }

    m_Modified = true;
    EnsureCursorVisible();
}

void SlipTextEditor::HandleKeyboardInputs() {
    ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) return;

    // ESC para cerrar sugerencias
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        DismissAutocomplete();
    }

    // F12 para 'Ir a Definición'
    if (ImGui::IsKeyPressed(ImGuiKey_F12)) {
        std::string word = GetCurrentWord();
        if (m_OnGoToDefinition && !word.empty()) {
            m_OnGoToDefinition(word);
        }
    }

    // Ctrl+Click para 'Ir a Definición'
    if (io.KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        std::string word = GetCurrentWord();
        if (m_OnGoToDefinition && !word.empty()) {
            m_OnGoToDefinition(word);
        }
    }

    // Atajos de selección y portapapeles: Ctrl+A, Ctrl+C, Ctrl+X, Ctrl+V
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
        SelectAll();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
        Copy();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X) && !m_ReadOnly) {
        Cut();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !m_ReadOnly) {
        Paste();
    }

    // Navegación con flechas (con o sin Shift para extender selección)
    bool shiftHeld = io.KeyShift;

    auto updateNavSelection = [&](int prevLine, int prevCol) {
        if (shiftHeld) {
            if (!m_HasSelection) {
                m_SelectionStartLine = prevLine;
                m_SelectionStartCol = prevCol;
            }
            m_SelectionEndLine = m_CursorLine;
            m_SelectionEndCol = m_CursorCol;
            m_HasSelection = (m_SelectionStartLine != m_SelectionEndLine || m_SelectionStartCol != m_SelectionEndCol);
        } else {
            ClearSelection();
        }
        EnsureCursorVisible();
    };

    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        int prevLine = m_CursorLine;
        int prevCol = m_CursorCol;
        if (m_CursorCol > 0) {
            m_CursorCol--;
        } else if (m_CursorLine > 0) {
            m_CursorLine--;
            m_CursorCol = static_cast<int>(m_Lines[m_CursorLine].text.size());
        }
        updateNavSelection(prevLine, prevCol);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        int prevLine = m_CursorLine;
        int prevCol = m_CursorCol;
        int lineLen = static_cast<int>(m_Lines[m_CursorLine].text.size());
        if (m_CursorCol < lineLen) {
            m_CursorCol++;
        } else if (m_CursorLine + 1 < static_cast<int>(m_Lines.size())) {
            m_CursorLine++;
            m_CursorCol = 0;
        }
        updateNavSelection(prevLine, prevCol);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        int prevLine = m_CursorLine;
        int prevCol = m_CursorCol;
        if (m_CursorLine > 0) m_CursorLine--;
        ClampCursor();
        updateNavSelection(prevLine, prevCol);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        int prevLine = m_CursorLine;
        int prevCol = m_CursorCol;
        if (m_CursorLine + 1 < static_cast<int>(m_Lines.size())) m_CursorLine++;
        ClampCursor();
        updateNavSelection(prevLine, prevCol);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
        int prevLine = m_CursorLine;
        int prevCol = m_CursorCol;
        m_CursorCol = 0;
        updateNavSelection(prevLine, prevCol);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_End)) {
        int prevLine = m_CursorLine;
        int prevCol = m_CursorCol;
        m_CursorCol = static_cast<int>(m_Lines[m_CursorLine].text.size());
        updateNavSelection(prevLine, prevCol);
    }

    bool typedSomething = false;
    if (!m_ReadOnly) {
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            Backspace();
            typedSomething = true;
            EnsureCursorVisible();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            DeleteKey();
            typedSomething = true;
            EnsureCursorVisible();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
            InsertNewLine();
            DismissAutocomplete();
            EnsureCursorVisible();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !io.KeyCtrl) {
            if (HasSelection()) DeleteSelection();
            for (int i = 0; i < 4; ++i) InsertCharacter(' ');
            DismissAutocomplete();
            EnsureCursorVisible();
        }

        // Caracteres tipados o texto pegado desde teclado en pantalla (Gboard/IME/Portapapeles)
        if (!io.KeyCtrl && io.InputQueueCharacters.Size > 0) {
            if (io.InputQueueCharacters.Size > 1) {
                // Pegado en bloque desde el teclado virtual o portapapeles
                std::string bulkText;
                for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
                    ImWchar wc = io.InputQueueCharacters[i];
                    if (wc == '\r') continue;
                    if (wc == '\n') {
                        bulkText += '\n';
                    } else if (wc == '\t') {
                        bulkText += "    ";
                    } else if (wc >= 32) {
                        char utf8[5] = {0};
                        ImTextCharToUtf8(utf8, wc);
                        bulkText += utf8;
                    }
                }
                if (!bulkText.empty()) {
                    PasteText(bulkText);
                    typedSomething = true;
                }
            } else {
                ImWchar wc = io.InputQueueCharacters[0];
                if (wc >= 32) {
                    char utf8[5] = {0};
                    ImTextCharToUtf8(utf8, wc);
                    for (int k = 0; utf8[k] != '\0'; ++k) {
                        InsertCharacter(utf8[k]);
                    }
                    typedSomething = true;
                    EnsureCursorVisible();
                } else if (wc == '\t') {
                    for (int k = 0; k < 4; ++k) InsertCharacter(' ');
                    typedSomething = true;
                    EnsureCursorVisible();
                } else if (wc == '\n') {
                    if (!ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                        InsertNewLine();
                        DismissAutocomplete();
                        EnsureCursorVisible();
                        typedSomething = true;
                    }
                }
            }
        }
    }

    std::string word = GetCurrentWord();
    if (typedSomething && word != m_LastDismissedWord) {
        m_AutocompleteSuppressed = false;
    }

    bool ctrlSpace = io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space);
    if (ctrlSpace) {
        m_AutocompleteSuppressed = false;
        m_LastDismissedWord.clear();
    }

    // Regla de aparición de sugerencias no invasiva:
    // 1. Con Ctrl+Espacio se fuerza siempre en cualquier posición.
    // 2. Tras un punto o dos puntos (ej: Slip. o self: o entity:) se muestra al escribirlo.
    // 3. Ya no es invasivo: no salta al escribir texto normal de 3 letras.
    bool shouldTrigger = !m_AutocompleteSuppressed && (
        ctrlSpace ||
        (typedSomething && !word.empty() && (word.back() == '.' || word.back() == ':'))
    );

    if (shouldTrigger && m_OnAutocompleteTrigger && (ctrlSpace || !word.empty())) {
        ImVec2 basePos = ImGui::GetCursorScreenPos();
        float gutterWidth = 45.0f;
        float textStartX = basePos.x + gutterWidth + 8.0f;
        float curX = GetColumnX(m_Lines[m_CursorLine].text, m_CursorCol);
        ImVec2 cursorPos(textStartX + curX, basePos.y + m_CursorLine * m_LineHeight + m_LineHeight);
        m_OnAutocompleteTrigger(word, cursorPos);
    }
}

void SlipTextEditor::Render(const char* title, const ImVec2& size, bool border) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
    ImGuiWindowFlags childFlags = ImGuiWindowFlags_HorizontalScrollbar | 
                                 ImGuiWindowFlags_AlwaysHorizontalScrollbar | 
                                 ImGuiWindowFlags_NoNavInputs;
    ImGui::BeginChild(title, size, border, childFlags);

    m_LineHeight = ImGui::GetTextLineHeightWithSpacing();
    m_CharWidth = ImGui::CalcTextSize("W").x;

    HandleKeyboardInputs();

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 basePos = ImGui::GetCursorScreenPos();
    float gutterWidth = 45.0f;
    float textStartX = basePos.x + gutterWidth + 8.0f;

    // Control de desplazamiento suave hacia el cursor (sin que la pantalla salte bruscamente)
    if (m_RequestScrollToCursor) {
        float cursorY = m_CursorLine * m_LineHeight;
        float scrollY = ImGui::GetScrollY();
        float windowH = ImGui::GetWindowHeight();
        float marginY = m_LineHeight * 2.0f;

        if (cursorY < scrollY) {
            ImGui::SetScrollY(cursorY);
        } else if (cursorY + m_LineHeight > scrollY + windowH - marginY) {
            ImGui::SetScrollY(cursorY + m_LineHeight - windowH + marginY);
        }

        if (m_CursorLine >= 0 && m_CursorLine < static_cast<int>(m_Lines.size())) {
            float curX = GetColumnX(m_Lines[m_CursorLine].text, m_CursorCol);
            float scrollX = ImGui::GetScrollX();
            float windowW = ImGui::GetWindowWidth() - (gutterWidth + 30.0f);
            if (curX < scrollX) {
                ImGui::SetScrollX(curX);
            } else if (curX > scrollX + windowW) {
                ImGui::SetScrollX(curX - windowW + 40.0f);
            }
        }
        m_RequestScrollToCursor = false;
    }

    // Posicionamiento de cursor y Selección con el ratón (1 clic, doble clic y triple clic estilo VS Code)
    if ((ImGui::IsWindowHovered() || m_IsDraggingMouse) && !io.KeyCtrl) {
        ImVec2 mousePos = ImGui::GetMousePos();
        float relY = mousePos.y - basePos.y;
        float relX = mousePos.x - textStartX;

        int clickedLine = static_cast<int>(relY / m_LineHeight);
        if (clickedLine < 0) clickedLine = 0;
        if (clickedLine >= static_cast<int>(m_Lines.size())) clickedLine = static_cast<int>(m_Lines.size()) - 1;

        int clickedCol = GetColumnFromX(m_Lines[clickedLine].text, relX);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered()) {
            ImGui::SetWindowFocus();

            float currentTime = static_cast<float>(ImGui::GetTime());
            if (currentTime - m_LastClickTime < io.MouseDoubleClickTime && clickedLine == m_LastClickLine) {
                m_ClickCount++;
            } else {
                m_ClickCount = 1;
            }
            m_LastClickTime = currentTime;
            m_LastClickLine = clickedLine;
            m_LastClickCol = clickedCol;

            if (m_ClickCount == 1) {
                m_IsDraggingMouse = true;
                if (io.KeyShift) {
                    if (!m_HasSelection) {
                        m_SelectionStartLine = m_CursorLine;
                        m_SelectionStartCol = m_CursorCol;
                    }
                    m_SelectionEndLine = clickedLine;
                    m_SelectionEndCol = clickedCol;
                    m_HasSelection = (m_SelectionStartLine != m_SelectionEndLine || m_SelectionStartCol != m_SelectionEndCol);
                } else {
                    m_SelectionStartLine = clickedLine;
                    m_SelectionStartCol = clickedCol;
                    m_SelectionEndLine = clickedLine;
                    m_SelectionEndCol = clickedCol;
                    m_HasSelection = false;
                }
                m_CursorLine = clickedLine;
                m_CursorCol = clickedCol;
                ClampCursor();
                DismissAutocomplete();
            } else if (m_ClickCount == 2) {
                // Doble clic: Seleccionar la palabra completa bajo el ratón
                m_IsDraggingMouse = false;
                const std::string& line = m_Lines[clickedLine].text;
                int wordStart = clickedCol;
                while (wordStart > 0 && (std::isalnum(static_cast<unsigned char>(line[wordStart - 1])) || line[wordStart - 1] == '_')) {
                    wordStart--;
                }
                int wordEnd = clickedCol;
                while (wordEnd < static_cast<int>(line.size()) && (std::isalnum(static_cast<unsigned char>(line[wordEnd])) || line[wordEnd] == '_')) {
                    wordEnd++;
                }
                m_SelectionStartLine = clickedLine;
                m_SelectionStartCol = wordStart;
                m_SelectionEndLine = clickedLine;
                m_SelectionEndCol = wordEnd;
                m_CursorLine = clickedLine;
                m_CursorCol = wordEnd;
                m_HasSelection = (wordStart != wordEnd);
                ClampCursor();
                DismissAutocomplete();
            } else if (m_ClickCount >= 3) {
                // Triple clic: Seleccionar toda la línea / párrafo completo
                m_IsDraggingMouse = false;
                m_SelectionStartLine = clickedLine;
                m_SelectionStartCol = 0;
                m_SelectionEndLine = clickedLine;
                m_SelectionEndCol = static_cast<int>(m_Lines[clickedLine].text.size());
                m_CursorLine = clickedLine;
                m_CursorCol = m_SelectionEndCol;
                m_HasSelection = true;
                ClampCursor();
                DismissAutocomplete();
                if (m_ClickCount > 3) m_ClickCount = 1;
            }
        } else if (m_IsDraggingMouse && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_SelectionEndLine = clickedLine;
            m_SelectionEndCol = clickedCol;
            m_CursorLine = clickedLine;
            m_CursorCol = clickedCol;
            ClampCursor();
            m_HasSelection = (m_SelectionStartLine != m_SelectionEndLine || m_SelectionStartCol != m_SelectionEndCol);
            DismissAutocomplete();
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_IsDraggingMouse = false;
    }

    // Menú contextual con clic derecho (Cortar, Copiar, Pegar, Seleccionar todo)
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsWindowHovered()) {
        ImVec2 mousePos = ImGui::GetMousePos();
        float relY = mousePos.y - basePos.y;
        float relX = mousePos.x - textStartX;
        int clickedLine = static_cast<int>(relY / m_LineHeight);
        if (clickedLine >= 0 && clickedLine < static_cast<int>(m_Lines.size())) {
            int clickedCol = GetColumnFromX(m_Lines[clickedLine].text, relX);
            int sL, sC, eL, eC;
            bool insideSel = false;
            if (HasSelection()) {
                GetSelectionRange(sL, sC, eL, eC);
                if (clickedLine > sL && clickedLine < eL) insideSel = true;
                else if (clickedLine == sL && clickedLine == eL) insideSel = (clickedCol >= sC && clickedCol <= eC);
                else if (clickedLine == sL) insideSel = (clickedCol >= sC);
                else if (clickedLine == eL) insideSel = (clickedCol <= eC);
            }
            if (!insideSel) {
                m_CursorLine = clickedLine;
                m_CursorCol = clickedCol;
                ClampCursor();
                ClearSelection();
            }
        }
        ImGui::OpenPopup("TextEditorContextMenu");
    }

    if (ImGui::BeginPopup("TextEditorContextMenu")) {
        bool hasSel = HasSelection();
        const char* clip = ImGui::GetClipboardText();
        bool canPaste = !m_ReadOnly && (clip != nullptr && clip[0] != '\0');

        if (ImGui::MenuItem("Cortar", "Ctrl+X", false, hasSel && !m_ReadOnly)) {
            Cut();
        }
        if (ImGui::MenuItem("Copiar", "Ctrl+C", false, hasSel)) {
            Copy();
        }
        if (ImGui::MenuItem("Pegar", "Ctrl+V", false, canPaste)) {
            Paste();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Seleccionar todo", "Ctrl+A")) {
            SelectAll();
        }

        std::string curWord = GetCurrentWord();
        if (!curWord.empty() && m_OnGoToDefinition) {
            ImGui::Separator();
            std::string label = "Ir a definición de '" + curWord + "'";
            if (ImGui::MenuItem(label.c_str(), "F12")) {
                m_OnGoToDefinition(curWord);
            }
        }
        ImGui::EndPopup();
    }

    // Fondo del gutter de números de línea
    drawList->AddRectFilled(
        basePos,
        ImVec2(basePos.x + gutterWidth, basePos.y + m_Lines.size() * m_LineHeight + 100.0f),
        ImColor(0.09f, 0.09f, 0.11f, 1.0f)
    );
    // Línea divisoria de números
    drawList->AddLine(
        ImVec2(basePos.x + gutterWidth, basePos.y),
        ImVec2(basePos.x + gutterWidth, basePos.y + m_Lines.size() * m_LineHeight + 100.0f),
        ImColor(0.20f, 0.20f, 0.23f, 1.0f)
    );

    // Resaltado de la línea activa del cursor
    float activeLineY = basePos.y + m_CursorLine * m_LineHeight;
    drawList->AddRectFilled(
        ImVec2(basePos.x + gutterWidth + 1.0f, activeLineY),
        ImVec2(basePos.x + ImGui::GetWindowWidth() + ImGui::GetScrollX(), activeLineY + m_LineHeight),
        ImColor(0.18f, 0.20f, 0.25f, 0.5f)
    );

    // Rango de selección si existe
    int selStartLine = -1, selStartCol = -1, selEndLine = -1, selEndCol = -1;
    if (HasSelection()) {
        GetSelectionRange(selStartLine, selStartCol, selEndLine, selEndCol);
    }

    // Renderizado línea por línea
    for (size_t lineIdx = 0; lineIdx < m_Lines.size(); ++lineIdx) {
        float lineY = basePos.y + lineIdx * m_LineHeight;
        const std::string& lineText = m_Lines[lineIdx].text;
        int lineLen = static_cast<int>(lineText.size());

        // 1. Número de línea
        char numBuf[16];
        snprintf(numBuf, sizeof(numBuf), "%3d", static_cast<int>(lineIdx + 1));
        drawList->AddText(ImVec2(basePos.x + 8.0f, lineY), ImColor(m_ColorLineNum), numBuf);

        // 2. Resaltado de Selección sobre esta línea
        if (HasSelection() && static_cast<int>(lineIdx) >= selStartLine && static_cast<int>(lineIdx) <= selEndLine) {
            int colFrom = 0;
            int colTo = lineLen;

            if (static_cast<int>(lineIdx) == selStartLine) colFrom = selStartCol;
            if (static_cast<int>(lineIdx) == selEndLine)   colTo = selEndCol;

            float x1 = textStartX + GetColumnX(lineText, colFrom);
            float x2 = textStartX + GetColumnX(lineText, colTo);
            if (colTo == lineLen && static_cast<int>(lineIdx) < selEndLine) {
                x2 += 8.0f; // Pequeño indicador de selección del salto de línea
            }

            if (x2 > x1) {
                drawList->AddRectFilled(
                    ImVec2(x1, lineY),
                    ImVec2(x2, lineY + m_LineHeight),
                    ImColor(m_ColorSelection)
                );
            }
        }

        // 3. Tokens y Coloreado Sintáctico con posicionamiento milimétrico
        size_t i = 0;
        bool inComment = false;

        while (i < lineText.size()) {
            // Comentarios Lua (-- ...)
            if (!inComment && i + 1 < lineText.size() && lineText[i] == '-' && lineText[i + 1] == '-') {
                float tokenX = textStartX + GetColumnX(lineText, static_cast<int>(i));
                std::string comment = lineText.substr(i);
                drawList->AddText(ImVec2(tokenX, lineY), ImColor(m_ColorComment), comment.c_str());
                break;
            }

            // Cadenas literales ("..." o '...')
            if (lineText[i] == '"' || lineText[i] == '\'') {
                char quote = lineText[i];
                size_t strStart = i;
                i++;
                while (i < lineText.size() && lineText[i] != quote) {
                    if (lineText[i] == '\\' && i + 1 < lineText.size()) i++;
                    i++;
                }
                if (i < lineText.size()) i++;
                float tokenX = textStartX + GetColumnX(lineText, static_cast<int>(strStart));
                std::string strVal = lineText.substr(strStart, i - strStart);
                drawList->AddText(ImVec2(tokenX, lineY), ImColor(m_ColorString), strVal.c_str());
                continue;
            }

            // Identificadores y palabras clave
            if (std::isalpha(static_cast<unsigned char>(lineText[i])) || lineText[i] == '_') {
                size_t tokenStart = i;
                while (i < lineText.size() && (std::isalnum(static_cast<unsigned char>(lineText[i])) || lineText[i] == '_')) {
                    i++;
                }
                float tokenX = textStartX + GetColumnX(lineText, static_cast<int>(tokenStart));
                std::string token = lineText.substr(tokenStart, i - tokenStart);
                ImVec4 col = m_ColorText;
                if (s_LuaKeywords.find(token) != s_LuaKeywords.end()) {
                    col = m_ColorKeyword;
                } else if (s_EngineKeywords.find(token) != s_EngineKeywords.end()) {
                    col = m_ColorEngine;
                }
                drawList->AddText(ImVec2(tokenX, lineY), ImColor(col), token.c_str());
                continue;
            }

            // Números
            if (std::isdigit(static_cast<unsigned char>(lineText[i]))) {
                size_t numStart = i;
                while (i < lineText.size() && (std::isdigit(static_cast<unsigned char>(lineText[i])) || lineText[i] == '.' || lineText[i] == 'x' || lineText[i] == 'f')) {
                    i++;
                }
                float tokenX = textStartX + GetColumnX(lineText, static_cast<int>(numStart));
                std::string numStr = lineText.substr(numStart, i - numStart);
                drawList->AddText(ImVec2(tokenX, lineY), ImColor(m_ColorNumber), numStr.c_str());
                continue;
            }

            // Caracteres individuales (espacios, operadores, puntuación o secuencias UTF-8 multi-byte)
            if (lineText[i] != ' ') {
                float tokenX = textStartX + GetColumnX(lineText, static_cast<int>(i));
                unsigned char c = static_cast<unsigned char>(lineText[i]);
                size_t utf8Len = 1;
                if ((c & 0xE0) == 0xC0) utf8Len = 2;
                else if ((c & 0xF0) == 0xE0) utf8Len = 3;
                else if ((c & 0xF8) == 0xF0) utf8Len = 4;
                if (i + utf8Len > lineText.size()) utf8Len = 1;

                std::string utf8Char = lineText.substr(i, utf8Len);
                drawList->AddText(ImVec2(tokenX, lineY), ImColor(m_ColorText), utf8Char.c_str());
                i += utf8Len;
            } else {
                i++;
            }
        }
    }

    // 4. Dibujar Cursor parpadeante perfectamente alineado
    if (fmod(static_cast<float>(ImGui::GetTime()), 1.0f) < 0.5f) {
        float cursorColX = 0.0f;
        if (m_CursorLine >= 0 && m_CursorLine < static_cast<int>(m_Lines.size())) {
            cursorColX = GetColumnX(m_Lines[m_CursorLine].text, m_CursorCol);
        }
        float cursorX = textStartX + cursorColX;
        float cursorY = basePos.y + m_CursorLine * m_LineHeight;
        drawList->AddLine(
            ImVec2(cursorX, cursorY),
            ImVec2(cursorX, cursorY + m_LineHeight),
            ImColor(m_ColorCursor),
            1.8f
        );
    }

    // Espacio reservado para permitir scroll completo
    ImGui::Dummy(ImVec2(gutterWidth + 200.0f + m_Lines.size() * 10.0f, m_Lines.size() * m_LineHeight + 40.0f));

    ImGui::EndChild();
    ImGui::PopStyleColor();
}
