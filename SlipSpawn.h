#pragma once

#include "SlipEntity.h"

class SlipMesh;

class SlipSpawn : public SlipEntity
{
	glm::mat4 getMatrix();

public:
	SlipSpawn(int team, glm::vec3 position, glm::vec3 rotation);

	void draw();

	SlipMesh* model;

	int team;
};

