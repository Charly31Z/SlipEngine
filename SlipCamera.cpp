#include "SlipCamera.h"

#include <glad/glad.h>

#include "SlipDebug.h"

#include "SlipInput.h"

#include "SlipGlobals.h"

#include "SlipWindow.h"

void SlipCamera::Update()
{
    /*if (SlipInput::Get().GetKey(GLFW_KEY_W) == GLFW_PRESS)
        ProcessKeyboard(Camera_Movement::FORWARD, SlipGlobals::Get().GetDeltaTime());
    if (SlipInput::Get().GetKey(GLFW_KEY_A) == GLFW_PRESS)
        ProcessKeyboard(Camera_Movement::LEFT, SlipGlobals::Get().GetDeltaTime());
    if (SlipInput::Get().GetKey(GLFW_KEY_S) == GLFW_PRESS)
        ProcessKeyboard(Camera_Movement::BACKWARD, SlipGlobals::Get().GetDeltaTime());
    if (SlipInput::Get().GetKey(GLFW_KEY_D) == GLFW_PRESS)
        ProcessKeyboard(Camera_Movement::RIGHT, SlipGlobals::Get().GetDeltaTime());
    if (SlipInput::Get().GetKey(GLFW_KEY_E) == GLFW_PRESS)
        ProcessKeyboard(Camera_Movement::UP, SlipGlobals::Get().GetDeltaTime());
    if (SlipInput::Get().GetKey(GLFW_KEY_Q) == GLFW_PRESS)
        ProcessKeyboard(Camera_Movement::DOWN, SlipGlobals::Get().GetDeltaTime());
    if (SlipInput::Get().GetKey(GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        MovementSpeed = 30.f;
    if (SlipInput::Get().GetKey(GLFW_KEY_LEFT_SHIFT) == GLFW_RELEASE)
        MovementSpeed = 2.5f;*/

    if (SlipInput::Get().GetKey(GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwDestroyWindow(&SlipWindow::Get().getWindow());

    if (canMove)
    {
        glm::vec2 posMouse = SlipInput::Get().GetMousePos();

        if (firstMouse)
        {
            lastX = posMouse.x;
            lastY = posMouse.y;
            firstMouse = false;
        }

        float xoffset = posMouse.x - lastX;
        float yoffset = lastY - posMouse.y; // reversed since y-coordinates go from bottom to top
        lastX = posMouse.x;
        lastY = posMouse.y;

        ProcessMouseMovement(xoffset, yoffset);
    }

    glm::vec2* scrollMouse = &SlipInput::Get().GetMouseScroll();

    if (scrollMouse != nullptr)
        ProcessMouseScroll(scrollMouse->y);

    if (SlipInput::Get().GetMouseButton(1) == GLFW_PRESS)
    {
        canMove = true;
    }
    else {
        canMove = false;
        firstMouse = true;
    }

    ProcessWindow(SlipWindow::Get().getWidth(), SlipWindow::Get().getHeight());
}

SlipCamera::SlipCamera(glm::vec3 position, glm::vec3 up, float yaw, float pitch) : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY), Zoom(ZOOM)
{
    this->position = position;
    WorldUp = up;
    Yaw = yaw;
    Pitch = pitch;
    updateCameraVectors();
}

void SlipCamera::drawDebug()
{
    glm::mat4 inv = glm::inverse(GetViewMatrix() * GetProjectionMatrix());

    glm::vec4 f[8u] =
    {
        // nCam face
        {(width * nCam), (height * nCam), -nCam, 1.f},
        {-(width * nCam), (height * nCam), -nCam, 1.f},
        {(width * nCam), -(height * nCam), -nCam, 1.f},
        {-(width * nCam), -(height * nCam), -nCam, 1.f},

        // fCam face
        {(width * fCam), (height * fCam), fCam, 1.f},
        {-(width * fCam), (height * fCam), fCam , 1.f},
        {(width * fCam), -(height * fCam), fCam , 1.f},
        {-(width * fCam), -(height * fCam),fCam, 1.f},
    };

    glm::vec3 v[8u];
    for (int i = 0; i < 8; i++)
    {
        glm::vec4 ff = inv * f[i];
        v[i].x = ff.x / ff.w;
        v[i].y = ff.y / ff.w;
        v[i].z = ff.z / ff.w;
    }

    SlipDebug::Get().drawLines(v[0], v[1], Color::yellow);
    SlipDebug::Get().drawLines(v[0], v[2], Color::yellow);
    SlipDebug::Get().drawLines(v[3], v[1], Color::yellow);
    SlipDebug::Get().drawLines(v[3], v[2], Color::yellow);

    SlipDebug::Get().drawLines(v[4], v[5], Color::yellow);
    SlipDebug::Get().drawLines(v[4], v[6], Color::yellow);
    SlipDebug::Get().drawLines(v[7], v[5], Color::yellow);
    SlipDebug::Get().drawLines(v[7], v[6], Color::yellow);

    SlipDebug::Get().drawLines(v[0], v[4], Color::yellow);
    SlipDebug::Get().drawLines(v[1], v[5], Color::yellow);
    SlipDebug::Get().drawLines(v[3], v[7], Color::yellow);
    SlipDebug::Get().drawLines(v[2], v[6], Color::yellow);
}

glm::mat4 SlipCamera::GetViewMatrix()
{
    return glm::lookAt(position, position + Front, Up);
}

glm::mat4 SlipCamera::GetProjectionMatrix()
{
    return glm::perspective(glm::radians(Zoom), (float)width / (float)height, nCam, fCam);
}

glm::mat4 SlipCamera::GetOrthographicMatrix()
{
    float aspect = (float)width / height;
    return glm::ortho(-aspect, aspect, -10.0f, 20.0f);
    //return glm::ortho(0.f, (float)SlipWindow::Get().getWidth(), 0.f, (float)SlipWindow::Get().getHeight());
}

void SlipCamera::ProcessKeyboard(Camera_Movement direction, float deltaTime)
{
    float velocity = MovementSpeed * deltaTime;
    if (direction == FORWARD)
        this->position += Front * velocity;
    if (direction == BACKWARD)
        this->position -= Front * velocity;
    if (direction == LEFT)
        this->position -= Right * velocity;
    if (direction == RIGHT)
        this->position += Right * velocity;
    if (direction == DOWN)
        this->position -= Up * velocity;
    if (direction == UP)
        this->position += Up * velocity;
}

void SlipCamera::ProcessWindow(float width, float height)
{
    this->width = width;
    this->height = height;
}

void SlipCamera::ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch)
{
    xoffset *= MouseSensitivity;
    yoffset *= MouseSensitivity;

    Yaw += xoffset;
    Pitch += yoffset;

    // make sure that when pitch is out of bounds, screen doesn't get flipped
    if (constrainPitch)
    {
        if (Pitch > 89.0f)
            Pitch = 89.0f;
        if (Pitch < -89.0f)
            Pitch = -89.0f;
    }

    // update Front, Right and Up Vectors using the updated Euler angles
    updateCameraVectors();
}

void SlipCamera::ProcessMouseScroll(float yoffset)
{
    Zoom -= (float)yoffset;
    if (Zoom < 20.0f)
        Zoom = 20.0f;
    if (Zoom > 75.0f)
        Zoom = 75.0f;
}

void SlipCamera::updateCameraVectors()
{
    // calculate the new Front vector
    glm::vec3 front;
    front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    front.y = sin(glm::radians(Pitch));
    front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    Front = glm::normalize(front);
    // also re-calculate the Right and Up vector
    Right = glm::normalize(glm::cross(Front, WorldUp));  // normalize the vectors, because their length gets closer to 0 the more you look up or down which results in slower movement.
    Up = glm::normalize(glm::cross(Right, Front));
}