#pragma once

#ifndef SLIP_DEBUG_H
#define SLIP_DEBUG_H

#include <glm/glm.hpp>
#include <vector>

class SlipShader;

enum Color
{
	white,
	black,
	red,
	blue,
	yellow,
	green
};

class SlipDebug
{
private:
	unsigned int VAO, VBO;
	SlipShader* shader;

	glm::vec3 colorToVec3(Color color);

	std::vector<glm::vec3> lines;

	inline static SlipDebug* m_Instance;
public:
	inline static SlipDebug& Get() { return *m_Instance; }

	char spawnMeshPath[192] = "";

	void drawLines(glm::vec3 lineStart, glm::vec3 lineEnd, Color color);

	void draw();

	SlipDebug();
};

#endif // !SLIP_DEBUG_H