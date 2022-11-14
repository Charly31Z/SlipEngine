#include "SlipEntity.h"

glm::mat4 SlipEntity::getMatrix()
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

	/*glm::quat quaternion;

	quaternion = glm::quat(1.0f, rotation.x, rotation.y, rotation.z);*/

	//glm::mat4 RotationMatrix = glm::toMat4(quaternion);

	return translate * rotationX * rotationY * rotationZ * scaling;
}

SlipEntity::SlipEntity(std::string modelPath) : modelPath(modelPath)
{
}

void SlipEntity::init()
{
	/*position = oldPos;
	rotation = oldRot;
	scale = oldSca;*/

	if (model == nullptr)
	{
		model = new SlipMesh(modelPath.c_str());

		model->init();

		if (model->collision != nullptr)
		{
			model->collision->init();
		}
	}
}

void SlipEntity::initRigiedBody()
{
	oldPos = position;
	oldRot = rotation;
	oldSca = scale;

	if (model->collision != nullptr)
	{
		model->collision->initRigidBody(oldPos, oldRot);
	}

	initializedRigidBody = true;
}

void SlipEntity::draw()
{
	if (model != nullptr)
	{
		//model->model = model->model * getMatrix();

		/*if (model->collision != nullptr)
		{
			btScalar transform[16];

			btTransform trans;
			trans = model->collision->rigidBody->getWorldTransform();
			trans.getOpenGLMatrix(transform);

			position = glm::vec3(trans.getOrigin().getX(), trans.getOrigin().getY(), trans.getOrigin().getZ());
			rotation = glm::vec3(model->collision->rigidBody->getOrientation().getX(), model->collision->rigidBody->getOrientation().getY(), model->collision->rigidBody->getOrientation().getZ());
		}*/

		model->draw(getMatrix());
	}
}
