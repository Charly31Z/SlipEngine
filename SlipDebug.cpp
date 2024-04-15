#include "SlipDebug.h"

#include <glad/glad.h>

#include "SlipLevel.h"

#include <assert.h>

DebugCollision::DebugCollision()
{

}

DebugCollision::~DebugCollision()
{
	lines.clear();
}

void DebugCollision::drawLine(const btVector3& from, const btVector3& to, const btVector3& fromColor, const btVector3& toColor)
{
	glm::vec3 temp = glm::vec3(from.getX(), from.getY(), from.getZ());
	lines.push_back(temp);
	temp = glm::vec3(fromColor.getX(), fromColor.getY(), fromColor.getZ());
	lines.push_back(temp);
	temp = glm::vec3(to.getX(), to.getY(), to.getZ());
	lines.push_back(temp);
	temp = glm::vec3(toColor.getX(), toColor.getY(), toColor.getZ());
	lines.push_back(temp);
}

void DebugCollision::drawLine(const btVector3& from, const btVector3& to, const btVector3& color)
{
	glm::vec3 temp = glm::vec3(from.getX(), from.getY(), from.getZ());
	lines.push_back(temp);
	temp = glm::vec3(color.getX(), color.getY(), color.getZ());
	lines.push_back(temp);
	temp = glm::vec3(to.getX(), to.getY(), to.getZ());
	lines.push_back(temp);
	temp = glm::vec3(color.getX(), color.getY(), color.getZ());
	lines.push_back(temp);
}

void DebugCollision::drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color)
{
}

void DebugCollision::reportErrorWarning(const char* warningString)
{
}

void DebugCollision::draw3dText(const btVector3& location, const char* textString)
{
}

void DebugCollision::setDebugMode(int debugMode)
{
}

void DebugCollision::render()
{
	if (lines.size() == 0)
		return;

	if (!initialized)
	{
		glGenVertexArrays(1, &VAO);
		glGenBuffers(1, &VBO);

		shader = new SlipShader("assets/shaders/debug.vert", "assets/shaders/debug.frag");
		initialized = true;
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
	glm::mat4 model{ 1.f };

	shader->setMat4("projection", proj);
	shader->setMat4("view", view);
	shader->setMat4("model", model);

	shader->setFloat("transparency", 0.5f);

	glBindVertexArray(VAO);
	glDrawArrays(GL_LINES, 0, lines.size() / 2);
	glBindVertexArray(0);

	lines.clear();
}

void DebugCollision::clean()
{
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);

	shader->destroy();
}

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
	debuggedColl.render();

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
	glm::mat4 model{ 1.f };

	shader->setMat4("projection", proj);
	shader->setMat4("view", view);
	shader->setMat4("model", model);

	shader->setFloat("transparency", 0.5f);

	glBindVertexArray(VAO);
	glDrawArrays(GL_LINES, 0, lines.size() / 2);
	glBindVertexArray(0);

	lines.clear();
}

void SlipDebug::clean()
{
	debuggedColl.clean();

	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);

	shader->destroy();
}

SlipDebug::SlipDebug()
{
	assert(!m_Instance && "SlipDebug has initialized...");
	m_Instance = this;
	//strcpy(spawnMeshPath, "cache/debug/spawn.model_cache");
}
