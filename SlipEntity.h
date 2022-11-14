#pragma once
#ifndef SLIP_ENTITY_H
#define SLIP_ENTITY_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "SlipMesh.h"
#include "SlipMaterial.h"

class SlipEntity
{
private:
	glm::vec3 oldPos = glm::vec3(1.0f);
	glm::vec3 oldRot = glm::vec3(.0f);
	glm::vec3 oldSca = glm::vec3(1.0f);
public:
	glm::vec3 position = glm::vec3(1.0f);
	glm::vec3 rotation = glm::vec3(.0f);
	glm::vec3 scale = glm::vec3(1.0f);

	glm::mat4 getMatrix();

	std::string modelPath;
	SlipMesh* model;

	bool initializedRigidBody = false;

	SlipEntity(std::string modelPath);

	void init();
	void initRigiedBody();
	void draw();
};

#endif // !SLIP_ENTITY_H