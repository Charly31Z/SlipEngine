#pragma once

#ifndef SLIP_MODEL_EXTRACTOR_H
#define SLIP_MODEL_EXTRACTOR_H

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <string>

#include "SlipLevel.h"
#include "io.h"

#include <filesystem>

class SlipModelExtract
{
private:
	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;

	std::string writeMeshToCache(std::string& filepath, std::vector<SlipMesh::Mesh> meshes, std::vector<SlipMesh::Material> materials);
	std::string writeMatToCache(std::string& filepath, const char* name);
public:
	void save(SlipMesh& ms, std::string filepath);
	void save(SlipCollision ms, std::string filepath);
	void save(SlipMaterial ms, std::string filepath);
	void save(SlipBsp bsp, std::string filepath);
	
	SlipMesh extract(std::string filepath);
	void extractCol(std::string filepath);
	void extractBsp(std::string filepath);
	void extractTexture(std::string filepath);
	void extractFromPreCache(SlipLevel* lvl, std::string filepath);

	SlipModelExtract();
};

#endif // !SLIP_MODEL_EXTRACTOR_H