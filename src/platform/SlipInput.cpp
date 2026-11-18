#include "SlipInput.h"
#include <algorithm>
#include <cmath>
#include <assert.h>

SlipInput::SlipInput() : mousePos(0.0f, 0.0f), mouseScroll(0.0f, 0.0f)
{
    assert(!m_instance && "SlipInput has initialized...");
    m_currKeys.assign(SDL_NUM_SCANCODES, 0);
    m_prevKeys.assign(SDL_NUM_SCANCODES, 0);
    m_currPad.assign(SDL_CONTROLLER_BUTTON_MAX, 0);
    m_prevPad.assign(SDL_CONTROLLER_BUTTON_MAX, 0);
    m_instance = this;
    OpenConnectedControllers();
}

SlipInput::~SlipInput()
{
    for (SDL_GameController* controller : m_controllers) {
        if (controller) SDL_GameControllerClose(controller);
    }
    m_controllers.clear();
    m_instance = nullptr;
}

void SlipInput::OpenConnectedControllers()
{
    if ((SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER) == 0) return;
    int count = SDL_NumJoysticks();
    for (int i = 0; i < count; ++i) {
        AddController(i);
    }
}

void SlipInput::AddController(int deviceIndex)
{
    if ((SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER) == 0) return;
    if (!SDL_IsGameController(deviceIndex)) return;
    SDL_GameController* controller = SDL_GameControllerOpen(deviceIndex);
    if (!controller) return;
    for (SDL_GameController* existing : m_controllers) {
        if (existing == controller) return;
    }
    m_controllers.push_back(controller);
}

void SlipInput::RemoveController(SDL_JoystickID instanceId)
{
    SDL_GameController* controller = SDL_GameControllerFromInstanceID(instanceId);
    if (!controller) return;
    auto it = std::find(m_controllers.begin(), m_controllers.end(), controller);
    if (it != m_controllers.end()) m_controllers.erase(it);
    SDL_GameControllerClose(controller);
}

void SlipInput::BeginFrame()
{
    if (!m_instance) return;

    m_instance->m_prevKeys = m_instance->m_currKeys;
    m_instance->m_prevMouse = m_instance->m_currMouse;
    m_instance->m_prevPad = m_instance->m_currPad;

    std::fill(m_instance->m_currKeys.begin(), m_instance->m_currKeys.end(), 0);
    std::fill(m_instance->m_currPad.begin(), m_instance->m_currPad.end(), 0);
    m_instance->m_currMouse = 0;

    if (m_instance->m_currKeys.size() < SDL_NUM_SCANCODES) {
        m_instance->m_currKeys.assign(SDL_NUM_SCANCODES, 0);
        m_instance->m_prevKeys.resize(SDL_NUM_SCANCODES, 0);
    }
    if (m_instance->m_currPad.size() < SDL_CONTROLLER_BUTTON_MAX) {
        m_instance->m_currPad.assign(SDL_CONTROLLER_BUTTON_MAX, 0);
        m_instance->m_prevPad.resize(SDL_CONTROLLER_BUTTON_MAX, 0);
    }

    m_instance->m_mouseDelta = m_instance->m_mouseDeltaAccum;
    m_instance->m_mouseDeltaAccum = glm::vec2(0.0f);
    m_instance->m_wheelFrame = m_instance->m_wheelAccum;
    m_instance->m_wheelAccum = glm::vec2(0.0f);

    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0) {
        int numKeys = 0;
        const Uint8* state = SDL_GetKeyboardState(&numKeys);
        if (state) {
            int count = numKeys < static_cast<int>(SDL_NUM_SCANCODES) ? numKeys : static_cast<int>(SDL_NUM_SCANCODES);
            for (int i = 0; i < count; ++i) {
                m_instance->m_currKeys[i] = state[i];
            }
        }
        m_instance->m_currMouse = SDL_GetMouseState(nullptr, nullptr);
    }

    if ((SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER) != 0) {
        for (SDL_GameController* controller : m_instance->m_controllers) {
            if (!controller || SDL_GameControllerGetAttached(controller) != SDL_TRUE) continue;
            for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; ++button) {
                if (SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(button)) == 1) {
                    m_instance->m_currPad[button] = 1;
                }
            }
        }
    }
}

