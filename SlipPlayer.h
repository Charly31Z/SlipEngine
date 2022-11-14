#pragma once

#ifndef SLIP_PLAYER_H
#define SLIP_PLAYER_H

#include "SlipCharacter.h"

class SlipPlayer : public SlipCharacter
{
public:
	SlipCamera* camera;

	void update();
};

struct SlipSpawnPlayer
{
private:
	char meshPath[192];
	SlipMesh* mesh;

	bool initialized;
public:
	glm::vec3 pos, rot, scale;

	void debug();
};

#endif // !SLIP_PLAYER_H