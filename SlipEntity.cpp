#include "SlipEntity.h"

#include "SlipID.h"

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

glm::mat4 SlipEntity::getMatrix()
{
	glm::mat4 translate = glm::translate(position);
	glm::mat4 scaling = glm::scale(scale);

	glm::mat4 rotationX = glm::rotate(glm::mat4(1.f), rotation.x, glm::vec3(1.f, 0.f, 0.f));
	glm::mat4 rotationY = glm::rotate(glm::mat4(1.f), rotation.y, glm::vec3(0.f, 1.f, 0.f));
	glm::mat4 rotationZ = glm::rotate(glm::mat4(1.f), rotation.z, glm::vec3(0.f, 0.f, 1.f));

	return translate * rotationX * rotationY * rotationZ * scaling;
}

SlipEntity::SlipEntity()
{
	id = SlipID::Get().next();
}

void SlipEntity::ejecStart()
{
	if (!startFunEjec)
	{
		Start();
		startFunEjec = true;
	}
}

void SlipEntity::Start()
{
}

void SlipEntity::Update()
{
}
