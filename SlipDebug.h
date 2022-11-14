#pragma once

#ifndef SLIP_DEBUG_H
#define SLIP_DEBUG_H

#include <glm/glm.hpp>
#include <glad/glad.h>

#include <string>
#include <vector>

#include "SlipShader.h"
#include "SlipLevel.h"

enum Color
{
	white,
	black,
	red,
	blue,
	yellow,
	green
};

struct SlipDebug
{
private:
	unsigned int VAO, VBO;
	SlipShader* shader;

	inline static glm::vec3 colorToVec3(Color color);

	inline static std::vector<glm::vec3> lines;
public:
	inline static char spawnMeshPath[192] = "";

	inline static void drawLines(glm::vec3 lineStart, glm::vec3 lineEnd, Color color);

	void draw();

	SlipDebug();
};

#endif // !SLIP_DEBUG_H