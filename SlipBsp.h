#pragma once

#ifndef SLIP_BSP_H
#define SLIP_BSP_H

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <windows.h>

#include "SlipMesh.h"
#include "SlipTexture.h"

#include "SlipCollision.h"

class SlipBsp
{
private:
	SlipShader* shader;
public:
	struct Material
	{
		SlipTexture lightmaps[3];

		char pathCombineTxt[192];
		SlipTexture* combineTexture;

		char pathBaseTxtR[192];
		SlipTexture* baseTextureR;

		char pathBaseTxtG[192];
		SlipTexture* baseTextureG;

		char pathBaseTxtB[192];
		SlipTexture* baseTextureB;
	};

	struct Mesh
	{
		std::vector<Vertex> vertices;
		std::vector<unsigned int> indices;

		unsigned int VAO, VBO, EBO;

		Material mat;
	};

	struct Collision
	{
		std::vector<Vertex> vertices;
		std::vector<unsigned int> indices;

		btCollisionShape* collisionShape;
		btRigidBody* rigidBody;
	};

	std::string path;

	bool initialized = false;

	Mesh mesh;
	Collision coll;

	char skyboxPath[192] = "";
	SlipMesh* skybox;

	SlipBsp(std::string path);

	void initCol();
	void init(SlipMesh& skymesh);
	int draw();
	void clean();
};

#endif // !SLIP_BSP_H