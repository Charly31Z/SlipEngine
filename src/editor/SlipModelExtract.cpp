#include "SlipEngine/editor/SlipModelExtract.h"
#include "SlipEngine/editor/SlipClusterBuilder.h"
#include "SlipEngine/editor/SlipProject.h"
#include "SlipEngine/render/SlipMesh.h"
#include "SlipEngine/render/SlipMaterial.h"
#include "SlipEngine/render/SlipSkeleton.h"
#include "SlipEngine/render/SlipAnimation.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/core/SlipConsole.h"
#include "SlipEngine/editor/SlipThumbnailManager.h"
#include <meshoptimizer.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <assimp/ProgressHandler.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <random>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <unordered_set>

namespace fs = std::filesystem;

static std::mutex s_MainThreadMutex;
static std::vector<std::function<void()>> s_MainThreadQueue;

std::string SlipModelExtract::GenerateUID() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    uint64_t part1 = dis(gen);
    uint64_t part2 = dis(gen);

    std::stringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << ((part1 >> 32) & 0xFFFFFFFF) << "-"
       << std::setw(4) << ((part1 >> 16) & 0xFFFF) << "-"
       << std::setw(4) << (part1 & 0xFFFF) << "-"
       << std::setw(4) << ((part2 >> 48) & 0xFFFF) << "-"
       << std::setw(12) << (part2 & 0xFFFFFFFFFFFFULL);

    return ss.str();
}

std::string SlipModelExtract::DeduceAssetType(const std::string& extension) {
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".fbx" || ext == ".obj" || ext == ".dae" || ext == ".gltf" || ext == ".glb" || ext == ".smd") {
        return "Model";
    }
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr") {
        return "Texture";
    }
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac") {
        return "Audio";
    }
    if (ext == ".vert" || ext == ".frag" || ext == ".geom" || ext == ".glsl") {
        return "Shader";
    }
    if (ext == ".shadergraph") {
        return "ShaderGraph";
    }
    if (ext == ".scene" || ext == ".level" || ext == ".bsp") {
        return "Scene";
    }
    if (ext == ".anim") {
        return "Animation";
    }
    if (ext == ".slipproj") {
        return "Project";
    }

    return "Generic";
}

bool SlipModelExtract::LoadMeta(const std::string& metaFilePath, AssetMetadata& outMeta) {
    std::string content;
    if (!SlipFileSystem::ReadAllText(metaFilePath, content)) {
        return false;
    }

    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);

        // Trim
        key.erase(key.find_last_not_of(" \t\r\n") + 1);
        key.erase(0, key.find_first_not_of(" \t\r\n"));
        val.erase(val.find_last_not_of(" \t\r\n") + 1);
        val.erase(0, val.find_first_not_of(" \t\r\n"));

        if (key == "guid") outMeta.guid = val;
        else if (key == "type") outMeta.assetType = val;
        else if (key == "originalFile") outMeta.originalFile = val;
        else if (key == "fileSize") outMeta.fileSize = std::stoull(val);
        else if (key == "importScale") outMeta.importScale = std::stof(val);
        else if (key == "importRotation") {
            std::stringstream r(val);
            r >> outMeta.importRotation.x >> outMeta.importRotation.y >> outMeta.importRotation.z;
        }
        else if (key == "bakeTransforms") outMeta.bakeTransforms = (val == "true" || val == "1");
        else if (key == "importMaterials") outMeta.importMaterials = (val == "true" || val == "1");
        else if (key == "importTextures") outMeta.importTextures = (val == "true" || val == "1");
        else if (key == "flipUVs") outMeta.flipUVs = (val == "true" || val == "1");
        else if (key == "importNormals") outMeta.importNormals = (val == "true" || val == "1");
        else if (key == "generateColliders") outMeta.generateColliders = (val == "true" || val == "1");
        else if (key == "importBones") outMeta.importBones = (val == "true" || val == "1");
        else if (key == "importAnimations") outMeta.importAnimations = (val == "true" || val == "1");
        else if (key == "sourceOriginDir") outMeta.sourceOriginDir = val;
        else if (key == "filterMode") outMeta.filterMode = val;
        else if (key == "wrapMode") outMeta.wrapMode = val;
        else if (key == "generateMipmaps") outMeta.generateMipmaps = (val == "true" || val == "1");
    }

    return !outMeta.guid.empty();
}

bool SlipModelExtract::SaveMeta(const std::string& metaFilePath, const AssetMetadata& meta) {
    std::stringstream ss;
    ss << "guid: " << meta.guid << "\n";
    ss << "type: " << meta.assetType << "\n";
    ss << "originalFile: " << meta.originalFile << "\n";
    ss << "fileSize: " << meta.fileSize << "\n";

    if (meta.assetType == "Model") {
        ss << "importScale: " << meta.importScale << "\n";
        ss << "importRotation: " << meta.importRotation.x << " " << meta.importRotation.y << " " << meta.importRotation.z << "\n";
        ss << "bakeTransforms: " << (meta.bakeTransforms ? "true" : "false") << "\n";
        ss << "importMaterials: " << (meta.importMaterials ? "true" : "false") << "\n";
        ss << "importTextures: " << (meta.importTextures ? "true" : "false") << "\n";
        ss << "flipUVs: " << (meta.flipUVs ? "true" : "false") << "\n";
        ss << "importNormals: " << (meta.importNormals ? "true" : "false") << "\n";
        ss << "generateColliders: " << (meta.generateColliders ? "true" : "false") << "\n";
        ss << "importBones: " << (meta.importBones ? "true" : "false") << "\n";
        ss << "importAnimations: " << (meta.importAnimations ? "true" : "false") << "\n";
        if (!meta.sourceOriginDir.empty()) {
            ss << "sourceOriginDir: " << meta.sourceOriginDir << "\n";
        }
    } else if (meta.assetType == "Texture") {
        ss << "filterMode: " << meta.filterMode << "\n";
        ss << "wrapMode: " << meta.wrapMode << "\n";
        ss << "generateMipmaps: " << (meta.generateMipmaps ? "true" : "false") << "\n";
    }

    return SlipFileSystem::WriteAllText(metaFilePath, ss.str());
}

static void CopyCompanionMtlFiles(const std::string& sourceFilePath, const std::string& targetDir) {
    std::string ext = SlipFileSystem::GetFileExtension(sourceFilePath);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext != ".obj") return;

    std::string sourceDir = SlipFileSystem::GetParentPath(sourceFilePath);
    if (sourceDir.empty()) return;

    std::vector<std::string> mtlNames;
    std::string stem = SlipFileSystem::GetFileNameWithoutExtension(sourceFilePath);
    if (!stem.empty()) {
        mtlNames.push_back(stem + ".mtl");
    }

    std::ifstream in(sourceFilePath);
    if (in.is_open()) {
        std::string line;
        int lineCount = 0;
        while (lineCount < 800 && std::getline(in, line)) {
            lineCount++;
            size_t firstChar = line.find_first_not_of(" \t\r\n");
            if (firstChar == std::string::npos) continue;
            if (line.compare(firstChar, 7, "mtllib ") == 0) {
                std::string mtlRef = line.substr(firstChar + 7);
                mtlRef.erase(mtlRef.find_last_not_of(" \t\r\n") + 1);
                mtlRef.erase(0, mtlRef.find_first_not_of(" \t\r\n"));
                if (!mtlRef.empty()) {
                    mtlNames.push_back(mtlRef);
                }
            }
        }
    }

    for (const auto& mtlName : mtlNames) {
        std::string srcMtl = SlipFileSystem::CombinePath(sourceDir, mtlName);
        if (SlipFileSystem::Exists(srcMtl)) {
            std::string dstMtl = SlipFileSystem::CombinePath(targetDir, SlipFileSystem::GetFileName(mtlName));
            std::error_code ec;
            if (!fs::equivalent(srcMtl, dstMtl, ec)) {
                fs::copy_file(srcMtl, dstMtl, fs::copy_options::overwrite_existing, ec);
            }
        }
    }
}

bool SlipModelExtract::EnsureMetaForFile(const std::string& filePath, AssetMetadata* outMeta) {
    if (filePath.empty() || !SlipFileSystem::Exists(filePath)) return false;

    std::string metaPath = filePath + ".meta";
    AssetMetadata meta;

    if (SlipFileSystem::Exists(metaPath)) {
        if (LoadMeta(metaPath, meta)) {
            std::string currentFileName = SlipFileSystem::GetFileName(filePath);
            if (!currentFileName.empty() && meta.originalFile != currentFileName) {
                meta.originalFile = currentFileName;
                SaveMeta(metaPath, meta);
            }
            if (outMeta) *outMeta = meta;
            return true;
        }
    }

    // Generar nuevo .meta con UID único
    meta.guid = GenerateUID();
    meta.originalFile = SlipFileSystem::GetFileName(filePath);
    meta.assetType = DeduceAssetType(SlipFileSystem::GetFileExtension(filePath));
    meta.fileSize = SlipFileSystem::GetFileSize(filePath);

    bool ok = SaveMeta(metaPath, meta);
    if (outMeta) *outMeta = meta;
    return ok;
}

std::string SlipModelExtract::GetGuidForAsset(const std::string& assetPath) {
    if (assetPath.empty()) return "";
    if (assetPath.rfind("primitive:", 0) == 0) return "";
    AssetMetadata meta;
    if (EnsureMetaForFile(assetPath, &meta)) {
        return meta.guid;
    }
    return "";
}

std::string SlipModelExtract::FindAssetByGuid(const std::string& guid) {
    if (guid.empty()) return "";
    std::string root = SlipFileSystem::GetProjectAssetsPath();
    if (root.empty()) root = SlipFileSystem::GetProjectRootPath();
    if (root.empty()) return "";

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        std::string p = entry.path().string();
        if (p.size() > 5 && p.substr(p.size() - 5) == ".meta") {
            AssetMetadata meta;
            if (LoadMeta(p, meta) && meta.guid == guid) {
                std::string assetPath = p.substr(0, p.size() - 5);
                if (SlipFileSystem::Exists(assetPath)) {
                    return assetPath;
                }
            }
        }
    }
    return "";
}

