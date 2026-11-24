#include "SlipMesh.h"
#include "SlipShadows.h"
#include "SlipGlobals.h"
#include "SlipGL.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/core/SlipConsole.h"
#include <cstring>
#include <algorithm>

static SlipMesh::BakeCallback s_BakeCallback = nullptr;

void SlipMesh::SetBakeCallback(BakeCallback cb)
{
    s_BakeCallback = cb;
}

void SlipMesh::init()
{
    computeBounds();
    setupMesh();

    for (int i = 0; i < materials.size(); i++)
    {
        if (materials[i].mat)
            materials[i].mat->init();
    }

    initialized = true;
}

void SlipMesh::computeBounds()
{
    if (hasBounds) {
        // Asegurar que cada submalla tenga bounds si aún no los tiene
        for (auto& m : meshes) {
            if (!m.hasBounds && !m.vertices.empty()) {
                glm::vec3 smMin(1e30f), smMax(-1e30f);
                for (const auto& v : m.vertices) {
                    smMin = glm::min(smMin, v.Position);
                    smMax = glm::max(smMax, v.Position);
                }
                glm::vec3 ext = smMax - smMin;
                if (ext.x < 0.02f) { smMin.x -= 0.01f; smMax.x += 0.01f; }
                if (ext.y < 0.02f) { smMin.y -= 0.01f; smMax.y += 0.01f; }
                if (ext.z < 0.02f) { smMin.z -= 0.01f; smMax.z += 0.01f; }
                m.aabbMin = smMin;
                m.aabbMax = smMax;
                m.hasBounds = true;
            }
        }
        return;
    }

    glm::vec3 bMin(1e30f);
    glm::vec3 bMax(-1e30f);
    bool foundAny = false;

    for (auto& m : meshes) {
        glm::vec3 smMin(1e30f), smMax(-1e30f);
        bool smFound = false;
        for (const auto& v : m.vertices) {
            bMin = glm::min(bMin, v.Position);
            bMax = glm::max(bMax, v.Position);
            smMin = glm::min(smMin, v.Position);
            smMax = glm::max(smMax, v.Position);
            foundAny = true;
            smFound = true;
        }
        if (smFound) {
            glm::vec3 ext = smMax - smMin;
            if (ext.x < 0.02f) { smMin.x -= 0.01f; smMax.x += 0.01f; }
            if (ext.y < 0.02f) { smMin.y -= 0.01f; smMax.y += 0.01f; }
            if (ext.z < 0.02f) { smMin.z -= 0.01f; smMax.z += 0.01f; }
            m.aabbMin = smMin;
            m.aabbMax = smMax;
            m.hasBounds = true;
        }
    }

    if (foundAny) {
        glm::vec3 ext = bMax - bMin;
        if (ext.x < 0.02f) { bMin.x -= 0.01f; bMax.x += 0.01f; }
        if (ext.y < 0.02f) { bMin.y -= 0.01f; bMax.y += 0.01f; }
        if (ext.z < 0.02f) { bMin.z -= 0.01f; bMax.z += 0.01f; }
        aabbMin = bMin;
        aabbMax = bMax;
        hasBounds = true;
    } else {
        aabbMin = glm::vec3(-0.5f);
        aabbMax = glm::vec3(0.5f);
        hasBounds = false;
    }
}

void SlipMesh::initColl()
{
    if (collision != nullptr)
    {
        collision->init();
    }
}

SlipMesh::SlipMesh(const char* path)
{
    this->path = path ? path : "";
    std::memset(colPath, 0, sizeof(colPath));
    collision = nullptr;
    initialized = false;

    loadFromFile();
    loadColFromFile(colPath);
    loadMatFromFile();
}

SlipMesh::SlipMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices)
{
    this->path = "primitive";
    std::memset(colPath, 0, sizeof(colPath));
    collision = nullptr;

    Mesh m;
    m.VAO = m.VBO = m.EBO = 0;
    m.materialIndex = 0;
    m.vertices = vertices;
    m.indices = indices;
    meshes.push_back(m);

    Material defMat("default");
    defMat.mat = new SlipMaterial();
    materials.push_back(defMat);

    init();
}

SlipMesh::~SlipMesh()
{
    clean();
}

