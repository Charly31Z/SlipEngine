#pragma once

#ifndef SLIP_MATERIAL_H
#define SLIP_MATERIAL_H

#include <glm/glm.hpp>

#include "SlipShader.h"
#include "SlipLight.h"
#include "SlipCamera.h"

class SlipMaterial
{
public:
	glm::vec3 color = glm::vec3(1.0f);
	glm::vec3 ambient = glm::vec3(0.125f);
	glm::vec3 specular = glm::vec3(0.16f);
	float shininess = 3;

	SlipLight sunLight;
	std::vector<SlipLight> lights;
	SlipShader shader;

	static SlipMaterial generateMaterial(std::string code, SlipLight& sunLight, std::vector<SlipLight> &lights);
	static SlipMaterial generateMaterial2(SlipShader code, SlipLight sunLight, std::vector<SlipLight> lights);

	SlipMaterial(SlipShader &shader, SlipLight& sunLight, std::vector<SlipLight> &lights);

	void bind(SlipCamera& camera, glm::mat4& model);
};

#endif // !SLIP_MATERIAL_H