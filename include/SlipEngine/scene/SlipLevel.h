#pragma once

#ifndef SLIP_LEVEL_H
#define SLIP_LEVEL_H

#include "SlipEngine.h"

#include <vector>
#include <string>
#include <algorithm>

#include "SlipActor.h"
#include "SlipEntity.h"
#include "SlipBsp.h"

#include "SlipSpawn.h"

class CameraComponent;

class SLIPENGINE_API SlipLevel
{
private:
	inline static SlipCamera* m_Camera;

	inline static SlipLevel* m_Instance;

	std::string m_PendingScenePath;
	bool m_HasPendingSceneLoad = false;
public:
	inline static SlipLevel& Get() {
		if (!m_Instance) {
			static SlipLevel s_DefaultLevel;
			m_Instance = &s_DefaultLevel;
		}
		return *m_Instance;
	}
	inline static void SetActiveLevel(SlipLevel* level) { m_Instance = level; }
	
	inline static SlipCamera& GetCamera() { return *m_Camera; }

	void SetCamera(SlipCamera& camera) { m_Camera = &camera; }

	std::string path;
	std::string lightmapPath;
	unsigned int lightmapTexture = 0;

	bool canDraw = false;

	bool debugMode = true;

	char bspPath[192] = "";
	SlipBsp* bsp;

	std::vector<SlipMesh*> models;
	std::vector<SlipMesh> debugModels;
	std::vector<SlipSpawn*> spawns;
	std::vector<SlipEntity*> entities;
	//std::vector<SlipEntity> spawnLocation;

	SlipLevel();

	const std::vector<SlipEntity*>& GetEntities() const { return entities; }
	std::vector<SlipEntity*>& GetEntities() { return entities; }
	void AddEntity(SlipEntity* entity) { if (entity) entities.push_back(entity); }
	void RemoveEntity(SlipEntity* entity) {
		entities.erase(std::remove(entities.begin(), entities.end(), entity), entities.end());
	}

	using EntityDestroyedCallback = std::function<void(SlipEntity*)>;
	static void SetOnEntityDestroyedCallback(EntityDestroyedCallback cb);
	void DestroyEntity(SlipEntity* root);

	/*void newLevel(std::string path);
	void saveLevel();
	int openLevel(std::string path);*/

	bool SaveScene(const std::string& filePath = "");
	bool LoadScene(const std::string& filePath, bool preservePersistent = true);
	void ClearLevel(bool preservePersistent = false);

	void RequestLoadScene(const std::string& scenePath) {
		m_PendingScenePath = scenePath;
		m_HasPendingSceneLoad = true;
	}
	bool HasPendingSceneLoad() const { return m_HasPendingSceneLoad; }
	bool ProcessPendingSceneLoad();

	std::string GetCurrentScenePath() const { return path; }
	std::string GetCurrentSceneName() const;

	CameraComponent* GetMainCamera() const;

	void apply();

	void scripts();
	void draw();
	void clean();

	int searchModel(std::string path);
};

#endif // !SLIP_LEVEL_H