bool SlipModelExtract::ImportRawAsset(const std::string& sourceFilePath,
                                      const std::string& targetDirectory,
                                      std::string* outImportedPath,
                                      const AssetMetadata* customOptions) {
    if (sourceFilePath.empty() || !SlipFileSystem::Exists(sourceFilePath)) {
        SlipConsole::LogError("Error al importar: archivo de origen no existe (" + sourceFilePath + ")");
        return false;
    }

    std::string targetDir = targetDirectory;
    if (targetDir.empty()) {
        targetDir = SlipProject::Get().GetCurrentProject().assetsPath;
    }
    SlipFileSystem::CreateDirectoryPath(targetDir);

    std::string fileName = SlipFileSystem::GetFileName(sourceFilePath);
    std::string destPath = SlipFileSystem::CombinePath(targetDir, fileName);

    // Copiar el archivo crudo (.fbx, .obj, .png, etc.) al proyecto sin alterarlo
    std::error_code ec;
    if (!fs::equivalent(sourceFilePath, destPath, ec)) {
        fs::copy_file(sourceFilePath, destPath, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            SlipConsole::LogError("Fallo al copiar archivo crudo: " + ec.message());
            return false;
        }
    }

    CopyCompanionMtlFiles(sourceFilePath, targetDir);

    // Generar o actualizar el archivo .meta
    AssetMetadata meta;
    EnsureMetaForFile(destPath, &meta);
    meta.sourceOriginDir = SlipFileSystem::GetParentPath(sourceFilePath);
    if (customOptions) {
        meta.importScale = customOptions->importScale;
        meta.importRotation = customOptions->importRotation;
        meta.bakeTransforms = customOptions->bakeTransforms;
        meta.importMaterials = customOptions->importMaterials;
        meta.importTextures = customOptions->importTextures;
        meta.flipUVs = customOptions->flipUVs;
        meta.importNormals = customOptions->importNormals;
        meta.generateColliders = customOptions->generateColliders;
        meta.importBones = customOptions->importBones;
        meta.importAnimations = customOptions->importAnimations;
    }
    SaveMeta(destPath + ".meta", meta);

    // Si es un modelo, procesar y bakear el cache binario con las opciones del .meta
    if (meta.assetType == "Model") {
        std::string cachePath;
        if (BakeModelToCache(destPath, meta, &cachePath)) {
            SlipConsole::LogSuccess("Modelo importado y bakeado a cache (" + fileName + ") - UID: " + meta.guid);
        } else {
            SlipConsole::LogWarning("Modelo importado pero falló el bakeado a cache (" + fileName + ") - UID: " + meta.guid);
        }

        // Extraer esqueleto y clips de animación solo si el archivo es ligero o tiene animaciones reales
        std::error_code szEc;
        uintmax_t rawSize = fs::file_size(destPath, szEc);
        if ((meta.importAnimations || meta.importBones) && (szEc || rawSize < 80ull * 1024ull * 1024ull)) {
            SlipSkeleton extractedSkeleton;
            std::vector<std::shared_ptr<AnimationClip>> extractedClips;
            if (ExtractSkeletonAndAnimations(destPath, extractedSkeleton, extractedClips) && !extractedClips.empty()) {
                SlipConsole::LogSuccess("Animaciones extraídas (" + std::to_string(extractedClips.size()) + " clips) en subcarpeta 'Animations'");
            }
        }
    } else {
        if (meta.assetType == "Texture") {
            std::lock_guard<std::mutex> lock(s_MainThreadMutex);
            s_MainThreadQueue.push_back([destPath]() {
                SlipThumbnailManager::Get().GenerateTextureThumbnail(destPath, true);
            });
        }
        SlipConsole::LogSuccess("Asset importado (" + fileName + ") - UID: " + meta.guid);
    }

    if (outImportedPath) {
        *outImportedPath = destPath;
    }

    return true;
}

void SlipModelExtract::ScanAndGenerateMetas(const std::string& rootDirectory) {
    if (rootDirectory.empty() || !SlipFileSystem::Exists(rootDirectory)) return;

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(rootDirectory, fs::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file(ec)) continue;

        std::string path = entry.path().string();
        if (path.length() >= 5 && path.substr(path.length() - 5) == ".meta") {
            continue; // Ignorar archivos .meta existentes
        }

        std::string metaPath = path + ".meta";
        if (!fs::exists(metaPath, ec)) {
            EnsureMetaForFile(path);
        }
    }
}

bool SlipModelExtract::InspectModel(const std::string& modelPath, int& outMeshCount, int& outVertexCount) {
    outMeshCount = 0;
    outVertexCount = 0;

    Assimp::Importer importer;
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    const aiScene* scene = importer.ReadFile(modelPath, aiProcess_Triangulate | aiProcess_GenSmoothNormals);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        return false;
    }

    outMeshCount = scene->mNumMeshes;
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        if (scene->mMeshes[i]) {
            outVertexCount += scene->mMeshes[i]->mNumVertices;
        }
    }

    return true;
}

bool SlipModelExtract::InspectModelDetailed(const std::string& modelPath, ModelInspectionInfo& outInfo) {
    outInfo = ModelInspectionInfo();
    if (modelPath.empty() || !SlipFileSystem::Exists(modelPath)) return false;

    // Si existe un .meta que ya especifica que no se importan huesos/animaciones o si el archivo
    // supera los 60 MB (ej. scans de fotogrametría de millones de triángulos), evitar re-parsear
    // el FBX completo en el hilo principal durante Drag & Drop.
    AssetMetadata existingMeta;
    if (SlipFileSystem::Exists(modelPath + ".meta") && LoadMeta(modelPath + ".meta", existingMeta)) {
        if (!existingMeta.importBones && !existingMeta.importAnimations) {
            outInfo.meshCount = 1;
            outInfo.boneCount = 0;
            return true;
        }
    }
    std::error_code szEc;
    uintmax_t rawBytes = fs::file_size(modelPath, szEc);
    if (!szEc && rawBytes > 60ull * 1024ull * 1024ull) {
        outInfo.meshCount = 1;
        outInfo.boneCount = 0;
        return true;
    }

    Assimp::Importer importer;
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    const aiScene* scene = importer.ReadFile(modelPath, aiProcess_Triangulate);
    if (!scene || !scene->mRootNode) {
        return false;
    }

    outInfo.meshCount = scene->mNumMeshes;
    std::unordered_set<std::string> uniqueBones;
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        if (scene->mMeshes[i]) {
            outInfo.vertexCount += scene->mMeshes[i]->mNumVertices;
            for (unsigned int b = 0; b < scene->mMeshes[i]->mNumBones; ++b) {
                if (scene->mMeshes[i]->mBones[b]) {
                    uniqueBones.insert(scene->mMeshes[i]->mBones[b]->mName.C_Str());
                }
            }
        }
    }
    outInfo.boneCount = static_cast<int>(uniqueBones.size());

    for (unsigned int i = 0; i < scene->mNumAnimations; ++i) {
        const aiAnimation* anim = scene->mAnimations[i];
        if (!anim) continue;
        ModelInspectionInfo::AnimInfo info;
        info.name = anim->mName.C_Str();
        if (info.name.empty()) info.name = "Anim_" + std::to_string(i);
        size_t barPos = info.name.find_last_of('|');
        if (barPos != std::string::npos && barPos + 1 < info.name.length()) {
            info.name = info.name.substr(barPos + 1);
        }
        info.ticksPerSecond = (anim->mTicksPerSecond > 1e-4) ? static_cast<float>(anim->mTicksPerSecond) : 25.0f;
        info.durationSeconds = static_cast<float>(anim->mDuration / info.ticksPerSecond);
        outInfo.animations.push_back(info);
    }

    return true;
}

void SlipModelExtract::extract(const std::string& filepath) {
    ImportRawAsset(filepath);
}

void SlipModelExtract::extractCol(const std::string& filepath) {
    ImportRawAsset(filepath);
}

void SlipModelExtract::extractBsp(const std::string& filepath) {
    ImportRawAsset(filepath);
}

void SlipModelExtract::extractTexture(const std::string& filepath) {
    ImportRawAsset(filepath);
}

std::string SlipModelExtract::GetCachePathForGuid(const std::string& guid) {
    std::string cacheDir = "cache";
    if (SlipProject::Get().HasActiveProject()) {
        cacheDir = SlipFileSystem::CombinePath(SlipProject::Get().GetCurrentProject().rootPath, "cache");
    }
    SlipFileSystem::CreateDirectoryPath(cacheDir);
    return SlipFileSystem::CombinePath(cacheDir, guid + ".model_data");
}

class AssimpBakeProgressHandler : public Assimp::ProgressHandler {
public:
    std::function<void(float)> callback;
    AssimpBakeProgressHandler(std::function<void(float)> cb) : callback(cb) {}
    bool Update(float percentage) override {
        if (callback && percentage >= 0.0f) {
            callback(percentage * 0.70f);
        }
        return true;
    }
};

struct AsyncTask {
    enum class Type { Import, Bake };
    Type type;
    std::string sourcePath;
    std::string targetDir;
    AssetMetadata meta;
    bool hasCustomOptions = false;
    std::function<void(bool success, const std::string& resultPath)> onComplete;
};

static std::thread s_WorkerThread;
static std::mutex s_TaskMutex;
static std::condition_variable s_TaskCv;
static std::queue<AsyncTask> s_TaskQueue;
static std::atomic<bool> s_WorkerRunning{false};
static std::atomic<bool> s_IsProcessing{false};
static std::atomic<float> s_CurrentProgress{0.0f};

static std::mutex s_StatusMutex;
static std::string s_StatusMessage = "";
static std::string s_ActiveAssetName = "";

