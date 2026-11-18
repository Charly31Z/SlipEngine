#pragma once

#ifndef SLIP_MODEL_EXTRACTOR_H
#define SLIP_MODEL_EXTRACTOR_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include "SlipEngine/render/SlipSkeleton.h"
#include "SlipEngine/render/SlipAnimation.h"

struct AssetMetadata {
    std::string guid;                  // UID único para cada asset (estilo Unity/Unreal)
    std::string assetType;             // "Model", "Texture", "Audio", "Shader", "Scene", "Generic"
    std::string originalFile;          // Nombre del archivo original
    uintmax_t fileSize = 0;            // Tamaño en bytes

    // Configuraciones de importación para Modelos 3D (.fbx, .obj, .dae, .gltf)
    float importScale = 1.0f;
    glm::vec3 importRotation = glm::vec3(0.0f); // Rotación adicional en grados (ej: -90 en X para corregir modelos Z-up de UE5/Blender)
    bool bakeTransforms = true;                 // Hornear transformaciones de nodos (corrige ejes Z-up a Y-up)
    bool importMaterials = true;
    bool importTextures = true;
    bool flipUVs = false;
    bool importNormals = true;
    bool generateColliders = false;
    bool importBones = true;
    bool importAnimations = true;
    std::string sourceOriginDir = "";

    // Configuraciones de importación para Texturas (.png, .jpg, .tga)
    std::string filterMode = "Linear"; // Linear, Nearest
    std::string wrapMode = "Repeat";   // Repeat, ClampToEdge, MirroredRepeat
    bool generateMipmaps = true;
};

struct ModelInspectionInfo {
    int meshCount = 0;
    int vertexCount = 0;
    int boneCount = 0;
    struct AnimInfo {
        std::string name;
        float durationSeconds = 0.0f;
        float ticksPerSecond = 24.0f;
    };
    std::vector<AnimInfo> animations;
};

struct AnimationClipAsset {
    std::string name;
    std::string sourceModel;
    int clipIndex = 0;
    float duration = 1.0f;
    float ticksPerSecond = 30.0f;
    bool loop = true;
    float playbackSpeed = 1.0f;
    float startTime = 0.0f;
    float endTime = 1.0f;
};

class SlipModelExtract {
public:
    SlipModelExtract() = default;

    // Genera un UID único de 128 bits (32 caracteres hexadecimales)
    static std::string GenerateUID();

    // Deduce el tipo de asset según su extensión
    static std::string DeduceAssetType(const std::string& extension);

    // Obtener UID de un asset (asegura su .meta) y buscar asset por UID en el proyecto
    static std::string GetGuidForAsset(const std::string& assetPath);
    static std::string FindAssetByGuid(const std::string& guid);

    // Cargar y guardar archivos .meta
    static bool LoadMeta(const std::string& metaFilePath, AssetMetadata& outMeta);
    static bool SaveMeta(const std::string& metaFilePath, const AssetMetadata& meta);

    // Asegura que un archivo crudo tenga su respectivo .meta; si no existe, lo genera con un UID nuevo
    static bool EnsureMetaForFile(const std::string& filePath, AssetMetadata* outMeta = nullptr);

    // Importa un archivo crudo al proyecto (copia el .fbx, .png, etc. crudo sin modificar y le crea su .meta)
    static bool ImportRawAsset(const std::string& sourceFilePath,
                               const std::string& targetDirectory = "",
                               std::string* outImportedPath = nullptr,
                               const AssetMetadata* customOptions = nullptr);

    // Escanea recursivamente un directorio y genera .meta para todos los archivos crudos que no lo tengan
    static void ScanAndGenerateMetas(const std::string& rootDirectory);

    // Inspecciona un modelo crudo (.fbx, .obj) usando Assimp sin crear ningún archivo .cache_model
    static bool InspectModel(const std::string& modelPath, int& outMeshCount, int& outVertexCount);
    static bool InspectModelDetailed(const std::string& modelPath, ModelInspectionInfo& outInfo);

    // Procesa un modelo crudo con Assimp aplicando las opciones del .meta y guarda los datos binarios en cache/ usando SlipFileSystem
    static bool BakeModelToCache(const std::string& modelPath, 
                                 const AssetMetadata& meta, 
                                 std::string* outCacheFilePath = nullptr,
                                 std::function<void(float progress)> progressCallback = nullptr);

    // Extrae esqueleto de huesos y animaciones de un modelo crudo (FBX, GLTF, DAE)
    static bool ExtractSkeletonAndAnimations(const std::string& modelPath,
                                            SlipSkeleton& outSkeleton,
                                            std::vector<std::shared_ptr<AnimationClip>>& outClips);

    // Extrae y guarda únicamente las animaciones de un FBX/GLTF como archivos .anim en la carpeta de destino
    static bool ExtractAnimationClipsOnly(const std::string& sourceModelPath,
                                         const std::string& targetAnimationsDir,
                                         std::vector<std::string>& outCreatedAnimFiles);

    // Guardado y carga de clips de animación individuales (.anim)
    static bool SaveAnimAsset(const std::string& filePath, const AnimationClipAsset& data);
    static bool LoadAnimAsset(const std::string& filePath, AnimationClipAsset& outData);
    static std::string SanitizeClipFileName(const std::string& clipName);

    // Sistema de Tareas Asíncronas en Segundo Plano (Background Task & Progress Bar)
    static void ImportRawAssetAsync(const std::string& sourceFilePath,
                                   const std::string& targetDirectory = "",
                                   std::function<void(bool success, const std::string& importedPath)> onComplete = nullptr,
                                   const AssetMetadata* customOptions = nullptr);

    static void BakeModelToCacheAsync(const std::string& modelPath,
                                     const AssetMetadata& meta,
                                     std::function<void(bool success, const std::string& cachePath)> onComplete = nullptr);

    static bool IsProcessing();
    static float GetProcessProgress();
    static std::string GetProcessStatus();
    static std::string GetActiveAssetName();
    static void UpdateTasks();
    static void Shutdown();

    // Obtiene la ruta de cache esperada para un GUID o archivo de modelo
    static std::string GetCachePathForGuid(const std::string& guid);

    // Métodos de compatibilidad histórica
    void extract(const std::string& filepath);
    void extractCol(const std::string& filepath);
    void extractBsp(const std::string& filepath);
    void extractTexture(const std::string& filepath);
};

#endif // !SLIP_MODEL_EXTRACTOR_H
