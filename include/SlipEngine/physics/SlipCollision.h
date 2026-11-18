#pragma once

#ifndef SLIP_COLLISION_H
#define SLIP_COLLISION_H

#include "SlipEngine.h"

#include "SlipShader.h"
#include <vector>

#include "io.h"

#include "SlipPhysics.h"

class SLIPENGINE_API SlipCollision
{
public:
    struct Vertex {
        glm::vec3 Position;
    };

	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;

	btCollisionShape* collisionShape = nullptr;
	btRigidBody* rigidBody = nullptr;

	std::string path;

	int mass = 3;

	float scale[3] = {1.0f, 1.0f, 1.0f};

	btDefaultMotionState* motionstate = nullptr;

	void init();
	void initRigidBody(glm::vec3 pos, glm::vec3 rot);

	SlipCollision();
	SlipCollision(std::string path);
	SlipCollision(const std::vector<glm::vec3>& positions, const std::vector<unsigned int>& indices, int mass = 0);
	~SlipCollision();
};

#endif