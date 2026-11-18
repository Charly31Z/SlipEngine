#pragma once

#include "SlipEngine.h"

#include <vector>

#include "SlipPhysics.h"
#include "SlipShader.h"
#include "SlipMesh.h"

class SLIPENGINE_API SlipTerrain
{
private:
	const unsigned int NUM_PATCH_PTS = 4;

	unsigned int terrainVAO, terrainVBO, texture;
	int width, height, nChannels;

	unsigned rez;

	SlipShader* shader;

	std::vector<float> vertices;

	bool initialized = false;
public:
	bool &isInitialized() { return initialized; }

	char heightfieldIMG[192];
	char collisionPath[192];

	//SlipTerrain(int width = 512, int height = 512);
	SlipTerrain();
	~SlipTerrain();

	SlipCollision* collision;

	void apply();

	void init();

	void draw();
	void clean();
};

