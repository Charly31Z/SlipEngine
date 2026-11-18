#pragma once

#ifndef SLIP_MATERIAL_H
#define SLIP_MATERIAL_H

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

#include "SlipEngine.h"

#include "SlipShader.h"
#include "SlipCamera.h"
#include "SlipTexture.h"

class SLIPENGINE_API SlipMaterial
{
private:
	std::shared_ptr<SlipShader> m_CustomShader;

	void loadMatFromFile(std::string path);
	void bindGraphParameters(SlipShader* targetShader);
public:
	std::string path;

	glm::vec3 color = glm::vec3(1.0f);
	glm::vec3 ambient = glm::vec3(0.125f);
	glm::vec3 specular = glm::vec3(0.16f);
	float shininess = 3;

	// Propiedades PBR Lit
    float roughness = 0.5f;
    float metallic = 0.0f;
    float ao = 1.0f;
    float normalScale = 1.0f;
    glm::vec3 emissiveColor = glm::vec3(0.0f);
    float emissiveIntensity = 0.0f;

	char shaderType[32] = "Lit";
	SlipShader* shader = nullptr;

	// Shader Graph Integration
	std::string shaderGraphPath;
	std::unordered_map<std::string, float> graphFloatParams;
	std::unordered_map<std::string, glm::vec4> graphVectorParams;
	std::unordered_map<std::string, std::string> graphTextureParams;

	char diffPath[192] = "";
	SlipTexture* diffuseTexture = nullptr;

	char normalPath[192] = "";
	SlipTexture* normalTexture = nullptr;

	char roughnessPath[192] = "";
	SlipTexture* roughnessTexture = nullptr;

	char metallicPath[192] = "";
	SlipTexture* metallicTexture = nullptr;

	char aoPath[192] = "";
	SlipTexture* aoTexture = nullptr;

	char emissivePath[192] = "";
	SlipTexture* emissiveTexture = nullptr;

	char opacityPath[192] = "";
	SlipTexture* opacityTexture = nullptr;

	bool alphaClip = false;
	float alphaCutoff = 0.5f;
	bool invertRoughness = false;
	bool doubleSided = false;

	static SlipMaterial generateMaterial(std::string code, const char* path);
	static SlipMaterial generateMaterial2(SlipShader code, const char* path);

	SlipMaterial(std::string path = "");
	~SlipMaterial();

	bool Save(const std::string& savePath = "");
	void reloadTextures();

	void init();
	void bind(glm::mat4& model);
	void bindMaterialProperties(SlipShader* targetShader);

	// Shader Graph Methods
	bool LoadShaderGraph(const std::string& path);
	void SetGraphFloat(const std::string& name, float value);
	void SetGraphVector(const std::string& name, const glm::vec4& value);
	void SetGraphTexture(const std::string& name, const std::string& texturePath);

	float GetGraphFloat(const std::string& name, float fallback = 0.0f) const;
	glm::vec4 GetGraphVector(const std::string& name, const glm::vec4& fallback = glm::vec4(0.0f)) const;
	std::string GetGraphTexture(const std::string& name, const std::string& fallback = "") const;

	bool HasGraphFloat(const std::string& name) const;
	bool HasGraphVector(const std::string& name) const;
	bool HasGraphTexture(const std::string& name) const;
};

#endif // !SLIP_MATERIAL_H