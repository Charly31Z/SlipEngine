#pragma once

#ifndef SLIP_COMPONENT_H
#define SLIP_COMPONENT_H

#include "SlipEngine.h"

#include "SlipMesh.h"

#include <string>

class SLIPENGINE_API SlipComponent
{
	char name[129];
};

class LightComponent : public SlipComponent
{

};

class CameraComponent : public SlipComponent
{

};

class MeshComponent : public SlipComponent
{
	std::string modelPath;
	SlipMesh* model;

	void init();
	void draw();
};

class MaterialComponent : public SlipComponent
{
	char matPath[192];
	SlipMaterial* mat;

	void bind();
};

class CollisionComponent : public SlipComponent
{
	char colPath[192];
	SlipCollision* collision;

	void init();
};

class RigidBodyComponent : public SlipComponent
{
	bool initializedRigidBody = false;

	void init();
	void draw();
};

#endif // !SLIP_COMPONENT_H