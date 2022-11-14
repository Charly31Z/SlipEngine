#pragma once

#ifndef SLIP_CHARACTER_H
#define SLIP_CHARACTER_H

#include "SlipMesh.h"

class SlipCharacter
{
public:
	char modelPath[192];
	SlipMesh* model;

	int speed;

	void init();

	void spawn();
	void kill();
	void clean();
};

#endif // !SLIP_CHARACTER_H