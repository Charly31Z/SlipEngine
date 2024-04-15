#pragma once

#ifndef SLIP_WINDOW_H
#define SLIP_WINDOW_H

#include <glad/glad.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

class SlipWindow
{
private:
	const char* title;
	int width, height;

	GLFWwindow* m_window;

	inline static SlipWindow* m_instance;
public:
	inline static SlipWindow& Get() { return *m_instance; }

	GLFWwindow& getWindow() { return *m_window; }

	int getWidth() { return width; }
	int getHeight() { return height; }

	void setWidth(int x) { width = x; }
	void setHeight(int x) { height = x; }

	SlipWindow(const char* title, int width, int height);
	~SlipWindow() { delete m_instance; }
};

#endif // !SLIP_WINDOW_H