#pragma once

#ifndef SLIP_SHADER_H
#define SLIP_SHADER_H

#include "SlipEngine.h"

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    #include <GLES3/gl3.h>
    #include <GLES3/gl32.h>
#else
    #include <glad/glad.h>
#endif

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stdio.h>
#include <stdint.h>
#include <string>

class SLIPENGINE_API SlipShader
{
public:
	unsigned int ID;

	SlipShader() : ID(0) {}
	SlipShader(const char* vertexPath, const char* fragmentPath);
	SlipShader(const char* vertexPath, const char* fragmentPath, const char* tessellationControlPath, const char* tessellationEvaluationPath);

	static SlipShader* CreateFromSource(const char* vertexSrc, const char* fragmentSrc);

	void use();
	void destroy();

	void setBool(const std::string& name, bool value) const;
	void setInt(const std::string& name, int value) const;
	void setFloat(const std::string& name, float value) const;
	void setMat4(const std::string& name, const glm::mat4 &value) const;
	void setMat4Array(const std::string& name, const glm::mat4* values, int count) const;
	void setVec2(const std::string& name, const glm::vec2 &value) const;
	void setVec2(const std::string& name, float x, float y) const;
	void setVec3(const std::string& name, const glm::vec3 &value) const;
	void setVec3(const std::string& name, float x, float y, float z) const;
	void setVec4(const std::string& name, const glm::vec4 &value) const;
	void setVec4(const std::string& name, float x, float y, float z, float w) const;
};

#endif // !SLIP_SHADER_H