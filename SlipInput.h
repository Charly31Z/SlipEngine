#pragma once

#ifndef SLIP_INPUT_H
#define SLIP_INPUT_H

#include "SlipWindow.h"

#include <glm/glm.hpp>

class SlipInput
{
private:
	glm::vec2* mousePos;
	glm::vec2* mouseScroll;

	inline static SlipInput* m_instance;
public:
	inline static SlipInput& Get() { return *m_instance; }

	int GetKey(int key) { return glfwGetKey(&SlipWindow::Get().getWindow(), key); }
	int GetMouseButton(int button) { return glfwGetMouseButton(&SlipWindow::Get().getWindow(), button); }

	void SetMousePos(float x, float y) { mousePos = new glm::vec2(x, y); }
	void SetMouseScroll(float x, float y) { mouseScroll = new glm::vec2(x, y); }

	glm::vec2& GetMousePos() { return *mousePos; }
	glm::vec2& GetMouseScroll() { return *mouseScroll; }

	SlipInput();
	~SlipInput() { delete m_instance; }
};

#endif // !SLIP_INPUT_H