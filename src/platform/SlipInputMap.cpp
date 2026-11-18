#include "SlipEngine/platform/SlipInputMap.h"
#include "SlipEngine/platform/SlipInput.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace {

struct CatalogEntry {
    InputBindingKind kind;
    int code;
    const char* token;
    const char* label;
    bool selectable;
};

struct PadAxisEntry {
    int axis;
    const char* token;
    const char* label;
};

const CatalogEntry kCatalog[] = {
    { InputBindingKind::None, 0, "None", "(Ninguna)", true },
    { InputBindingKind::None, 0, "", "Teclado", false },
    { InputBindingKind::Key, SDL_SCANCODE_A, "A", "A", true },
    { InputBindingKind::Key, SDL_SCANCODE_B, "B", "B", true },
    { InputBindingKind::Key, SDL_SCANCODE_C, "C", "C", true },
    { InputBindingKind::Key, SDL_SCANCODE_D, "D", "D", true },
    { InputBindingKind::Key, SDL_SCANCODE_E, "E", "E", true },
    { InputBindingKind::Key, SDL_SCANCODE_F, "F", "F", true },
    { InputBindingKind::Key, SDL_SCANCODE_G, "G", "G", true },
    { InputBindingKind::Key, SDL_SCANCODE_H, "H", "H", true },
    { InputBindingKind::Key, SDL_SCANCODE_I, "I", "I", true },
    { InputBindingKind::Key, SDL_SCANCODE_J, "J", "J", true },
    { InputBindingKind::Key, SDL_SCANCODE_K, "K", "K", true },
    { InputBindingKind::Key, SDL_SCANCODE_L, "L", "L", true },
    { InputBindingKind::Key, SDL_SCANCODE_M, "M", "M", true },
    { InputBindingKind::Key, SDL_SCANCODE_N, "N", "N", true },
    { InputBindingKind::Key, SDL_SCANCODE_O, "O", "O", true },
    { InputBindingKind::Key, SDL_SCANCODE_P, "P", "P", true },
    { InputBindingKind::Key, SDL_SCANCODE_Q, "Q", "Q", true },
    { InputBindingKind::Key, SDL_SCANCODE_R, "R", "R", true },
    { InputBindingKind::Key, SDL_SCANCODE_S, "S", "S", true },
    { InputBindingKind::Key, SDL_SCANCODE_T, "T", "T", true },
    { InputBindingKind::Key, SDL_SCANCODE_U, "U", "U", true },
    { InputBindingKind::Key, SDL_SCANCODE_V, "V", "V", true },
    { InputBindingKind::Key, SDL_SCANCODE_W, "W", "W", true },
    { InputBindingKind::Key, SDL_SCANCODE_X, "X", "X", true },
    { InputBindingKind::Key, SDL_SCANCODE_Y, "Y", "Y", true },
    { InputBindingKind::Key, SDL_SCANCODE_Z, "Z", "Z", true },
    { InputBindingKind::Key, SDL_SCANCODE_1, "1", "1", true },
    { InputBindingKind::Key, SDL_SCANCODE_2, "2", "2", true },
    { InputBindingKind::Key, SDL_SCANCODE_3, "3", "3", true },
    { InputBindingKind::Key, SDL_SCANCODE_4, "4", "4", true },
    { InputBindingKind::Key, SDL_SCANCODE_5, "5", "5", true },
    { InputBindingKind::Key, SDL_SCANCODE_6, "6", "6", true },
    { InputBindingKind::Key, SDL_SCANCODE_7, "7", "7", true },
    { InputBindingKind::Key, SDL_SCANCODE_8, "8", "8", true },
    { InputBindingKind::Key, SDL_SCANCODE_9, "9", "9", true },
    { InputBindingKind::Key, SDL_SCANCODE_0, "0", "0", true },
    { InputBindingKind::Key, SDL_SCANCODE_F1, "F1", "F1", true },
    { InputBindingKind::Key, SDL_SCANCODE_F2, "F2", "F2", true },
    { InputBindingKind::Key, SDL_SCANCODE_F3, "F3", "F3", true },
    { InputBindingKind::Key, SDL_SCANCODE_F4, "F4", "F4", true },
    { InputBindingKind::Key, SDL_SCANCODE_F5, "F5", "F5", true },
    { InputBindingKind::Key, SDL_SCANCODE_F6, "F6", "F6", true },
    { InputBindingKind::Key, SDL_SCANCODE_F7, "F7", "F7", true },
    { InputBindingKind::Key, SDL_SCANCODE_F8, "F8", "F8", true },
    { InputBindingKind::Key, SDL_SCANCODE_F9, "F9", "F9", true },
    { InputBindingKind::Key, SDL_SCANCODE_F10, "F10", "F10", true },
    { InputBindingKind::Key, SDL_SCANCODE_F11, "F11", "F11", true },
    { InputBindingKind::Key, SDL_SCANCODE_F12, "F12", "F12", true },
    { InputBindingKind::Key, SDL_SCANCODE_SPACE, "Space", "Espacio", true },
    { InputBindingKind::Key, SDL_SCANCODE_ESCAPE, "Escape", "Escape", true },
    { InputBindingKind::Key, SDL_SCANCODE_RETURN, "Return", "Intro", true },
    { InputBindingKind::Key, SDL_SCANCODE_TAB, "Tab", "Tabulador", true },
    { InputBindingKind::Key, SDL_SCANCODE_BACKSPACE, "Backspace", "Retroceso", true },
    { InputBindingKind::Key, SDL_SCANCODE_LEFT, "Left", "Flecha izquierda", true },
    { InputBindingKind::Key, SDL_SCANCODE_RIGHT, "Right", "Flecha derecha", true },
    { InputBindingKind::Key, SDL_SCANCODE_UP, "Up", "Flecha arriba", true },
    { InputBindingKind::Key, SDL_SCANCODE_DOWN, "Down", "Flecha abajo", true },
    { InputBindingKind::Key, SDL_SCANCODE_LSHIFT, "LShift", "Shift izquierdo", true },
    { InputBindingKind::Key, SDL_SCANCODE_RSHIFT, "RShift", "Shift derecho", true },
    { InputBindingKind::Key, SDL_SCANCODE_LCTRL, "LCtrl", "Ctrl izquierdo", true },
    { InputBindingKind::Key, SDL_SCANCODE_RCTRL, "RCtrl", "Ctrl derecho", true },
    { InputBindingKind::Key, SDL_SCANCODE_LALT, "LAlt", "Alt izquierdo", true },
    { InputBindingKind::Key, SDL_SCANCODE_RALT, "RAlt", "Alt derecho", true },
    { InputBindingKind::Key, SDL_SCANCODE_CAPSLOCK, "CapsLock", "Bloq Mayús", true },
    { InputBindingKind::Key, SDL_SCANCODE_MINUS, "Minus", "-", true },
    { InputBindingKind::Key, SDL_SCANCODE_EQUALS, "Equals", "=", true },
    { InputBindingKind::Key, SDL_SCANCODE_LEFTBRACKET, "LeftBracket", "[", true },
    { InputBindingKind::Key, SDL_SCANCODE_RIGHTBRACKET, "RightBracket", "]", true },
    { InputBindingKind::Key, SDL_SCANCODE_BACKSLASH, "Backslash", "\\", true },
    { InputBindingKind::Key, SDL_SCANCODE_SEMICOLON, "Semicolon", ";", true },
    { InputBindingKind::Key, SDL_SCANCODE_APOSTROPHE, "Apostrophe", "'", true },
    { InputBindingKind::Key, SDL_SCANCODE_GRAVE, "Grave", "`", true },
    { InputBindingKind::Key, SDL_SCANCODE_COMMA, "Comma", ",", true },
    { InputBindingKind::Key, SDL_SCANCODE_PERIOD, "Period", ".", true },
    { InputBindingKind::Key, SDL_SCANCODE_SLASH, "Slash", "/", true },
    { InputBindingKind::Key, SDL_SCANCODE_INSERT, "Insert", "Insert", true },
    { InputBindingKind::Key, SDL_SCANCODE_DELETE, "Delete", "Supr", true },
    { InputBindingKind::Key, SDL_SCANCODE_HOME, "Home", "Inicio", true },
    { InputBindingKind::Key, SDL_SCANCODE_END, "End", "Fin", true },
    { InputBindingKind::Key, SDL_SCANCODE_PAGEUP, "PageUp", "Re Pág", true },
    { InputBindingKind::Key, SDL_SCANCODE_PAGEDOWN, "PageDown", "Av Pág", true },
    { InputBindingKind::Key, SDL_SCANCODE_PRINTSCREEN, "PrintScreen", "Impr Pant", true },
    { InputBindingKind::Key, SDL_SCANCODE_PAUSE, "Pause", "Pausa", true },
    { InputBindingKind::Key, SDL_SCANCODE_KP_0, "KP0", "Num 0", true },
    { InputBindingKind::Key, SDL_SCANCODE_KP_1, "KP1", "Num 1", true },
    { InputBindingKind::Key, SDL_SCANCODE_KP_2, "KP2", "Num 2", true },
    { InputBindingKind::Key, SDL_SCANCODE_KP_3, "KP3", "Num 3", true },
    { InputBindingKind::Key, SDL_SCANCODE_KP_4, "KP4", "Num 4", true },
    { InputBindingKind::Key, SDL_SCANCODE_KP_5, "KP5", "Num 5", true },
    { InputBindingKind::Key, SDL_SCANCODE_KP_6, "KP6", "Num 6", true },
    { InputBindingKind::Key, SDL_SCANCODE_KP_7, "KP7", "Num 7", true },
    { InputBindingKind::Key, SDL_SCANCODE_KP_8, "KP8", "Num 8", true },
    { InputBindingKind::Key, SDL_SCANCODE_KP_9, "KP9", "Num 9", true },
    { InputBindingKind::Key, SDL_SCANCODE_KP_ENTER, "KPEnter", "Num Intro", true },
    { InputBindingKind::Key, SDL_SCANCODE_KP_PLUS, "KPPlus", "Num +", true },
    { InputBindingKind::Key, SDL_SCANCODE_KP_MINUS, "KPMinus", "Num -", true },
    { InputBindingKind::Key, SDL_SCANCODE_KP_MULTIPLY, "KPMultiply", "Num *", true },
    { InputBindingKind::Key, SDL_SCANCODE_KP_DIVIDE, "KPDivide", "Num /", true },
    { InputBindingKind::Key, SDL_SCANCODE_KP_PERIOD, "KPPeriod", "Num .", true },
    { InputBindingKind::None, 0, "", "Ratón", false },
    { InputBindingKind::MouseButton, SDL_BUTTON_LEFT, "Left", "Ratón izquierdo", true },
    { InputBindingKind::MouseButton, SDL_BUTTON_RIGHT, "Right", "Ratón derecho", true },
    { InputBindingKind::MouseButton, SDL_BUTTON_MIDDLE, "Middle", "Ratón central", true },
    { InputBindingKind::None, 0, "", "Mando", false },
    { InputBindingKind::GamepadButton, SDL_CONTROLLER_BUTTON_A, "A", "Mando A", true },
    { InputBindingKind::GamepadButton, SDL_CONTROLLER_BUTTON_B, "B", "Mando B", true },
    { InputBindingKind::GamepadButton, SDL_CONTROLLER_BUTTON_X, "X", "Mando X", true },
    { InputBindingKind::GamepadButton, SDL_CONTROLLER_BUTTON_Y, "Y", "Mando Y", true },
    { InputBindingKind::GamepadButton, SDL_CONTROLLER_BUTTON_BACK, "Back", "Mando Atrás", true },
    { InputBindingKind::GamepadButton, SDL_CONTROLLER_BUTTON_GUIDE, "Guide", "Mando Guía", true },
    { InputBindingKind::GamepadButton, SDL_CONTROLLER_BUTTON_START, "Start", "Mando Start", true },
    { InputBindingKind::GamepadButton, SDL_CONTROLLER_BUTTON_LEFTSTICK, "LeftStick", "Click stick izquierdo", true },
    { InputBindingKind::GamepadButton, SDL_CONTROLLER_BUTTON_RIGHTSTICK, "RightStick", "Click stick derecho", true },
    { InputBindingKind::GamepadButton, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, "LeftShoulder", "Hombro izquierdo", true },
    { InputBindingKind::GamepadButton, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, "RightShoulder", "Hombro derecho", true },
    { InputBindingKind::GamepadButton, SDL_CONTROLLER_BUTTON_DPAD_UP, "DpadUp", "Cruceta arriba", true },
    { InputBindingKind::GamepadButton, SDL_CONTROLLER_BUTTON_DPAD_DOWN, "DpadDown", "Cruceta abajo", true },
    { InputBindingKind::GamepadButton, SDL_CONTROLLER_BUTTON_DPAD_LEFT, "DpadLeft", "Cruceta izquierda", true },
    { InputBindingKind::GamepadButton, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, "DpadRight", "Cruceta derecha", true },
};

