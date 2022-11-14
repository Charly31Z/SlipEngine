#include "SlipLight.h"

/*void SlipLight::draw(glm::mat4 viewProj)
{
	lightShader.use();

	lightShader.setVec3("lightColor", color);

	glm::mat4 model = glm::mat4(1.0f);
	model = glm::translate(model, position);

	glm::mat4 mvp = viewProj * model;
	lightShader.setMat4("mvp", mvp);

	lightModel.Draw(lightShader);
}*/

SlipLight::SlipLight(glm::vec3 position, glm::vec3 color, LIGHT type) : type(type)
{
	this->position = position;
	this->color = color;

	if (type == LIGHT::DIRECTIONAL)
	{
		this->diffuse = color * glm::vec3(0.5f);
		this->ambient = diffuse * glm::vec3(0.2f);
	}
	else {
		this->diffuse = color * glm::vec3(0.5f);
		this->ambient = diffuse * glm::vec3(0.2f);
	}
}
