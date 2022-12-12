#pragma once

#ifndef SLIP_MATERIAL_H
#define SLIP_MATERIAL_H

#include <glm/glm.hpp>
#include <vector>

#include "SlipShader.h"
#include "SlipCamera.h"
#include "SlipTexture.h"

class SlipMaterial
{
private:

	//void createTexture(const char* type);

	void loadMatFromFile(std::string path);
public:
	std::string path;

	glm::vec3 color = glm::vec3(1.0f);
	glm::vec3 ambient = glm::vec3(0.125f);
	glm::vec3 specular = glm::vec3(0.16f);
	float shininess = 3;

	char shaderType[4];
	SlipShader* shader;

	char diffPath[192] = "";
	SlipTexture* diffuseTexture;

	static SlipMaterial generateMaterial(std::string code, const char* path);
	static SlipMaterial generateMaterial2(SlipShader code, const char* path);

	SlipMaterial(std::string path);

	void bind(glm::mat4& model);
};

#endif // !SLIP_MATERIAL_H