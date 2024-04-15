#pragma once

#ifndef SLIP_GLOBALS_H
#define SLIP_GLOBALS_H

#include "SlipCamera.h"

class SlipGlobals
{
private:
	float currentFrame, deltaTime, lastFrame;

	SlipCamera* camera;

	glm::mat4 proj, view;

	inline static SlipGlobals* m_instance;
public:
	inline static SlipGlobals& Get() { return *m_instance; }

	float GetDeltaTime() { return deltaTime; }

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