const int kCatalogCount = static_cast<int>(sizeof(kCatalog) / sizeof(kCatalog[0]));

const PadAxisEntry kPadAxes[] = {
    { -1, "None", "Ninguno" },
    { SDL_CONTROLLER_AXIS_LEFTX, "LeftX", "Stick izquierdo X" },
    { SDL_CONTROLLER_AXIS_LEFTY, "LeftY", "Stick izquierdo Y" },
    { SDL_CONTROLLER_AXIS_RIGHTX, "RightX", "Stick derecho X" },
    { SDL_CONTROLLER_AXIS_RIGHTY, "RightY", "Stick derecho Y" },
    { SDL_CONTROLLER_AXIS_TRIGGERLEFT, "LeftTrigger", "Gatillo izquierdo" },
    { SDL_CONTROLLER_AXIS_TRIGGERRIGHT, "RightTrigger", "Gatillo derecho" },
};

const int kPadAxisCount = static_cast<int>(sizeof(kPadAxes) / sizeof(kPadAxes[0]));

std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::string SanitizeName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (c == '[' || c == ']' || c == '\n' || c == '\r') out.push_back('_');
        else out.push_back(c);
    }
    if (Trim(out).empty()) return "Accion";
    return out;
}

bool ParseBool(const std::string& val) {
    std::string lower = val;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower == "true" || lower == "1" || lower == "yes";
}

