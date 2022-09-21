#include "SlipEntity.h"

glm::mat4 SlipEntity::getMatrix()
{
	glm::mat4 matrix = glm::mat4(1.0f);
	matrix = glm::translate(matrix, position);
	matrix = glm::rotate(matrix, rotation.x, rotation);
	matrix = glm::rotate(matrix, rotation.y, rotation);
	matrix = glm::rotate(matrix, rotation.z, rotation);
	matrix = glm::scale(matrix, scale);

	return matrix;
}

SlipEntity SlipEntity::generateEntity(std::string name, SlipModel& model, SlipMaterial& material)
{
	SlipEntity ent(name, model, material);
	return ent;
}

SlipEntity::SlipEntity(std::string name, SlipModel& model, SlipMaterial& material) : name(name)
{
	this->model = &model;
	this->material = &material;
}

void SlipEntity::draw(SlipCamera& camera)
{
	glm::mat4 modelMat = getMatrix();
	material->bind(camera, modelMat);

	model->Draw(material->shader);
}
