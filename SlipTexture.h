#pragma once

#ifndef SLIP_TEXTURE_H
#define SLIP_TEXTURE_H

#include "SlipEngine.h"

#include <string>

struct ImageData
{
	unsigned char* data;
	size_t size = 0;
};

class SLIPENGINE_API SlipTexture
{
private:
	unsigned int ID;
public:
	std::string path;

	int width, height, nrComponents;
	int format;

	ImageData imgData;

	SlipTexture* load(const char* image);

	void init();
	void draw();

	unsigned int& getId() { return ID; }
};

#endif // !SLIP_TEXTURE_H