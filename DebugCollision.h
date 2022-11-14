#pragma once

#ifndef DEBUG_COLLISION_H
#define DEBUG_COLLISION_H 

#include <btBulletCollisionCommon.h>

#include "SlipShader.h"

#include <iostream>
#include <vector>

#include "SlipCamera.h"

#include <glm/glm.hpp>

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

	void render(SlipCamera& camera);

	void clean();

	virtual void    drawLine(const btVector3& from, const btVector3& to, const btVector3& fromColor, const btVector3& toColor);

	virtual void    drawLine(const btVector3& from, const btVector3& to, const btVector3& color);

	virtual void    drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color);

	virtual void    reportErrorWarning(const char* warningString);

	virtual void    draw3dText(const btVector3& location, const char* textString);

	virtual void    setDebugMode(int debugMode);

	virtual int     getDebugMode() const { return m_debugMode; }
};

#endif // !DEBUG_COLLISION_H