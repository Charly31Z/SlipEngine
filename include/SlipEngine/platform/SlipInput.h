#pragma once

#ifndef SLIP_INPUT_H
#define SLIP_INPUT_H

#include "SlipEngine.h"
#include "SlipWindow.h"
#include <glm/glm.hpp>
#include <SDL2/SDL.h>
#include <vector>

class SLIPENGINE_API SlipInput
{
private:
	glm::vec2 mousePos;
	glm::vec2 mouseScroll;
	glm::vec2 m_mouseDelta = glm::vec2(0.0f);
	glm::vec2 m_mouseDeltaAccum = glm::vec2(0.0f);
	glm::vec2 m_wheelFrame = glm::vec2(0.0f);
	glm::vec2 m_wheelAccum = glm::vec2(0.0f);
	bool m_requestLock = false;
	bool m_requestVisible = true;
	bool m_userRelease = false;

	std::vector<uint8_t> m_currKeys;
	std::vector<uint8_t> m_prevKeys;
	Uint32 m_currMouse = 0;
	Uint32 m_prevMouse = 0;
	std::vector<uint8_t> m_currPad;
	std::vector<uint8_t> m_prevPad;
	std::vector<SDL_GameController*> m_controllers;

	void OpenConnectedControllers();
	void AddController(int deviceIndex);
	void RemoveController(SDL_JoystickID instanceId);
	void ApplyCursorMode();

	inline static SlipInput* m_instance;
public:
	inline static SlipInput& Get() { return *m_instance; }
	static bool HasInstance() { return m_instance != nullptr; }

	// Copia el estado anterior y toma una foto del teclado, ratón y mando.
	static void BeginFrame();

	int GetKey(int key);
	int GetMouseButton(int button);

	bool IsKeyScanDown(int scancode) const;
	bool WasKeyScanDown(int scancode) const;
	bool WasKeyPressed(int scancode) const;
	bool IsMouseDown(int button) const;
	bool WasMouseDown(int button) const;
	bool IsPadButtonDown(int button) const;
	bool WasPadButtonDown(int button) const;

	// Eje de mando en -1..1 (gatillos en 0..1). 0 si no hay mando.
	float ReadPadAxis(int axis) const;

	// Primera pulsación nueva del frame, salvo Escape. kind: 1 tecla, 2 ratón, 3 mando.
	bool PollPressedBinding(int& outKind, int& outCode) const;

	void SetMousePos(float x, float y) { mousePos = glm::vec2(x, y); }
	void SetMouseScroll(float x, float y) { mouseScroll = glm::vec2(x, y); }

	glm::vec2& GetMousePos();
	glm::vec2& GetMouseScroll() { return mouseScroll; }
	glm::vec2 GetMouseDelta() const { return m_mouseDelta; }
	glm::vec2 GetMouseWheel() const { return m_wheelFrame; }

	void SetCursorLocked(bool locked);
	void SetCursorVisible(bool visible);
	bool IsCursorLocked() const { return m_requestLock && !m_userRelease; }
	bool IsCursorVisible() const;
	bool WantsCursorCapture() const { return m_requestLock || !m_requestVisible; }
	bool IsCursorReleasedByUser() const { return m_userRelease; }
	void ToggleCursorRelease();
	void ResetCursor();

	static void ProcessEvent(const SDL_Event& event);

	SlipInput();
	~SlipInput();
};

#endif // !SLIP_INPUT_H
