#include "SlipSpawn.h"

#include "SlipMesh.h"
#include "SlipLevel.h"

glm::mat4 SlipSpawn::getMatrix()
{
	glm::mat4 translate = glm::translate(position);
	glm::mat4 scaling = glm::scale(scale);

	glm::mat4 rotationX = glm::rotate(glm::mat4(1.f), rotation.x, glm::vec3(1.f, 0.f, 0.f));
	glm::mat4 rotationY = glm::rotate(glm::mat4(1.f), rotation.y, glm::vec3(0.f, 1.f, 0.f));
	glm::mat4 rotationZ = glm::rotate(glm::mat4(1.f), rotation.z, glm::vec3(0.f, 0.f, 1.f));

	return translate * rotationX * rotationY * rotationZ * scaling;
}

SlipSpawn::SlipSpawn(int team, glm::vec3 position, glm::vec3 rotation) : SlipEntity()
{
	model = &SlipLevel::Get().debugModels[0];

	this->team = team;
	this->position = position;
	this->rotation = rotation;
}

void SlipSpawn::draw()
{
	if (model != nullptr)
	{
		model->draw(getMatrix());
	}
}
