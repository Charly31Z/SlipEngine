#pragma once

#ifndef SLIP_EDITOR_H
#define SLIP_EDITOR_H

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"

#include <ImGuizmo.h>

#include "SlipFrameBuffer.h"
#include "SlipModelExtract.h"

#include <time.h>
#include <map>

class SlipEditor
{
private:
	enum property_type {
		LEVEL,
		BSP,
		MODEL,
		COLLISION,
		MATERIAL,
		ENTITY,
		SPAWN,
		UI
	};

	bool fileManager;

	bool hierarchy;
	bool hierarchyOpen;

	bool properties;
	property_type prop = property_type::ENTITY;

	bool existSpawn = false;
	int spawnSelected;

	bool existEnity = false;
	const char* entity_mesh = NULL;
	const char* collision_type = NULL;
	std::string materials_name = "";
	const char* playBtn = "Play";
	int entitySelected;
	int uiSelected;

	int matSelected = 0;

	bool game;
	bool mouseRClicked;

	bool scene;
	bool createBsp;
	bool createModel;
	bool createCol;
	bool createTexture;
	bool sceneImport;

	bool createEntity;

	SlipModelExtract extractor;

	int sceneTypeSelected = 0;

	int selectedType;
	int selectedChildType;

	float posSpa[3] = { 0.f, 0.f, 0.f };
	float rotSpa[3] = { 0.f, 0.f, 0.f };

	int teamSpa;

	float posEnt[3] = { 0.f, 0.f, 0.f };
	float rotEnt[3] = { 0.f, 0.f, 0.f };
	float scaEnt[3] = { 1.f, 1.f, 1.f };

	SlipCamera* camera;

	/* IMGuizmo */
	ImGuizmo::OPERATION mCurrentGizmoOperation = ImGuizmo::OPERATION::TRANSLATE;
	ImGuizmo::MODE mCurrentGizmoMode = ImGuizmo::MODE::WORLD;
	bool useSnap = true;
	glm::vec3 snap{1.f};
	glm::mat4 matrix;

	glm::vec3 posGuizmo;
	glm::vec3 rotGuizmo;
	glm::vec3 scaGuizmo;

	bool toolbar;

	// Window Debug
	bool debug;
	int current_textItem;
	std::vector<std::string> textItem;

	std::map<const char*, SlipTexture*> icons;

	bool playMode = false;

	inline static SlipEditor* m_Instante;
public:
	inline static SlipEditor& Get() { return *m_Instante; }

	bool& isPlayMode() { return playMode; }

	bool mouseRPressed();

	bool wireframe = false;

	SlipEditor();

	void init();

	void loadIcons();

	void startRender();
	void renderGuizmo();
	void renderHierarchy();
	void renderProperties();
	void renderViewport(SlipFrameBuffer* framebuffer);
	void renderScene();
	void renderDebug();
	void processInput();
	void endRender();

	void print(std::string text);

	SlipCamera& getViewportCamera() { return *camera; }
};

#endif // !SLIP_EDITOR_H