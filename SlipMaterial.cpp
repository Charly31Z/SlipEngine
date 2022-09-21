#include "SlipMaterial.h"

SlipMaterial SlipMaterial::generateMaterial(std::string code, SlipLight& sunLight, std::vector<SlipLight>& lights)
{
    std::string vertPath = code + ".vert";
    std::string fragPath = code + ".frag";
    SlipShader s{ vertPath.c_str(), fragPath.c_str() };

    return SlipMaterial(s, sunLight, lights);
}

SlipMaterial::SlipMaterial(SlipShader& shader, SlipLight& sunLight, std::vector<SlipLight>& lights) : shader(shader), sunLight(sunLight), lights(lights)
{
}

void SlipMaterial::bind(SlipCamera& camera, glm::mat4 &model)
{
    printf("\nUsando shader: %u", shader.ID);
    shader.use();

    shader.setVec3("material.ambient", ambient);
    shader.setVec3("material.diffuse", color);
    shader.setVec3("material.specular", specular);
    shader.setFloat("material.shininess", shininess);

    shader.setVec3("sunLight.direction", sunLight.position);
    shader.setVec3("sunLight.ambient", sunLight.ambient);
    shader.setVec3("sunLight.diffuse", sunLight.diffuse);
    shader.setVec3("sunLight.specular", sunLight.diffuse);

    for (int i = 0; i < lights.size(); i++)
    {
        shader.setVec3("lights["+ std::to_string(i) + "].position", lights[i].position);
        shader.setVec3("lights[" + std::to_string(i) + "].ambient", lights[i].ambient);
        shader.setVec3("lights[" + std::to_string(i) + "].diffuse", lights[i].diffuse);
        shader.setVec3("lights[" + std::to_string(i) + "].specular", lights[i].diffuse);
        shader.setFloat("lights[" + std::to_string(i) + "].constant", 1.0f);
        shader.setFloat("lights[" + std::to_string(i) + "].linear", 0.09f);
        shader.setFloat("lights[" + std::to_string(i) + "].quadratic", 0.032f);
    }

    shader.setVec3("viewPos", camera.Position);

    glm::mat4 proj = camera.GetProjectionMatrix();
    glm::mat4 view = camera.GetViewMatrix();

    shader.setMat4("projection", proj);
    shader.setMat4("view", view);
    shader.setMat4("model", model);
}
