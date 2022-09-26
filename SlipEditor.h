#pragma once

#ifndef SLIP_EDITOR_H
#define SLIP_EDITOR_H

#include <ImGuiFileBrowser.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>

#include <iostream>
#include <vector>

#include "SlipEntity.h"
#include "SlipUI.h"
#include "SlipFrameBuffer.h"

class SlipEditor
{
private:
	int& width;
	int& height;

	enum property_type {
		ENTITY,
		UI
	};

	imgui_addons::ImGuiFileBrowser file_dialog;

	bool hierarchy;
	bool hierarchyImport;
	bool hierarchySave;

	bool properties;
	property_type prop = property_type::ENTITY;

	int entitySelected;
	int uiSelected;

	std::vector<SlipEntity>& entities;
	std::vector<SlipUI>& uis;

	bool game;
public:
	SlipEditor(std::vector<SlipEntity>& entities, std::vector<SlipUI>& uis, int& width, int& height);

	void init(GLFWwindow* window);

	void startRender();
	void renderHierarchy(SlipShader& shader, SlipLight& sun, std::vector<SlipLight>& lights);
	void renderProperties();
	void renderGame(SlipFrameBuffer& frameBuffer);
	void endRender();
};

#endif // !SLIP_EDITOR_H