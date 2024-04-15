#pragma once

#include "SlipEngine.h"

#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>
#include <btBulletDynamicsCommon.h>

#include <glm/glm.hpp>

class SLIPENGINE_API SlipPhysics
{
private:
	btBroadphaseInterface* broadphase;
	btDefaultCollisionConfiguration* collisionConfiguration;
	btCollisionDispatcher* dispatcher;
	btSequentialImpulseConstraintSolver* solver;
	btDiscreteDynamicsWorld* dynamicsWorld;

	inline static SlipPhysics* m_Instance;
public:
	inline static SlipPhysics& Get() { return *m_Instance; }

	SlipPhysics();

	void setGravity(glm::vec3 gravity) { btVector3* grav = new btVector3(gravity.x, gravity.y, gravity.z); dynamicsWorld->setGravity(*grav); dynamicsWorld->applyGravity(); }
	void update();
	void debugDraw();
	void clean();

	btDiscreteDynamicsWorld& getWorld() { return *dynamicsWorld; }
};

