#pragma once

#ifndef SLIP_INPUT_MAP_H
#define SLIP_INPUT_MAP_H

#include "SlipEngine/core/SlipExport.h"
#include <glm/vec2.hpp>
#include <string>
#include <unordered_map>
#include <vector>

enum class InputBindingKind : int {
    None = 0,
    Key = 1,
    MouseButton = 2,
    GamepadButton = 3
};

struct SLIPENGINE_API InputBinding {
    InputBindingKind kind = InputBindingKind::None;
    int code = 0;
};

struct SLIPENGINE_API InputAction {
    std::string name;
    std::vector<InputBinding> bindings;
};

struct SLIPENGINE_API InputAxis {
    std::string name;
    InputBinding negative;
    InputBinding positive;
    InputBinding altNegative;
    InputBinding altPositive;
    int gamepadAxis = -1;
    float deadzone = 0.15f;
    bool invert = false;
};

struct SLIPENGINE_API InputTouchStick {
    std::string name;
    std::string axisX = "Horizontal";
    std::string axisY = "Vertical";
    float deadzone = 0.15f;
};

class SLIPENGINE_API SlipInputMap {
public:
    static SlipInputMap& Get();

    std::vector<InputAction>& GetActions() { return m_Actions; }
    const std::vector<InputAction>& GetActions() const { return m_Actions; }
    std::vector<InputAxis>& GetAxes() { return m_Axes; }
    const std::vector<InputAxis>& GetAxes() const { return m_Axes; }
    std::vector<InputTouchStick>& GetTouchSticks() { return m_TouchSticks; }
    const std::vector<InputTouchStick>& GetTouchSticks() const { return m_TouchSticks; }

    void ResetToDefaults();
    bool LoadFromFile(const std::string& filePath);
    bool SaveToFile(const std::string& filePath);

    // Copia el toque del frame anterior para que los scripts lo lean, y vacía el valor en vivo.
    void BeginTouchFrame();
    void SetTouchStick(const std::string& name, float x, float y);
    void SetTouchAction(const std::string& name, bool down);
    static bool TouchControlsEnabled();

    bool GetAction(const std::string& name) const;
    bool GetActionDown(const std::string& name) const;
    bool GetActionUp(const std::string& name) const;
    float GetAxis(const std::string& name) const;

    static int BindingOptionCount();
    static const char* BindingOptionLabel(int index);
    static bool IsBindingOptionSelectable(int index);
    static InputBinding BindingFromOption(int index);
    static int OptionFromBinding(const InputBinding& binding);

    static int GamepadAxisOptionCount();
    static const char* GamepadAxisOptionLabel(int index);
    static int GamepadAxisFromOption(int index);
    static int OptionFromGamepadAxis(int axis);

    static std::string BindingToString(const InputBinding& binding);
    static InputBinding BindingFromString(const std::string& token);
    static std::string GamepadAxisToString(int axis);
    static int GamepadAxisFromString(const std::string& token);

private:
    SlipInputMap();

    const InputAction* FindAction(const std::string& name) const;
    const InputAxis* FindAxis(const std::string& name) const;
    const InputTouchStick* FindTouchStick(const std::string& name) const;
    bool TouchActionDown(const std::string& name) const;
    bool TouchActionWasDown(const std::string& name) const;
    float TouchAxisValue(const std::string& axisName) const;
    static bool IsBindingDown(const InputBinding& binding);
    static bool WasBindingDown(const InputBinding& binding);

    std::vector<InputAction> m_Actions;
    std::vector<InputAxis> m_Axes;
    std::vector<InputTouchStick> m_TouchSticks;
    std::unordered_map<std::string, glm::vec2> m_TouchStickLive;
    std::unordered_map<std::string, glm::vec2> m_TouchStickSample;
    std::unordered_map<std::string, bool> m_TouchActionLive;
    std::unordered_map<std::string, bool> m_TouchActionSample;
    std::unordered_map<std::string, bool> m_TouchActionWas;
};

#endif
