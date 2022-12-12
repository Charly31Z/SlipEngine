#pragma once

#ifndef SLIP_ACTOR_H
#define SLIP_ACTOR_H

#include <string>

#include "SlipEntity.h"

class SlipMesh;

class SlipActor : public SlipEntity
{
public:
	SlipActor(std::string modelPath);

	glm::mat4 getMatrix();

	std::string modelPath;
	SlipMesh* model;

	bool initializedRigidBody = false;

	void init();
	void draw();

	void Start() override;

	void initRigiedBody();
};

#endif // !SLIP_ACTOR_H