#include "SlipPhysics.h"

#include "SlipDebug.h"
#include "SlipGlobals.h"

#include <cassert>

SlipPhysics::SlipPhysics()
{
	assert(!m_Instance && "SlipPhysics has initialized...");
	m_Instance = this;

	// Build the broadphase
	broadphase = new btDbvtBroadphase();

	// Set up the collision configuration and dispatcher
	collisionConfiguration = new btDefaultCollisionConfiguration();
	dispatcher = new btCollisionDispatcher(collisionConfiguration);

	// The actual physics solver
	solver = new btSequentialImpulseConstraintSolver;

	// The world.
	dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, collisionConfiguration);
	dynamicsWorld->setGravity(btVector3(0, 0, 0));

	SlipDebug::Get().debuggedColl.setDebugMode(SlipDebug::Get().debuggedColl.DBG_DrawWireframe);
	dynamicsWorld->setDebugDrawer(&SlipDebug::Get().debuggedColl);
}

void SlipPhysics::update()
{
	dynamicsWorld->stepSimulation(SlipGlobals::Get().GetDeltaTime());
}

void SlipPhysics::debugDraw()
{
	dynamicsWorld->debugDrawWorld();
	SlipDebug::Get().draw();
}

void SlipPhysics::clean()
{
	dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, collisionConfiguration);
	dynamicsWorld->setGravity(btVector3(0, 0, 0));

	SlipDebug::Get().debuggedColl.setDebugMode(SlipDebug::Get().debuggedColl.DBG_DrawWireframe);
	dynamicsWorld->setDebugDrawer(&SlipDebug::Get().debuggedColl);
}
