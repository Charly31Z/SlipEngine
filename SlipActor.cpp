#include "SlipActor.h"

#include "SlipMesh.h"

SlipActor::SlipActor(const char* modelPath) : SlipEntity()
{
	strcpy(this->modelPath, modelPath);
}

SlipActor::SlipActor()
{
}

glm::mat4 SlipActor::getMatrix()
{
	if (model->collision != nullptr && model->collision->rigidBody != nullptr)
	{
		glm::mat4 matrix = glm::mat4(1.0f);

		btTransform t;
		model->collision->rigidBody->getMotionState()->getWorldTransform(t);
		t.getOpenGLMatrix(glm::value_ptr(matrix));

		return matrix;
	}

	glm::mat4 translate = glm::translate(position);
	glm::mat4 scaling = glm::scale(scale);

	glm::mat4 rotationX = glm::rotate(glm::mat4(1.f), rotation.x, glm::vec3(1.f, 0.f, 0.f));
	glm::mat4 rotationY = glm::rotate(glm::mat4(1.f), rotation.y, glm::vec3(0.f, 1.f, 0.f));
	glm::mat4 rotationZ = glm::rotate(glm::mat4(1.f), rotation.z, glm::vec3(0.f, 0.f, 1.f));

	return translate * rotationX * rotationY * rotationZ * scaling;
}

void SlipActor::init()
{
	if (model == nullptr)
	{
		model = new SlipMesh(modelPath);

		model->init();

		model->initColl();
	}
}

void SlipActor::draw()
{
	if (model != nullptr)
	{
		model->draw(getMatrix());
	}
}

void SlipActor::Start()
{
	oldPos = position;
	oldRot = rotation;
	oldSca = scale;

	initRigiedBody();
}

void SlipActor::initRigiedBody()
{
	if (!initializedRigidBody && model->collision != nullptr)
	{
		model->collision->initRigidBody(oldPos, oldRot);
	}

	initializedRigidBody = true;
}
