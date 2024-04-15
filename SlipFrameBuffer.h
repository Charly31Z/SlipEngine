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

	unsigned int texture;

	SlipShader* shader;

	void initMesh();
	void initFramebuffer();

	inline static SlipFrameBuffer* m_Instance;
public:
	inline static SlipFrameBuffer& Get() { return *m_Instance; }

	unsigned int& getTexture() { return texture; }

	SlipFrameBuffer();

	void init();
	void resize();

	void bind();
	void unbind();

	void draw();
	void clean();
};

#endif // !SLIP_FRAMEBUFFER_H