static void EnsureWorkerStarted() {
    if (s_WorkerRunning.load()) return;
    s_WorkerRunning.store(true);
    s_WorkerThread = std::thread([]() {
        while (s_WorkerRunning.load()) {
            AsyncTask task;
            {
                std::unique_lock<std::mutex> lock(s_TaskMutex);
                s_TaskCv.wait(lock, []() {
                    return !s_TaskQueue.empty() || !s_WorkerRunning.load();
                });
                if (!s_WorkerRunning.load() && s_TaskQueue.empty()) break;
                task = s_TaskQueue.front();
                s_TaskQueue.pop();
            }

            s_IsProcessing.store(true);
            s_CurrentProgress.store(0.0f);

            std::string fileName = SlipFileSystem::GetFileName(task.sourcePath);
            {
                std::lock_guard<std::mutex> lock(s_StatusMutex);
                s_ActiveAssetName = fileName;
                s_StatusMessage = "Iniciando procesamiento: " + fileName;
            }

            bool success = false;
            std::string resultPath = "";

            if (task.type == AsyncTask::Type::Import) {
                {
                    std::lock_guard<std::mutex> lock(s_StatusMutex);
                    s_StatusMessage = "Copiando archivo: " + fileName;
                }
                s_CurrentProgress.store(0.1f);

                std::string targetDir = task.targetDir;
                if (targetDir.empty()) {
                    targetDir = SlipProject::Get().GetCurrentProject().assetsPath;
                }
                SlipFileSystem::CreateDirectoryPath(targetDir);
                std::string destPath = SlipFileSystem::CombinePath(targetDir, fileName);

                std::error_code ec;
                if (!fs::equivalent(task.sourcePath, destPath, ec)) {
                    fs::copy_file(task.sourcePath, destPath, fs::copy_options::overwrite_existing, ec);
                }
                if (ec) {
                    SlipConsole::LogError("Fallo al copiar archivo crudo: " + ec.message());
                } else {
                    CopyCompanionMtlFiles(task.sourcePath, targetDir);

                    AssetMetadata meta;
                    SlipModelExtract::EnsureMetaForFile(destPath, &meta);
                    meta.sourceOriginDir = SlipFileSystem::GetParentPath(task.sourcePath);
                    if (task.hasCustomOptions) {
                        meta.importScale = task.meta.importScale;
                        meta.importRotation = task.meta.importRotation;
                        meta.bakeTransforms = task.meta.bakeTransforms;
                        meta.importMaterials = task.meta.importMaterials;
                        meta.importTextures = task.meta.importTextures;
                        meta.flipUVs = task.meta.flipUVs;
                        meta.importNormals = task.meta.importNormals;
                        meta.generateColliders = task.meta.generateColliders;
                        meta.importBones = task.meta.importBones;
                        meta.importAnimations = task.meta.importAnimations;
                    }
                    SlipModelExtract::SaveMeta(destPath + ".meta", meta);

                    if (meta.assetType == "Model") {
                        {
                            std::lock_guard<std::mutex> lock(s_StatusMutex);
                            s_StatusMessage = "Bakeando cache binario: " + fileName;
                        }
                        std::string cachePath;
                        success = SlipModelExtract::BakeModelToCache(destPath, meta, &cachePath, [](float p) {
                            s_CurrentProgress.store(p);
                        });
                        resultPath = destPath;

                        if (success) {
                            SlipConsole::LogSuccess("Modelo importado y bakeado a cache (" + fileName + ") - UID: " + meta.guid);
                        } else {
                            SlipConsole::LogWarning("Modelo importado pero falló el bakeado a cache (" + fileName + ") - UID: " + meta.guid);
                        }

                        // Extraer esqueleto y clips de animación solo si el archivo no es un scan estático masivo (> 80 MB)
                        std::error_code szEc;
                        uintmax_t rawSize = fs::file_size(destPath, szEc);
                        if ((meta.importAnimations || meta.importBones) && (szEc || rawSize < 80ull * 1024ull * 1024ull)) {
                            SlipSkeleton extractedSkeleton;
                            std::vector<std::shared_ptr<AnimationClip>> extractedClips;
                            if (SlipModelExtract::ExtractSkeletonAndAnimations(destPath, extractedSkeleton, extractedClips) && !extractedClips.empty()) {
                                SlipConsole::LogSuccess("Animaciones extraídas (" + std::to_string(extractedClips.size()) + " clips) en subcarpeta 'Animations'");
                            }
                        }
                    } else {
                        success = true;
                        resultPath = destPath;
                        s_CurrentProgress.store(1.0f);
                        SlipConsole::LogSuccess("Asset importado (" + fileName + ") - UID: " + meta.guid);
                    }
                }
            } else if (task.type == AsyncTask::Type::Bake) {
                {
                    std::lock_guard<std::mutex> lock(s_StatusMutex);
                    s_StatusMessage = "Re-bakeando modelo: " + fileName;
                }
                std::string cachePath;
                success = SlipModelExtract::BakeModelToCache(task.sourcePath, task.meta, &cachePath, [](float p) {
                    s_CurrentProgress.store(p);
                });
                resultPath = cachePath;
                if (success) {
                    SlipConsole::LogSuccess("Modelo bakeado a cache con éxito: " + fileName);
                } else {
                    SlipConsole::LogError("Error al bakear modelo: " + task.sourcePath);
                }
            }

            s_CurrentProgress.store(1.0f);
            {
                std::lock_guard<std::mutex> lock(s_StatusMutex);
                s_StatusMessage = success ? ("Completado: " + fileName) : ("Fallo: " + fileName);
            }

            // Encolar callback para ser ejecutado en el hilo principal
            if (task.onComplete) {
                std::lock_guard<std::mutex> lock(s_MainThreadMutex);
                s_MainThreadQueue.push_back([task, success, resultPath]() {
                    task.onComplete(success, resultPath);
                });
            }

            {
                std::unique_lock<std::mutex> lock(s_TaskMutex);
                if (s_TaskQueue.empty()) {
                    s_IsProcessing.store(false);
                    std::lock_guard<std::mutex> slock(s_StatusMutex);
                    s_ActiveAssetName = "";
                }
            }
        }
        s_IsProcessing.store(false);
    });
}

void SlipModelExtract::ImportRawAssetAsync(const std::string& sourceFilePath,
                                          const std::string& targetDirectory,
                                          std::function<void(bool, const std::string&)> onComplete,
                                          const AssetMetadata* customOptions) {
    if (sourceFilePath.empty() || !SlipFileSystem::Exists(sourceFilePath)) {
        SlipConsole::LogError("Error al importar: archivo no existe (" + sourceFilePath + ")");
        if (onComplete) onComplete(false, "");
        return;
    }
    EnsureWorkerStarted();
    AsyncTask task;
    task.type = AsyncTask::Type::Import;
    task.sourcePath = sourceFilePath;
    task.targetDir = targetDirectory;
    if (customOptions) {
        task.meta = *customOptions;
        task.hasCustomOptions = true;
    }
    task.onComplete = onComplete;
    {
        std::lock_guard<std::mutex> lock(s_TaskMutex);
        s_TaskQueue.push(task);
    }
    s_TaskCv.notify_one();
}

void SlipModelExtract::BakeModelToCacheAsync(const std::string& modelPath,
                                            const AssetMetadata& meta,
                                            std::function<void(bool, const std::string&)> onComplete) {
    if (modelPath.empty() || !SlipFileSystem::Exists(modelPath)) {
        SlipConsole::LogError("Error al bakear: modelo no existe (" + modelPath + ")");
        if (onComplete) onComplete(false, "");
        return;
    }
    EnsureWorkerStarted();
    AsyncTask task;
    task.type = AsyncTask::Type::Bake;
    task.sourcePath = modelPath;
    task.meta = meta;
    task.onComplete = onComplete;
    {
        std::lock_guard<std::mutex> lock(s_TaskMutex);
        s_TaskQueue.push(task);
    }
    s_TaskCv.notify_one();
}

bool SlipModelExtract::IsProcessing() {
    return s_IsProcessing.load();
}

float SlipModelExtract::GetProcessProgress() {
    return s_CurrentProgress.load();
}

std::string SlipModelExtract::GetProcessStatus() {
    std::lock_guard<std::mutex> lock(s_StatusMutex);
    return s_StatusMessage;
}

std::string SlipModelExtract::GetActiveAssetName() {
    std::lock_guard<std::mutex> lock(s_StatusMutex);
    return s_ActiveAssetName;
}

void SlipModelExtract::UpdateTasks() {
    std::vector<std::function<void()>> toRun;
    {
        std::lock_guard<std::mutex> lock(s_MainThreadMutex);
        if (!s_MainThreadQueue.empty()) {
            toRun = std::move(s_MainThreadQueue);
            s_MainThreadQueue.clear();
        }
    }
    for (auto& fn : toRun) {
        if (fn) fn();
    }
}

void SlipModelExtract::Shutdown() {
    if (s_WorkerRunning.load()) {
        s_WorkerRunning.store(false);
        s_TaskCv.notify_all();
        if (s_WorkerThread.joinable()) {
            s_WorkerThread.join();
        }
    }
}

static std::string SanitizeAssetName(const std::string& rawName, const std::string& fallback) {
    std::string out;
    for (char c : rawName) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            out.push_back(c);
        } else if (c == ' ' || c == '.' || c == ':' || c == '/' || c == '\\') {
            if (!out.empty() && out.back() != '_') out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty()) return fallback;
    return out;
}

static std::string MakePortableAssetPath(const std::string& fullPath) {
    std::string norm = fullPath;
    std::replace(norm.begin(), norm.end(), '\\', '/');

    std::string rootDir = SlipFileSystem::GetProjectRootPath();
    if (!rootDir.empty()) {
        std::replace(rootDir.begin(), rootDir.end(), '\\', '/');
        if (rootDir.back() != '/') rootDir.push_back('/');
        if (norm.rfind(rootDir, 0) == 0) {
            return norm.substr(rootDir.size());
        }
    }
    return norm;
}

