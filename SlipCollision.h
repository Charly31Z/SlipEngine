#pragma once

#ifndef SLIP_COLLISION_H
#define SLIP_COLLISION_H

#include "glm/glm.hpp"
#include <vector>

#include "io.h"

#include <btBulletDynamicsCommon.h>

class SlipCollision
{
private:
    struct Vertex {
        glm::vec3 Position;
    };
public:
	enum Collision_Type
	{
		BOX,
		SPHERE,
		CAPSULE,
		CONE,
		CYLINDER
	};

	std::string path;

	int mass = 3;
	Collision_Type col_type;

	float scale[3];

	btCollisionShape* collisionModel;

	btDefaultMotionState* motionstate;

	btRigidBody* rigidBody;

	void init();
	void initRigidBody(glm::vec3 pos, glm::vec3 rot);

	SlipCollision(std::string path);
};

#endif