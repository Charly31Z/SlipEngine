#pragma once

#ifndef SLIP_FRAMEBUFFER_H
#define SLIP_FRAMEBUFFER_H

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <iostream>
#include <vector>

#include "SlipShader.h"

class SlipFrameBuffer
{
private:
	struct Vertex
	{
		glm::vec2 positions;
		glm::vec2 texCoords;
	};

	std::vector<Vertex> vertices;
	std::vector<GLuint> indices;

	GLuint VAO, VBO, EBO;

	unsigned int fbo, rbo;

	SlipShader* shader;

	void initMesh();
	void initFramebuffer(int& width, int& height);
public:
	unsigned int textureColorBuffer;

	SlipFrameBuffer();

	void init(int& width, int& height);

	void updateSize(int& width, int& height);

	void bind();
	void unbind();

	void draw();
};

#endif // !SLIP_FRAMEBUFFER_H