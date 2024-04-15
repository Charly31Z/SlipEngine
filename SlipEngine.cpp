// SlipEngine.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

/****

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <assimp/config.h>

#include <iostream>

#include "io.h"
#include "SlipEditor.h"
#include "SlipShader.h"
#include "SlipUI.h"
#include "SlipEntity.h"
#include "SlipFrameBuffer.h"
#include "SlipLevel.h"

int widthS = 1280, heightS = 720;

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);

bool firstMouse = true;
bool canMoveMouse = false;
float lastX = widthS / 2.0;
float lastY = heightS / 2.0;

// timing
float deltaTime = 0.0f;	// time between current frame and last frame
float lastFrame = 0.0f;

std::vector<SlipEntity> entities;
std::vector<SlipUI> uis;

// editor
SlipEditor editor{widthS, heightS};

SlipFrameBuffer framebuffer;

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
    glfwSetWindowAspectRatio(window, widthS, heightS);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    SlipLevel::GetCamera().ProcessWindow(widthS, heightS);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    SlipLevel level{ "test" };

    editor.init(window, level);

    /*SlipLight sun(glm::vec3(-15.0f, -10.0f, 8.0f), glm::vec3(1.f, 1.f, 1.f), SlipLight::LIGHT::DIRECTIONAL);

    std::vector<SlipLight> lights;
    
    SlipLight light(glm::vec3(.0f, 5.0f, 5.0f), glm::vec3(1.f, .0f, .0f), SlipLight::LIGHT::POINT);
    lights.push_back(light);

    SlipShader skyShader("assets/shaders/sky.vert", "assets/shaders/sky.frag");
    SlipShader bspShader("assets/shaders/mvp.vert", "assets/shaders/mvp.frag");
    SlipShader shader("assets/shaders/mvp.vert", "assets/shaders/mvp.frag");

    SlipMaterial skyMat{skyShader, sun, lights};

    SlipModel sky("assets/models/sky.obj");
    entities.push_back(SlipEntity::generateEntity("sky", sky, skyMat));

    SlipMaterial simpleMat{bspShader, sun, lights };

    SlipModel ourModel("assets/models/scene.obj");
    entities.push_back(SlipEntity::generateEntity("bsp", ourModel, simpleMat));

    SlipMaterial bkMat{ shader, sun, lights };

    SlipModel tele("assets/models/teletubbie.obj");

    SlipEntity entTele{"teletubbie", tele, bkMat};
    entTele.position.y = -3.f;
    entities.push_back(entTele);

    SlipUI testUi{"placeholder", camera, glm::vec2(0.f), "assets/textures/placeholder.png"};
    uis.push_back(testUi);

    framebuffer.init(widthS, heightS);

    while (!glfwWindowShouldClose(window))
    {
        editor.startRender();
        editor.renderHierarchy();
        editor.renderProperties();
        editor.renderGame(framebuffer);
        editor.renderScene();

        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        SlipLevel::GetCamera().dt = deltaTime;

        processInput(window);

        framebuffer.bind();

        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);

        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        SlipLevel::GetCamera().ProcessWindow(widthS, heightS);

        level.draw();

        for (auto& entity : entities)
        {
            entity.draw(camera);
        }

        // DRAW HUD

        glDepthMask(GL_FALSE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        for (auto& ui : uis)
        {
            ui.draw();
        }

        framebuffer.unbind();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        //framebuffer.draw();

        editor.endRender();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    level.clean();

    glfwTerminate();

    return 0;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    widthS = width;
    heightS = height;
    glfwSetWindowSize(window, width, height);
    glViewport(0, 0, width, height);

    framebuffer.updateSize(width, height);
}

void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if (canMoveMouse)
    {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            SlipLevel::GetCamera().ProcessKeyboard(Camera_Movement::FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            SlipLevel::GetCamera().ProcessKeyboard(Camera_Movement::BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            SlipLevel::GetCamera().ProcessKeyboard(Camera_Movement::LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            SlipLevel::GetCamera().ProcessKeyboard(Camera_Movement::RIGHT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            SlipLevel::GetCamera().ProcessKeyboard(Camera_Movement::DOWN, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            SlipLevel::GetCamera().ProcessKeyboard(Camera_Movement::UP, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
            SlipLevel::GetCamera().MovementSpeed = 30.f;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_RELEASE)
            SlipLevel::GetCamera().MovementSpeed = 2.5f;
    }

    if (editor.mouseRPressed())
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        canMoveMouse = true;
    }
    else
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

        SlipLevel::GetCamera().ProcessMouseMovement(xoffset, yoffset);
    }
}

// glfw: whenever the mouse scroll wheel scrolls, this callback is called
// ----------------------------------------------------------------------
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    if (canMoveMouse)
        SlipLevel::GetCamera().ProcessMouseScroll((float)yoffset);
}****/

#include "SlipEngine.h"

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file

