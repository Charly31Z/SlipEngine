#pragma once

#include "SlipEngine.h"

#include <btBulletDynamicsCommon.h>
#include <BulletDynamics/Vehicle/btRaycastVehicle.h>

#include <glm/glm.hpp>
#include <set>

class SlipEntity;

struct CollisionPair {
	SlipEntity* entA = nullptr;
	SlipEntity* entB = nullptr;
	bool isTrigger = false;

	bool operator<(const CollisionPair& o) const {
		if (entA != o.entA) return entA < o.entA;
		if (entB != o.entB) return entB < o.entB;
		return isTrigger < o.isTrigger;
	}
};

class SLIPENGINE_API SlipPhysics
{
private:
	btBroadphaseInterface* broadphase;
	btDefaultCollisionConfiguration* collisionConfiguration;
	btCollisionDispatcher* dispatcher;
	btSequentialImpulseConstraintSolver* solver;
	btDiscreteDynamicsWorld* dynamicsWorld;
	glm::vec3 m_Gravity = glm::vec3(0.0f, -9.81f, 0.0f);
	std::set<CollisionPair> m_PreviousPairs;

	// Particionamiento activo / Burbuja de simulación ("Simulation Bubble")
	bool m_SimulationBubbleEnabled = false;
	float m_SimulationBubbleRadius = 60.0f;
	glm::vec3 m_SimulationBubbleCenter = glm::vec3(0.0f);

	// Configuración de subpasos de simulación física (120 Hz para eliminar pitch jittering en vehículos a alta velocidad)
	float m_FixedTimeStep = 1.0f / 120.0f;
	int m_MaxSubSteps = 10;

	void ConfigureDynamicsWorld();
	void ProcessCollisionEvents();
	void UpdateSimulationBubble();

	inline static SlipPhysics* m_Instance = nullptr;
public:
	inline static SlipPhysics& Get() {
		if (!m_Instance) {
			static SlipPhysics s_DefaultPhysics;
			m_Instance = &s_DefaultPhysics;
		}
		return *m_Instance;
	}
	inline static bool IsInitialized() { return m_Instance != nullptr && m_Instance->dynamicsWorld != nullptr; }

	SlipPhysics();

	bool m_Simulating = false;
	void SetSimulating(bool sim) { m_Simulating = sim; }
	bool IsSimulating() const { return m_Simulating; }

	void setGravity(glm::vec3 gravity) {
		m_Gravity = gravity;
		if (dynamicsWorld) {
			dynamicsWorld->setGravity(btVector3(gravity.x, gravity.y, gravity.z));
			dynamicsWorld->applyGravity();
		}
	}
	glm::vec3 getGravity() const { return m_Gravity; }
	void update(float dt = -1.0f);
	void debugDraw();
	void clean();

	// Control de subpasos de simulación física
	void SetFixedTimeStep(float step) { m_FixedTimeStep = (step > 0.0001f) ? step : 1.0f / 120.0f; }
	float GetFixedTimeStep() const { return m_FixedTimeStep; }
	void SetMaxSubSteps(int subSteps) { m_MaxSubSteps = (subSteps > 0) ? subSteps : 1; }
	int GetMaxSubSteps() const { return m_MaxSubSteps; }

	// Burbuja de simulación para mantener 60 FPS estables con múltiples cuerpos
	void SetSimulationBubble(bool enable, float radius = 60.0f, const glm::vec3& center = glm::vec3(0.0f)) {
		m_SimulationBubbleEnabled = enable;
		m_SimulationBubbleRadius = radius;
		m_SimulationBubbleCenter = center;
	}
	bool IsSimulationBubbleEnabled() const { return m_SimulationBubbleEnabled; }
	float GetSimulationBubbleRadius() const { return m_SimulationBubbleRadius; }
	void SetSimulationBubbleCenter(const glm::vec3& center) { m_SimulationBubbleCenter = center; }

	btDiscreteDynamicsWorld& getWorld() { return *dynamicsWorld; }
	btDiscreteDynamicsWorld* GetDynamicsWorld() { return dynamicsWorld; }
};

