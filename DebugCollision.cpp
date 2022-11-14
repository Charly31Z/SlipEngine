#include "DebugCollision.h"

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

void DebugCollision::render(SlipCamera& camera)
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

	glm::mat4 proj = camera.GetProjectionMatrix();
	glm::mat4 view = camera.GetViewMatrix();

	shader->setMat4("projection", proj);
	shader->setMat4("view", view);

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