const char* KindPrefix(InputBindingKind kind) {
    switch (kind) {
        case InputBindingKind::Key: return "Key";
        case InputBindingKind::MouseButton: return "Mouse";
        case InputBindingKind::GamepadButton: return "Pad";
        default: return "None";
    }
}

InputBinding MakeKey(int scancode) {
    InputBinding binding;
    binding.kind = InputBindingKind::Key;
    binding.code = scancode;
    return binding;
}

} // namespace

SlipInputMap& SlipInputMap::Get() {
    static SlipInputMap instance;
    return instance;
}

SlipInputMap::SlipInputMap() {
    ResetToDefaults();
}

void SlipInputMap::ResetToDefaults() {
    m_Actions.clear();
    m_Axes.clear();
    m_TouchSticks.clear();
    m_TouchStickLive.clear();
    m_TouchStickSample.clear();
    m_TouchActionLive.clear();
    m_TouchActionSample.clear();
    m_TouchActionWas.clear();

    InputAction jump;
    jump.name = "Jump";
    jump.bindings.push_back(MakeKey(SDL_SCANCODE_SPACE));
    m_Actions.push_back(jump);

    InputAxis horizontal;
    horizontal.name = "Horizontal";
    horizontal.negative = MakeKey(SDL_SCANCODE_A);
    horizontal.positive = MakeKey(SDL_SCANCODE_D);
    horizontal.altNegative = MakeKey(SDL_SCANCODE_LEFT);
    horizontal.altPositive = MakeKey(SDL_SCANCODE_RIGHT);
    horizontal.gamepadAxis = SDL_CONTROLLER_AXIS_LEFTX;
    horizontal.deadzone = 0.15f;
    horizontal.invert = false;
    m_Axes.push_back(horizontal);

    InputAxis vertical;
    vertical.name = "Vertical";
    vertical.negative = MakeKey(SDL_SCANCODE_S);
    vertical.positive = MakeKey(SDL_SCANCODE_W);
    vertical.altNegative = MakeKey(SDL_SCANCODE_DOWN);
    vertical.altPositive = MakeKey(SDL_SCANCODE_UP);
    vertical.gamepadAxis = SDL_CONTROLLER_AXIS_LEFTY;
    vertical.deadzone = 0.15f;
    vertical.invert = false;
    m_Axes.push_back(vertical);

    InputTouchStick move;
    move.name = "Move";
    move.axisX = "Horizontal";
    move.axisY = "Vertical";
    move.deadzone = 0.15f;
    m_TouchSticks.push_back(move);
}

