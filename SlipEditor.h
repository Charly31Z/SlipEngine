#pragma once

#ifndef SLIP_EDITOR_H
#define SLIP_EDITOR_H

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"

#include "SlipFrameBuffer.h"
#include "SlipModelExtract.h"

class SlipEditor
{
private:
	int width;
	int height;

	enum property_type {
		LEVEL,
		BSP,
		MODEL,
		COLLISION,
		MATERIAL,
		ENTITY,
		UI
	};

	bool hierarchy;
	bool hierarchyOpen;

	bool properties;
	property_type prop = property_type::ENTITY;

	bool existEnity = false;
	const char* entity_mesh = NULL;
	const char* collision_type = NULL;
	std::string materials_name = "";
	const char* playBtn = "Play";
	int entitySelected;
	int uiSelected;

	int matSelected = 0;

	/*std::vector<SlipEntity>& entities;
	std::vector<SlipUI>& uis;*/

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

	float posEnt[3] = { 0.f, 0.f, 0.f };
	float rotEnt[3] = { 0.f, 0.f, 0.f };
	float scaEnt[3] = { 1.f, 1.f, 1.f };

	inline static SlipEditor* m_Instante;
public:
	inline static SlipEditor& Get() { return *m_Instante; }

	bool mouseRPressed();

	SlipEditor(int width, int height);

	void init();

	void startRender();
	void renderHierarchy();
	void renderProperties();
	void renderGame(SlipFrameBuffer& frameBuffer);
	void renderScene();
	void processWindow();
	void processInput();
	void endRender();
};

#endif // !SLIP_EDITOR_H