bool SlipModelExtract::BakeModelToCache(const std::string& modelPath, 
                                        const AssetMetadata& meta, 
                                        std::string* outCacheFilePath,
                                        std::function<void(float progress)> progressCallback) {
    if (modelPath.empty() || !SlipFileSystem::Exists(modelPath)) return false;

    if (progressCallback) progressCallback(0.05f);

    Assimp::Importer importer;
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    if (progressCallback) {
        importer.SetProgressHandler(new AssimpBakeProgressHandler(progressCallback));
    }

    // NOTA DE RENDIMIENTO: No activar aiProcess_JoinIdenticalVertices en Assimp porque en modelos de
    // millones de triángulos (ej. scans 3D de 4M+ tris) Assimp tarda varios minutos en su post-proceso.
    // En su lugar hacemos la deduplicación exacta con meshopt_generateVertexRemap en milisegundos.
    unsigned int flags = aiProcess_Triangulate | aiProcess_GenUVCoords;
    std::error_code fileSzEc;
    uintmax_t modelFileBytes = fs::file_size(modelPath, fileSzEc);
    bool isMassiveModel = (!fileSzEc && modelFileBytes > 50ull * 1024ull * 1024ull);

    if (meta.importNormals) {
        flags |= isMassiveModel ? aiProcess_GenNormals : aiProcess_GenSmoothNormals;
    }
    if (meta.flipUVs) {
        flags |= aiProcess_FlipUVs;
    }
    if (!isMassiveModel) {
        flags |= aiProcess_CalcTangentSpace;
    }

    const aiScene* scene = importer.ReadFile(modelPath, flags);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        SlipConsole::LogError("Assimp falló al cargar modelo para bake: " + std::string(importer.GetErrorString()));
        return false;
    }

    if (progressCallback) progressCallback(0.72f);

    auto aiToGlm = [](const aiMatrix4x4& m) -> glm::mat4 {
        return glm::mat4(
            m.a1, m.b1, m.c1, m.d1,
            m.a2, m.b2, m.c2, m.d2,
            m.a3, m.b3, m.c3, m.d3,
            m.a4, m.b4, m.c4, m.d4
        );
    };

    // 1. Recolectar matriz de nodo para cada aiMesh de la escena
    std::vector<glm::mat4> meshNodeMats(scene->mNumMeshes, glm::mat4(1.0f));
    std::vector<bool> meshVisited(scene->mNumMeshes, false);

    if (meta.bakeTransforms && scene->mRootNode) {
        std::function<void(const aiNode*, const glm::mat4&)> traverseNodes = [&](const aiNode* node, const glm::mat4& parentMat) {
            if (!node) return;
            glm::mat4 globalMat = parentMat * aiToGlm(node->mTransformation);
            for (unsigned int m = 0; m < node->mNumMeshes; ++m) {
                unsigned int meshIdx = node->mMeshes[m];
                if (meshIdx < scene->mNumMeshes && !meshVisited[meshIdx]) {
                    meshNodeMats[meshIdx] = globalMat;
                    meshVisited[meshIdx] = true;
                }
            }
            for (unsigned int c = 0; c < node->mNumChildren; ++c) {
                traverseNodes(node->mChildren[c], globalMat);
            }
        };
        traverseNodes(scene->mRootNode, glm::mat4(1.0f));

        glm::mat4 rootMat = aiToGlm(scene->mRootNode->mTransformation);
        for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
            if (!meshVisited[i]) {
                meshNodeMats[i] = rootMat;
            }
        }
    }

    // 2. Calcular Bounding Box tras transformaciones de nodos para auto-orientación y auto-escala
    glm::vec3 nodeMin(1e30f), nodeMax(-1e30f);
    bool hasVertices = false;
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[i];
        if (!mesh) continue;
        const glm::mat4& nMat = meshNodeMats[i];
        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            glm::vec3 p = glm::vec3(nMat * glm::vec4(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z, 1.0f));
            nodeMin = glm::min(nodeMin, p);
            nodeMax = glm::max(nodeMax, p);
            hasVertices = true;
        }
    }

    glm::vec3 effectiveRot = meta.importRotation;
    float effectiveScale = meta.importScale;
    bool metaAutoAdjusted = false;

    if (hasVertices) {
        glm::vec3 nodeExt = nodeMax - nodeMin;

        // Si el usuario no ha configurado una rotación manual, detectar si el modelo viene acostado en el eje Z (Z-up)
        if (glm::length(effectiveRot) < 0.01f && meta.bakeTransforms) {
            bool isLyingOnZ = false;
            if (nodeExt.z > nodeExt.y * 1.25f && nodeExt.y > 0.001f && nodeExt.x < nodeExt.z * 0.85f) {
                isLyingOnZ = true;
            }
            else if (nodeExt.z > nodeExt.y * 1.05f && nodeExt.x < nodeExt.z * 0.85f &&
                     (std::abs(nodeMin.z) < nodeExt.z * 0.15f || std::abs(nodeMax.z) < nodeExt.z * 0.15f) &&
                     (nodeMin.y < -nodeExt.y * 0.15f && nodeMax.y > nodeExt.y * 0.15f)) {
                isLyingOnZ = true;
            }

            if (isLyingOnZ) {
                if (std::abs(nodeMax.z) >= std::abs(nodeMin.z)) {
                    effectiveRot = glm::vec3(-90.0f, 0.0f, 0.0f);
                } else {
                    effectiveRot = glm::vec3(90.0f, 0.0f, 0.0f);
                }
                metaAutoAdjusted = true;
            }
        }

        // Evaluar extensión después de la rotación para auto-ajustar la escala si viene en cm/mm o diminuto
        glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), glm::radians(effectiveRot.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
                           glm::rotate(glm::mat4(1.0f), glm::radians(effectiveRot.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
                           glm::rotate(glm::mat4(1.0f), glm::radians(effectiveRot.z), glm::vec3(0.0f, 0.0f, 1.0f));

        glm::vec3 corners[8] = {
            {nodeMin.x, nodeMin.y, nodeMin.z}, {nodeMax.x, nodeMin.y, nodeMin.z},
            {nodeMax.x, nodeMax.y, nodeMin.z}, {nodeMin.x, nodeMax.y, nodeMin.z},
            {nodeMin.x, nodeMin.y, nodeMax.z}, {nodeMax.x, nodeMin.y, nodeMax.z},
            {nodeMax.x, nodeMax.y, nodeMax.z}, {nodeMin.x, nodeMax.y, nodeMax.z}
        };
        glm::vec3 rotMin(1e30f), rotMax(-1e30f);
        for (int c = 0; c < 8; ++c) {
            glm::vec3 rp = glm::vec3(rotMat * glm::vec4(corners[c], 1.0f));
            rotMin = glm::min(rotMin, rp);
            rotMax = glm::max(rotMax, rp);
        }
        glm::vec3 rotExt = rotMax - rotMin;
        float maxDim = (std::max)({ rotExt.x, rotExt.y, rotExt.z });

        if (std::abs(effectiveScale - 1.0f) < 1e-4f && maxDim > 1e-5f) {
            if (maxDim >= 40.0f) {
                // Escala estándar en centímetros (ej. Mixamo/UE5/Maya ~180 cm -> 1.8 m, o Crytek Sponza ~3000 cm -> 30 m)
                effectiveScale = 0.01f;
                metaAutoAdjusted = true;
            } else if (maxDim < 0.15f) {
                effectiveScale = 2.0f / maxDim;
                metaAutoAdjusted = true;
            }
        }

        if (metaAutoAdjusted) {
            AssetMetadata updatedMeta = meta;
            updatedMeta.importRotation = effectiveRot;
            updatedMeta.importScale = effectiveScale;
            SaveMeta(modelPath + ".meta", updatedMeta);
        }
    }

    glm::mat4 importRotMat = glm::rotate(glm::mat4(1.0f), glm::radians(effectiveRot.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
                             glm::rotate(glm::mat4(1.0f), glm::radians(effectiveRot.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
                             glm::rotate(glm::mat4(1.0f), glm::radians(effectiveRot.z), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 importScaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(effectiveScale));

    // Detectar si el modelo contiene esqueleto / huesos y mapear IDs idénticos a ExtractSkeletonAndAnimations
    std::unordered_set<std::string> boneNamesInScene;
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[i];
        if (!mesh) continue;
        for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
            if (mesh->mBones[b]) boneNamesInScene.insert(mesh->mBones[b]->mName.C_Str());
        }
    }

    std::unordered_map<std::string, int> boneNameToIndex;
    int nextBoneIndex = 0;
    std::function<void(const aiNode*)> buildBoneIndices = [&](const aiNode* node) {
        if (!node) return;
        std::string nName = node->mName.C_Str();
        if (boneNamesInScene.count(nName)) {
            boneNameToIndex[nName] = nextBoneIndex++;
        }
        for (unsigned int c = 0; c < node->mNumChildren; ++c) {
            buildBoneIndices(node->mChildren[c]);
        }
    };
    if (scene->mRootNode && !boneNamesInScene.empty()) {
        buildBoneIndices(scene->mRootNode);
    }
    bool hasBonesInModel = !boneNamesInScene.empty();

    // 3. Transformar todos los vértices y calcular el AABB final para centrar X/Z y apoyar la base en Y = 0
    struct BakedSubMesh {
        int32_t matIdx = 0;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };
    std::vector<BakedSubMesh> bakedMeshes(scene->mNumMeshes);
    glm::vec3 finalMin(1e30f), finalMax(-1e30f);

    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        if (progressCallback && scene->mNumMeshes > 0) {
            float p = 0.72f + 0.18f * ((float)(i + 1) / (float)scene->mNumMeshes);
            progressCallback(p);
        }

        const aiMesh* mesh = scene->mMeshes[i];
        BakedSubMesh& sub = bakedMeshes[i];
        sub.matIdx = mesh->mMaterialIndex;
        uint32_t vCount = mesh->mNumVertices;

        glm::mat4 fullPosMat = hasBonesInModel ? meshNodeMats[i] : (importScaleMat * importRotMat * meshNodeMats[i]);
        glm::mat3 fullNormalMat = hasBonesInModel ? glm::mat3(glm::transpose(glm::inverse(meshNodeMats[i])))
                                                  : glm::mat3(glm::transpose(glm::inverse(importRotMat * meshNodeMats[i])));
        bool flipWinding = (glm::determinant(fullPosMat) < 0.0f);

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices == 3 && flipWinding) {
                sub.indices.push_back(face.mIndices[0]);
                sub.indices.push_back(face.mIndices[2]);
                sub.indices.push_back(face.mIndices[1]);
            } else {
                for (unsigned int j = 0; j < face.mNumIndices; ++j) {
                    sub.indices.push_back(face.mIndices[j]);
                }
            }
        }

        sub.vertices.resize(vCount);
        for (unsigned int v = 0; v < vCount; ++v) {
            Vertex vert{};
            glm::vec3 rawPos(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
            vert.Position = glm::vec3(fullPosMat * glm::vec4(rawPos, 1.0f));

            finalMin = glm::min(finalMin, vert.Position);
            finalMax = glm::max(finalMax, vert.Position);

            if (mesh->HasNormals()) {
                glm::vec3 n(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
                glm::vec3 tn = fullNormalMat * n;
                float len = glm::length(tn);
                vert.Normal = (len > 1e-6f) ? (tn / len) : glm::vec3(0.0f, 1.0f, 0.0f);
            } else {
                vert.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            if (mesh->HasTextureCoords(0)) {
                vert.TexCoords = glm::vec2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y);
            } else {
                vert.TexCoords = glm::vec2(0.0f);
            }

            if (mesh->HasTextureCoords(1)) {
                vert.TexCoords2 = glm::vec2(mesh->mTextureCoords[1][v].x, mesh->mTextureCoords[1][v].y);
            } else {
                vert.TexCoords2 = vert.TexCoords;
            }

            if (mesh->HasTangentsAndBitangents()) {
                glm::vec3 t(mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z);
                glm::vec3 b(mesh->mBitangents[v].x, mesh->mBitangents[v].y, mesh->mBitangents[v].z);
                glm::vec3 tt = fullNormalMat * t;
                glm::vec3 tb = fullNormalMat * b;
                vert.Tangent = (glm::length(tt) > 1e-6f) ? glm::normalize(tt) : glm::vec3(1.0f, 0.0f, 0.0f);
                vert.Bitangent = (glm::length(tb) > 1e-6f) ? glm::normalize(tb) : glm::vec3(0.0f, 0.0f, 1.0f);
            } else {
                vert.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
                vert.Bitangent = glm::vec3(0.0f, 0.0f, 1.0f);
            }

            for (int bi = 0; bi < MAX_BONE_INFLUENCE; ++bi) {
                vert.m_BoneIDs[bi] = 0;
                vert.m_Weights[bi] = 0.0f;
            }
            vert.materialIndex = sub.matIdx;
            sub.vertices[v] = vert;
        }

        // Extraer influencias de huesos de este submesh
        if (mesh->mNumBones > 0) {
            for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
                const aiBone* bone = mesh->mBones[b];
                std::string bName = bone->mName.C_Str();
                int boneId = boneNameToIndex.count(bName) ? boneNameToIndex[bName] : static_cast<int>(b);
                for (unsigned int w = 0; w < bone->mNumWeights; ++w) {
                    unsigned int vId = bone->mWeights[w].mVertexId;
                    float weight = bone->mWeights[w].mWeight;
                    if (vId < sub.vertices.size()) {
                        Vertex& vRef = sub.vertices[vId];
                        for (int bi = 0; bi < MAX_BONE_INFLUENCE; ++bi) {
                            if (vRef.m_Weights[bi] <= 0.0f) {
                                vRef.m_BoneIDs[bi] = boneId;
                                vRef.m_Weights[bi] = weight;
                                break;
                            }
                        }
                    }
                }
            }

            // Normalizar pesos y asegurar ranuras vacías en 0 con peso 0.0
            for (auto& vRef : sub.vertices) {
                float totalW = 0.0f;
                for (int bi = 0; bi < MAX_BONE_INFLUENCE; ++bi) {
                    if (vRef.m_BoneIDs[bi] >= 0) totalW += vRef.m_Weights[bi];
                }
                if (totalW > 0.0001f) {
                    for (int bi = 0; bi < MAX_BONE_INFLUENCE; ++bi) {
                        if (vRef.m_BoneIDs[bi] >= 0) vRef.m_Weights[bi] /= totalW;
                    }
                }
                for (int bi = 0; bi < MAX_BONE_INFLUENCE; ++bi) {
                    if (vRef.m_BoneIDs[bi] < 0 || vRef.m_Weights[bi] <= 0.0f) {
                        vRef.m_BoneIDs[bi] = 0;
                        vRef.m_Weights[bi] = 0.0f;
                    }
                }
            }
        } else if (!sub.vertices.empty() && !sub.indices.empty()) {
            // Deduplicación ultrarrápida en O(N) con meshoptimizer (reemplaza aiProcess_JoinIdenticalVertices,
            // siendo ~50x más veloz en mallas de millones de triángulos y garantizando conectividad topológica para V-Clust)
            std::vector<unsigned int> remap(sub.vertices.size());
            size_t uniqueVertexCount = meshopt_generateVertexRemap(
                remap.data(),
                sub.indices.data(),
                sub.indices.size(),
                sub.vertices.data(),
                sub.vertices.size(),
                sizeof(Vertex)
            );
            if (uniqueVertexCount > 0 && uniqueVertexCount < sub.vertices.size()) {
                std::vector<uint32_t> remappedIndices(sub.indices.size());
                std::vector<Vertex> remappedVertices(uniqueVertexCount);
                meshopt_remapIndexBuffer(remappedIndices.data(), sub.indices.data(), sub.indices.size(), remap.data());
                meshopt_remapVertexBuffer(remappedVertices.data(), sub.vertices.data(), sub.vertices.size(), sizeof(Vertex), remap.data());
                sub.indices.swap(remappedIndices);
                sub.vertices.swap(remappedVertices);
            }
        }
    }

    // 4. Alinear automáticamente la base del modelo en Y = 0 y centrarlo en X/Z (solo modelos estáticos sin huesos)
    if (hasVertices && meta.bakeTransforms && !hasBonesInModel) {
        glm::vec3 pivotOffset(
            -(finalMin.x + finalMax.x) * 0.5f,
            -finalMin.y,
            -(finalMin.z + finalMax.z) * 0.5f
        );
        for (auto& sub : bakedMeshes) {
            for (auto& vert : sub.vertices) {
                vert.Position += pivotOffset;
            }
        }
    }

    // 5. Extracción de Materiales y Texturas en subcarpetas <modelDir>/Materials y <modelDir>/Textures
    std::string modelDir = SlipFileSystem::GetParentPath(modelPath);
    if (modelDir.empty()) {
        modelDir = SlipFileSystem::GetProjectAssetsPath();
    }
    std::string materialsDir = SlipFileSystem::CombinePath(modelDir, "Materials");
    std::string texturesDir = SlipFileSystem::CombinePath(modelDir, "Textures");
    bool texturesFolderCreated = false;

    auto resolveAndImportTexture = [&](const std::string& rawTexRef, const std::string& matBaseName) -> std::string {
        if (!meta.importTextures || rawTexRef.empty()) return "";

        // 1. Verificar si es una textura embebida (*0, *1, etc.)
        if (const aiTexture* embTex = scene->GetEmbeddedTexture(rawTexRef.c_str())) {
            if (!texturesFolderCreated) {
                SlipFileSystem::CreateDirectoryPath(texturesDir);
                texturesFolderCreated = true;
            }
            std::string embName = SanitizeAssetName(embTex->mFilename.C_Str(), matBaseName + "_tex");
            if (embTex->mHeight == 0) {
                std::string extHint = embTex->achFormatHint[0] ? std::string(embTex->achFormatHint) : "png";
                std::string destTex = SlipFileSystem::CombinePath(texturesDir, embName + "." + extHint);
                SlipFileSystem::WriteAllBytes(destTex, reinterpret_cast<const uint8_t*>(embTex->pcData), embTex->mWidth);
                EnsureMetaForFile(destTex);
                return MakePortableAssetPath(destTex);
            } else {
                std::string destTex = SlipFileSystem::CombinePath(texturesDir, embName + ".png");
                std::vector<uint8_t> rgba(static_cast<size_t>(embTex->mWidth) * embTex->mHeight * 4);
                for (size_t px = 0; px < static_cast<size_t>(embTex->mWidth) * embTex->mHeight; ++px) {
                    rgba[px * 4 + 0] = embTex->pcData[px].r;
                    rgba[px * 4 + 1] = embTex->pcData[px].g;
                    rgba[px * 4 + 2] = embTex->pcData[px].b;
                    rgba[px * 4 + 3] = embTex->pcData[px].a;
                }
                stbi_write_png(destTex.c_str(), embTex->mWidth, embTex->mHeight, 4, rgba.data(), embTex->mWidth * 4);
                EnsureMetaForFile(destTex);
                return MakePortableAssetPath(destTex);
            }
        }

        // 2. Textura externa referenciada por ruta
        std::string cleanRef = rawTexRef;
        std::replace(cleanRef.begin(), cleanRef.end(), '\\', '/');
        while (cleanRef.rfind("./", 0) == 0) cleanRef = cleanRef.substr(2);
        while (!cleanRef.empty() && cleanRef.front() == '/') cleanRef.erase(cleanRef.begin());

        std::string texFileName = SlipFileSystem::GetFileName(cleanRef);
        std::string texStem = SlipFileSystem::GetFileNameWithoutExtension(texFileName);

        std::vector<std::string> searchDirs;
        searchDirs.push_back(modelDir);
        if (!meta.sourceOriginDir.empty() && meta.sourceOriginDir != modelDir) {
            searchDirs.push_back(meta.sourceOriginDir);
            std::string parentOrigin = SlipFileSystem::GetParentPath(meta.sourceOriginDir);
            if (!parentOrigin.empty()) searchDirs.push_back(parentOrigin);
        }
        std::string parentModelDir = SlipFileSystem::GetParentPath(modelDir);
        if (!parentModelDir.empty()) searchDirs.push_back(parentModelDir);

        std::vector<std::string> candidates;
        candidates.push_back(cleanRef);
        for (const auto& dir : searchDirs) {
            candidates.push_back(SlipFileSystem::CombinePath(dir, cleanRef));
            candidates.push_back(SlipFileSystem::CombinePath(dir, texFileName));
            candidates.push_back(SlipFileSystem::CombinePath(dir, "Textures/" + texFileName));
            candidates.push_back(SlipFileSystem::CombinePath(dir, "textures/" + texFileName));
            candidates.push_back(SlipFileSystem::CombinePath(dir, "tex/" + texFileName));
            if (!texStem.empty()) {
                const char* altExts[] = { ".png", ".jpg", ".tga", ".jpeg", ".bmp" };
                for (const char* altExt : altExts) {
                    candidates.push_back(SlipFileSystem::CombinePath(dir, "textures/" + texStem + altExt));
                    candidates.push_back(SlipFileSystem::CombinePath(dir, "Textures/" + texStem + altExt));
                    candidates.push_back(SlipFileSystem::CombinePath(dir, texStem + altExt));
                }
            }
        }

        std::string foundSourceTex;
        for (const auto& c : candidates) {
            if (!c.empty() && SlipFileSystem::Exists(c)) {
                foundSourceTex = c;
                break;
            }
        }

        if (foundSourceTex.empty()) {
            return "";
        }

        if (!texturesFolderCreated) {
            SlipFileSystem::CreateDirectoryPath(texturesDir);
            texturesFolderCreated = true;
        }

        std::string destTexPath = SlipFileSystem::CombinePath(texturesDir, SlipFileSystem::GetFileName(foundSourceTex));
        std::error_code ec;
        if (!fs::equivalent(foundSourceTex, destTexPath, ec)) {
            fs::copy_file(foundSourceTex, destTexPath, fs::copy_options::overwrite_existing, ec);
        }
        EnsureMetaForFile(destTexPath);
        return MakePortableAssetPath(destTexPath);
    };

    auto findFirstAssimpTexture = [&](const aiMaterial* aiMat, const std::vector<aiTextureType>& types, const std::string& matBaseName, aiTextureType* outMatchedType = nullptr) -> std::string {
        if (!meta.importTextures || !aiMat) return "";
        for (aiTextureType t : types) {
            unsigned int count = aiMat->GetTextureCount(t);
            for (unsigned int idx = 0; idx < count; ++idx) {
                aiString str;
                if (aiMat->GetTexture(t, idx, &str) == AI_SUCCESS && str.length > 0) {
                    std::string resolved = resolveAndImportTexture(str.C_Str(), matBaseName);
                    if (!resolved.empty()) {
                        if (outMatchedType) *outMatchedType = t;
                        return resolved;
                    }
                }
            }
        }
        return "";
    };

    uint32_t matCount = scene->mNumMaterials;
    std::vector<std::string> serializedMatEntries(matCount);

    if (meta.importMaterials && matCount > 0) {
        SlipFileSystem::CreateDirectoryPath(materialsDir);
    }

    std::unordered_set<std::string> usedMatFileNames;
    for (unsigned int m = 0; m < matCount; ++m) {
        const aiMaterial* aiMat = scene->mMaterials[m];
        aiString aiName;
        std::string rawSlotName = "Material_" + std::to_string(m);
        if (aiMat && aiMat->Get(AI_MATKEY_NAME, aiName) == AI_SUCCESS && aiName.length > 0) {
            rawSlotName = aiName.C_Str();
        }

        if (!meta.importMaterials) {
            // Si no se importan materiales, dejamos el slot vacío conservando su nombre
            serializedMatEntries[m] = rawSlotName + "|";
            continue;
        }

        std::string sanitizedName = SanitizeAssetName(rawSlotName, "Material_" + std::to_string(m));
        std::string uniqueMatName = sanitizedName;
        int dupCounter = 1;
        while (usedMatFileNames.count(uniqueMatName) > 0) {
            uniqueMatName = sanitizedName + "_" + std::to_string(dupCounter++);
        }
        usedMatFileNames.insert(uniqueMatName);

        std::string matFilePath = SlipFileSystem::CombinePath(materialsDir, uniqueMatName + ".mat");

        glm::vec3 baseColor(1.0f);
        float roughness = 0.6f;
        float metallic = 0.0f;
        float ao = 1.0f;
        glm::vec3 emissiveColor(0.0f);
        float emissiveIntensity = 0.0f;
        bool alphaClip = false;
        float alphaCutoff = 0.5f;
        bool invertRoughness = false;

        if (aiMat) {
            aiColor3D kd(1.0f, 1.0f, 1.0f);
            if (aiMat->Get(AI_MATKEY_BASE_COLOR, kd) == AI_SUCCESS ||
                aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, kd) == AI_SUCCESS) {
                baseColor = glm::vec3(kd.r, kd.g, kd.b);
            }

            float rFactor = -1.0f;
            if (aiMat->Get(AI_MATKEY_ROUGHNESS_FACTOR, rFactor) == AI_SUCCESS && rFactor >= 0.0f && rFactor <= 1.0f) {
                roughness = rFactor;
            } else {
                float shininess = 0.0f;
                if (aiMat->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS && shininess > 0.0f) {
                    roughness = glm::clamp(1.0f - std::sqrt(glm::clamp(shininess / 256.0f, 0.0f, 1.0f)), 0.08f, 0.95f);
                }
            }

            float mFactor = -1.0f;
            if (aiMat->Get(AI_MATKEY_METALLIC_FACTOR, mFactor) == AI_SUCCESS && mFactor >= 0.0f && mFactor <= 1.0f) {
                metallic = mFactor;
            }

            aiColor3D ke(0.0f, 0.0f, 0.0f);
            if (aiMat->Get(AI_MATKEY_COLOR_EMISSIVE, ke) == AI_SUCCESS) {
                emissiveColor = glm::vec3(ke.r, ke.g, ke.b);
                if (glm::length(emissiveColor) > 0.01f) {
                    emissiveIntensity = 1.0f;
                }
            }

            float opacityVal = 1.0f;
            if (aiMat->Get(AI_MATKEY_OPACITY, opacityVal) == AI_SUCCESS && opacityVal < 0.98f) {
                // Opacidad escalar menor a 1.0 no implica recortar caras o doble cara
            }
        }

        std::string diffTex = findFirstAssimpTexture(aiMat, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }, uniqueMatName);
        if (!diffTex.empty() && glm::length(baseColor) < 0.05f) {
            baseColor = glm::vec3(1.0f);
        }

        std::string normTex = findFirstAssimpTexture(aiMat, { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT, aiTextureType_DISPLACEMENT }, uniqueMatName);

        aiTextureType roughMatchedType = aiTextureType_NONE;
        std::string roughTex = findFirstAssimpTexture(aiMat, { aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_SHININESS, aiTextureType_UNKNOWN }, uniqueMatName, &roughMatchedType);
        if (!roughTex.empty()) {
            roughness = 1.0f;
            if (roughMatchedType == aiTextureType_SHININESS) {
                invertRoughness = true;
            }
        }

        aiTextureType metalMatchedType = aiTextureType_NONE;
        std::string metalTex = findFirstAssimpTexture(aiMat, { aiTextureType_METALNESS, aiTextureType_SPECULAR, aiTextureType_UNKNOWN }, uniqueMatName, &metalMatchedType);
        if (!metalTex.empty() && roughTex.empty() && metalMatchedType == aiTextureType_SPECULAR) {
            // En .mtl clásicos donde map_Ks se usa como mapa especular/brillo sin mapa de rugosidad separado
            roughTex = metalTex;
            invertRoughness = true;
            roughness = 1.0f;
            metalTex = "";
        }

        std::string aoTex = findFirstAssimpTexture(aiMat, { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP }, uniqueMatName);
        if (aoTex == diffTex) {
            aoTex.clear();
        }

        std::string emTex = findFirstAssimpTexture(aiMat, { aiTextureType_EMISSIVE, aiTextureType_EMISSION_COLOR }, uniqueMatName);
        if (!emTex.empty() && emissiveIntensity <= 0.0f) {
            emissiveColor = glm::vec3(1.0f);
            emissiveIntensity = 1.0f;
        }

        std::string opTex = findFirstAssimpTexture(aiMat, { aiTextureType_OPACITY }, uniqueMatName);
        if (!opTex.empty()) {
            alphaClip = true;
        }

        // Auto-detección de follaje, hojas, cadenas y plantas que usan canal alfa en la textura difusa
        std::string lowerMatName = uniqueMatName;
        std::transform(lowerMatName.begin(), lowerMatName.end(), lowerMatName.begin(), ::tolower);
        std::string lowerDiff = diffTex;
        std::transform(lowerDiff.begin(), lowerDiff.end(), lowerDiff.begin(), ::tolower);

        bool isFoliage = (lowerMatName.find("plant") != std::string::npos ||
                          lowerMatName.find("leaf") != std::string::npos ||
                          lowerMatName.find("thorn") != std::string::npos ||
                          lowerMatName.find("flower") != std::string::npos ||
                          lowerMatName.find("foliage") != std::string::npos ||
                          lowerMatName.find("chain") != std::string::npos ||
                          lowerDiff.find("plant") != std::string::npos ||
                          lowerDiff.find("leaf") != std::string::npos ||
                          lowerDiff.find("thorn") != std::string::npos ||
                          lowerDiff.find("flower") != std::string::npos ||
                          lowerDiff.find("chain") != std::string::npos);

        bool doubleSided = false;
        if (isFoliage) {
            alphaClip = true;
            doubleSided = true;
        }

        std::stringstream matContent;
        matContent << "shader: Lit\n";
        matContent << "color: " << baseColor.r << " " << baseColor.g << " " << baseColor.b << "\n";
        matContent << "roughness: " << roughness << "\n";
        matContent << "metallic: " << metallic << "\n";
        matContent << "ao: " << ao << "\n";
        matContent << "emissive: " << emissiveColor.r << " " << emissiveColor.g << " " << emissiveColor.b << "\n";
        matContent << "emissiveIntensity: " << emissiveIntensity << "\n";
        matContent << "ambient: 0.125 0.125 0.125\n";
        matContent << "specular: 0.16 0.16 0.16\n";
        matContent << "shininess: 3\n";
        matContent << "diffuseTexture: " << diffTex << "\n";
        matContent << "normalTexture: " << normTex << "\n";
        matContent << "roughnessTexture: " << roughTex << "\n";
        matContent << "metallicTexture: " << metalTex << "\n";
        matContent << "aoTexture: " << aoTex << "\n";
        matContent << "emissiveTexture: " << emTex << "\n";
        matContent << "opacityTexture: " << opTex << "\n";
        matContent << "alphaClip: " << (alphaClip ? 1 : 0) << "\n";
        matContent << "alphaCutoff: " << alphaCutoff << "\n";
        matContent << "invertRoughness: " << (invertRoughness ? 1 : 0) << "\n";
        matContent << "doubleSided: " << (doubleSided ? 1 : 0) << "\n";

        SlipFileSystem::WriteAllText(matFilePath, matContent.str());
        EnsureMetaForFile(matFilePath);

        std::string portableMatPath = MakePortableAssetPath(matFilePath);
        serializedMatEntries[m] = rawSlotName + "|" + portableMatPath;
    }

    // 6. Serializar en formato binario en bloque SLIPMSH5 con buffers unificados, bone mapping exacto y AABBs precalculadas
    std::vector<uint8_t> buffer;
    auto writeBytes = [&buffer](const void* src, size_t size) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(src);
        buffer.insert(buffer.end(), p, p + size);
    };

    const char magic[8] = {'S', 'L', 'I', 'P', 'M', 'S', 'H', '6'};
    writeBytes(magic, sizeof(magic));

    glm::vec3 globalAabbMin(1e30f), globalAabbMax(-1e30f);
    struct SubmeshMeta {
        int32_t matIdx = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t baseVertex = 0;
        uint32_t vertexCount = 0;
        glm::vec3 aabbMin = glm::vec3(0.0f);
        glm::vec3 aabbMax = glm::vec3(0.0f);
    };

    std::vector<SubmeshMeta> subMetas(bakedMeshes.size());
    std::vector<Vertex> masterVertices;
    std::vector<uint32_t> masterIndices;

    for (size_t i = 0; i < bakedMeshes.size(); ++i) {
        const auto& sub = bakedMeshes[i];
        SubmeshMeta& sm = subMetas[i];
        sm.matIdx = sub.matIdx;
        sm.baseVertex = static_cast<uint32_t>(masterVertices.size());
        sm.vertexCount = static_cast<uint32_t>(sub.vertices.size());
        sm.firstIndex = static_cast<uint32_t>(masterIndices.size());
        sm.indexCount = static_cast<uint32_t>(sub.indices.size());

        glm::vec3 sMin(1e30f), sMax(-1e30f);
        for (const auto& v : sub.vertices) {
            sMin = glm::min(sMin, v.Position);
            sMax = glm::max(sMax, v.Position);
        }
        if (sub.vertices.empty()) {
            sMin = glm::vec3(-0.5f);
            sMax = glm::vec3(0.5f);
        } else {
            glm::vec3 ext = sMax - sMin;
            if (ext.x < 0.02f) { sMin.x -= 0.01f; sMax.x += 0.01f; }
            if (ext.y < 0.02f) { sMin.y -= 0.01f; sMax.y += 0.01f; }
            if (ext.z < 0.02f) { sMin.z -= 0.01f; sMax.z += 0.01f; }
        }
        sm.aabbMin = sMin;
        sm.aabbMax = sMax;

        globalAabbMin = glm::min(globalAabbMin, sMin);
        globalAabbMax = glm::max(globalAabbMax, sMax);

        masterVertices.insert(masterVertices.end(), sub.vertices.begin(), sub.vertices.end());
        for (uint32_t idx : sub.indices) {
            masterIndices.push_back(idx + sm.baseVertex);
        }
    }

    if (bakedMeshes.empty() || masterVertices.empty()) {
        globalAabbMin = glm::vec3(-0.5f);
        globalAabbMax = glm::vec3(0.5f);
    }

    writeBytes(&globalAabbMin, sizeof(globalAabbMin));
    writeBytes(&globalAabbMax, sizeof(globalAabbMax));

    uint32_t meshCount = static_cast<uint32_t>(subMetas.size());
    writeBytes(&meshCount, sizeof(meshCount));

    for (const auto& sm : subMetas) {
        writeBytes(&sm.matIdx, sizeof(sm.matIdx));
        writeBytes(&sm.firstIndex, sizeof(sm.firstIndex));
        writeBytes(&sm.indexCount, sizeof(sm.indexCount));
        writeBytes(&sm.baseVertex, sizeof(sm.baseVertex));
        writeBytes(&sm.vertexCount, sizeof(sm.vertexCount));
        writeBytes(&sm.aabbMin, sizeof(sm.aabbMin));
        writeBytes(&sm.aabbMax, sizeof(sm.aabbMax));
    }

    uint32_t totalVertices = static_cast<uint32_t>(masterVertices.size());
    uint32_t totalIndices = static_cast<uint32_t>(masterIndices.size());
    writeBytes(&totalVertices, sizeof(totalVertices));
    writeBytes(&totalIndices, sizeof(totalIndices));

    if (totalVertices > 0) {
        writeBytes(masterVertices.data(), totalVertices * sizeof(Vertex));
    }
    if (totalIndices > 0) {
        writeBytes(masterIndices.data(), totalIndices * sizeof(uint32_t));
    }

    char colPath[192] = { 0 };
    writeBytes(colPath, sizeof(colPath));

    writeBytes(&matCount, sizeof(matCount));
    for (unsigned int m = 0; m < matCount; ++m) {
        char matEntry[192] = { 0 };
        std::strncpy(matEntry, serializedMatEntries[m].c_str(), sizeof(matEntry) - 1);
        writeBytes(matEntry, sizeof(matEntry));
    }

    // 7. V-Clust (Voxel-Anchored Cluster LOD): Precalcular jerarquía de clusters para modelos estáticos
    if (!hasBonesInModel && !bakedMeshes.empty() && !masterVertices.empty()) {
        std::vector<SlipMesh::Mesh> clusterInputMeshes;
        clusterInputMeshes.reserve(bakedMeshes.size());
        for (size_t i = 0; i < bakedMeshes.size(); ++i) {
            SlipMesh::Mesh sm;
            sm.materialIndex = bakedMeshes[i].matIdx;
            sm.vertices = bakedMeshes[i].vertices;
            sm.indices = bakedMeshes[i].indices;
            sm.aabbMin = subMetas[i].aabbMin;
            sm.aabbMax = subMetas[i].aabbMax;
            clusterInputMeshes.push_back(std::move(sm));
        }

        SlipClusterBuildOptions vclustOpts;
        vclustOpts.maxVerticesPerCluster = 64;
        vclustOpts.maxTrianglesPerCluster = 124;
        vclustOpts.groupSize = 4;

        SlipClusterBuildResult vclustResult;
        if (SlipClusterBuilder::BuildFromSubmeshes(clusterInputMeshes, vclustOpts, vclustResult)) {
            std::vector<uint8_t> clusterBlob;
            SlipClusterBuilder::SerializeClusterData(vclustResult, clusterBlob);
            if (!clusterBlob.empty()) {
                writeBytes(clusterBlob.data(), clusterBlob.size());
            }
        }
    }

    std::string cacheFile = GetCachePathForGuid(meta.guid);
    bool ok = SlipFileSystem::WriteAllBytes(cacheFile, buffer.data(), buffer.size());

    // Guardar también con el nombre de archivo en cache/ y sin extensión
    std::string fileName = SlipFileSystem::GetFileName(modelPath);
    std::string fileNameNoExt = SlipFileSystem::GetFileNameWithoutExtension(modelPath);
    std::string cacheParent = SlipFileSystem::GetParentPath(cacheFile);
    SlipFileSystem::WriteAllBytes(SlipFileSystem::CombinePath(cacheParent, fileName + ".model_data"), buffer.data(), buffer.size());
    SlipFileSystem::WriteAllBytes(SlipFileSystem::CombinePath(cacheParent, fileNameNoExt + ".model_data"), buffer.data(), buffer.size());

    if (progressCallback) progressCallback(1.0f);

    if (ok) {
        std::lock_guard<std::mutex> lock(s_MainThreadMutex);
        s_MainThreadQueue.push_back([modelPath]() {
            SlipThumbnailManager::Get().GenerateModelThumbnail(modelPath, true);
        });
    }

    if (outCacheFilePath) {
        *outCacheFilePath = cacheFile;
    }
    return ok;
}

