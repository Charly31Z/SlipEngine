#pragma once

#ifndef SLIP_UI_H
#define SLIP_UI_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stdio.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "SlipCamera.h"
#include "SlipShader.h"

class SlipUI
{
private:
	struct Texture
	{
		unsigned int id;
		std::string path;
	};

	struct Vertex
	{
		glm::vec3 positions;
		glm::vec2 texCoords;
	};

	glm::vec2 position;
	glm::vec2 scale{1.f};

	std::vector<Vertex> vertices;
	std::vector<GLuint> indices;
	Texture texture;

	SlipShader shader{ "assets/shaders/ui.vert", "assets/shaders/ui.frag" };

	glm::mat4 getModelMatrix();

	void setupMesh();
	void setupTexture();

	GLuint VAO, VBO, EBO;
public:
	SlipUI(glm::vec2 position, std::string texturePath);

	void draw(SlipCamera& camera);
};

#endif // !SLIP_UI_H