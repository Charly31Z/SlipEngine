#pragma once

#ifndef SLIP_ENTITY_H
#define SLIP_ENTITY_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

class SlipEntity
{
protected:
	glm::vec3 oldPos = glm::vec3(1.0f);
	glm::vec3 oldRot = glm::vec3(.0f);
	glm::vec3 oldSca = glm::vec3(1.0f);
private:
	virtual void Start();
	bool startFunEjec = false;

	uint32_t id;
public:
	glm::vec3 position = glm::vec3(1.0f);
	glm::vec3 rotation = glm::vec3(.0f);
	glm::vec3 scale = glm::vec3(1.0f);

	uint32_t GetId() { return id; }

	SlipEntity();

	void ejecStart();

	virtual void Update();
};

#endif // !SLIP_ENTITY_H