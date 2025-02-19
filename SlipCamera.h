#pragma once

#ifndef SLIP_CAMERA_H
#define SLIP_CAMERA_H

#include "SlipEngine.h"

#include "SlipEntity.h"

// Defines several possible options for camera movement. Used as abstraction to stay away from window-system specific input methods
enum Camera_Movement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN,
    SHIFT,
    RESHIFT
};

// Default camera values
const float YAW = -90.0f;
const float PITCH = 0.0f;
const float SPEED = 2.5f;
const float SENSITIVITY = 0.1f;
const float ZOOM = 75.0f;

class SLIPENGINE_API SlipCamera
{
public:
    void Update();

    // camera Attributes
    glm::vec3 position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;
    // euler Angles
    float Yaw;
    float Pitch;
    // camera options
    float MovementSpeed;
    float MouseSensitivity;
    float Zoom;

    float dt;

    float nCam = 0.1f;
    float fCam = 50000.0f;

    float width, height;

    float lastX, lastY;

    bool firstMouse = false;
    bool canMove = false;

    SlipCamera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH);

    void drawDebug();

    glm::mat4 GetViewMatrix();
    glm::mat4 GetProjectionMatrix();
    glm::mat4 GetOrthographicMatrix();

    void ProcessKeyboard(Camera_Movement direction, float deltaTime);
    void ProcessWindow(float width, float height);
    void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);
    void ProcessMouseScroll(float yoffset);
private:
    void updateCameraVectors();
};

#endif