bool SlipModelExtract::ExtractSkeletonAndAnimations(const std::string& modelPath,
                                                    SlipSkeleton& outSkeleton,
                                                    std::vector<std::shared_ptr<AnimationClip>>& outClips) {
    if (modelPath.empty() || !SlipFileSystem::Exists(modelPath)) return false;

    Assimp::Importer importer;
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    const aiScene* scene = importer.ReadFile(modelPath, aiProcess_LimitBoneWeights | aiProcess_Triangulate);
    if (!scene || !scene->mRootNode) return false;

    auto toGlmMat4 = [](const aiMatrix4x4& from) -> glm::mat4 {
        glm::mat4 to;
        to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
        to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
        to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
        to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
        return to;
    };

    std::unordered_map<std::string, glm::mat4> boneOffsets;
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[i];
        if (!mesh) continue;
        for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
            const aiBone* bone = mesh->mBones[b];
            std::string bName = bone->mName.C_Str();
            if (boneOffsets.find(bName) == boneOffsets.end()) {
                boneOffsets[bName] = toGlmMat4(bone->mOffsetMatrix);
            }
        }
    }

    if (boneOffsets.empty()) {
        return false;
    }

    std::function<void(const aiNode*, int)> traverseNodes = [&](const aiNode* node, int parentIdx) {
        if (!node) return;
        std::string nodeName = node->mName.C_Str();
        int currentIdx = parentIdx;

        auto it = boneOffsets.find(nodeName);
        if (it != boneOffsets.end()) {
            glm::mat4 offset = it->second;
            glm::mat4 localBind = toGlmMat4(node->mTransformation);
            currentIdx = outSkeleton.AddBone(nodeName, offset, localBind, parentIdx);
        }

        for (unsigned int c = 0; c < node->mNumChildren; ++c) {
            traverseNodes(node->mChildren[c], currentIdx);
        }
    };
    traverseNodes(scene->mRootNode, -1);

    for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
        const aiAnimation* anim = scene->mAnimations[a];
        if (!anim) continue;

        auto clip = std::make_shared<AnimationClip>();
        clip->name = anim->mName.length > 0 ? anim->mName.C_Str() : ("Clip_" + std::to_string(a));
        double tps = anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 25.0;
        clip->ticksPerSecond = static_cast<float>(tps);
        clip->duration = static_cast<float>(anim->mDuration / tps);

        for (unsigned int c = 0; c < anim->mNumChannels; ++c) {
            const aiNodeAnim* channel = anim->mChannels[c];
            if (!channel) continue;

            AnimationTrack track;
            track.boneName = channel->mNodeName.C_Str();
            track.boneIndex = outSkeleton.GetBoneIndex(track.boneName);

            for (unsigned int p = 0; p < channel->mNumPositionKeys; ++p) {
                KeyPosition kp;
                kp.position = glm::vec3(channel->mPositionKeys[p].mValue.x, channel->mPositionKeys[p].mValue.y, channel->mPositionKeys[p].mValue.z);
                kp.timeStamp = static_cast<float>(channel->mPositionKeys[p].mTime / tps);
                track.positions.push_back(kp);
            }

            for (unsigned int r = 0; r < channel->mNumRotationKeys; ++r) {
                KeyRotation kr;
                kr.orientation = glm::quat(channel->mRotationKeys[r].mValue.w, channel->mRotationKeys[r].mValue.x, channel->mRotationKeys[r].mValue.y, channel->mRotationKeys[r].mValue.z);
                kr.timeStamp = static_cast<float>(channel->mRotationKeys[r].mTime / tps);
                track.rotations.push_back(kr);
            }

            for (unsigned int s = 0; s < channel->mNumScalingKeys; ++s) {
                KeyScale ks;
                ks.scale = glm::vec3(channel->mScalingKeys[s].mValue.x, channel->mScalingKeys[s].mValue.y, channel->mScalingKeys[s].mValue.z);
                ks.timeStamp = static_cast<float>(channel->mScalingKeys[s].mTime / tps);
                track.scales.push_back(ks);
            }

            clip->AddTrack(track);
        }
        outClips.push_back(clip);
    }

    // Guardar / sincronizar los clips en la subcarpeta interna "Animations"
    std::string modelDir = SlipFileSystem::GetParentPath(modelPath);
    if (modelDir.empty() || !SlipFileSystem::Exists(modelDir)) {
        if (SlipProject::Get().HasActiveProject()) {
            modelDir = SlipProject::Get().GetCurrentProject().assetsPath;
        } else {
            modelDir = "assets";
        }
    }
    std::string animDir = SlipFileSystem::CombinePath(modelDir, "Animations");
    SlipFileSystem::CreateDirectoryPath(animDir);

    for (size_t a = 0; a < outClips.size(); ++a) {
        auto& clip = outClips[a];
        std::string safeName = SanitizeClipFileName(clip->name);
        if (safeName.empty()) safeName = "Clip_" + std::to_string(a);
        std::string animFilePath = SlipFileSystem::CombinePath(animDir, safeName + ".anim");

        AnimationClipAsset assetData;
        if (SlipFileSystem::Exists(animFilePath)) {
            if (LoadAnimAsset(animFilePath, assetData)) {
                if (!assetData.name.empty()) clip->name = assetData.name;
            }
        } else {
            assetData.name = clip->name;
            assetData.sourceModel = modelPath;
            assetData.clipIndex = static_cast<int>(a);
            assetData.duration = clip->duration;
            assetData.ticksPerSecond = clip->ticksPerSecond;
            assetData.loop = true;
            assetData.playbackSpeed = 1.0f;
            assetData.startTime = 0.0f;
            assetData.endTime = clip->duration;
            SaveAnimAsset(animFilePath, assetData);
        }
    }

    return true;
}

