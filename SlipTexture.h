#pragma once

#ifndef SLIP_TEXTURE_H
#define SLIP_TEXTURE_H

#include <string>

struct ImageData
{
	unsigned char* data;
	size_t size = 0;
};

class SlipTexture
{
private:
	unsigned int ID;
public:
	std::string path;

	int width, height, nrComponents;
	int format;

	ImageData imgData;

	void init();
	void draw();
};

#endif // !SLIP_TEXTURE_H