#include "SlipInput.h"

float lastX, lastY;
bool firstMouse = true;

void mouse_callback(GLFWwindow* window, double xposIn, double yposIn)
{
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    SlipInput::Get().SetMousePos(xpos, ypos);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    float xpos = static_cast<float>(xoffset);
    float ypos = static_cast<float>(yoffset);

    SlipInput::Get().SetMouseScroll(xpos, ypos);
}

SlipInput::SlipInput()
{
    assert(!m_instance && "SlipInput has initialized...");
    m_instance = this;

	glfwSetCursorPosCallback(&SlipWindow::Get().getWindow(), mouse_callback);
    glfwSetScrollCallback(&SlipWindow::Get().getWindow(), scroll_callback);
}