int SlipInput::GetKey(int key)
{
    const Uint8* state = nullptr;
    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0) {
        state = SDL_GetKeyboardState(NULL);
    }
    if (!state) return 0;

    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
    if (key >= 0 && key < SDL_NUM_SCANCODES)
    {
        scancode = static_cast<SDL_Scancode>(key);
    }
    else
    {
        scancode = SDL_GetScancodeFromKey(key);
    }

    if (scancode >= 0 && scancode < SDL_NUM_SCANCODES)
    {
        return state[scancode] ? 1 : 0;
    }
    return 0;
}

int SlipInput::GetMouseButton(int button)
{
    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) return 0;
    Uint32 mask = SDL_GetMouseState(NULL, NULL);
    Uint32 buttonMask = 0;

    if (button == SDL_BUTTON_LEFT || button == 0)
        buttonMask = SDL_BUTTON(SDL_BUTTON_LEFT);
    else if (button == SDL_BUTTON_RIGHT || button == 1)
        buttonMask = SDL_BUTTON(SDL_BUTTON_RIGHT);
    else if (button == SDL_BUTTON_MIDDLE || button == 2)
        buttonMask = SDL_BUTTON(SDL_BUTTON_MIDDLE);
    else
        buttonMask = SDL_BUTTON(button);

    return (mask & buttonMask) ? 1 : 0;
}

bool SlipInput::IsKeyScanDown(int scancode) const
{
    if (scancode < 0 || scancode >= static_cast<int>(m_currKeys.size())) return false;
    return m_currKeys[scancode] != 0;
}

bool SlipInput::WasKeyScanDown(int scancode) const
{
    if (scancode < 0 || scancode >= static_cast<int>(m_prevKeys.size())) return false;
    return m_prevKeys[scancode] != 0;
}

bool SlipInput::WasKeyPressed(int scancode) const
{
    return IsKeyScanDown(scancode) && !WasKeyScanDown(scancode);
}

bool SlipInput::IsMouseDown(int button) const
{
    if (button <= 0) return false;
    return (m_currMouse & SDL_BUTTON(button)) != 0;
}

bool SlipInput::WasMouseDown(int button) const
{
    if (button <= 0) return false;
    return (m_prevMouse & SDL_BUTTON(button)) != 0;
}

bool SlipInput::IsPadButtonDown(int button) const
{
    if (button < 0 || button >= static_cast<int>(m_currPad.size())) return false;
    return m_currPad[button] != 0;
}

bool SlipInput::WasPadButtonDown(int button) const
{
    if (button < 0 || button >= static_cast<int>(m_prevPad.size())) return false;
    return m_prevPad[button] != 0;
}

float SlipInput::ReadPadAxis(int axis) const
{
    if (axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX) return 0.0f;
    if ((SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER) == 0) return 0.0f;

    const bool trigger = axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
    float best = 0.0f;
    bool any = false;
    for (SDL_GameController* controller : m_controllers) {
        if (!controller || SDL_GameControllerGetAttached(controller) != SDL_TRUE) continue;
        Sint16 raw = SDL_GameControllerGetAxis(controller, static_cast<SDL_GameControllerAxis>(axis));
        float value;
        if (trigger) {
            value = raw <= 0 ? 0.0f : static_cast<float>(raw) / 32767.0f;
        } else {
            value = static_cast<float>(raw) / 32767.0f;
        }
        if (value > 1.0f) value = 1.0f;
        if (value < -1.0f) value = -1.0f;
        if (!any || std::fabs(value) > std::fabs(best)) {
            best = value;
            any = true;
        }
    }
    return best;
}

