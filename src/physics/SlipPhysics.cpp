#include "SlipPhysics.h"

#include "SlipDebug.h"
#include "SlipGlobals.h"
#include "SlipEntity.h"

#include <cassert>

SlipPhysics::SlipPhysics()
{
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
	ConfigureDynamicsWorld();
}

void SlipPhysics::ConfigureDynamicsWorld()
{
	if (!dynamicsWorld) return;

	dynamicsWorld->setGravity(btVector3(m_Gravity.x, m_Gravity.y, m_Gravity.z));

	// Optimización avanzada del Constraint Solver para estabilidad y 60 FPS fijos
	btContactSolverInfo& solverInfo = dynamicsWorld->getSolverInfo();
	solverInfo.m_numIterations = 6;

	// Split Impulse: resuelve penetraciones sin añadir energía de velocidad ni rebotes
	solverInfo.m_splitImpulse = 1;
	solverInfo.m_splitImpulsePenetrationThreshold = -0.02f;
	solverInfo.m_splitImpulseTurnErp = 0.1f;

	solverInfo.m_erp = 0.2f;
	solverInfo.m_erp2 = 0.2f; // Mantener 0.2 para evitar rigidez extrema y vibraciones
	solverInfo.m_globalCfm = 0.0001f;
	solverInfo.m_damping = 1.0f;
	// Desactivar Warmstarting para evitar acumulación explosiva de impulsos cuando cuerpos dinámicos
	// son movidos por scripts mediante SetLinearVelocity contra paredes u otros objetos.
	solverInfo.m_solverMode = SOLVER_SIMD | SOLVER_ENABLE_FRICTION_DIRECTION_CACHING;

	dynamicsWorld->getDispatchInfo().m_allowedCcdPenetration = 0.04f;
	dynamicsWorld->getDispatchInfo().m_enableSPU = true;

	SlipDebug::Get().debuggedColl.setDebugMode(SlipDebug::Get().debuggedColl.DBG_DrawWireframe);
	dynamicsWorld->setDebugDrawer(&SlipDebug::Get().debuggedColl);
}

void SlipPhysics::UpdateSimulationBubble()
{
	if (!m_SimulationBubbleEnabled || !dynamicsWorld) return;

	float r2 = m_SimulationBubbleRadius * m_SimulationBubbleRadius;
	int numObjects = dynamicsWorld->getNumCollisionObjects();
	btCollisionObjectArray& objects = dynamicsWorld->getCollisionObjectArray();

	for (int i = 0; i < numObjects; ++i) {
		btCollisionObject* obj = objects[i];
		if (!obj) continue;

		btRigidBody* body = btRigidBody::upcast(obj);
		if (!body || body->isStaticOrKinematicObject()) continue;

		const btVector3& pos = body->getWorldTransform().getOrigin();
		float dx = pos.x() - m_SimulationBubbleCenter.x;
		float dy = pos.y() - m_SimulationBubbleCenter.y;
		float dz = pos.z() - m_SimulationBubbleCenter.z;
		float dist2 = dx * dx + dy * dy + dz * dz;

		if (dist2 > r2) {
			// Fuera de la burbuja activa: dormir para liberar ciclos de CPU
			if (body->isActive()) {
				body->setActivationState(ISLAND_SLEEPING);
			}
		}
	}
}

void SlipPhysics::update(float dt)
{
	if (dt <= 0.0f) {
		dt = SlipGlobals::Get().GetDeltaTime();
	}
	if (dt <= 0.0f || dt > 0.2f) {
		dt = 1.0f / 60.0f;
	}
	// Subpasos de simulación: ejecutamos a 120 Hz con hasta 10 subpasos para garantizar estabilidad numérica
	// perfecta en vehículos a alta velocidad (>100 km/h) y erradicar el cabeceo (pitch jittering) por sobrecompensación de resortes.
	float stepDt = (std::min)(dt, 1.0f / 15.0f);
	dynamicsWorld->stepSimulation(stepDt, m_MaxSubSteps, m_FixedTimeStep);

	UpdateSimulationBubble();
	ProcessCollisionEvents();
}

void SlipPhysics::ProcessCollisionEvents()
{
	if (!dynamicsWorld || !dispatcher) return;

	int numManifolds = dispatcher->getNumManifolds();
	if (numManifolds <= 0 && m_PreviousPairs.empty()) return;

	std::set<CollisionPair> currentPairs;

	for (int i = 0; i < numManifolds; ++i) {
		btPersistentManifold* contactManifold = dispatcher->getManifoldByIndexInternal(i);
		if (!contactManifold) continue;

		int numContacts = contactManifold->getNumContacts();
		if (numContacts <= 0) continue;

		bool isTouching = false;
		for (int j = 0; j < numContacts; ++j) {
			if (contactManifold->getContactPoint(j).getDistance() <= 0.05f) {
				isTouching = true;
				break;
			}
		}
		if (!isTouching) continue;

		const btCollisionObject* obA = contactManifold->getBody0();
		const btCollisionObject* obB = contactManifold->getBody1();
		if (!obA || !obB) continue;

		SlipEntity* entA = static_cast<SlipEntity*>(obA->getUserPointer());
		SlipEntity* entB = static_cast<SlipEntity*>(obB->getUserPointer());
		if (!entA || !entB || entA == entB) continue;

		bool isTrigger = (obA->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE) ||
		                 (obB->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE);

		CollisionPair pair;
		if (entA < entB) {
			pair = { entA, entB, isTrigger };
		} else {
			pair = { entB, entA, isTrigger };
		}
		currentPairs.insert(pair);
	}

	// 1. Detectar pares nuevos (Enter) y existentes (Stay)
	for (const auto& pair : currentPairs) {
		if (m_PreviousPairs.find(pair) == m_PreviousPairs.end()) {
			if (pair.isTrigger) {
				pair.entA->OnTriggerEnter(pair.entB);
				pair.entB->OnTriggerEnter(pair.entA);
			} else {
				pair.entA->OnCollisionEnter(pair.entB);
				pair.entB->OnCollisionEnter(pair.entA);
			}
		} else {
			if (pair.isTrigger) {
				pair.entA->OnTriggerStay(pair.entB);
				pair.entB->OnTriggerStay(pair.entA);
			} else {
				pair.entA->OnCollisionStay(pair.entB);
				pair.entB->OnCollisionStay(pair.entA);
			}
		}
	}

	// 2. Detectar pares que salieron (Exit)
	for (const auto& pair : m_PreviousPairs) {
		if (currentPairs.find(pair) == currentPairs.end()) {
			if (pair.isTrigger) {
				pair.entA->OnTriggerExit(pair.entB);
				pair.entB->OnTriggerExit(pair.entA);
			} else {
				pair.entA->OnCollisionExit(pair.entB);
				pair.entB->OnCollisionExit(pair.entA);
			}
		}
	}

	m_PreviousPairs = std::move(currentPairs);
}

void SlipPhysics::debugDraw()
{
	dynamicsWorld->debugDrawWorld();
	SlipDebug::Get().draw();
}

void SlipPhysics::clean()
{
	m_PreviousPairs.clear();
	if (dynamicsWorld) {
		for (int i = dynamicsWorld->getNumCollisionObjects() - 1; i >= 0; --i) {
			btCollisionObject* obj = dynamicsWorld->getCollisionObjectArray()[i];
			dynamicsWorld->removeCollisionObject(obj);
		}
		delete dynamicsWorld;
	}

	dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, collisionConfiguration);
	ConfigureDynamicsWorld();
}
