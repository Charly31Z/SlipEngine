#pragma once
#ifndef SLIP_ENTITY_H
#define SLIP_ENTITY_H

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "SlipModel.h"
#include "SlipMaterial.h"

class SlipEntity
{
public:
	std::string name;
	glm::vec3 position = glm::vec3(1.0f);
	glm::vec3 rotation = glm::vec3(0.0f);
	glm::vec3 scale = glm::vec3(1.0f);

	glm::mat4 getMatrix();

	SlipModel model;
	SlipMaterial material;

	static SlipEntity generateEntity(std::string name, SlipModel& model, SlipMaterial& material);
	static SlipEntity generateEntity2(std::string name, SlipModel model, SlipMaterial material);

	SlipEntity(std::string name, SlipModel &model, SlipMaterial& material);

	void draw(SlipCamera& camera);
};

#endif // !SLIP_ENTITY_H