bool SlipModelExtract::ExtractAnimationClipsOnly(const std::string& sourceModelPath,
                                                 const std::string& targetAnimationsDir,
                                                 std::vector<std::string>& outCreatedAnimFiles) {
    outCreatedAnimFiles.clear();
    if (sourceModelPath.empty() || !SlipFileSystem::Exists(sourceModelPath)) return false;

    std::string animDir = targetAnimationsDir;
    if (animDir.empty()) {
        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
        animDir = SlipFileSystem::CombinePath(assetsDir, "Animations");
    }
    SlipFileSystem::CreateDirectoryPath(animDir);

    SlipSkeleton skeleton;
    std::vector<std::shared_ptr<AnimationClip>> clips;
    if (!ExtractSkeletonAndAnimations(sourceModelPath, skeleton, clips) || clips.empty()) {
        SlipConsole::LogWarning("El archivo no contiene pistas de animacion validas: " + sourceModelPath);
        return false;
    }

    std::string srcBaseName = SlipFileSystem::GetFileNameWithoutExtension(sourceModelPath);

    for (size_t i = 0; i < clips.size(); ++i) {
        auto& clip = clips[i];
        std::string clipCleanName = SanitizeClipFileName(clip->name);
        if (clipCleanName.empty() || clipCleanName == "mixamo_com" || clipCleanName == "Armature" || clipCleanName == "Take_001") {
            clipCleanName = srcBaseName + (clips.size() > 1 ? ("_" + std::to_string(i)) : "");
        }

        std::string outFilePath = SlipFileSystem::CombinePath(animDir, clipCleanName + ".anim");
        AnimationClipAsset assetData;
        assetData.name = clipCleanName;
        assetData.sourceModel = sourceModelPath;
        assetData.clipIndex = static_cast<int>(i);
        assetData.duration = clip->duration;
        assetData.ticksPerSecond = clip->ticksPerSecond;
        assetData.loop = true;
        assetData.playbackSpeed = 1.0f;
        assetData.startTime = 0.0f;
        assetData.endTime = clip->duration;

        if (SaveAnimAsset(outFilePath, assetData)) {
            outCreatedAnimFiles.push_back(outFilePath);
            SlipConsole::LogSuccess("Animacion extraida como clip: " + outFilePath);
        }
    }

    return !outCreatedAnimFiles.empty();
}

