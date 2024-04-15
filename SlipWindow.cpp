#include "SlipWindow.h"

#include <iostream>
#include <assert.h>

#include <SlipFrameBuffer.h>

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    SlipWindow::Get().setWidth(width);
    SlipWindow::Get().setHeight(height);
    glfwSetWindowSize(window, width, height);
    glViewport(0, 0, width, height);
    if (&SlipFrameBuffer::Get() != nullptr)
    {
        SlipFrameBuffer::Get().resize();
    }
}

SlipWindow::SlipWindow(const char* title, int width, int height) : title(title), width(width), height(height)
{
    assert(!m_instance && "Is already open the application...");
    m_instance = this;

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (m_window == NULL)
    {
        std::cout << "Failed to create " << title << " window" << std::endl;
        glfwTerminate();
        return;
    }
    glfwMakeContextCurrent(m_window);
    glfwSetWindowAspectRatio(m_window, width, height);

    glfwSetFramebufferSizeCallback(m_window, framebuffer_size_callback);

    glfwSwapInterval(0);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return;
    }
}
