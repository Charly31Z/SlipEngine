// SlipEngine.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <assimp/config.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <iostream>
#include "SlipCamera.h"
#include "SlipShader.h"
#include "SlipModel.h"
#include "SlipLight.h"
#include "SlipMaterial.h"
#include "SlipUI.h"
#include "SlipEntity.h"

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);

const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

// camera
SlipCamera camera(glm::vec3(-6.f, 10.f, 15.f));

bool firstMouse = true;
bool canMoveMouse = false;
float lastX = SCR_WIDTH / 2.0;
float lastY = SCR_HEIGHT / 2.0;

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

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Programa prron", NULL, NULL);
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

    camera.ProcessWindow(SCR_WIDTH, SCR_HEIGHT);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    //SETUP IMGUI
    IMGUI_CHECKVERSION();

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    ImGui::StyleColorsDark();

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

    SlipModel bk("assets/models/bk.obj");
    entities.push_back(SlipEntity::generateEntity("bk", bk, simpleMat));

    SlipUI ui{glm::vec2(1.f), "assets/textures/placeholder.png"};

    while (!glfwWindowShouldClose(window))
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Scene");

        ImGui::End();

        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);

        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /*float timeValue = glfwGetTime();
        float greenValue = sin(timeValue) / 2.0f + 0.5f;
        shader.setFloat("ourColorGreen", greenValue);*/

        camera.ProcessWindow(SCR_WIDTH, SCR_HEIGHT);

        //glm::mat4 projection = camera.GetProjectionMatrix();
        //glm::mat4 view = camera.GetViewMatrix();

        //glm::mat4 viewProj = projection * view;

        //glm::mat4 skyModel = glm::mat4(1.0f);
        //skyModel = glm::rotate(skyModel, (float)glfwGetTime() * 0.015f, glm::vec3(.0f, 1.f, .0f));

        for (auto& entity : entities)
        {
            if (entity.model != NULL && entity.material != NULL)
            {
                entity.draw(camera);
            }
        }

        /*skyShader.use();
        skyShader.setMat4("projection", projection);
        skyShader.setMat4("view", view);
        skyShader.setMat4("model", skyModel);

        sky.Draw(skyShader);

        glm::mat4 model = glm::mat4(1.0f);

        sun.draw(viewProj);
        //light.draw(viewProj);

        shader.use();

        shader.setVec3("material.ambient", mat.ambient);
        shader.setVec3("material.diffuse", mat.color);
        shader.setVec3("material.specular", mat.specular);
        shader.setFloat("material.shininess", mat.shininess);

        shader.setVec3("sunLight.direction", sun.position);
        shader.setVec3("sunLight.ambient", sun.ambient);
        shader.setVec3("sunLight.diffuse", sun.diffuse);
        shader.setVec3("sunLight.specular", sun.diffuse);

        shader.setVec3("lights[0].position", light.position);
        shader.setVec3("lights[0].ambient", light.ambient);
        shader.setVec3("lights[0].diffuse", light.diffuse);
        shader.setVec3("lights[0].specular", light.diffuse);
        shader.setFloat("lights[0].constant", 1.0f);
        shader.setFloat("lights[0].linear", 0.09f);
        shader.setFloat("lights[0].quadratic", 0.032f);

        shader.setVec3("viewPos", camera.Position);

        shader.setMat4("projection", projection);
        shader.setMat4("view", view);

        
        //model = glm::rotate(model, (float)glfwGetTime(), glm::vec3(0.5f, 1.0f, 0.0f));

        shader.setMat4("model", model);

        ourModel.Draw(shader);

        bk.Draw(shader);*/

        // DRAW HUD

        //glm::mat4 Ortho = glm::ortho(0.0f, (float)SCR_WIDTH, 0.0f, (float)SCR_HEIGHT);

        /*glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);*/

        //ui.draw(Ortho);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();

    return 0;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

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
