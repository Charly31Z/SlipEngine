#pragma once

#ifndef SLIP_ACTOR_H
#define SLIP_ACTOR_H

#include <string>

#include "SlipEngine.h"

#include "SlipEntity.h"

class SlipMesh;

class SLIPENGINE_API SlipActor : public SlipEntity
{
public:
	SlipActor(const char* modelPath);
	SlipActor();

	glm::mat4 getMatrix();

	char modelPath[192];
	SlipMesh* model;

	bool initializedRigidBody = false;

	void init();
	void draw();

	void Start() override;

	void initRigiedBody();
};

#endif // !SLIP_ACTOR_H