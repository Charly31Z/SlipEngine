#pragma once

#ifndef SLIP_TEXTURE_H
#define SLIP_TEXTURE_H

#include "SlipEngine.h"
#include <glm/glm.hpp>
#include <string>

struct ImageData
{
	unsigned char* data;
	size_t size = 0;
};

class SLIPENGINE_API SlipTexture
{
private:
	unsigned int ID = 0;
public:
	std::string path;

	int width = 0, height = 0, nrComponents = 0;
	int format = 0;

	glm::vec3 averageColor = glm::vec3(1.0f);
	bool hasTransparency = false;

	ImageData imgData;

	static SlipTexture* load(const char* image);
	static SlipTexture* reload(const char* image);
	static SlipTexture* findInCache(const char* image);
	static void clearCache();

	void init();
	void draw();
	void bind(unsigned int unit = 0);
	void SetFilterMode(bool pointFilter);

	unsigned int& getId() { return ID; }
	unsigned int getID() const { return ID; }
};

#endif // !SLIP_TEXTURE_H