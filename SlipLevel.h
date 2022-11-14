#pragma once

#ifndef SLIP_LEVEL_H
#define SLIP_LEVEL_H

#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <shlwapi.h>

#include "SlipEntity.h"
#include "SlipMesh.h"

#include "SlipBsp.h"
#include "io.h"

#include "DebugCollision.h"

#include <btBulletDynamicsCommon.h>

struct SlipLevel
{
private:
	btBroadphaseInterface* broadphase;
	btDefaultCollisionConfiguration* collisionConfiguration;
	btCollisionDispatcher* dispatcher;
	btSequentialImpulseConstraintSolver* solver;
	btDiscreteDynamicsWorld* dynamicsWorld;

	DebugCollision* debugCol;

	bool playing = false;
public:
	inline static std::string levelName;

	inline static SlipCamera Camera;

	bool canDraw = false;

	char bsp[192] = "";

	SlipBsp* mBsp;
	std::vector<SlipMesh> models;
	std::vector<SlipEntity> entities;
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