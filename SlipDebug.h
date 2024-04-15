#pragma once

#ifndef SLIP_DEBUG_H
#define SLIP_DEBUG_H

#include "SlipEngine.h"

#include "SlipCollision.h"

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

class DebugCollision : public btIDebugDraw
{
	SlipShader* shader;

	unsigned int VAO, VBO;
	int m_debugMode;

	std::vector<glm::vec3> lines;

	bool initialized = false;

public:
	DebugCollision();
	virtual ~DebugCollision();

	void render();

	void clean();

	virtual void    drawLine(const btVector3& from, const btVector3& to, const btVector3& fromColor, const btVector3& toColor);

	virtual void    drawLine(const btVector3& from, const btVector3& to, const btVector3& color);

	virtual void    drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color);

	virtual void    reportErrorWarning(const char* warningString);

	virtual void    draw3dText(const btVector3& location, const char* textString);

	virtual void    setDebugMode(int debugMode);

	virtual int     getDebugMode() const { return m_debugMode; }
};

class SLIPENGINE_API SlipDebug
{
private:
	unsigned int VAO, VBO;
	SlipShader* shader;

	glm::vec3 colorToVec3(Color color);

	std::vector<glm::vec3> lines;

	inline static SlipDebug* m_Instance;
public:
	DebugCollision debuggedColl;

	inline static SlipDebug& Get() { return *m_Instance; }

	char spawnMeshPath[192] = "";

	void drawLines(glm::vec3 lineStart, glm::vec3 lineEnd, Color color);

	void draw();

	void clean();

	SlipDebug();
};

#endif // !SLIP_DEBUG_H