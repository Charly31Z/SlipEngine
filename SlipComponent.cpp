#include "SlipComponent.h"

void MeshComponent::init()
{
	if (model == nullptr)
	{
		model = new SlipMesh(modelPath.c_str());

		model->init();
	}
}

void MeshComponent::draw()
{
	//model->draw(getMatrix());
}

void CollisionComponent::init()
{
	if (collision == nullptr)
	{
		if (std::strlen(colPath) != 0)
		{
			collision = new SlipCollision(colPath);

			collision->init();
		}
	}
}