bool SlipInputMap::TouchControlsEnabled() {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}

void SlipInputMap::BeginTouchFrame() {
    m_TouchStickSample = m_TouchStickLive;
    m_TouchActionWas = m_TouchActionSample;
    m_TouchActionSample = m_TouchActionLive;
    m_TouchStickLive.clear();
    m_TouchActionLive.clear();
    if (!TouchControlsEnabled()) {
        m_TouchStickSample.clear();
        m_TouchActionSample.clear();
        m_TouchActionWas.clear();
    }
}

void SlipInputMap::SetTouchStick(const std::string& name, float x, float y) {
    if (!TouchControlsEnabled()) return;
    const InputTouchStick* stick = FindTouchStick(name);
    if (!stick) return;
    float zone = std::clamp(stick->deadzone, 0.0f, 0.95f);
    float len = std::sqrt(x * x + y * y);
    if (len < zone || len <= 0.0001f) {
        x = 0.0f;
        y = 0.0f;
    } else {
        float scaled = (len - zone) / (1.0f - zone);
        if (scaled > 1.0f) scaled = 1.0f;
        x = (x / len) * scaled;
        y = (y / len) * scaled;
    }
    m_TouchStickLive[name] = glm::vec2(std::clamp(x, -1.0f, 1.0f), std::clamp(y, -1.0f, 1.0f));
}

