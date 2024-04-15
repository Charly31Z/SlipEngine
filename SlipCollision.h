#pragma once

#ifndef SLIP_COLLISION_H
#define SLIP_COLLISION_H

#include "SlipShader.h"
#include <vector>

#include "io.h"

#include "SlipPhysics.h"

class SlipCollision
{
private:
    struct Vertex {
        glm::vec3 Position;
    };
public:
	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;

	btCollisionShape* collisionShape;
	btRigidBody* rigidBody;

	std::string path;

	int mass = 3;

	float scale[3];

	btDefaultMotionState* motionstate;

	void init();
	void initRigidBody(glm::vec3 pos, glm::vec3 rot);

	SlipCollision(std::string path);
};

#endif