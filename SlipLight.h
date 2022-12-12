#pragma once

#ifndef SLIP_LIGHT_H
#define SLIP_LIGHT_H

#include <glm/glm.hpp>

#include "SlipMesh.h"

class SlipLight
{
public:
	enum LIGHT
	{
		DIRECTIONAL,
		POINT
	};

	SlipShader lightShader{ "assets/shaders/light.vert", "assets/shaders/light.frag" };
	//SlipMesh lightModel;

	glm::vec3 position;
	glm::vec3 color;
	glm::vec3 ambient;
	glm::vec3 diffuse;
	LIGHT type;

	SlipLight(glm::vec3 position, glm::vec3 color, LIGHT type);

	//void draw(glm::mat4 viewProj);

	glm::vec3 getAmbient() { return getDiffuse() * glm::vec3(0.2f); }
	glm::vec3 getDiffuse() { return color * glm::vec3(0.5f); }
};

#endif // !SLIP_LIGHT_H