void SlipInputMap::SetTouchAction(const std::string& name, bool down) {
    if (!TouchControlsEnabled() || name.empty() || !down) return;
    m_TouchActionLive[name] = true;
}

const InputTouchStick* SlipInputMap::FindTouchStick(const std::string& name) const {
    for (const InputTouchStick& stick : m_TouchSticks) {
        if (stick.name == name) return &stick;
    }
    return nullptr;
}

bool SlipInputMap::TouchActionDown(const std::string& name) const {
    auto it = m_TouchActionSample.find(name);
    return it != m_TouchActionSample.end() && it->second;
}

bool SlipInputMap::TouchActionWasDown(const std::string& name) const {
    auto it = m_TouchActionWas.find(name);
    return it != m_TouchActionWas.end() && it->second;
}

float SlipInputMap::TouchAxisValue(const std::string& axisName) const {
    float best = 0.0f;
    for (const InputTouchStick& stick : m_TouchSticks) {
        auto it = m_TouchStickSample.find(stick.name);
        if (it == m_TouchStickSample.end()) continue;
        float component = 0.0f;
        if (stick.axisX == axisName) component = it->second.x;
        else if (stick.axisY == axisName) component = it->second.y;
        else continue;
        if (std::fabs(component) > std::fabs(best)) best = component;
    }
    return best;
}

int SlipInputMap::BindingOptionCount() { return kCatalogCount; }

const char* SlipInputMap::BindingOptionLabel(int index) {
    if (index < 0 || index >= kCatalogCount) return "(Ninguna)";
    return kCatalog[index].label;
}

bool SlipInputMap::IsBindingOptionSelectable(int index) {
    if (index < 0 || index >= kCatalogCount) return false;
    return kCatalog[index].selectable;
}

InputBinding SlipInputMap::BindingFromOption(int index) {
    InputBinding binding;
    if (index < 0 || index >= kCatalogCount || !kCatalog[index].selectable) return binding;
    binding.kind = kCatalog[index].kind;
    binding.code = kCatalog[index].code;
    return binding;
}

int SlipInputMap::OptionFromBinding(const InputBinding& binding) {
    if (binding.kind == InputBindingKind::None) return 0;
    for (int i = 0; i < kCatalogCount; ++i) {
        if (!kCatalog[i].selectable) continue;
        if (kCatalog[i].kind == binding.kind && kCatalog[i].code == binding.code) return i;
    }
    return 0;
}

int SlipInputMap::GamepadAxisOptionCount() { return kPadAxisCount; }

const char* SlipInputMap::GamepadAxisOptionLabel(int index) {
    if (index < 0 || index >= kPadAxisCount) return "Ninguno";
    return kPadAxes[index].label;
}

