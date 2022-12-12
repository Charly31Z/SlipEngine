#pragma once

#ifndef SLIP_FRAMEBUFFER_H
#define SLIP_FRAMEBUFFER_H

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
	std::vector<unsigned int> indices;

	unsigned int VAO, VBO, EBO;

	unsigned int fbo, rbo;

	SlipShader* shader;

	void initMesh();
	void initFramebuffer();

	inline static std::vector<SlipFrameBuffer*> m_Instances;
public:
	inline static SlipFrameBuffer& Get(int index) { return *m_Instances[index]; }

	unsigned int textureColorBuffer;

	SlipFrameBuffer();

	void init();

	void updateSize();

	void bind();
	void unbind();

	void draw();
};

#endif // !SLIP_FRAMEBUFFER_H