#pragma once

#ifndef SLIP_GLOBALS_H
#define SLIP_GLOBALS_H

#include "SlipExport.h"

#include "SlipCamera.h"

class SLIPENGINE_API SlipGlobals
{
private:
	float currentFrame, deltaTime, lastFrame;
	float fixedDeltaTime;

	SlipCamera* camera;

	glm::mat4 proj, view;

	inline static SlipGlobals* m_instance = nullptr;
public:
	static SlipGlobals& Get();

	float GetTime() const { return currentFrame; }
	void SetTime(float t) { this->currentFrame = t; }

	float GetDeltaTime() const { return (deltaTime > 0.0f && deltaTime < 0.2f) ? deltaTime : (1.0f / 60.0f); }
	void SetDeltaTime(float dt) { this->deltaTime = dt; }

	float GetFixedDeltaTime() const { return fixedDeltaTime; }
	void SetFixedDeltaTime(float fdt) { fixedDeltaTime = fdt; }

	void update(double time);

	void setCamera(SlipCamera* camera) { this->camera = camera; }

	SlipCamera& getCamera() { return *camera; }

	glm::mat4 getProjection() { return proj; }
	glm::mat4 getView() { return view; }

	void setProjection(glm::mat4 proj) { this->proj = proj; }
	void setView(glm::mat4 view) { this->view = view; }

	SlipGlobals();
};

#endif // !SLIP_GLOBALS_H
