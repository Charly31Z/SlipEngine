#pragma once

#ifndef SLIP_SHADER_H
#define SLIP_SHADER_H

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stdio.h>
#include <stdint.h>
#include <string>

class SlipShader
{
public:
	unsigned int ID;

	SlipShader(const char* vertexPath, const char* fragmentPath);
	SlipShader(const char* vertexPath, const char* fragmentPath, const char* tessellationControlPath, const char* tessellationEvaluationPath);

	void use();
	void destroy();

	void setBool(const std::string& name, bool value) const;
	void setInt(const std::string& name, int value) const;
	void setFloat(const std::string& name, float value) const;
	void setMat4(const std::string& name, glm::mat4 &value) const;
	void setVec2(const std::string& name, glm::vec2 &value) const;
	void setVec2(const std::string& name, float x, float y) const;
	void setVec3(const std::string& name, glm::vec3 &value) const;
	void setVec3(const std::string& name, float x, float y, float z) const;
};

#endif // !SLIP_SHADER_H