int SlipInputMap::GamepadAxisFromOption(int index) {
    if (index < 0 || index >= kPadAxisCount) return -1;
    return kPadAxes[index].axis;
}

int SlipInputMap::OptionFromGamepadAxis(int axis) {
    for (int i = 0; i < kPadAxisCount; ++i) {
        if (kPadAxes[i].axis == axis) return i;
    }
    return 0;
}

std::string SlipInputMap::BindingToString(const InputBinding& binding) {
    if (binding.kind == InputBindingKind::None) return "None";
    for (int i = 0; i < kCatalogCount; ++i) {
        if (!kCatalog[i].selectable) continue;
        if (kCatalog[i].kind == binding.kind && kCatalog[i].code == binding.code) {
            return std::string(KindPrefix(binding.kind)) + ":" + kCatalog[i].token;
        }
    }
    if (binding.kind == InputBindingKind::Key) {
        return "Key:" + std::to_string(binding.code);
    }
    return "None";
}

InputBinding SlipInputMap::BindingFromString(const std::string& token) {
    InputBinding binding;
    std::string value = Trim(token);
    if (value.empty() || value == "None") return binding;

    size_t colon = value.find(':');
    if (colon == std::string::npos) return binding;
    std::string kind = value.substr(0, colon);
    std::string name = value.substr(colon + 1);

    InputBindingKind wanted = InputBindingKind::None;
    if (kind == "Key") wanted = InputBindingKind::Key;
    else if (kind == "Mouse") wanted = InputBindingKind::MouseButton;
    else if (kind == "Pad") wanted = InputBindingKind::GamepadButton;
    else return binding;

    for (int i = 0; i < kCatalogCount; ++i) {
        if (!kCatalog[i].selectable) continue;
        if (kCatalog[i].kind == wanted && name == kCatalog[i].token) {
            binding.kind = wanted;
            binding.code = kCatalog[i].code;
            return binding;
        }
    }

    if (wanted == InputBindingKind::Key) {
        try {
            int code = std::stoi(name);
            if (code >= 0 && code < SDL_NUM_SCANCODES) {
                binding.kind = InputBindingKind::Key;
                binding.code = code;
            }
        } catch (...) {}
    }
    return binding;
}

std::string SlipInputMap::GamepadAxisToString(int axis) {
    for (int i = 0; i < kPadAxisCount; ++i) {
        if (kPadAxes[i].axis == axis) return kPadAxes[i].token;
    }
    return "None";
}

int SlipInputMap::GamepadAxisFromString(const std::string& token) {
    std::string value = Trim(token);
    for (int i = 0; i < kPadAxisCount; ++i) {
        if (value == kPadAxes[i].token) return kPadAxes[i].axis;
    }
    return -1;
}

bool SlipInputMap::IsBindingDown(const InputBinding& binding) {
    if (!SlipInput::HasInstance()) return false;
    const SlipInput& input = SlipInput::Get();
    switch (binding.kind) {
        case InputBindingKind::Key: return input.IsKeyScanDown(binding.code);
        case InputBindingKind::MouseButton: return input.IsMouseDown(binding.code);
        case InputBindingKind::GamepadButton: return input.IsPadButtonDown(binding.code);
        default: return false;
    }
}

bool SlipInputMap::WasBindingDown(const InputBinding& binding) {
    if (!SlipInput::HasInstance()) return false;
    const SlipInput& input = SlipInput::Get();
    switch (binding.kind) {
        case InputBindingKind::Key: return input.WasKeyScanDown(binding.code);
        case InputBindingKind::MouseButton: return input.WasMouseDown(binding.code);
        case InputBindingKind::GamepadButton: return input.WasPadButtonDown(binding.code);
        default: return false;
    }
}

const InputAction* SlipInputMap::FindAction(const std::string& name) const {
    for (const InputAction& action : m_Actions) {
        if (action.name == name) return &action;
    }
    return nullptr;
}

const InputAxis* SlipInputMap::FindAxis(const std::string& name) const {
    for (const InputAxis& axis : m_Axes) {
        if (axis.name == name) return &axis;
    }
    return nullptr;
}

bool SlipInputMap::GetAction(const std::string& name) const {
    if (TouchActionDown(name)) return true;
    const InputAction* action = FindAction(name);
    if (!action) return false;
    for (const InputBinding& binding : action->bindings) {
        if (IsBindingDown(binding)) return true;
    }
    return false;
}

