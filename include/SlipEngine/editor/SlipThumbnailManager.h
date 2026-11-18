#pragma once

#ifndef SLIP_THUMBNAIL_MANAGER_H
#define SLIP_THUMBNAIL_MANAGER_H

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>

class SlipThumbnailManager {
public:
    static SlipThumbnailManager& Get();

    void Init();
    void Shutdown();

    // Devuelve el ID de textura de OpenGL (128x128) para el asset indicado.
    // Si no existe miniatura generada para modelos, texturas o materiales,
    // la genera automáticamente y la almacena en cache/thumbnails/.
    unsigned int GetThumbnail(const std::string& assetPath, bool isDirectory, const std::string& extension);

    // Genera miniatura PNG para modelos 3D (.fbx, .obj, .gltf, .dae)
    bool GenerateModelThumbnail(const std::string& modelPath, bool force = false);

    // Genera miniatura PNG para texturas (.png, .jpg, .jpeg, .tga, .bmp)
    bool GenerateTextureThumbnail(const std::string& texturePath, bool force = false);

    // Genera miniatura PNG para materiales (.mat)
    bool GenerateMaterialThumbnail(const std::string& matPath, bool force = false);

    // Invalida la miniatura en caché y memoria para que se vuelva a generar
    void InvalidateThumbnail(const std::string& assetPath);

    // Limpia la caché de texturas OpenGL cargadas
    void ClearTextureCache();

    // Obtiene la ruta al archivo PNG en el disco para la miniatura
    std::string GetThumbnailPngPath(const std::string& assetPath);

    // Iconos de sistema por defecto (OpenGL Texture IDs)
    unsigned int GetFolderIcon() const { return m_FolderIcon; }
    unsigned int GetScriptIcon() const { return m_ScriptIcon; }
    unsigned int GetAnimationIcon() const { return m_AnimationIcon; }
    unsigned int GetShaderGraphIcon() const { return m_ShaderGraphIcon; }
    unsigned int GetPrefabIcon() const { return m_PrefabIcon; }
    unsigned int GetSceneIcon() const { return m_SceneIcon; }
    unsigned int GetControllerIcon() const { return m_ControllerIcon; }
    unsigned int GetAudioIcon() const { return m_AudioIcon; }
    unsigned int GetFileIcon() const { return m_FileIcon; }

private:
    SlipThumbnailManager() = default;
    ~SlipThumbnailManager();
    SlipThumbnailManager(const SlipThumbnailManager&) = delete;
    SlipThumbnailManager& operator=(const SlipThumbnailManager&) = delete;

    std::string GetCacheDirectory();

    void EnsureFBO();
    void EnsureSphereResources();

    unsigned int LoadTextureFromFile(const std::string& pngPath);
    void GenerateBuiltinIcons();
    unsigned int CreateTextureFromRGBA(const unsigned char* data, int width, int height);

    bool m_Initialized = false;

    // FBO offscreen para renderizar miniaturas a menor resolución (128x128)
    unsigned int m_FBO = 0;
    unsigned int m_ColorTex = 0;
    unsigned int m_DepthRBO = 0;
    const int m_ThumbWidth = 128;
    const int m_ThumbHeight = 128;

    // Esfera para preview de materiales
    unsigned int m_SphereVAO = 0;
    unsigned int m_SphereVBO = 0;
    unsigned int m_SphereEBO = 0;
    unsigned int m_SphereIndexCount = 0;
    bool m_SphereInitialized = false;

    // Texturas de iconos integrados
    unsigned int m_FolderIcon = 0;
    unsigned int m_ScriptIcon = 0;
    unsigned int m_AnimationIcon = 0;
    unsigned int m_ShaderGraphIcon = 0;
    unsigned int m_PrefabIcon = 0;
    unsigned int m_SceneIcon = 0;
    unsigned int m_ControllerIcon = 0;
    unsigned int m_AudioIcon = 0;
    unsigned int m_FileIcon = 0;

    // Mapeo: ruta del asset -> ID de textura GL cargada
    std::unordered_map<std::string, unsigned int> m_LoadedTextures;
};

#endif // !SLIP_THUMBNAIL_MANAGER_H
