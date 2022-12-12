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

SlipEntity::SlipEntity()
{
	id = SlipID::Get().next();
}

void SlipEntity::ejecStart()
{
	if (!startFunEjec)
	{
		Start();
		startFunEjec = false;
	}
}

void SlipEntity::Start()
{
}

void SlipEntity::Update()
{
}
