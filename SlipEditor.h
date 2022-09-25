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

class SlipEditor
{
private:
	imgui_addons::ImGuiFileBrowser file_dialog;

	bool hierarchy;
	bool hierarchyImport;
	bool hierarchySave;

	bool properties;

	int entitySelected;
public:
	void init(GLFWwindow* window);

	void startRender();
	void renderHierarchy(std::vector<SlipEntity>& entities, SlipShader& shader, SlipLight& sun, std::vector<SlipLight>& lights);
	void renderProperties(std::vector<SlipEntity>& entities);
	void endRender();
};

#endif // !SLIP_EDITOR_H