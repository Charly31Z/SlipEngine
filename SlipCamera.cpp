#include "SlipCamera.h"

SlipCamera::SlipCamera(glm::vec3 position, glm::vec3 up, float yaw, float pitch) : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY), Zoom(ZOOM)
{
    Position = position;
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
        // near face
        {(width * near), (height * near), -near, 1.f},
        {-(width * near), (height * near), -near, 1.f},
        {(width * near), -(height * near), -near, 1.f},
        {-(width * near), -(height * near), -near, 1.f},

        // far face
        {(width * far), (height * far), far, 1.f},
        {-(width * far), (height * far), far , 1.f},
        {(width * far), -(height * far), far , 1.f},
        {-(width * far), -(height * far),far, 1.f},
    };

    glm::vec3 v[8u];
    for (int i = 0; i < 8; i++)
    {
        glm::vec4 ff = inv * f[i];
        v[i].x = ff.x / ff.w;
        v[i].y = ff.y / ff.w;
        v[i].z = ff.z / ff.w;
    }

    SlipDebug::drawLines(v[0], v[1], Color::yellow);
    SlipDebug::drawLines(v[0], v[2], Color::yellow);
    SlipDebug::drawLines(v[3], v[1], Color::yellow);
    SlipDebug::drawLines(v[3], v[2], Color::yellow);

    SlipDebug::drawLines(v[4], v[5], Color::yellow);
    SlipDebug::drawLines(v[4], v[6], Color::yellow);
    SlipDebug::drawLines(v[7], v[5], Color::yellow);
    SlipDebug::drawLines(v[7], v[6], Color::yellow);

    SlipDebug::drawLines(v[0], v[4], Color::yellow);
    SlipDebug::drawLines(v[1], v[5], Color::yellow);
    SlipDebug::drawLines(v[3], v[7], Color::yellow);
    SlipDebug::drawLines(v[2], v[6], Color::yellow);
}

glm::mat4 SlipCamera::GetViewMatrix()
{
    return glm::lookAt(Position, Position + Front, Up);
}

glm::mat4 SlipCamera::GetProjectionMatrix()
{
    return glm::perspective(glm::radians(Zoom), (float)width / (float)height, near, far);
}

glm::mat4 SlipCamera::GetOrthographicMatrix()
{
    float aspect = (float)width / height;
    return glm::ortho(-aspect, aspect, -1.0f, 1.0f);
}

void SlipCamera::ProcessKeyboard(Camera_Movement direction, float deltaTime)
{
    float velocity = MovementSpeed * deltaTime;
    if (direction == FORWARD)
        Position += Front * velocity;
    if (direction == BACKWARD)
        Position -= Front * velocity;
    if (direction == LEFT)
        Position -= Right * velocity;
    if (direction == RIGHT)
        Position += Right * velocity;
    if (direction == DOWN)
        Position -= Up * velocity;
    if (direction == UP)
        Position += Up * velocity;
}

void SlipCamera::ProcessWindow(float width, float height)
{
    this->width = width;
    this->height = height;
}

void SlipCamera::ProcessMouseMovement(float xoffset, float yoffset, GLboolean constrainPitch)
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
    if (Zoom < 1.0f)
        Zoom = 1.0f;
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