#include "SlipDebug.h"

#include "SlipGL.h"
#include "SlipLevel.h"
#include "render/SlipShader.h"

#include <assert.h>

static const char* s_DebugLineVertSrc = R"(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

out vec3 ourColor;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    ourColor = aColor;
}
)";

static const char* s_DebugLineFragSrc = R"(#version 330 core
in vec3 ourColor;
out vec4 FragColor;

uniform float transparency;

void main()
{
    FragColor = vec4(ourColor, 1.0);
}
)";

static SlipShader* GetOrCreateDebugShader()
{
	static SlipShader* s_Shader = nullptr;
	if (!s_Shader)
	{
		s_Shader = SlipShader::CreateFromSource(s_DebugLineVertSrc, s_DebugLineFragSrc);
	}
	return s_Shader;
}

DebugCollision::DebugCollision()
{
}

DebugCollision::~DebugCollision()
{
	lines.clear();
}

void DebugCollision::drawLine(const btVector3& from, const btVector3& to, const btVector3& fromColor, const btVector3& toColor)
{
	lines.push_back(glm::vec3(from.getX(), from.getY(), from.getZ()));
	lines.push_back(glm::vec3(fromColor.getX(), fromColor.getY(), fromColor.getZ()));
	lines.push_back(glm::vec3(to.getX(), to.getY(), to.getZ()));
	lines.push_back(glm::vec3(toColor.getX(), toColor.getY(), toColor.getZ()));
}

void DebugCollision::drawLine(const btVector3& from, const btVector3& to, const btVector3& color)
{
	lines.push_back(glm::vec3(from.getX(), from.getY(), from.getZ()));
	lines.push_back(glm::vec3(color.getX(), color.getY(), color.getZ()));
	lines.push_back(glm::vec3(to.getX(), to.getY(), to.getZ()));
	lines.push_back(glm::vec3(color.getX(), color.getY(), color.getZ()));
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
	m_debugMode = debugMode;
}

void DebugCollision::render(const glm::mat4& view, const glm::mat4& projection)
{
	if (lines.empty())
		return;

	if (!initialized)
	{
		glGenVertexArrays(1, &VAO);
		glGenBuffers(1, &VBO);
		shader = GetOrCreateDebugShader();
		initialized = true;
	}

	if (!shader)
		shader = GetOrCreateDebugShader();
	if (!shader)
		return;

	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);

	glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * lines.size(), lines.data(), GL_DYNAMIC_DRAW);

	// Positions
	glVertexAttribPointer(
		0,
		3,
		GL_FLOAT,
		GL_FALSE,
		sizeof(glm::vec3) * 2,
		(void*)(0)
	);
	glEnableVertexAttribArray(0);

	// Colours
	glVertexAttribPointer(
		1,
		3,
		GL_FLOAT,
		GL_FALSE,
		sizeof(glm::vec3) * 2,
		(void*)(sizeof(glm::vec3))
	);
	glEnableVertexAttribArray(1);

	shader->use();

	glm::mat4 model{ 1.f };
	shader->setMat4("projection", projection);
	shader->setMat4("view", view);
	shader->setMat4("model", model);
	shader->setFloat("transparency", 0.0f);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	glBindVertexArray(VAO);
	glDrawArrays(GL_LINES, 0, (GLsizei)(lines.size() / 2));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	lines.clear();
}

void DebugCollision::render()
{
	glm::mat4 proj = SlipLevel::Get().GetCamera().GetProjectionMatrix();
	glm::mat4 view = SlipLevel::Get().GetCamera().GetViewMatrix();
	render(view, proj);
}

void DebugCollision::clean()
{
	if (VAO != 0) {
		glDeleteVertexArrays(1, &VAO);
		VAO = 0;
	}
	if (VBO != 0) {
		glDeleteBuffers(1, &VBO);
		VBO = 0;
	}
	lines.clear();
	initialized = false;
}

void DebugCollision::cleanLines()
{
	lines.clear();
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
		out = glm::vec3(1.f);
		break;
	}
	return out;
}

void SlipDebug::drawLines(glm::vec3 lineStart, glm::vec3 lineEnd, const glm::vec3& rgbColor)
{
	lines.push_back(lineStart);
	lines.push_back(rgbColor);
	lines.push_back(lineEnd);
	lines.push_back(rgbColor);
}

void SlipDebug::drawLines(glm::vec3 lineStart, glm::vec3 lineEnd, Color color)
{
	drawLines(lineStart, lineEnd, colorToVec3(color));
}

void SlipDebug::draw(const glm::mat4& view, const glm::mat4& projection)
{
	debuggedColl.render(view, projection);

	if (lines.empty())
		return;

	if (VAO == 0)
	{
		glGenVertexArrays(1, &VAO);
		glGenBuffers(1, &VBO);
	}

	if (!shader)
		shader = GetOrCreateDebugShader();
	if (!shader)
		return;

	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);

	glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * lines.size(), lines.data(), GL_DYNAMIC_DRAW);

	// Positions
	glVertexAttribPointer(
		0,
		3,
		GL_FLOAT,
		GL_FALSE,
		sizeof(glm::vec3) * 2,
		(void*)(0)
	);
	glEnableVertexAttribArray(0);

	// Colours
	glVertexAttribPointer(
		1,
		3,
		GL_FLOAT,
		GL_FALSE,
		sizeof(glm::vec3) * 2,
		(void*)(sizeof(glm::vec3))
	);
	glEnableVertexAttribArray(1);

	shader->use();

	glm::mat4 model{ 1.f };
	shader->setMat4("projection", projection);
	shader->setMat4("view", view);
	shader->setMat4("model", model);
	shader->setFloat("transparency", 0.0f);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	glBindVertexArray(VAO);
	glDrawArrays(GL_LINES, 0, (GLsizei)(lines.size() / 2));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	lines.clear();
}

void SlipDebug::draw()
{
	glm::mat4 proj = SlipLevel::Get().GetCamera().GetProjectionMatrix();
	glm::mat4 view = SlipLevel::Get().GetCamera().GetViewMatrix();
	draw(view, proj);
}

void SlipDebug::clean()
{
	debuggedColl.clean();

	if (VAO != 0) {
		glDeleteVertexArrays(1, &VAO);
		VAO = 0;
	}
	if (VBO != 0) {
		glDeleteBuffers(1, &VBO);
		VBO = 0;
	}
	lines.clear();
}

void SlipDebug::cleanLines()
{
	lines.clear();
	debuggedColl.cleanLines();
}

SlipDebug::SlipDebug()
{
	m_Instance = this;
	//strcpy(spawnMeshPath, "cache/debug/spawn.model_cache");
}
