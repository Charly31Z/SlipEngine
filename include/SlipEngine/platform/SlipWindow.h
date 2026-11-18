#pragma once

#ifndef SLIP_WINDOW_H
#define SLIP_WINDOW_H

#include "SlipEngine.h"

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    #include <GLES3/gl3.h>
    #include <GLES3/gl32.h>
#else
    #include <glad/glad.h>
#endif
#include <SDL2/SDL.h>

class SLIPENGINE_API SlipWindow
{
private:
	const char* title;
	int width, height;

	SDL_Window* m_window;
	SDL_GLContext m_glContext;

	inline static SlipWindow* m_instance;
public:
	inline static SlipWindow& Get() { return *m_instance; }

	SDL_Window* getWindow() { return m_window; }
	SDL_GLContext getGLContext() { return m_glContext; }

	int getWidth() { return width; }
	int getHeight() { return height; }

	void setWidth(int x) { width = x; }
	void setHeight(int x) { height = x; }

	void OnResize(int newWidth, int newHeight);
	void SwapBuffers() { if (m_window) SDL_GL_SwapWindow(m_window); }
	void SetVSync(bool enabled);
	bool IsValid() const { return m_window != nullptr && m_glContext != nullptr; }

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
	inline static bool InitGLAD() { return true; }
#else
	inline static bool InitGLAD() { return gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress) != 0; }
#endif

	SlipWindow(const char* title, int width, int height);
	~SlipWindow();
};

#endif // !SLIP_WINDOW_H