bool SlipInput::PollPressedBinding(int& outKind, int& outCode) const
{
    for (int scancode = 0; scancode < static_cast<int>(m_currKeys.size()) && scancode < SDL_NUM_SCANCODES; ++scancode) {
        if (scancode == SDL_SCANCODE_ESCAPE) continue;
        if (IsKeyScanDown(scancode) && !WasKeyScanDown(scancode)) {
            outKind = 1;
            outCode = scancode;
            return true;
        }
    }

    const int mouseButtons[] = { SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE };
    for (int button : mouseButtons) {
        if (IsMouseDown(button) && !WasMouseDown(button)) {
            outKind = 2;
            outCode = button;
            return true;
        }
    }

    for (int button = 0; button < static_cast<int>(m_currPad.size()) && button < SDL_CONTROLLER_BUTTON_MAX; ++button) {
        if (IsPadButtonDown(button) && !WasPadButtonDown(button)) {
            outKind = 3;
            outCode = button;
            return true;
        }
    }
    return false;
}

bool SlipInput::IsCursorVisible() const
{
    if (m_userRelease) return true;
    if (m_requestLock) return false;
    return m_requestVisible;
}

void SlipInput::ApplyCursorMode()
{
    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) return;
    const bool lock = IsCursorLocked();
    const bool visible = IsCursorVisible();
    SDL_SetRelativeMouseMode(lock ? SDL_TRUE : SDL_FALSE);
    if (!lock) {
        SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);
    }
}

void SlipInput::SetCursorLocked(bool locked)
{
    m_requestLock = locked;
    if (!WantsCursorCapture()) m_userRelease = false;
    ApplyCursorMode();
}

void SlipInput::SetCursorVisible(bool visible)
{
    m_requestVisible = visible;
    if (!WantsCursorCapture()) m_userRelease = false;
    ApplyCursorMode();
}

void SlipInput::ToggleCursorRelease()
{
    if (!WantsCursorCapture()) {
        m_userRelease = false;
        ApplyCursorMode();
        return;
    }
    m_userRelease = !m_userRelease;
    ApplyCursorMode();
}

void SlipInput::ResetCursor()
{
    m_requestLock = false;
    m_requestVisible = true;
    m_userRelease = false;
    ApplyCursorMode();
}

glm::vec2& SlipInput::GetMousePos()
{
    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) return mousePos;
    int x, y;
    SDL_GetMouseState(&x, &y);
    mousePos.x = static_cast<float>(x);
    mousePos.y = static_cast<float>(y);
    return mousePos;
}

void SlipInput::ProcessEvent(const SDL_Event& event)
{
    if (!m_instance) return;

    if (event.type == SDL_CONTROLLERDEVICEADDED)
    {
        m_instance->AddController(event.cdevice.which);
    }
    else if (event.type == SDL_CONTROLLERDEVICEREMOVED)
    {
        m_instance->RemoveController(event.cdevice.which);
    }
    else if (event.type == SDL_MOUSEMOTION)
    {
        m_instance->mousePos.x = static_cast<float>(event.motion.x);
        m_instance->mousePos.y = static_cast<float>(event.motion.y);
        m_instance->m_mouseDeltaAccum.x += static_cast<float>(event.motion.xrel);
        m_instance->m_mouseDeltaAccum.y += static_cast<float>(event.motion.yrel);
    }
    else if (event.type == SDL_MOUSEWHEEL)
    {
        m_instance->mouseScroll.x = static_cast<float>(event.wheel.x);
        m_instance->mouseScroll.y = static_cast<float>(event.wheel.y);
        m_instance->m_wheelAccum.x += static_cast<float>(event.wheel.x);
        m_instance->m_wheelAccum.y += static_cast<float>(event.wheel.y);
    }
    else if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_RESIZED || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
    {
        SlipWindow::Get().OnResize(event.window.data1, event.window.data2);
    }
}
