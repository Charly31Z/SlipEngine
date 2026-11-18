#pragma once

#ifndef SLIP_UI_H
#define SLIP_UI_H

#include "SlipEngine.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stdio.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "SlipCamera.h"
#include "SlipShader.h"

#include <map>

#ifndef SLIP_INSPECTOR
#include <ft2build.h>
#include FT_FREETYPE_H
#endif // !SLIP_INSPECTOR

class SLIPENGINE_API SlipUIInterface
{
public:
	glm::vec2 position;
	float rotation;
	glm::vec2 scale{ 1.f };

	glm::vec3 color{ 1.f };

	virtual void draw() {}
};

class SLIPENGINE_API SlipUIText : public SlipUIInterface
{
private:
	glm::mat4 getModelMatrix();

	struct Character
	{
		unsigned int TextureID;  // ID handle of the glyph texture
		glm::ivec2   Size;       // Size of glyph
		glm::ivec2   Bearing;    // Offset from baseline to left/top of glyph
		unsigned int Advance;    // Offset to advance to next glyph
	};

	std::map<char, Character> Characters;

#ifndef SLIP_INSPECTOR
	FT_Face face;
	FT_Library ft;
#endif // !SLIP_INSPECTOR

	unsigned int VAO, VBO;
public:
	std::string text;

	SlipShader* shader;

	void setupMesh();

	SlipUIText();

	void draw();
};

class SLIPENGINE_API SlipUIImage : public SlipUIInterface
{
private:
	glm::mat4 getModelMatrix();
public:
	struct Texture
	{
		unsigned int id;
		std::string path;
	};

	struct Vertex
	{
		glm::vec3 positions;
		glm::vec2 texCoords;
	};

	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;
	Texture texture;

	SlipShader* shader;

	void setupMesh();
	void setupTexture();

	unsigned int VAO, VBO, EBO;

	void draw();
};

class SLIPENGINE_API SlipUI
{
private:
	std::map<std::string, SlipUIInterface*> elements;

	inline static SlipUI *m_Instance;
public:

	SlipUI();

	SlipUI& Get() { return *m_Instance; }

	SlipUIInterface& Find(std::string name);

	SlipUIImage& image(std::string name, std::string texturePath, glm::vec2 position);
	SlipUIImage& image(std::string name, unsigned int texture, glm::vec2 position);

	SlipUIText* text(std::string name, std::string text, glm::vec2 position);

	void draw();
};

#endif // !SLIP_UI_H