#include "SlipShadows.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "SlipGlobals.h"
#include "SlipWindow.h"

#include <iostream>

void SlipShadows::initFramebuffer()
{
	glGenFramebuffers(1, &depthFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, depthFbo);

	glGenTextures(1, &depthMap);
	glBindTexture(GL_TEXTURE_2D, depthMap);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 1024, 1024, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

SlipShadows::SlipShadows()
{
	assert(!m_Instance && "SlipShadows has initialized...");
	m_Instance = this;
}

void SlipShadows::init()
{
	shader = new SlipShader("assets/shaders/depth.vert", "assets/shaders/depth.frag");
	lightPos = glm::vec3(-8.0f, 10.0f, -4.0f);

	initFramebuffer();
}

void SlipShadows::calculateLight()
{
	proj = glm::ortho<float>(-20.0f, 20.0f, -20.0f, 20.0f, -10.0f, 80.f);
	// glm::lookAt(glm::vec3(-2.0f, 4.0f, -1.0f), glm::vec3(0.f), glm::vec3(0.0, 1.0, 0.0));
	// SlipGlobals::Get().getView();
	view = glm::lookAt(lightPos, glm::vec3(0.f), glm::vec3(0.0, 1.0, 0.0));
}

void SlipShadows::configure()
{
	shader->use();
	shader->setMat4("projection", proj);
	shader->setMat4("view", view);
}

void SlipShadows::bind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, depthFbo);
	glViewport(0, 0, 1024, 1024);

	glClear(GL_DEPTH_BUFFER_BIT);

	settingShadows = true;
}

void SlipShadows::unbind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	settingShadows = false;
}
