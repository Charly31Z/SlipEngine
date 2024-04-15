#pragma once

#ifndef SLIP_SHADOWS_H
#define SLIP_SHADOWS_H

#include "SlipShader.h"

class SlipShadows
{
private:
	unsigned int depthFbo;

	unsigned int depthMap;

	SlipShader* shader;

	bool settingShadows = false;

	glm::mat4 proj, view;

	void initFramebuffer();

	inline static SlipShadows* m_Instance;
public:
	inline static SlipShadows& Get() { return *m_Instance; }

	SlipShadows();

	bool calculating() { return settingShadows; }

	SlipShader* getShader() { return shader; }

	unsigned int& getDepthMap() { return depthMap; }

	void init();

	glm::vec3 lightPos;

	glm::mat4 getProjection() { return proj; }
	glm::mat4 getView() { return view; }

	void calculateLight();

	void configure();

	void bind();
	void unbind();
};

#endif // !SLIP_SHADOWS_H