void SlipMesh::clean()
{
    if (glad_glDeleteVertexArrays) {
        if (masterVAO != 0) glDeleteVertexArrays(1, &masterVAO);
        if (masterVBO != 0) glDeleteBuffers(1, &masterVBO);
        if (masterEBO != 0) glDeleteBuffers(1, &masterEBO);
        masterVAO = masterVBO = masterEBO = 0;

        if (clusterVAO != 0) glDeleteVertexArrays(1, &clusterVAO);
        if (clusterVBO != 0) glDeleteBuffers(1, &clusterVBO);
        if (clusterEBO != 0) glDeleteBuffers(1, &clusterEBO);
        clusterVAO = clusterVBO = clusterEBO = 0;
    } else {
        masterVAO = masterVBO = masterEBO = 0;
        clusterVAO = clusterVBO = clusterEBO = 0;
    }
    hasClusterLOD = false;
    clusters.clear();
    clusterVertices.clear();
    clusterIndices.clear();

    for (int i = 0; i < meshes.size(); i++)
    {
        if (!isUnifiedBuffer
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
            && glad_glDeleteVertexArrays
#endif
        ) {
            if (meshes[i].VAO != 0) glDeleteVertexArrays(1, &meshes[i].VAO);
            if (meshes[i].VBO != 0) glDeleteBuffers(1, &meshes[i].VBO);
            if (meshes[i].EBO != 0) glDeleteBuffers(1, &meshes[i].EBO);
        }
    }
    meshes.clear();
    m_UnifiedVertices.clear();
    m_UnifiedIndices.clear();
    isUnifiedBuffer = false;

    for (int i = 0; i < materials.size(); i++)
    {
        if (materials[i].mat)
        {
            delete materials[i].mat;
            materials[i].mat = nullptr;
        }
    }
    materials.clear();

    if (collision != nullptr)
    {
        delete collision;
        collision = nullptr;
    }

    initialized = false;
}

void SlipMesh::draw(glm::mat4 transform, SlipMaterial* overrideMaterial, bool bindMaterial)
{
    for (int i = 0; i < meshes.size(); i++)
    {
        if (meshes[i].VAO == 0) {
            continue;
        }
        uint32_t count = isUnifiedBuffer ? meshes[i].indexCount : static_cast<uint32_t>(meshes[i].indices.size());
        if (count == 0) continue;

        void* offset = isUnifiedBuffer ? (void*)(uintptr_t)(meshes[i].firstIndex * sizeof(unsigned int)) : 0;

        if (SlipShadows::Get().calculating())
        {
            if (SlipShadows::Get().getShader()) {
                SlipShadows::Get().getShader()->setMat4("model", transform);
            }

            // draw mesh
            glBindVertexArray(meshes[i].VAO);
            glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, offset);
            glBindVertexArray(0);
        }
        else {
            if (bindMaterial) {
                if (overrideMaterial) {
                    overrideMaterial->bind(transform);
                } else {
                    int matIdx = meshes[i].materialIndex;
                    if (matIdx >= 0 && matIdx < static_cast<int>(materials.size()) && materials[matIdx].mat != nullptr)
                    {
                        materials[matIdx].mat->bind(transform);
                    } else {
                        // Material fallback si la malla o submalla no tiene material asignado aún
                        static SlipMaterial* s_DefaultFallbackMat = nullptr;
                        if (!s_DefaultFallbackMat) {
                            s_DefaultFallbackMat = new SlipMaterial("");
                            s_DefaultFallbackMat->init();
                        }
                        if (s_DefaultFallbackMat) {
                            s_DefaultFallbackMat->bind(transform);
                        }
                    }
                }
            }

            // draw mesh
            glBindVertexArray(meshes[i].VAO);
            glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, offset);
            glBindVertexArray(0);
        }
    }
}

