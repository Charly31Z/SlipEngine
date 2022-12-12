#include "SlipFrameBuffer.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <iostream>

#include "SlipWindow.h"

void SlipFrameBuffer::initMesh()
{

	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glGenBuffers(1, &EBO);

	glBindVertexArray(VAO);
	// load data into vertex buffers
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	// A great thing about structs is that their memory layout is sequential for all its items.
	// The effect is that we can simply pass a pointer to the struct and it translates perfectly to a glm::vec3/2 array which
	// again translates to 3/2 floats which translates to a byte array.
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

	// set the vertex attribute pointers
	// vertex Positions
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
	// vertex texture coords
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords));

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

void SlipFrameBuffer::initFramebuffer()
{
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	glGenTextures(1, &textureColorBuffer);
	glBindTexture(GL_TEXTURE_2D, textureColorBuffer);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, SlipWindow::Get().getWidth(), SlipWindow::Get().getHeight(), 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureColorBuffer, 0);

	glGenRenderbuffers(1, &rbo);
	glBindRenderbuffer(GL_RENDERBUFFER, rbo);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, SlipWindow::Get().getWidth(), SlipWindow::Get().getHeight());
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	shader->use();

	shader->setInt("screenTexture", 0);
}

SlipFrameBuffer::SlipFrameBuffer()
{
	m_Instances.push_back(this);

	vertices = {
		Vertex{glm::vec2(1.f,  1.f), glm::vec2(1.0f, 1.0f)},
		Vertex{glm::vec2(1.f, -1.f), glm::vec2(1.0f, 0.0f)},
		Vertex{glm::vec2(-1.f, -1.f), glm::vec2(0.0f, 0.0f)},
		Vertex{glm::vec2(-1.f,  1.f), glm::vec2(0.0f, 1.0f)}
	};

	indices = {
		0, 1, 3,
		1, 2, 3
	};
}

void SlipFrameBuffer::init()
{
	SlipShader s{"assets/shaders/screen.vert", "assets/shaders/screen.frag"};
	shader = &s;

	initMesh();
	initFramebuffer();
}

void SlipFrameBuffer::updateSize()
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	glBindTexture(GL_TEXTURE_2D, textureColorBuffer);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, SlipWindow::Get().getWidth(), SlipWindow::Get().getHeight(), 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureColorBuffer, 0);

	glGenRenderbuffers(1, &rbo);
	glBindRenderbuffer(GL_RENDERBUFFER, rbo);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, SlipWindow::Get().getWidth(), SlipWindow::Get().getHeight());
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SlipFrameBuffer::bind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
}

void SlipFrameBuffer::unbind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SlipFrameBuffer::draw()
{
	shader->use();

	glBindVertexArray(VAO);
	glBindTexture(GL_TEXTURE_2D, textureColorBuffer);
	glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);
}