bool SlipInputMap::GetActionDown(const std::string& name) const {
    bool down = TouchActionDown(name);
    bool was = TouchActionWasDown(name);
    const InputAction* action = FindAction(name);
    if (action) {
        for (const InputBinding& binding : action->bindings) {
            down = down || IsBindingDown(binding);
            was = was || WasBindingDown(binding);
        }
    }
    return down && !was;
}

bool SlipInputMap::GetActionUp(const std::string& name) const {
    bool down = TouchActionDown(name);
    bool was = TouchActionWasDown(name);
    const InputAction* action = FindAction(name);
    if (action) {
        for (const InputBinding& binding : action->bindings) {
            down = down || IsBindingDown(binding);
            was = was || WasBindingDown(binding);
        }
    }
    return was && !down;
}

float SlipInputMap::GetAxis(const std::string& name) const {
    const InputAxis* axis = FindAxis(name);
    if (!axis) return 0.0f;

    bool negative = IsBindingDown(axis->negative) || IsBindingDown(axis->altNegative);
    bool positive = IsBindingDown(axis->positive) || IsBindingDown(axis->altPositive);
    float keyValue = 0.0f;
    if (positive && !negative) keyValue = 1.0f;
    else if (negative && !positive) keyValue = -1.0f;

    float padValue = 0.0f;
    if (axis->gamepadAxis >= 0 && SlipInput::HasInstance()) {
        padValue = SlipInput::Get().ReadPadAxis(axis->gamepadAxis);
        float zone = std::clamp(axis->deadzone, 0.0f, 0.95f);
        if (std::fabs(padValue) < zone) padValue = 0.0f;
    }

    float touchValue = TouchAxisValue(name);
    float value = keyValue;
    if (std::fabs(padValue) > std::fabs(value)) value = padValue;
    if (std::fabs(touchValue) > std::fabs(value)) value = touchValue;
    if (axis->invert) value = -value;
    return std::clamp(value, -1.0f, 1.0f);
}

bool SlipInputMap::LoadFromFile(const std::string& filePath) {
    if (filePath.empty() || !SlipFileSystem::Exists(filePath)) return false;
    std::string content;
    if (!SlipFileSystem::ReadAllText(filePath, content)) return false;

    m_Actions.clear();
    m_Axes.clear();
    m_TouchSticks.clear();

    InputAction* action = nullptr;
    InputAxis* axis = nullptr;
    InputTouchStick* touch = nullptr;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            action = nullptr;
            axis = nullptr;
            touch = nullptr;
            const std::string actionPrefix = "[Action.";
            const std::string axisPrefix = "[Axis.";
            const std::string touchPrefix = "[TouchStick.";
            if (line.compare(0, actionPrefix.size(), actionPrefix) == 0) {
                std::string name = line.substr(actionPrefix.size(), line.size() - actionPrefix.size() - 1);
                name = SanitizeName(name);
                for (InputAction& existing : m_Actions) {
                    if (existing.name == name) {
                        action = &existing;
                        break;
                    }
                }
                if (!action) {
                    m_Actions.push_back(InputAction{});
                    action = &m_Actions.back();
                    action->name = name;
                }
            } else if (line.compare(0, axisPrefix.size(), axisPrefix) == 0) {
                std::string name = line.substr(axisPrefix.size(), line.size() - axisPrefix.size() - 1);
                name = SanitizeName(name);
                for (InputAxis& existing : m_Axes) {
                    if (existing.name == name) {
                        axis = &existing;
                        break;
                    }
                }
                if (!axis) {
                    m_Axes.push_back(InputAxis{});
                    axis = &m_Axes.back();
                    axis->name = name;
                    axis->deadzone = 0.15f;
                }
            } else if (line.compare(0, touchPrefix.size(), touchPrefix) == 0) {
                std::string name = line.substr(touchPrefix.size(), line.size() - touchPrefix.size() - 1);
                name = SanitizeName(name);
                for (InputTouchStick& existing : m_TouchSticks) {
                    if (existing.name == name) {
                        touch = &existing;
                        break;
                    }
                }
                if (!touch) {
                    m_TouchSticks.push_back(InputTouchStick{});
                    touch = &m_TouchSticks.back();
                    touch->name = name;
                    touch->deadzone = 0.15f;
                }
            }
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));

        if (action) {
            if (key.rfind("Binding", 0) == 0) {
                int index = 0;
                std::string digits = key.substr(7);
                if (!digits.empty()) {
                    try { index = std::stoi(digits); } catch (...) { index = 0; }
                }
                if (index < 0) index = 0;
                if (static_cast<int>(action->bindings.size()) <= index) {
                    action->bindings.resize(static_cast<size_t>(index) + 1);
                }
                action->bindings[index] = BindingFromString(val);
            }
        } else if (axis) {
            if (key == "Negative") axis->negative = BindingFromString(val);
            else if (key == "Positive") axis->positive = BindingFromString(val);
            else if (key == "AltNegative") axis->altNegative = BindingFromString(val);
            else if (key == "AltPositive") axis->altPositive = BindingFromString(val);
            else if (key == "GamepadAxis") axis->gamepadAxis = GamepadAxisFromString(val);
            else if (key == "Deadzone") {
                try { axis->deadzone = std::clamp(std::stof(val), 0.0f, 0.95f); } catch (...) {}
            } else if (key == "Invert") {
                axis->invert = ParseBool(val);
            }
        } else if (touch) {
            if (key == "AxisX") touch->axisX = SanitizeName(val);
            else if (key == "AxisY") touch->axisY = SanitizeName(val);
            else if (key == "Deadzone") {
                try { touch->deadzone = std::clamp(std::stof(val), 0.0f, 0.95f); } catch (...) {}
            }
        }
    }

    if (m_Actions.empty() && m_Axes.empty()) {
        ResetToDefaults();
        return false;
    }
    if (m_TouchSticks.empty()) {
        InputTouchStick move;
        move.name = "Move";
        move.axisX = "Horizontal";
        move.axisY = "Vertical";
        move.deadzone = 0.15f;
        m_TouchSticks.push_back(move);
    }
    return true;
}

