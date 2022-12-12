#pragma once

#ifndef SLIP_LEVEL_H
#define SLIP_LEVEL_H

#include <vector>
#include <string>

#include "SlipEntity.h"
#include "SlipBsp.h"

#include "DebugCollision.h"

#include <btBulletDynamicsCommon.h>

class SlipLevel
{
private:
	btBroadphaseInterface* broadphase;
	btDefaultCollisionConfiguration* collisionConfiguration;
	btCollisionDispatcher* dispatcher;
	btSequentialImpulseConstraintSolver* solver;
	btDiscreteDynamicsWorld* dynamicsWorld;

	DebugCollision* debugCol;

	bool playing = false;

	inline static SlipCamera* m_Camera;

	inline static SlipLevel* m_Instance;
public:
	inline static SlipLevel& Get() { return *m_Instance; }
	
	inline static SlipCamera& GetCamera() { return *m_Camera; }

	std::string levelName;

	bool canDraw = false;

	char bsp[192] = "";

	SlipBsp* mBsp;
	std::vector<SlipMesh> models;
	std::vector<SlipEntity*> entities;
	//std::vector<SlipEntity> spawnLocation;

	SlipLevel(std::string levelName);

	void playMode();
	bool isPlaying() { return playing; }

	void newLevel(std::string levelName);
	void saveLevel();
	void openLevel(std::string levelName);

	void draw();
	void clean();

	int searchModel(std::string path);
};

#endif // !SLIP_LEVEL_H