#pragma once

#ifndef SLIP_LEVEL_H
#define SLIP_LEVEL_H

#include "SlipEngine.h"

#include <vector>
#include <string>

#include "SlipTerrain.h"
#include "SlipActor.h"
#include "SlipEntity.h"
#include "SlipBsp.h"

#include "SlipSpawn.h"

class SLIPENGINE_API SlipLevel
{
private:
	inline static SlipCamera* m_Camera;

	inline static SlipLevel* m_Instance;
public:
	inline static SlipLevel& Get() { return *m_Instance; }
	
	inline static SlipCamera& GetCamera() { return *m_Camera; }

	void SetCamera(SlipCamera& camera) { m_Camera = &camera; }

	std::string path;

	bool canDraw = false;

	bool debugMode = true;

	char bspPath[192] = "";
	SlipBsp* bsp;

	SlipTerrain* terrain;

	std::vector<SlipMesh*> models;
	std::vector<SlipMesh> debugModels;
	std::vector<SlipSpawn*> spawns;
	std::vector<SlipEntity*> entities;
	//std::vector<SlipEntity> spawnLocation;

	SlipLevel();

	void newLevel(std::string path);
	void saveLevel();
	int openLevel(std::string path);

	void apply();

	void scripts();
	void draw();
	void clean();

	int searchModel(std::string path);
};

#endif // !SLIP_LEVEL_H