bool SlipInputMap::SaveToFile(const std::string& filePath) {
    if (filePath.empty()) return false;
    std::string dir = SlipFileSystem::GetParentPath(filePath);
    if (!dir.empty() && !SlipFileSystem::Exists(dir)) {
        SlipFileSystem::CreateDirectoryPath(dir);
    }

    for (InputAction& action : m_Actions) action.name = SanitizeName(action.name);
    for (InputAxis& axis : m_Axes) axis.name = SanitizeName(axis.name);
    for (InputTouchStick& stick : m_TouchSticks) {
        stick.name = SanitizeName(stick.name);
        stick.axisX = SanitizeName(stick.axisX);
        stick.axisY = SanitizeName(stick.axisY);
    }

    std::stringstream ss;
    ss << "; SlipEngine Input Settings\n";
    for (const InputAction& action : m_Actions) {
        ss << "[Action." << action.name << "]\n";
        for (size_t i = 0; i < action.bindings.size(); ++i) {
            ss << "Binding" << i << "=" << BindingToString(action.bindings[i]) << "\n";
        }
        if (action.bindings.empty()) ss << "Binding0=None\n";
    }
    for (const InputAxis& axis : m_Axes) {
        ss << "[Axis." << axis.name << "]\n";
        ss << "Negative=" << BindingToString(axis.negative) << "\n";
        ss << "Positive=" << BindingToString(axis.positive) << "\n";
        ss << "AltNegative=" << BindingToString(axis.altNegative) << "\n";
        ss << "AltPositive=" << BindingToString(axis.altPositive) << "\n";
        ss << "GamepadAxis=" << GamepadAxisToString(axis.gamepadAxis) << "\n";
        ss << "Deadzone=" << axis.deadzone << "\n";
        ss << "Invert=" << (axis.invert ? "true" : "false") << "\n";
    }
    for (const InputTouchStick& stick : m_TouchSticks) {
        ss << "[TouchStick." << stick.name << "]\n";
        ss << "AxisX=" << stick.axisX << "\n";
        ss << "AxisY=" << stick.axisY << "\n";
        ss << "Deadzone=" << stick.deadzone << "\n";
    }
    return SlipFileSystem::WriteAllText(filePath, ss.str());
}