void SlipMesh::loadFromFile()
{
    if (path.empty()) return;

    std::string normPath = SlipFileSystem::NormalizePath(path);
    std::string ext = SlipFileSystem::GetFileExtension(normPath);
    std::string lowerExt = ext;
    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);

    bool isRawModel = (lowerExt == ".fbx" || lowerExt == ".obj" || lowerExt == ".dae" || 
                       lowerExt == ".gltf" || lowerExt == ".glb" || lowerExt == ".3ds");

    std::string assets = SlipFileSystem::GetProjectAssetsPath();
    std::string root = SlipFileSystem::GetProjectRootPath();
    std::string fname = SlipFileSystem::GetFileName(normPath);
    std::string fnameNoExt = SlipFileSystem::GetFileNameWithoutExtension(normPath);

    std::vector<std::string> searchPaths;

    if (isRawModel) {
        // Para modelos crudos (.fbx, .obj, etc.), NUNCA leer el archivo crudo como binario de malla.
        // Buscamos su cache bakeado (.model_data).
        
        // 1. Extraer GUID del archivo .meta si existe
        std::string metaPath = normPath + ".meta";
        std::string guid;
        if (SlipFileSystem::Exists(metaPath)) {
            std::string metaContent;
            if (SlipFileSystem::ReadAllText(metaPath, metaContent)) {
                size_t pos = metaContent.find("guid:");
                if (pos != std::string::npos) {
                    size_t eol = metaContent.find_first_of("\r\n", pos);
                    guid = metaContent.substr(pos + 5, eol - (pos + 5));
                    guid.erase(guid.find_last_not_of(" \t\r\n") + 1);
                    guid.erase(0, guid.find_first_not_of(" \t\r\n"));
                }
            }
        }

        // Búsqueda ascendente en todas las carpetas padre (por ejemplo si está en assets/Models/ o subcarpetas)
        std::string curr = normPath;
        for (int level = 0; level < 6; ++level) {
            curr = SlipFileSystem::GetParentPath(curr);
            if (curr.empty() || curr == "/" || curr == "\\" || curr == ".") break;
            std::string cacheCandidate = SlipFileSystem::CombinePath(curr, "cache");
            if (!guid.empty()) {
                searchPaths.push_back(SlipFileSystem::CombinePath(cacheCandidate, guid + ".model_data"));
            }
            searchPaths.push_back(SlipFileSystem::CombinePath(cacheCandidate, fname + ".model_data"));
            searchPaths.push_back(SlipFileSystem::CombinePath(cacheCandidate, fnameNoExt + ".model_data"));
            searchPaths.push_back(SlipFileSystem::CombinePath(cacheCandidate, fname + ".model_cache"));
        }

        if (!root.empty()) {
            if (!guid.empty()) {
                searchPaths.push_back(SlipFileSystem::CombinePath(root, "cache/" + guid + ".model_data"));
            }
            searchPaths.push_back(SlipFileSystem::CombinePath(root, "cache/" + fname + ".model_data"));
            searchPaths.push_back(SlipFileSystem::CombinePath(root, "cache/" + fnameNoExt + ".model_data"));
            searchPaths.push_back(SlipFileSystem::CombinePath(root, "cache/" + fname + ".model_cache"));
        }
        if (!guid.empty()) {
            searchPaths.push_back("cache/" + guid + ".model_data");
        }
        searchPaths.push_back("cache/" + fname + ".model_data");
        searchPaths.push_back("cache/" + fnameNoExt + ".model_data");
        searchPaths.push_back("cache/" + fname + ".model_cache");
    } else {
        // Es una ruta directa a un archivo de cache (.model_data o .model_cache)
        searchPaths.push_back(normPath);
        if (!root.empty()) {
            searchPaths.push_back(SlipFileSystem::CombinePath(root, normPath));
            searchPaths.push_back(SlipFileSystem::CombinePath(root, "cache/" + fname));
        }
        searchPaths.push_back("cache/" + normPath);
        searchPaths.push_back("cache/" + fname);
    }

    std::string resolvedPath;
    for (const auto& sp : searchPaths) {
        if (!sp.empty() && SlipFileSystem::Exists(sp)) {
            resolvedPath = sp;
            break;
        }
    }

    // Si es un modelo crudo y no se encontró cache, intentar bakearlo automáticamente
    if (resolvedPath.empty() && isRawModel && s_BakeCallback) {
        std::string bakedPath;
        if (s_BakeCallback(normPath, bakedPath) && SlipFileSystem::Exists(bakedPath)) {
            resolvedPath = bakedPath;
        }
    }

    if (resolvedPath.empty()) {
        if (isRawModel) {
            SlipConsole::LogWarning("SlipMesh: No se encontró cache (.model_data) para: " + normPath + ". Por favor impórtelo o espere a que termine el bakeado.");
        }
        return;
    }

    std::vector<uint8_t> bytes;
    if (!SlipFileSystem::ReadAllBytes(resolvedPath, bytes) || bytes.empty()) {
        SlipConsole::LogError("SlipMesh: Archivo de cache vacío o no accesible: " + resolvedPath);
        return;
    }

    // Si es cache antiguo (SLIPMSH1..SLIPMSH4) y tenemos el modelo crudo y el callback de bake, actualizar automáticamente a SLIPMSH5
    if (bytes.size() >= 8 && (std::memcmp(&bytes[0], "SLIPMSH1", 8) == 0 || std::memcmp(&bytes[0], "SLIPMSH2", 8) == 0 || std::memcmp(&bytes[0], "SLIPMSH3", 8) == 0 || std::memcmp(&bytes[0], "SLIPMSH4", 8) == 0) && isRawModel && s_BakeCallback) {
        std::string bakedPath;
        if (s_BakeCallback(normPath, bakedPath) && SlipFileSystem::Exists(bakedPath)) {
            resolvedPath = bakedPath;
            bytes.clear();
            SlipFileSystem::ReadAllBytes(resolvedPath, bytes);
        }
    }

    bool isV6 = (bytes.size() >= 8 && std::memcmp(&bytes[0], "SLIPMSH6", 8) == 0);
    bool isV5 = (bytes.size() >= 8 && std::memcmp(&bytes[0], "SLIPMSH5", 8) == 0);
    bool isV4 = (bytes.size() >= 8 && std::memcmp(&bytes[0], "SLIPMSH4", 8) == 0);
    bool isV3 = (bytes.size() >= 8 && std::memcmp(&bytes[0], "SLIPMSH3", 8) == 0);
    bool isV2 = (bytes.size() >= 8 && std::memcmp(&bytes[0], "SLIPMSH2", 8) == 0);
    bool isV1 = (bytes.size() >= 8 && std::memcmp(&bytes[0], "SLIPMSH1", 8) == 0);

    // Validación estricta de firma mágica
    if (!isV6 && !isV5 && !isV4 && !isV3 && !isV2 && !isV1) {
        SlipConsole::LogError("SlipMesh: El archivo no es un cache binario de malla válido (firma 'SLIPMSH6' o compatible requerida): " + resolvedPath);
        return;
    }

    size_t cursor = 8;
    auto readBytes = [&bytes, &cursor](void* dest, size_t size) -> bool {
        if (cursor + size > bytes.size()) return false;
        std::memcpy(dest, &bytes[cursor], size);
        cursor += size;
        return true;
    };

    try {
        if (isV6 || isV5 || isV4 || isV3) {
            glm::vec3 gMin, gMax;
            if (!readBytes(&gMin, sizeof(gMin))) return;
            if (!readBytes(&gMax, sizeof(gMax))) return;
            this->aabbMin = gMin;
            this->aabbMax = gMax;
            this->hasBounds = true;

            uint32_t meshesCount = 0;
            if (!readBytes(&meshesCount, sizeof(meshesCount)) || meshesCount > 2000) return;

            meshes.resize(meshesCount);
            for (uint32_t i = 0; i < meshesCount; i++) {
                Mesh& sm = meshes[i];
                int32_t matIdx = 0;
                uint32_t firstIdx = 0, idxCount = 0, baseVtx = 0, vtxCount = 0;
                glm::vec3 smMin, smMax;

                if (!readBytes(&matIdx, sizeof(matIdx))) break;
                if (!readBytes(&firstIdx, sizeof(firstIdx))) break;
                if (!readBytes(&idxCount, sizeof(idxCount))) break;
                if (!readBytes(&baseVtx, sizeof(baseVtx))) break;
                if (!readBytes(&vtxCount, sizeof(vtxCount))) break;
                if (!readBytes(&smMin, sizeof(smMin))) break;
                if (!readBytes(&smMax, sizeof(smMax))) break;

                sm.materialIndex = matIdx;
                sm.firstIndex = firstIdx;
                sm.indexCount = idxCount;
                sm.baseVertex = baseVtx;
                sm.vertexCount = vtxCount;
                sm.aabbMin = smMin;
                sm.aabbMax = smMax;
                sm.hasBounds = true;
            }

            uint32_t totalVertices = 0;
            uint32_t totalIndices = 0;
            if (!readBytes(&totalVertices, sizeof(totalVertices))) return;
            if (!readBytes(&totalIndices, sizeof(totalIndices))) return;

            m_UnifiedVertices.resize(totalVertices);
            m_UnifiedIndices.resize(totalIndices);

            if (isV6) {
                if (totalVertices > 0 && !readBytes(m_UnifiedVertices.data(), totalVertices * sizeof(Vertex))) {
                    return;
                }
            } else {
                std::vector<VertexLegacyV5> legacyV(totalVertices);
                if (totalVertices > 0 && !readBytes(legacyV.data(), totalVertices * sizeof(VertexLegacyV5))) {
                    return;
                }
                for (uint32_t vi = 0; vi < totalVertices; ++vi) {
                    m_UnifiedVertices[vi].Position = legacyV[vi].Position;
                    m_UnifiedVertices[vi].Normal = legacyV[vi].Normal;
                    m_UnifiedVertices[vi].TexCoords = legacyV[vi].TexCoords;
                    m_UnifiedVertices[vi].TexCoords2 = legacyV[vi].TexCoords;
                    m_UnifiedVertices[vi].Tangent = legacyV[vi].Tangent;
                    m_UnifiedVertices[vi].Bitangent = legacyV[vi].Bitangent;
                    for (int b = 0; b < MAX_BONE_INFLUENCE; ++b) {
                        m_UnifiedVertices[vi].m_BoneIDs[b] = legacyV[vi].m_BoneIDs[b];
                        m_UnifiedVertices[vi].m_Weights[b] = legacyV[vi].m_Weights[b];
                    }
                    m_UnifiedVertices[vi].materialIndex = legacyV[vi].materialIndex;
                }
            }

            if (totalIndices > 0 && !readBytes(m_UnifiedIndices.data(), totalIndices * sizeof(uint32_t))) {
                return;
            }

            for (size_t i = 0; i < meshes.size(); ++i) {
                Mesh& sm = meshes[i];
                if (sm.baseVertex + sm.vertexCount <= totalVertices) {
                    sm.vertices.assign(m_UnifiedVertices.begin() + sm.baseVertex,
                                       m_UnifiedVertices.begin() + sm.baseVertex + sm.vertexCount);
                }
                if (sm.firstIndex + sm.indexCount <= totalIndices) {
                    sm.indices.resize(sm.indexCount);
                    for (uint32_t j = 0; j < sm.indexCount; ++j) {
                        sm.indices[j] = m_UnifiedIndices[sm.firstIndex + j] - sm.baseVertex;
                    }
                }
            }
            this->isUnifiedBuffer = true;
        } else {
            uint32_t meshesCount = 0;
            if (!readBytes(&meshesCount, sizeof(meshesCount))) return;
            if (meshesCount > 1000) {
                SlipConsole::LogError("SlipMesh: Cantidad de mallas inválida en cache (" + std::to_string(meshesCount) + "): " + resolvedPath);
                return;
            }

            for (uint32_t i = 0; i < meshesCount; i++) {
                Mesh tempMesh;
                tempMesh.VAO = tempMesh.VBO = tempMesh.EBO = 0;
                int32_t matIndex = 0;
                uint32_t vertexCount = 0;
                uint32_t indicesCount = 0;

                if (!readBytes(&matIndex, sizeof(matIndex))) break;
                if (!readBytes(&vertexCount, sizeof(vertexCount))) break;
                if (!readBytes(&indicesCount, sizeof(indicesCount))) break;

                if (vertexCount > 5000000 || indicesCount > 15000000) {
                    SlipConsole::LogError("SlipMesh: Conteo de vértices/índices anómalo en cache: " + resolvedPath);
                    break;
                }

                tempMesh.materialIndex = matIndex;

                if (isV2) {
                    tempMesh.vertices.resize(vertexCount);
                    if (vertexCount > 0 && !readBytes(tempMesh.vertices.data(), vertexCount * sizeof(Vertex))) {
                        break;
                    }
                    tempMesh.indices.resize(indicesCount);
                    if (indicesCount > 0 && !readBytes(tempMesh.indices.data(), indicesCount * sizeof(uint32_t))) {
                        break;
                    }
                } else {
                    tempMesh.vertices.reserve(vertexCount);
                    tempMesh.indices.reserve(indicesCount);

                    for (uint32_t j = 0; j < vertexCount; j++) {
                        Vertex vertex;
                        float v3[3], normal[3], tex[2], tan[3], bitan[3];
                        int boneIDs[4];
                        float weights[4];
                        int vMatIndex;

                        if (!readBytes(v3, sizeof(v3))) break;
                        if (!readBytes(normal, sizeof(normal))) break;
                        if (!readBytes(tex, sizeof(tex))) break;
                        if (!readBytes(tan, sizeof(tan))) break;
                        if (!readBytes(bitan, sizeof(bitan))) break;
                        if (!readBytes(boneIDs, sizeof(boneIDs))) break;
                        if (!readBytes(weights, sizeof(weights))) break;
                        if (!readBytes(&vMatIndex, sizeof(vMatIndex))) break;

                        vertex.Position = glm::vec3(v3[0], v3[1], v3[2]);
                        vertex.Normal = glm::vec3(normal[0], normal[1], normal[2]);
                        vertex.TexCoords = glm::vec2(tex[0], tex[1]);
                        vertex.Tangent = glm::vec3(tan[0], tan[1], tan[2]);
                        vertex.Bitangent = glm::vec3(bitan[0], bitan[1], bitan[2]);
                        for (int b = 0; b < 4; b++) {
                            vertex.m_BoneIDs[b] = boneIDs[b];
                            vertex.m_Weights[b] = weights[b];
                        }
                        vertex.materialIndex = vMatIndex;

                        tempMesh.vertices.push_back(vertex);
                    }

                    tempMesh.indices.resize(indicesCount);
                    if (indicesCount > 0 && !readBytes(tempMesh.indices.data(), indicesCount * sizeof(uint32_t))) {
                        break;
                    }
                }

                meshes.push_back(std::move(tempMesh));
            }
        }

        readBytes(colPath, sizeof(colPath));

        uint32_t materialsCount = 0;
        if (readBytes(&materialsCount, sizeof(materialsCount)) && materialsCount < 500) {
            for (uint32_t i = 0; i < materialsCount; i++) {
                char matPath[192] = { 0 };
                if (!readBytes(matPath, sizeof(matPath))) break;
                if ( matPath[0] != '\0') {
                    materials.emplace_back(matPath);
                } else {
                    Material emptySlot("");
                    emptySlot.slotName = "Material_" + std::to_string(i);
                    materials.push_back(emptySlot);
                }
            }
        }

        // Cargar bloque opcional de geometría virtualizada V-Clust (VCLUST01) al final del archivo .model_data
        if (cursor + sizeof(SlipClusterHierarchyHeader) <= bytes.size()) {
            if (std::memcmp(&bytes[cursor], "VCLUST01", 8) == 0) {
                SlipClusterHierarchyHeader ch{};
                if (readBytes(&ch, sizeof(ch)) && ch.totalClusters > 0 && ch.totalClusters < 1000000) {
                    clusterHeader = ch;
                    clusters.resize(ch.totalClusters);
                    clusterVertices.resize(ch.totalVertices);
                    clusterIndices.resize(ch.totalIndices);
                    bool clusterOk = true;
                    if (ch.totalClusters > 0 && !readBytes(clusters.data(), ch.totalClusters * sizeof(SlipCluster))) clusterOk = false;
                    if (clusterOk && ch.totalVertices > 0 && !readBytes(clusterVertices.data(), ch.totalVertices * sizeof(SlipClusterVertex))) clusterOk = false;
                    if (clusterOk && ch.totalIndices > 0 && !readBytes(clusterIndices.data(), ch.totalIndices * sizeof(uint32_t))) clusterOk = false;
                    if (clusterOk) {
                        hasClusterLOD = true;
                    } else {
                        clusters.clear();
                        clusterVertices.clear();
                        clusterIndices.clear();
                        hasClusterLOD = false;
                    }
                }
            }
        }

        if (!meshes.empty()) {
            int maxMatIdx = 0;
            for (const auto& m : meshes) {
                if (m.materialIndex > maxMatIdx) maxMatIdx = m.materialIndex;
            }
            while (static_cast<int>(materials.size()) <= maxMatIdx) {
                Material emptySlot("");
                emptySlot.slotName = "Material_" + std::to_string(materials.size());
                materials.push_back(emptySlot);
            }
        }
        computeBounds();
    } catch (const std::exception& e) {
        SlipConsole::LogError("Excepción al cargar datos de malla: " + std::string(e.what()));
        clean();
    } catch (...) {
        SlipConsole::LogError("Error desconocido al cargar malla: " + resolvedPath);
        clean();
    }
}