std::string SlipModelExtract::SanitizeClipFileName(const std::string& clipName) {
    std::string s = clipName;
    for (char& c : s) {
        if (c == '|' || c == ':' || c == '/' || c == '\\' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '.' || static_cast<unsigned char>(c) <= 31) {
            c = '_';
        }
    }
    size_t first = s.find_first_not_of('_');
    if (first == std::string::npos) return "Clip";
    size_t last = s.find_last_not_of('_');
    return s.substr(first, (last - first + 1));
}

bool SlipModelExtract::SaveAnimAsset(const std::string& filePath, const AnimationClipAsset& data) {
    std::string dir = SlipFileSystem::GetParentPath(filePath);
    if (!dir.empty() && !SlipFileSystem::Exists(dir)) {
        SlipFileSystem::CreateDirectoryPath(dir);
    }

    std::stringstream ss;
    ss << "# SlipEngine Animation Clip\n";
    ss << "AnimationClip:\n";
    ss << "  Name: " << data.name << "\n";
    ss << "  SourceModel: " << data.sourceModel << "\n";
    ss << "  ClipIndex: " << data.clipIndex << "\n";
    ss << "  Duration: " << data.duration << "\n";
    ss << "  TicksPerSecond: " << data.ticksPerSecond << "\n";
    ss << "  Loop: " << (data.loop ? "true" : "false") << "\n";
    ss << "  PlaybackSpeed: " << data.playbackSpeed << "\n";
    ss << "  StartTime: " << data.startTime << "\n";
    ss << "  EndTime: " << data.endTime << "\n";

    return SlipFileSystem::WriteAllText(filePath, ss.str());
}

