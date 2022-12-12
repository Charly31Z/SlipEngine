#include "SlipDebug.h"

#include <glad/glad.h>

#include "SlipLevel.h"

#include <assert.h>

glm::vec3 SlipDebug::colorToVec3(Color color)
{
	glm::vec3 out;
	switch (color)
	{
	case white:
		out = glm::vec3(1.f);
		break;
	case black:
		out = glm::vec3(0.f);
		break;
	case red:
		out = glm::vec3(1.f, 0.f, 0.f);
		break;
	case blue:
		out = glm::vec3(0.f, 0.f, 1.f);
		break;
	case yellow:
		out = glm::vec3(1.f, 1.f, 0.f);
		break;
	case green:
		out = glm::vec3(0.f, 1.f, 0.f);
		break;
	default:
		break;
	}
	return out;
}

void SlipDebug::drawLines(glm::vec3 lineStart, glm::vec3 lineEnd, Color color)
{
	lines.push_back(lineStart);
	lines.push_back(colorToVec3(color));
	lines.push_back(lineEnd);
	lines.push_back(colorToVec3(color));
}

void SlipDebug::draw()
{
	if (lines.size() == 0)
		return;

	if (VAO <= 0)
	{
		glGenVertexArrays(1, &VAO);
		glGenBuffers(1, &VBO);

		shader = new SlipShader("assets/shaders/debug.vert", "assets/shaders/debug.frag");
	}

	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);

	glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * lines.size(), &lines[0], GL_STATIC_DRAW);

	//Positions
	glVertexAttribPointer(
		0,									    //Attribute ID
		3,									    //Size
		GL_FLOAT,							    //Type
		GL_FALSE,							    //Normalized?
		sizeof(glm::vec3) * 2,				    //Stride
		(void*)(0)	//Array Buffer Offset
	);
	glEnableVertexAttribArray(0);

	//Colours
	glVertexAttribPointer(
		1,										//Attribute ID
		3,										//Size
		GL_FLOAT,								//Type
		GL_FALSE,								//Normalized?
		sizeof(glm::vec3) * 2,					//Stride
		(void*)(sizeof(glm::vec3))	//Array Buffer Offset
	);
	glEnableVertexAttribArray(1);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	shader->use();

	glm::mat4 proj = SlipLevel::Get().GetCamera().GetProjectionMatrix();
	glm::mat4 view = SlipLevel::Get().GetCamera().GetViewMatrix();

	shader->setMat4("projection", proj);
	shader->setMat4("view", view);

	shader->setFloat("transparency", 0.5f);

	glBindVertexArray(VAO);
	glDrawArrays(GL_LINES, 0, lines.size() / 2);
	glBindVertexArray(0);

	lines.clear();
}

SlipDebug::SlipDebug()
{
	assert(!m_Instance && "SlipDebug has initialized...");
	m_Instance = this;
	strcpy(spawnMeshPath, "cache/debug/spawn.model_cache");
}
