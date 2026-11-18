#pragma once

#include "SlipEngine.h"

#include "SlipEntity.h"

class SlipMesh;

class SLIPENGINE_API SlipSpawn : public SlipEntity
{
	glm::mat4 getMatrix();

public:
	SlipSpawn(int team, glm::vec3 position, glm::vec3 rotation);

	void draw();

	SlipMesh* model;

	int team;
};

