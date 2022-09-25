// SlipEngine.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <assimp/config.h>

#include <iostream>
#include <fstream>

#include "io.h"
#include "SlipEditor.h"
#include "SlipCamera.h"
#include "SlipShader.h"
#include "SlipModel.h"
#include "SlipLight.h"
#include "SlipMaterial.h"
#include "SlipUI.h"
#include "SlipEntity.h"

int widthS = 800, heightS = 600;

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);

// editor
SlipEditor editor;

// camera
SlipCamera camera(glm::vec3(-6.f, 10.f, 15.f));

bool firstMouse = true;
bool canMoveMouse = false;
float lastX = widthS / 2.0;
float lastY = heightS / 2.0;

// timing
float deltaTime = 0.0f;	// time between current frame and last frame
float lastFrame = 0.0f;

std::vector<SlipEntity> entities;

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(widthS, heightS, "Programa prron", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    camera.ProcessWindow(widthS, heightS);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    editor.init(window);

    SlipLight sun(glm::vec3(-15.0f, -10.0f, 8.0f), glm::vec3(1.f, 1.f, 1.f), SlipLight::LIGHT::DIRECTIONAL);

    std::vector<SlipLight> lights;
    
    SlipLight light(glm::vec3(.0f, 5.0f, 5.0f), glm::vec3(1.f, .0f, .0f), SlipLight::LIGHT::POINT);
    lights.push_back(light);

    SlipShader skyShader("assets/shaders/sky.vert", "assets/shaders/sky.frag");
    SlipShader shader("assets/shaders/mvp.vert", "assets/shaders/mvp.frag");

    SlipMaterial skyMat{skyShader, sun, lights};

    SlipModel sky("assets/models/sky.obj");
    entities.push_back(SlipEntity::generateEntity("sky", sky, skyMat));

    SlipMaterial simpleMat{shader, sun, lights };

    SlipModel ourModel("assets/models/scene2.obj");
    entities.push_back(SlipEntity::generateEntity("bsp", ourModel, simpleMat));

    SlipMaterial bkMat{ shader, sun, lights };

    SlipModel tele("assets/models/teletubbie.obj");
    entities.push_back(SlipEntity::generateEntity("teletubbie", tele, bkMat));

    SlipUI ui{glm::vec2(0.f), "assets/textures/placeholder.png"};

    while (!glfwWindowShouldClose(window))
    {
        editor.startRender();
        editor.renderHierarchy(entities, shader, sun, lights);
        editor.renderProperties(entities);

        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);

        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        camera.ProcessWindow(widthS, heightS);

        for (auto& entity : entities)
        {
            entity.draw(camera);
        }

        // DRAW HUD

        glDepthMask(GL_FALSE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        ui.draw(camera);

        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);

        editor.endRender();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();

    return 0;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    widthS = width;
    heightS = height;
    glViewport(0, 0, width, height);
    glfwSetWindowAspectRatio(window, widthS, heightS);
}

void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if (canMoveMouse)
    {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            camera.ProcessKeyboard(Camera_Movement::FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            camera.ProcessKeyboard(Camera_Movement::BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            camera.ProcessKeyboard(Camera_Movement::LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            camera.ProcessKeyboard(Camera_Movement::RIGHT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            camera.ProcessKeyboard(Camera_Movement::DOWN, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            camera.ProcessKeyboard(Camera_Movement::UP, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
            camera.MovementSpeed = 30.f;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_RELEASE)
            camera.MovementSpeed = 2.5f;
    }

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_2) == GLFW_PRESS)
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        canMoveMouse = true;
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_2) == GLFW_RELEASE)
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        canMoveMouse = false;
        firstMouse = true;
    }
}

void mouse_callback(GLFWwindow* window, double xposIn, double yposIn)
{
    if (canMoveMouse)
    {
        float xpos = static_cast<float>(xposIn);
        float ypos = static_cast<float>(yposIn);

        if (firstMouse)
        {
            lastX = xpos;
            lastY = ypos;
            firstMouse = false;
        }

        float xoffset = xpos - lastX;
        float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top
        lastX = xpos;
        lastY = ypos;

        camera.ProcessMouseMovement(xoffset, yoffset);
    }
}

// glfw: whenever the mouse scroll wheel scrolls, this callback is called
// ----------------------------------------------------------------------
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    camera.ProcessMouseScroll((float)yoffset);
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
