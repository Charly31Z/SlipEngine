#include "SlipEntity.h"

glm::mat4 SlipEntity::getMatrix()
{
	glm::mat4 matrix = glm::mat4(1.0f);
	matrix = glm::translate(matrix, position);
	matrix = glm::scale(matrix, scale);

	glm::quat quaternion;

	quaternion = glm::quat(1.0f, rotation.x, rotation.y, rotation.z);

	glm::mat4 RotationMatrix = glm::toMat4(quaternion);

	return matrix * RotationMatrix;
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