void SlipMesh::loadColFromFile(std::string path)
{
    if (std::strlen(colPath) != 0)
        collision = new SlipCollision(path);
}

void SlipMesh::loadMatFromFile()
{
    for (int i = 0; i < materials.size(); i++)
    {
        if (std::strlen(materials[i].matPath) > 0) {
            SlipMaterial* loadedMat = new SlipMaterial(materials[i].matPath);
            if (!loadedMat->path.empty() && SlipFileSystem::Exists(loadedMat->path)) {
                materials[i].mat = loadedMat;
            } else {
                delete loadedMat;
                materials[i].mat = nullptr;
            }
        } else {
            materials[i].mat = nullptr;
        }
    }
}

void SlipMesh::setupMesh()
{
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glGenVertexArrays) return;
#endif

    if (isUnifiedBuffer && !m_UnifiedVertices.empty() && !m_UnifiedIndices.empty()) {
        glGenVertexArrays(1, &masterVAO);
        glGenBuffers(1, &masterVBO);
        glGenBuffers(1, &masterEBO);

        glBindVertexArray(masterVAO);

        glBindBuffer(GL_ARRAY_BUFFER, masterVBO);
        glBufferData(GL_ARRAY_BUFFER, m_UnifiedVertices.size() * sizeof(Vertex), m_UnifiedVertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, masterEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_UnifiedIndices.size() * sizeof(uint32_t), m_UnifiedIndices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));
        glEnableVertexAttribArray(5);
        glVertexAttribIPointer(5, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, m_BoneIDs));
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, m_Weights));
        glEnableVertexAttribArray(7);
        glVertexAttribPointer(7, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords2));

        glBindVertexArray(0);

        for (auto& sm : meshes) {
            sm.VAO = masterVAO;
            sm.VBO = masterVBO;
            sm.EBO = masterEBO;
        }

        m_UnifiedVertices.clear();
        m_UnifiedIndices.clear();

        // Configurar el VAO/VBO/EBO cuantizado de V-Clust (24 bytes por vértice: GL_HALF_FLOAT UVs + GL_INT_2_10_10_10_REV normales)
        setupClusterBuffers();
        return;
    }

    setupClusterBuffers();

    if (!glad_glGenVertexArrays) return;

    for (int i = 0; i < meshes.size(); i++)
    {
        if (meshes[i].vertices.empty() || meshes[i].indices.empty()) {
            continue;
        }

        // create buffers/arrays
        glGenVertexArrays(1, &meshes[i].VAO);
        glGenBuffers(1, &meshes[i].VBO);
        glGenBuffers(1, &meshes[i].EBO);

        glBindVertexArray(meshes[i].VAO);
        // load data into vertex buffers
        glBindBuffer(GL_ARRAY_BUFFER, meshes[i].VBO);
        glBufferData(GL_ARRAY_BUFFER, meshes[i].vertices.size() * sizeof(Vertex), meshes[i].vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshes[i].EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, meshes[i].indices.size() * sizeof(unsigned int), meshes[i].indices.data(), GL_STATIC_DRAW);

        // set the vertex attribute pointers
        // vertex Positions
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        // vertex normals
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
        // vertex texture coords
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
        // vertex tangent
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
        // vertex bitangent
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));
        // ids
        glEnableVertexAttribArray(5);
        glVertexAttribIPointer(5, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, m_BoneIDs));

        // weights
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, m_Weights));
        // vertex lightmap uv (TexCoords2)
        glEnableVertexAttribArray(7);
        glVertexAttribPointer(7, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords2));
        glBindVertexArray(0);
    }
}

void SlipMesh::setupClusterBuffers()
{
    if (!hasClusterLOD || clusterVertices.empty() || clusterIndices.empty() || !glad_glGenVertexArrays) {
        return;
    }

    if (clusterVAO != 0) glDeleteVertexArrays(1, &clusterVAO);
    if (clusterVBO != 0) glDeleteBuffers(1, &clusterVBO);
    if (clusterEBO != 0) glDeleteBuffers(1, &clusterEBO);
    clusterVAO = clusterVBO = clusterEBO = 0;

    glGenVertexArrays(1, &clusterVAO);
    glGenBuffers(1, &clusterVBO);
    glGenBuffers(1, &clusterEBO);

    glBindVertexArray(clusterVAO);

    glBindBuffer(GL_ARRAY_BUFFER, clusterVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 clusterVertices.size() * sizeof(SlipClusterVertex),
                 clusterVertices.data(),
                 GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, clusterEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 clusterIndices.size() * sizeof(uint32_t),
                 clusterIndices.data(),
                 GL_STATIC_DRAW);

    SetupSlipClusterVertexAttributes();

    glBindVertexArray(0);
}