bool SlipModelExtract::LoadAnimAsset(const std::string& filePath, AnimationClipAsset& outData) {
    std::string content;
    if (!SlipFileSystem::ReadAllText(filePath, content)) return false;

    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        key.erase(0, key.find_first_not_of(" \t\r\n")); key.erase(key.find_last_not_of(" \t\r\n") + 1);
        val.erase(0, val.find_first_not_of(" \t\r\n")); val.erase(val.find_last_not_of(" \t\r\n") + 1);

        if (key == "Name") outData.name = val;
        else if (key == "SourceModel") outData.sourceModel = val;
        else if (key == "ClipIndex") { try { outData.clipIndex = std::stoi(val); } catch (...) {} }
        else if (key == "Duration") { try { outData.duration = std::stof(val); } catch (...) {} }
        else if (key == "TicksPerSecond") { try { outData.ticksPerSecond = std::stof(val); } catch (...) {} }
        else if (key == "Loop") { outData.loop = (val == "true" || val == "1" || val == "yes"); }
        else if (key == "PlaybackSpeed") { try { outData.playbackSpeed = std::stof(val); } catch (...) {} }
        else if (key == "StartTime") { try { outData.startTime = std::stof(val); } catch (...) {} }
        else if (key == "EndTime") { try { outData.endTime = std::stof(val); } catch (...) {} }
    }
    return true;
}
