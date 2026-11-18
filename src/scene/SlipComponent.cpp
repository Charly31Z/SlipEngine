#include "SlipComponent.h"
#include "SlipEntity.h"
#include "SlipLevel.h"
#include "SlipPhysics.h"
#include "SlipTexture.h"
#include "SlipEngine/core/SlipConsole.h"
#include "SlipEngine/render/SlipPrimitive.h"
#include "SlipEngine/scripting/SlipLuaEngine.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/platform/SlipInput.h"
#include "SlipEngine/tools/SlipDebug.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include "SlipEngine/render/SlipGraphicsSettings.h"
#include "SlipEngine/render/SlipShadows.h"
#include "SlipEngine/render/SlipAnimatorController.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

// ---------------------------------------------------------
// TransformComponent
// ---------------------------------------------------------
TransformComponent::TransformComponent()
{
    m_Name = "TransformComponent";
}

void TransformComponent::Translate(float x, float y, float z)
{
    position += glm::vec3(x, y, z);
    if (m_Owner) {
        m_Owner->position = position;
    }
}

void TransformComponent::Translate(const glm::vec3& delta)
{
    Translate(delta.x, delta.y, delta.z);
}

void TransformComponent::Rotate(float x, float y, float z)
{
    rotation += glm::vec3(x, y, z);
    if (rotation.x >= 360.0f || rotation.x <= -360.0f) rotation.x = std::fmod(rotation.x, 360.0f);
    if (rotation.y >= 360.0f || rotation.y <= -360.0f) rotation.y = std::fmod(rotation.y, 360.0f);
    if (rotation.z >= 360.0f || rotation.z <= -360.0f) rotation.z = std::fmod(rotation.z, 360.0f);
    if (m_Owner) {
        m_Owner->rotation = rotation;
    }
}

void TransformComponent::Rotate(const glm::vec3& delta)
{
    Rotate(delta.x, delta.y, delta.z);
}

glm::mat4 TransformComponent::GetLocalMatrix() const
{
    glm::mat4 translate = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 scaling = glm::scale(glm::mat4(1.0f), scale);

    return translate * rotX * rotY * rotZ * scaling;
}

glm::mat4 TransformComponent::GetWorldMatrix() const
{
    if (m_Owner) return const_cast<SlipEntity*>(m_Owner)->getMatrix();
    return GetLocalMatrix();
}

void TransformComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// MeshComponent
// ---------------------------------------------------------
MeshComponent::MeshComponent()
{
    m_Name = "MeshComponent";
}

MeshComponent::MeshComponent(const std::string& path)
    : modelPath(path)
{
    m_Name = "MeshComponent";
    SetModel(path);
}

MeshComponent::~MeshComponent()
{
    if (model) {
        if (ownsModel) {
            for (size_t i = 0; i < model->materials.size(); ++i) {
                if (model->materials[i].mat == material) {
                    model->materials[i].mat = nullptr;
                }
                for (size_t k = 0; k < submeshMaterials.size(); ++k) {
                    if (model->materials[i].mat == submeshMaterials[k]) {
                        model->materials[i].mat = nullptr;
                    }
                }
            }
            delete model;
        }
        model = nullptr;
    }
    if (ownsModel) {
        for (size_t k = 0; k < submeshMaterials.size(); ++k) {
            if (submeshMaterials[k] == material) {
                material = nullptr;
            }
            delete submeshMaterials[k];
        }
        if (material) {
            delete material;
            material = nullptr;
        }
    }
    submeshMaterials.clear();
    submeshMaterialPaths.clear();
    material = nullptr;
}

void MeshComponent::SetSharedModel(SlipMesh* sharedMesh, const std::string& path)
{
    if (!sharedMesh) return;
    ownsModel = false;
    model = sharedMesh;
    modelPath = path;

    size_t slotCount = model->materials.size();
    submeshMaterialPaths.resize(slotCount, "");
    submeshMaterials.resize(slotCount, nullptr);
    for (size_t i = 0; i < slotCount; ++i) {
        submeshMaterials[i] = model->materials[i].mat;
        if (model->materials[i].mat) {
            submeshMaterialPaths[i] = model->materials[i].mat->path.empty()
                ? std::string(model->materials[i].matPath)
                : model->materials[i].mat->path;
        }
    }
    material = submeshMaterials.empty() ? nullptr : submeshMaterials[0];
    materialPath = submeshMaterialPaths.empty() ? "" : submeshMaterialPaths[0];
}

void MeshComponent::SetModel(const std::string& path)
{
    if (path.empty()) return;

    try {
        // Desvincular punteros compartidos antes de eliminar el modelo previo
        if (model) {
            if (ownsModel) {
                for (size_t i = 0; i < model->materials.size(); ++i) {
                    if (model->materials[i].mat == material) {
                        model->materials[i].mat = nullptr;
                    }
                    for (size_t k = 0; k < submeshMaterials.size(); ++k) {
                        if (model->materials[i].mat == submeshMaterials[k]) {
                            model->materials[i].mat = nullptr;
                        }
                    }
                }
                delete model;
            }
            model = nullptr;
        }

        if (ownsModel) {
            for (size_t k = 0; k < submeshMaterials.size(); ++k) {
                if (submeshMaterials[k] && submeshMaterials[k] != material) {
                    delete submeshMaterials[k];
                }
            }
        }
        ownsModel = true;
        submeshMaterials.clear();
        submeshMaterialPaths.clear();

        modelPath = path;

        bool isPrimitive = (path == "primitive:cube" || path == "primitive:sphere" || path == "primitive:plane");
        if (path == "primitive:cube") {
            std::vector<Vertex> vertices;
            std::vector<unsigned int> indices;
            SlipPrimitive::GenerateCube(1.0f, vertices, indices);
            model = new SlipMesh(vertices, indices);
        } else if (path == "primitive:sphere") {
            std::vector<Vertex> vertices;
            std::vector<unsigned int> indices;
            SlipPrimitive::GenerateSphere(0.5f, 24, 24, vertices, indices);
            model = new SlipMesh(vertices, indices);
        } else if (path == "primitive:plane") {
            std::vector<Vertex> vertices;
            std::vector<unsigned int> indices;
            SlipPrimitive::GeneratePlane(10.0f, 10.0f, vertices, indices);
            model = new SlipMesh(vertices, indices);
        } else {
            model = new SlipMesh(modelPath.c_str());
            model->init();
            model->initColl();
        }

        if (model) {
            size_t slotCount = model->materials.size();
            submeshMaterialPaths.resize(slotCount, "");
            submeshMaterials.resize(slotCount, nullptr);

            for (size_t i = 0; i < slotCount; ++i) {
                if (!isPrimitive && model->materials[i].mat != nullptr && std::strlen(model->materials[i].matPath) > 0) {
                    submeshMaterials[i] = model->materials[i].mat;
                    submeshMaterialPaths[i] = model->materials[i].mat->path.empty()
                        ? std::string(model->materials[i].matPath)
                        : model->materials[i].mat->path;
                } else {
                    submeshMaterials[i] = nullptr;
                    submeshMaterialPaths[i] = "";
                }
            }

            // Si es un modelo de 1 slot (o primitiva) y ya existía un material explícito en slot 0
            if (slotCount == 1 && material != nullptr && !materialPath.empty() && submeshMaterials[0] == nullptr) {
                if (model->materials[0].mat && model->materials[0].mat != material) {
                    delete model->materials[0].mat;
                }
                model->materials[0].mat = material;
                submeshMaterials[0] = material;
                submeshMaterialPaths[0] = materialPath;
            } else if (!isPrimitive) {
                if (material && (submeshMaterials.empty() || submeshMaterials[0] != material)) {
                    delete material;
                }
                material = submeshMaterials.empty() ? nullptr : submeshMaterials[0];
                materialPath = submeshMaterialPaths.empty() ? "" : submeshMaterialPaths[0];
            }
        }
    } catch (const std::exception& e) {
        SlipConsole::LogError("Error al cargar modelo en MeshComponent: " + std::string(e.what()));
    } catch (...) {
        SlipConsole::LogError("Error desconocido al cargar modelo: " + path);
    }

    if (m_Owner) {
        if (auto mc = m_Owner->GetComponent<MeshColliderComponent>()) {
            mc->CreateOrUpdateShape();
            if (!mc->rigidBody) {
                mc->RecreateRigidBody(0.0f, false);
            }
        }
    }
}

void MeshComponent::SetSubmeshMaterial(size_t slotIndex, const std::string& path)
{
    size_t minSize = slotIndex + 1;
    if (model && model->materials.size() > minSize) {
        minSize = model->materials.size();
    }
    if (submeshMaterialPaths.size() < minSize) {
        submeshMaterialPaths.resize(minSize, "");
    }
    if (submeshMaterials.size() < minSize) {
        submeshMaterials.resize(minSize, nullptr);
    }

    if (submeshMaterials[slotIndex]) {
        if (model && slotIndex < model->materials.size() && model->materials[slotIndex].mat == submeshMaterials[slotIndex]) {
            model->materials[slotIndex].mat = nullptr;
        }
        if (submeshMaterials[slotIndex] == material) {
            material = nullptr;
        }
        delete submeshMaterials[slotIndex];
        submeshMaterials[slotIndex] = nullptr;
    }

    if (path.empty()) {
        submeshMaterialPaths[slotIndex] = "";
        if (model && slotIndex < model->materials.size()) {
            bool isPrimitive = (modelPath == "primitive:cube" || modelPath == "primitive:sphere" || modelPath == "primitive:plane");
            if (isPrimitive) {
                if (!model->materials[slotIndex].mat) {
                    model->materials[slotIndex].mat = new SlipMaterial("");
                    model->materials[slotIndex].mat->init();
                }
                submeshMaterials[slotIndex] = model->materials[slotIndex].mat;
            } else {
                if (model->materials[slotIndex].mat) {
                    delete model->materials[slotIndex].mat;
                    model->materials[slotIndex].mat = nullptr;
                }
                std::memset(model->materials[slotIndex].matPath, 0, sizeof(model->materials[slotIndex].matPath));
                submeshMaterials[slotIndex] = nullptr;
            }
        }
        if (slotIndex == 0) {
            material = nullptr;
            materialPath = "";
        }
        return;
    }

    SlipMaterial* newMat = new SlipMaterial(path);
    newMat->init();
    std::string resolvedMatPath = newMat->path.empty() ? path : newMat->path;

    submeshMaterialPaths[slotIndex] = resolvedMatPath;
    submeshMaterials[slotIndex] = newMat;

    if (model && slotIndex < model->materials.size()) {
        if (model->materials[slotIndex].mat && model->materials[slotIndex].mat != newMat) {
            delete model->materials[slotIndex].mat;
        }
        model->materials[slotIndex].mat = newMat;
        std::strncpy(model->materials[slotIndex].matPath, resolvedMatPath.c_str(), sizeof(model->materials[slotIndex].matPath) - 1);
    }

    if (slotIndex == 0) {
        if (material && material != newMat) {
            delete material;
        }
        material = newMat;
        materialPath = resolvedMatPath;
    }
}

void MeshComponent::SetMaterial(const std::string& path)
{
    SetSubmeshMaterial(0, path);
}

SlipMaterial* MeshComponent::GetSubmeshMaterial(size_t slotIndex) const
{
    if (slotIndex < submeshMaterials.size() && submeshMaterials[slotIndex] != nullptr) {
        return submeshMaterials[slotIndex];
    }
    if (model && slotIndex < model->materials.size() && model->materials[slotIndex].mat != nullptr) {
        return model->materials[slotIndex].mat;
    }
    if (slotIndex == 0 && material != nullptr) {
        return material;
    }
    return nullptr;
}

size_t MeshComponent::GetMaterialSlotCount() const
{
    if (model && !model->materials.empty()) {
        return model->materials.size();
    }
    if (!submeshMaterialPaths.empty()) {
        return submeshMaterialPaths.size();
    }
    return 1;
}

std::string MeshComponent::GetMaterialSlotName(size_t slotIndex) const
{
    if (model && slotIndex < model->materials.size() && !model->materials[slotIndex].slotName.empty()) {
        return model->materials[slotIndex].slotName;
    }
    return "Slot " + std::to_string(slotIndex);
}

std::string MeshComponent::GetSubmeshMaterialPath(size_t slotIndex) const
{
    if (slotIndex < submeshMaterialPaths.size() && !submeshMaterialPaths[slotIndex].empty()) {
        return submeshMaterialPaths[slotIndex];
    }
    if (slotIndex == 0) {
        return materialPath;
    }
    return "";
}

void MeshComponent::init()
{
    OnInit();
}

void MeshComponent::draw()
{
    if (m_Owner) {
        OnDraw(m_Owner->getMatrix());
    }
}

void MeshComponent::OnInit()
{
    if (!modelPath.empty() && !model) {
        SetModel(modelPath);
    }
    if (!materialPath.empty() && !material) {
        SetMaterial(materialPath);
    }
}

void MeshComponent::OnDraw(const glm::mat4& parentTransform)
{
    if (!m_Enabled || !model) return;

    if (material) {
        glm::mat4 t = parentTransform;
        material->bind(t);
        model->draw(parentTransform, material);
    } else {
        model->draw(parentTransform, nullptr);
    }
}

void MeshComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// BoneComponent
// ---------------------------------------------------------
void BoneComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// SkinnedMeshComponent
// ---------------------------------------------------------
static SkinnedMeshComponent::SkeletonExtractCallback s_SkeletonExtractCallback = nullptr;

void SkinnedMeshComponent::SetSkeletonExtractCallback(SkeletonExtractCallback cb)
{
    s_SkeletonExtractCallback = cb;
}

BoneComponent::~BoneComponent()
{
    if (m_Owner) {
        SlipEntity* p = m_Owner->GetParent();
        while (p) {
            if (auto smc = p->GetComponent<SkinnedMeshComponent>()) {
                for (auto& be : smc->boneEntities) {
                    if (be == m_Owner) be = nullptr;
                }
                break;
            }
            p = p->GetParent();
        }
    }
}

SkinnedMeshComponent::SkinnedMeshComponent()
{
    m_Name = "SkinnedMeshComponent";
    skeleton = std::make_shared<SlipSkeleton>();
    boneTransforms.resize(100, glm::mat4(1.0f));
}

SkinnedMeshComponent::SkinnedMeshComponent(const std::string& path)
    : SkinnedMeshComponent()
{
    SetModel(path);
}

SkinnedMeshComponent::~SkinnedMeshComponent()
{
    if (model) {
        for (size_t i = 0; i < model->materials.size(); ++i) {
            if (model->materials[i].mat == material) {
                model->materials[i].mat = nullptr;
            }
            for (size_t k = 0; k < submeshMaterials.size(); ++k) {
                if (model->materials[i].mat == submeshMaterials[k]) {
                    model->materials[i].mat = nullptr;
                }
            }
        }
        delete model;
        model = nullptr;
    }
    for (size_t k = 0; k < submeshMaterials.size(); ++k) {
        if (submeshMaterials[k] == material) {
            material = nullptr;
        }
        if (submeshMaterials[k]) {
            delete submeshMaterials[k];
            submeshMaterials[k] = nullptr;
        }
    }
    submeshMaterials.clear();
    submeshMaterialPaths.clear();
    if (material) {
        delete material;
        material = nullptr;
    }
}

void SkinnedMeshComponent::SetModel(const std::string& path)
{
    modelPath = path;
    if (model) {
        for (size_t i = 0; i < model->materials.size(); ++i) {
            if (model->materials[i].mat == material) {
                model->materials[i].mat = nullptr;
            }
            for (size_t k = 0; k < submeshMaterials.size(); ++k) {
                if (model->materials[i].mat == submeshMaterials[k]) {
                    model->materials[i].mat = nullptr;
                }
            }
        }
        delete model;
        model = nullptr;
    }
    for (size_t k = 0; k < submeshMaterials.size(); ++k) {
        if (submeshMaterials[k] == material) {
            material = nullptr;
        }
        if (submeshMaterials[k]) {
            delete submeshMaterials[k];
            submeshMaterials[k] = nullptr;
        }
    }
    submeshMaterials.clear();
    submeshMaterialPaths.clear();
    if (modelPath.empty()) return;

    try {
        model = new SlipMesh(modelPath.c_str());
        model->init();
        model->initColl();
        size_t slotCount = model->materials.size();
        submeshMaterialPaths.resize(slotCount, "");
        submeshMaterials.resize(slotCount, nullptr);
        for (size_t i = 0; i < slotCount; ++i) {
            if (model->materials[i].mat != nullptr && std::strlen(model->materials[i].matPath) > 0) {
                submeshMaterials[i] = model->materials[i].mat;
                submeshMaterialPaths[i] = model->materials[i].mat->path;
            }
        }

        std::vector<std::shared_ptr<AnimationClip>> extractedClips;
        if (s_SkeletonExtractCallback && skeleton) {
            s_SkeletonExtractCallback(modelPath, *skeleton, extractedClips);
        }

        if (m_Owner) {
            auto anim = m_Owner->GetComponent<AnimatorComponent>();
            if (!anim && !extractedClips.empty()) {
                anim = m_Owner->AddComponent<AnimatorComponent>();
            }
            if (anim) {
                for (auto& clip : extractedClips) {
                    anim->AddClip(clip);
                }
            }
            BuildBoneHierarchy(&SlipLevel::Get());
        }
    } catch (...) {
        SlipConsole::LogError("Error al cargar modelo en SkinnedMeshComponent: " + path);
    }
}

void SkinnedMeshComponent::SetMaterial(const std::string& path)
{
    materialPath = path;
    if (material) {
        delete material;
        material = nullptr;
    }
    if (!materialPath.empty()) {
        material = new SlipMaterial(materialPath.c_str());
        material->init();
    }
}

void SkinnedMeshComponent::SetSubmeshMaterial(size_t slotIndex, const std::string& path)
{
    if (slotIndex >= submeshMaterialPaths.size()) {
        submeshMaterialPaths.resize(slotIndex + 1, "");
        submeshMaterials.resize(slotIndex + 1, nullptr);
    }
    submeshMaterialPaths[slotIndex] = path;
    if (submeshMaterials[slotIndex]) {
        delete submeshMaterials[slotIndex];
        submeshMaterials[slotIndex] = nullptr;
    }
    if (!path.empty()) {
        submeshMaterials[slotIndex] = new SlipMaterial(path.c_str());
        submeshMaterials[slotIndex]->init();
    }
}

SlipMaterial* SkinnedMeshComponent::GetSubmeshMaterial(size_t slotIndex) const
{
    if (slotIndex < submeshMaterials.size()) return submeshMaterials[slotIndex];
    return material;
}

size_t SkinnedMeshComponent::GetMaterialSlotCount() const
{
    return model ? model->materials.size() : submeshMaterialPaths.size();
}

std::string SkinnedMeshComponent::GetMaterialSlotName(size_t slotIndex) const
{
    if (model && slotIndex < model->materials.size() && !model->materials[slotIndex].slotName.empty()) {
        return model->materials[slotIndex].slotName;
    }
    return "Slot " + std::to_string(slotIndex);
}

std::string SkinnedMeshComponent::GetSubmeshMaterialPath(size_t slotIndex) const
{
    if (slotIndex < submeshMaterialPaths.size()) return submeshMaterialPaths[slotIndex];
    return materialPath;
}

void SkinnedMeshComponent::BuildBoneHierarchy(SlipLevel* level)
{
    if (!skeleton || skeleton->GetBoneCount() == 0 || !m_Owner) return;

    boneEntities.clear();
    boneEntities.resize(skeleton->GetBoneCount(), nullptr);

    // Mapear entidades ya existentes en el subárbol
    for (size_t i = 0; i < skeleton->GetBoneCount(); ++i) {
        const BoneInfo* b = skeleton->GetBone(static_cast<int>(i));
        if (!b) continue;
        SlipEntity* existing = m_Owner->FindChild(b->name, true);
        if (existing) {
            boneEntities[i] = existing;
            auto bc = existing->GetComponent<BoneComponent>();
            if (!bc) {
                bc = existing->AddComponent<BoneComponent>(b->id, b->name, b->offsetMatrix);
            }
            const glm::mat4& bind = b->localBindTransform;
            glm::vec3 translation = glm::vec3(bind[3]);
            glm::vec3 scale(
                glm::length(glm::vec3(bind[0])),
                glm::length(glm::vec3(bind[1])),
                glm::length(glm::vec3(bind[2]))
            );
            glm::mat3 rotM;
            rotM[0] = (scale.x > 1e-6f) ? glm::vec3(bind[0]) / scale.x : glm::vec3(1, 0, 0);
            rotM[1] = (scale.y > 1e-6f) ? glm::vec3(bind[1]) / scale.y : glm::vec3(0, 1, 0);
            rotM[2] = (scale.z > 1e-6f) ? glm::vec3(bind[2]) / scale.z : glm::vec3(0, 0, 1);
            glm::quat rotQ = glm::normalize(glm::quat_cast(rotM));

            existing->position = translation;
            existing->scale = scale;
            existing->rotation = glm::degrees(glm::eulerAngles(rotQ));
            if (bc) {
                bc->localBindTransform = bind;
                bc->orientation = rotQ;
            }
            if (auto tc = existing->GetComponent<TransformComponent>()) {
                tc->position = existing->position;
                tc->rotation = existing->rotation;
                tc->scale = existing->scale;
            }
        }
    }

    // Instanciar cualquier entidad que falte
    for (size_t i = 0; i < skeleton->GetBoneCount(); ++i) {
        if (boneEntities[i] != nullptr) continue;
        const BoneInfo* b = skeleton->GetBone(static_cast<int>(i));
        if (!b) continue;

        SlipEntity* boneEnt = new SlipEntity(b->name);
        auto bc = boneEnt->AddComponent<BoneComponent>(b->id, b->name, b->offsetMatrix);

        const glm::mat4& bind = b->localBindTransform;
        glm::vec3 translation = glm::vec3(bind[3]);
        glm::vec3 scale(
            glm::length(glm::vec3(bind[0])),
            glm::length(glm::vec3(bind[1])),
            glm::length(glm::vec3(bind[2]))
        );
        glm::mat3 rotM;
        rotM[0] = (scale.x > 1e-6f) ? glm::vec3(bind[0]) / scale.x : glm::vec3(1, 0, 0);
        rotM[1] = (scale.y > 1e-6f) ? glm::vec3(bind[1]) / scale.y : glm::vec3(0, 1, 0);
        rotM[2] = (scale.z > 1e-6f) ? glm::vec3(bind[2]) / scale.z : glm::vec3(0, 0, 1);
        glm::quat rotQ = glm::normalize(glm::quat_cast(rotM));

        boneEnt->position = translation;
        boneEnt->scale = scale;
        boneEnt->rotation = glm::degrees(glm::eulerAngles(rotQ));
        if (bc) {
            bc->localBindTransform = bind;
            bc->orientation = rotQ;
        }
        if (auto tc = boneEnt->GetComponent<TransformComponent>()) {
            tc->position = boneEnt->position;
            tc->rotation = boneEnt->rotation;
            tc->scale = boneEnt->scale;
        }

        SlipEntity* parentEnt = m_Owner;
        if (b->parentIndex >= 0 && b->parentIndex < static_cast<int>(boneEntities.size()) && boneEntities[b->parentIndex]) {
            parentEnt = boneEntities[b->parentIndex];
        }

        if (level) {
            level->AddEntity(boneEnt);
        } else {
            SlipLevel::Get().AddEntity(boneEnt);
        }
        boneEnt->SetParent(parentEnt);
        boneEntities[i] = boneEnt;
    }
    if (auto anim = m_Owner->GetComponent<AnimatorComponent>()) {
        anim->InvalidateEntityCache();
    }
    m_BoneMatricesDirty = true;
    UpdateBoneMatrices();
}

void SkinnedMeshComponent::UpdateBoneMatrices()
{
    if (!m_BoneMatricesDirty) return;
    if (!skeleton || skeleton->GetBoneCount() == 0 || !m_Owner) {
        for (auto& m : boneTransforms) m = glm::mat4(1.0f);
        m_BoneMatricesDirty = false;
        return;
    }

    glm::mat4 invRootWorld = glm::inverse(m_Owner->getMatrix());
    size_t count = (std::min)(skeleton->GetBoneCount(), static_cast<size_t>(100));
    std::vector<glm::mat4> globalPose(skeleton->GetBoneCount(), glm::mat4(1.0f));

    for (size_t i = 0; i < count; ++i) {
        const BoneInfo* b = skeleton->GetBone(static_cast<int>(i));
        if (!b) continue;

        if (i < boneEntities.size() && boneEntities[i] != nullptr) {
            glm::mat4 boneWorld = boneEntities[i]->getMatrix();
            boneTransforms[i] = invRootWorld * boneWorld * b->offsetMatrix;
        } else {
            glm::mat4 localMat = b->localBindTransform;
            if (b->parentIndex >= 0 && b->parentIndex < static_cast<int>(globalPose.size())) {
                globalPose[i] = globalPose[b->parentIndex] * localMat;
            } else {
                globalPose[i] = localMat;
            }
            boneTransforms[i] = globalPose[i] * b->offsetMatrix;
        }
    }
    m_BoneMatricesDirty = false;
}

void SkinnedMeshComponent::OnInit()
{
    if (!modelPath.empty() && !model) {
        SetModel(modelPath);
    }
    if (!materialPath.empty() && !material) {
        SetMaterial(materialPath);
    }
}

void SkinnedMeshComponent::OnUpdate(float deltaTime)
{
    UpdateBoneMatrices();
}

void SkinnedMeshComponent::draw(const glm::mat4& worldTransform)
{
    if (!m_Enabled || !model) return;

    UpdateBoneMatrices();

    int skinQuality = SlipGraphicsSettings::Get().GetData().skinningQuality;

    if (SlipShadows::Get().calculating()) {
        if (!castShadows) return;
        SlipShader* shadowShader = SlipShadows::Get().getShader();
        if (shadowShader) {
            shadowShader->use();
            shadowShader->setBool("u_HasBones", true);
            shadowShader->setInt("u_SkinningQuality", skinQuality);
            shadowShader->setMat4Array("u_BoneTransforms", boneTransforms.data(), static_cast<int>(boneTransforms.size()));
        }
        model->draw(const_cast<glm::mat4&>(worldTransform), nullptr);
        if (shadowShader) {
            shadowShader->setBool("u_HasBones", false);
        }
        return;
    }

    auto uploadBoneUniforms = [&](SlipMaterial* mat) {
        if (!mat || !mat->shader) return;
        mat->shader->use();
        mat->shader->setBool("u_HasBones", true);
        mat->shader->setInt("u_SkinningQuality", skinQuality);
        mat->shader->setMat4Array("u_BoneTransforms", boneTransforms.data(), static_cast<int>(boneTransforms.size()));
    };

    if (material) {
        uploadBoneUniforms(material);
        material->bind(const_cast<glm::mat4&>(worldTransform));
        model->draw(worldTransform, material);
    } else {
        for (size_t i = 0; i < model->materials.size(); ++i) {
            if (model->materials[i].mat) {
                uploadBoneUniforms(model->materials[i].mat);
            }
        }
        model->draw(worldTransform, nullptr);
    }
}

void SkinnedMeshComponent::OnDraw(const glm::mat4& parentTransform)
{
    draw(parentTransform);
}

void SkinnedMeshComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// AnimatorComponent
// ---------------------------------------------------------
AnimatorComponent::AnimatorComponent()
{
    m_Name = "AnimatorComponent";
}

void AnimatorComponent::Play(const std::string& clipName, float speed, bool loop)
{
    for (size_t i = 0; i < clips.size(); ++i) {
        if (clips[i] && clips[i]->name == clipName) {
            currentClipIndex = static_cast<int>(i);
            playbackSpeed = speed;
            isLooping = loop;
            isPlaying = true;
            currentTime = 0.0f;
            return;
        }
    }
    SlipConsole::LogWarning("Clip de animacion no encontrado: " + clipName);
}

void AnimatorComponent::Play(int clipIndex, float speed, bool loop)
{
    if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
        currentClipIndex = clipIndex;
        playbackSpeed = speed;
        isLooping = loop;
        isPlaying = true;
        currentTime = 0.0f;
    }
}

void AnimatorComponent::ApplyPose(float time)
{
    if (!m_Owner) return;
    AnimationClip* clip = GetCurrentClip();
    if (!clip || clip->duration <= 0.0f) return;

    if (m_CachedTrackClip != clip || m_TrackEntities.size() != clip->tracks.size()) {
        m_CachedTrackClip = clip;
        m_TrackEntities.resize(clip->tracks.size(), nullptr);
        for (size_t t = 0; t < clip->tracks.size(); ++t) {
            m_TrackEntities[t] = m_Owner->FindChild(clip->tracks[t].boneName, true);
        }
    }

    for (size_t t = 0; t < clip->tracks.size(); ++t) {
        SlipEntity* boneEnt = m_TrackEntities[t];
        if (!boneEnt) continue;

        const auto& track = clip->tracks[t];
        auto bc = boneEnt->GetComponent<BoneComponent>();
        glm::vec3 pos = boneEnt->position;
        glm::vec3 scl = boneEnt->scale;
        glm::quat rot = bc ? bc->orientation : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        if (bc) {
            const glm::mat4& bind = bc->localBindTransform;
            pos = glm::vec3(bind[3]);
            scl = glm::vec3(
                glm::length(glm::vec3(bind[0])),
                glm::length(glm::vec3(bind[1])),
                glm::length(glm::vec3(bind[2]))
            );
        }

        if (!track.positions.empty()) {
            pos = track.InterpolatePosition(time);
        }
        if (!track.rotations.empty()) {
            rot = track.InterpolateRotation(time);
        }
        if (!track.scales.empty()) {
            scl = track.InterpolateScale(time);
        }

        boneEnt->position = pos;
        boneEnt->scale = scl;
        if (bc) {
            bc->orientation = rot;
        }
        boneEnt->rotation = glm::degrees(glm::eulerAngles(rot));

        if (auto tc = boneEnt->GetComponent<TransformComponent>()) {
            tc->position = pos;
            tc->rotation = boneEnt->rotation;
            tc->scale = scl;
        }
    }

    if (auto smc = m_Owner->GetComponent<SkinnedMeshComponent>()) {
        smc->MarkBoneMatricesDirty();
        smc->UpdateBoneMatrices();
    }
}

void AnimatorComponent::ApplyBlendedPose(const AnimationClip* clipA, float timeA, const AnimationClip* clipB, float timeB, float blendWeight)
{
    if (!m_Owner || !clipA) return;

    if (m_CachedTrackClip != clipA || m_TrackEntities.size() != clipA->tracks.size()) {
        m_CachedTrackClip = clipA;
        m_TrackEntities.resize(clipA->tracks.size(), nullptr);
        for (size_t t = 0; t < clipA->tracks.size(); ++t) {
            m_TrackEntities[t] = m_Owner->FindChild(clipA->tracks[t].boneName, true);
        }
    }

    float w = glm::clamp(blendWeight, 0.0f, 1.0f);

    for (size_t t = 0; t < clipA->tracks.size(); ++t) {
        SlipEntity* boneEnt = m_TrackEntities[t];
        if (!boneEnt) continue;

        const auto& trackA = clipA->tracks[t];
        auto bc = boneEnt->GetComponent<BoneComponent>();
        glm::vec3 pos = boneEnt->position;
        glm::vec3 scl = boneEnt->scale;
        glm::quat rot = bc ? bc->orientation : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        if (bc) {
            const glm::mat4& bind = bc->localBindTransform;
            pos = glm::vec3(bind[3]);
            scl = glm::vec3(
                glm::length(glm::vec3(bind[0])),
                glm::length(glm::vec3(bind[1])),
                glm::length(glm::vec3(bind[2]))
            );
        }

        glm::vec3 posA = pos, posB = pos;
        glm::quat rotA = rot, rotB = rot;
        glm::vec3 sclA = scl, sclB = scl;

        if (!trackA.positions.empty()) posA = trackA.InterpolatePosition(timeA);
        if (!trackA.rotations.empty()) rotA = trackA.InterpolateRotation(timeA);
        if (!trackA.scales.empty()) sclA = trackA.InterpolateScale(timeA);

        if (clipB) {
            const AnimationTrack* trackB = clipB->FindTrack(trackA.boneName);
            if (trackB) {
                if (!trackB->positions.empty()) posB = trackB->InterpolatePosition(timeB);
                if (!trackB->rotations.empty()) rotB = trackB->InterpolateRotation(timeB);
                if (!trackB->scales.empty()) sclB = trackB->InterpolateScale(timeB);
            }
        }

        glm::vec3 finalPos = glm::mix(posA, posB, w);
        glm::quat finalRot = glm::normalize(glm::slerp(rotA, rotB, w));
        glm::vec3 finalScl = glm::mix(sclA, sclB, w);

        boneEnt->position = finalPos;
        boneEnt->scale = finalScl;
        if (bc) bc->orientation = finalRot;
        boneEnt->rotation = glm::degrees(glm::eulerAngles(finalRot));

        if (auto tc = boneEnt->GetComponent<TransformComponent>()) {
            tc->position = finalPos;
            tc->rotation = boneEnt->rotation;
            tc->scale = finalScl;
        }
    }

    if (auto smc = m_Owner->GetComponent<SkinnedMeshComponent>()) {
        smc->MarkBoneMatricesDirty();
        smc->UpdateBoneMatrices();
    }
}

void AnimatorComponent::SetController(const std::string& path)
{
    controllerPath = path;
    if (path.empty()) {
        controller = nullptr;
        return;
    }
    controller = std::make_shared<SlipAnimatorController>();
    if (!controller->LoadFromFile(path)) {
        SlipConsole::LogWarning("No se pudo cargar AnimatorController: " + path);
    }
}

void AnimatorComponent::SetFloat(const std::string& name, float value)
{
    if (!controller) controller = std::make_shared<SlipAnimatorController>();
    controller->SetFloat(name, value);
}

void AnimatorComponent::SetBool(const std::string& name, bool value)
{
    if (!controller) controller = std::make_shared<SlipAnimatorController>();
    controller->SetBool(name, value);
}

void AnimatorComponent::SetInt(const std::string& name, int value)
{
    if (!controller) controller = std::make_shared<SlipAnimatorController>();
    controller->SetInt(name, value);
}

void AnimatorComponent::SetTrigger(const std::string& name)
{
    if (!controller) controller = std::make_shared<SlipAnimatorController>();
    controller->SetTrigger(name);
}

float AnimatorComponent::GetFloat(const std::string& name) const
{
    return controller ? controller->GetFloat(name) : 0.0f;
}

bool AnimatorComponent::GetBool(const std::string& name) const
{
    return controller ? controller->GetBool(name) : false;
}

int AnimatorComponent::GetInt(const std::string& name) const
{
    return controller ? controller->GetInt(name) : 0;
}

void AnimatorComponent::SetTime(float time)
{
    currentTime = time;
    AnimationClip* clip = GetCurrentClip();
    if (clip && clip->duration > 0.0f) {
        if (currentTime > clip->duration) {
            currentTime = isLooping ? std::fmod(currentTime, clip->duration) : clip->duration;
        } else if (currentTime < 0.0f) {
            currentTime = 0.0f;
        }
    }
    ApplyPose(currentTime);
}

float AnimatorComponent::GetCurrentDuration() const
{
    AnimationClip* clip = GetCurrentClip();
    return clip ? clip->duration : 0.0f;
}

AnimationClip* AnimatorComponent::GetCurrentClip() const
{
    if (currentClipIndex >= 0 && currentClipIndex < static_cast<int>(clips.size())) {
        return clips[currentClipIndex].get();
    }
    return nullptr;
}

AnimationClip* AnimatorComponent::GetClip(const std::string& name) const
{
    for (const auto& c : clips) {
        if (c && c->name == name) return c.get();
    }
    return nullptr;
}

AnimationClip* AnimatorComponent::GetClip(int index) const
{
    if (index >= 0 && index < static_cast<int>(clips.size())) return clips[index].get();
    return nullptr;
}

void AnimatorComponent::AddClip(std::shared_ptr<AnimationClip> clip)
{
    if (!clip) return;
    for (const auto& existing : clips) {
        if (existing && (existing == clip || existing->name == clip->name)) {
            return;
        }
    }
    clips.push_back(clip);
    if (currentClipIndex < 0) currentClipIndex = 0;
    InvalidateEntityCache();
}

void AnimatorComponent::OnInit()
{
    if (currentClipIndex < 0 && !clips.empty()) {
        currentClipIndex = 0;
    }
    if (!controllerPath.empty() && !controller) {
        SetController(controllerPath);
    }
}

void AnimatorComponent::OnUpdate(float deltaTime)
{
    if (!isPlaying || !m_Owner) return;

    if (controller) {
        controller->Update(deltaTime * playbackSpeed, clips);
        auto activePose = controller->GetActivePose(clips);
        if (activePose.clipA) {
            if (activePose.clipB && activePose.blendWeight > 0.001f) {
                ApplyBlendedPose(activePose.clipA, activePose.timeA, activePose.clipB, activePose.timeB, activePose.blendWeight);
            } else {
                currentTime = activePose.timeA;
                ApplyPose(activePose.timeA);
            }
        }
        return;
    }

    AnimationClip* clip = GetCurrentClip();
    if (!clip || clip->duration <= 0.0f) return;

    currentTime += deltaTime * playbackSpeed;
    if (currentTime >= clip->duration) {
        if (isLooping) {
            currentTime = std::fmod(currentTime, clip->duration);
        } else {
            currentTime = clip->duration;
            isPlaying = false;
        }
    }

    ApplyPose(currentTime);
}

void AnimatorComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// ColliderComponent Base
// ---------------------------------------------------------
ColliderComponent::ColliderComponent()
{
    m_Name = "ColliderComponent";
}

ColliderComponent::~ColliderComponent()
{
    if (rigidBody) {
        if (SlipPhysics::IsInitialized()) {
            SlipPhysics::Get().getWorld().removeRigidBody(rigidBody);
        }
        delete rigidBody;
        rigidBody = nullptr;
    }
    if (motionState) {
        delete motionState;
        motionState = nullptr;
    }
    if (collisionShape) {
        delete collisionShape;
        collisionShape = nullptr;
    }
}

static btQuaternion EulerDegreesToBulletQuat(const glm::vec3& rotDeg) {
    btQuaternion q;
    q.setEulerZYX(btScalar(glm::radians(rotDeg.z)), btScalar(glm::radians(rotDeg.y)), btScalar(glm::radians(rotDeg.x)));
    return q;
}

static glm::vec3 BulletMatrixToEulerDegrees(const btMatrix3x3& basis) {
    // Descomposición Euler exacta para la convención de SlipEngine (rotX * rotY * rotZ):
    // R[0][2] = sin(y)
    // R[1][2] = -sin(x) * cos(y),  R[2][2] = cos(x) * cos(y)  ==> tan(x) = -R[1][2] / R[2][2]
    // R[0][1] = -cos(y) * sin(z),  R[0][0] = cos(y) * cos(z)  ==> tan(z) = -R[0][1] / R[0][0]
    float r02 = glm::clamp(static_cast<float>(basis[0][2]), -1.0f, 1.0f);
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    if (std::abs(r02) < 0.9999f) {
        y = std::asin(r02);
        x = std::atan2(-static_cast<float>(basis[1][2]), static_cast<float>(basis[2][2]));
        z = std::atan2(-static_cast<float>(basis[0][1]), static_cast<float>(basis[0][0]));
    } else {
        // Bloqueo cardán (eje Y vertical hacia arriba o abajo a 90°)
        if (r02 > 0.0f) {
            y = 1.57079632679f;
            x = std::atan2(static_cast<float>(basis[1][0]), static_cast<float>(basis[1][1]));
        } else {
            y = -1.57079632679f;
            x = std::atan2(-static_cast<float>(basis[1][0]), static_cast<float>(basis[1][1]));
        }
        z = 0.0f;
    }

    return glm::vec3(glm::degrees(x), glm::degrees(y), glm::degrees(z));
}

void ColliderComponent::RecreateRigidBody(float mass, bool isKinematic)
{
    if (m_Owner) {
        if (auto veh = m_Owner->GetComponent<VehicleComponent>()) {
            veh->CleanVehicle();
        }
    }

    if (rigidBody) {
        if (SlipPhysics::IsInitialized()) {
            SlipPhysics::Get().getWorld().removeRigidBody(rigidBody);
        }
        delete rigidBody;
        rigidBody = nullptr;
    }
    if (motionState) {
        delete motionState;
        motionState = nullptr;
    }

    CreateOrUpdateShape();
    if (!collisionShape) return;

    glm::vec3 pos(0.0f);
    glm::vec3 rot(0.0f);
    glm::vec3 scale(1.0f);
    if (m_Owner) {
        scale = m_Owner->scale;
        glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), glm::radians(m_Owner->rotation.x), glm::vec3(1, 0, 0)) *
                           glm::rotate(glm::mat4(1.0f), glm::radians(m_Owner->rotation.y), glm::vec3(0, 1, 0)) *
                           glm::rotate(glm::mat4(1.0f), glm::radians(m_Owner->rotation.z), glm::vec3(0, 0, 1));
        glm::vec3 worldOffset = glm::vec3(rotMat * glm::vec4(centerOffset * scale, 0.0f));
        pos = m_Owner->position + worldOffset;
        rot = m_Owner->rotation;
    }

    if (collisionShape) {
        collisionShape->setLocalScaling(btVector3(scale.x, scale.y, scale.z));
        collisionShape->setMargin(0.01f);
    }

    btTransform startTransform;
    startTransform.setIdentity();
    startTransform.setOrigin(btVector3(pos.x, pos.y, pos.z));
    startTransform.setRotation(EulerDegreesToBulletQuat(rot));

    motionState = new btDefaultMotionState(startTransform);

    btVector3 localInertia(0, 0, 0);
    if (mass > 0.0f && !isKinematic) {
        collisionShape->calculateLocalInertia(mass, localInertia);
    }

    btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, motionState, collisionShape, localInertia);
    rigidBody = new btRigidBody(rbInfo);
    rigidBody->setUserPointer(m_Owner);
    rigidBody->setFriction(0.5f);
    rigidBody->setRollingFriction(0.0f);
    rigidBody->setRestitution(0.0f);

    if (isTrigger) {
        rigidBody->setCollisionFlags(rigidBody->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
    }

    // Deactivation / Sleeping para optimizar la CPU en reposo
    if (mass > 0.0f && !isKinematic) {
        rigidBody->setActivationState(ACTIVE_TAG);
        rigidBody->setSleepingThresholds(0.2f, 0.2f);

        // Continuous Collision Detection (CCD) equilibrado para cuerpos dinámicos rápidos
        rigidBody->setCcdMotionThreshold(1.0f);
        float ccdRadius = 0.2f;
        if (auto sc = dynamic_cast<SphereColliderComponent*>(this)) {
            ccdRadius = sc->radius * 0.5f;
        } else if (auto bc = dynamic_cast<BoxColliderComponent*>(this)) {
            ccdRadius = (std::min)({ bc->size.x * scale.x, bc->size.y * scale.y, bc->size.z * scale.z }) * 0.25f;
        }
        if (ccdRadius < 0.005f) ccdRadius = 0.05f;
        rigidBody->setCcdSweptSphereRadius(ccdRadius);
    } else if (mass == 0.0f && !isKinematic) {
        // Objeto estático puro: excluir de actualizaciones continuas del broadphase
        rigidBody->setActivationState(ISLAND_SLEEPING);
    }

    if (m_Owner) {
        auto rb = m_Owner->GetComponent<RigidBodyComponent>();
        if (rb) {
            rigidBody->setFriction(rb->friction);
            rigidBody->setRollingFriction(rb->rollingFriction);
            rigidBody->setRestitution(rb->restitution);

            if (!rb->useGravity) {
                rigidBody->setGravity(btVector3(0, 0, 0));
            } else {
                glm::vec3 g = SlipPhysics::Get().getGravity();
                rigidBody->setGravity(btVector3(g.x, g.y, g.z));
            }
            rigidBody->setDamping(rb->linearDrag, rb->angularDrag);
            rigidBody->setLinearFactor(btVector3(rb->freezePositionX ? 0.0f : 1.0f,
                                                 rb->freezePositionY ? 0.0f : 1.0f,
                                                 rb->freezePositionZ ? 0.0f : 1.0f));
            rigidBody->setAngularFactor(btVector3(rb->freezeRotationX ? 0.0f : 1.0f,
                                                  rb->freezeRotationY ? 0.0f : 1.0f,
                                                  rb->freezeRotationZ ? 0.0f : 1.0f));
            if (rb->isKinematic) {
                rigidBody->setCollisionFlags(rigidBody->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
                rigidBody->setActivationState(DISABLE_DEACTIVATION);
            }
        }
    }

    if (SlipPhysics::IsInitialized()) {
        if (m_Owner && m_Owner->HasComponent<RigidBody2DComponent>()) {
            // No añadir el collider 3D a Bullet si la entidad ya cuenta con un RigidBody 2D
            return;
        }
        SlipPhysics::Get().getWorld().addRigidBody(rigidBody);
    }

    if (m_Owner) {
        if (auto veh = m_Owner->GetComponent<VehicleComponent>()) {
            veh->InitVehicle();
        }
    }
}

void ColliderComponent::SyncTransformFromEntity()
{
    if (!rigidBody || !m_Owner) return;
    if (m_Owner->HasComponent<RigidBody2DComponent>()) return;

    bool isPlaying = SlipPhysics::IsInitialized() && SlipPhysics::Get().IsSimulating();
    if (isPlaying && rigidBody->getInvMass() > 0.0f && !(rigidBody->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT)) {
        // Optimización: si el cuerpo está en reposo (islas de simulación en sueño), evitar la sobrecarga de sincronización
        if (!rigidBody->isActive()) return;

        // Dinámico: actualizar posición y rotación de la entidad desde el rigid body de Bullet
        const btTransform& trans = rigidBody->getWorldTransform();
        glm::vec3 worldPos(trans.getOrigin().x(), trans.getOrigin().y(), trans.getOrigin().z());
        glm::vec3 worldRot = BulletMatrixToEulerDegrees(trans.getBasis());

        auto rbComp = m_Owner->GetComponent<RigidBodyComponent>();
        if (rbComp) {
            if (rbComp->freezeRotationX) worldRot.x = m_Owner->rotation.x;
            if (rbComp->freezeRotationY) worldRot.y = m_Owner->rotation.y;
            if (rbComp->freezeRotationZ) worldRot.z = m_Owner->rotation.z;
        }

        glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), glm::radians(worldRot.x), glm::vec3(1, 0, 0)) *
                           glm::rotate(glm::mat4(1.0f), glm::radians(worldRot.y), glm::vec3(0, 1, 0)) *
                           glm::rotate(glm::mat4(1.0f), glm::radians(worldRot.z), glm::vec3(0, 0, 1));
        glm::vec3 worldOffset = glm::vec3(rotMat * glm::vec4(centerOffset * m_Owner->scale, 0.0f));
        worldPos -= worldOffset;

        if (m_Owner->GetParent()) {
            glm::mat4 parentInv = glm::inverse(m_Owner->GetParent()->getMatrix());
            m_Owner->position = glm::vec3(parentInv * glm::vec4(worldPos, 1.0f));
            m_Owner->rotation = worldRot - m_Owner->GetParent()->rotation;
        } else {
            m_Owner->position = worldPos;
            m_Owner->rotation = worldRot;
        }

        if (m_Owner->GetTransform()) {
            m_Owner->GetTransform()->position = m_Owner->position;
            m_Owner->GetTransform()->rotation = m_Owner->rotation;
        }
    } else {
        // Estático o Kinemático (o durante Edición): sincronizar física con la posición y orientación de MUNDO real (soporta jerarquía de padres)
        bool isStaticBody = (rigidBody->isStaticObject() || (rigidBody->getInvMass() == 0.0f && !(rigidBody->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT)));
        if (isPlaying && isStaticBody) {
            return;
        }

        glm::mat4 worldMat = m_Owner->getMatrix();
        if (worldMat == lastSyncedMatrix) {
            return;
        }
        lastSyncedMatrix = worldMat;

        glm::vec3 worldScale(
            glm::length(glm::vec3(worldMat[0])),
            glm::length(glm::vec3(worldMat[1])),
            glm::length(glm::vec3(worldMat[2]))
        );
        if (worldScale.x < 0.0001f) worldScale.x = 1.0f;
        if (worldScale.y < 0.0001f) worldScale.y = 1.0f;
        if (worldScale.z < 0.0001f) worldScale.z = 1.0f;

        glm::mat3 rotM(
            glm::vec3(worldMat[0]) / worldScale.x,
            glm::vec3(worldMat[1]) / worldScale.y,
            glm::vec3(worldMat[2]) / worldScale.z
        );

        glm::vec3 worldPos = glm::vec3(worldMat[3]);
        glm::vec3 worldOffset = rotM * (centerOffset * worldScale);
        worldPos += worldOffset;

        glm::quat q = glm::quat_cast(rotM);
        btQuaternion btQ(q.x, q.y, q.z, q.w);

        btTransform trans;
        trans.setIdentity();
        trans.setOrigin(btVector3(worldPos.x, worldPos.y, worldPos.z));
        trans.setRotation(btQ);

        rigidBody->setWorldTransform(trans);
        if (motionState) motionState->setWorldTransform(trans);

        if (SlipPhysics::IsInitialized()) {
            SlipPhysics::Get().getWorld().updateSingleAabb(rigidBody);
            rigidBody->activate(true);
        }

        if (collisionShape) {
            btVector3 curScaling = collisionShape->getLocalScaling();
            if (std::abs(curScaling.x() - worldScale.x) > 0.0001f ||
                std::abs(curScaling.y() - worldScale.y) > 0.0001f ||
                std::abs(curScaling.z() - worldScale.z) > 0.0001f) {
                collisionShape->setLocalScaling(btVector3(worldScale.x, worldScale.y, worldScale.z));
                if (rigidBody->getInvMass() > 0.0f) {
                    btVector3 localInertia(0, 0, 0);
                    auto rb = m_Owner->GetComponent<RigidBodyComponent>();
                    float m = rb ? rb->mass : 1.0f;
                    collisionShape->calculateLocalInertia(m, localInertia);
                    rigidBody->setMassProps(m, localInertia);
                    rigidBody->updateInertiaTensor();
                }
                if (SlipPhysics::IsInitialized()) {
                    SlipPhysics::Get().getWorld().updateSingleAabb(rigidBody);
                }
            }
        }
    }
}

void ColliderComponent::OnInit()
{
    if (m_Owner) {
        auto rb = m_Owner->GetComponent<RigidBodyComponent>();
        if (rb) {
            rb->ApplyToCollider();
            return;
        }
    }
    RecreateRigidBody(0.0f, false);
}

void ColliderComponent::OnStart()
{
    if (!rigidBody) {
        RecreateRigidBody(0.0f, false);
    }
}

void ColliderComponent::OnUpdate(float deltaTime)
{
    SyncTransformFromEntity();
}

// ---------------------------------------------------------
// BoxColliderComponent
// ---------------------------------------------------------
BoxColliderComponent::BoxColliderComponent()
{
    m_Name = "BoxColliderComponent";
}

BoxColliderComponent::BoxColliderComponent(glm::vec3 boxSize)
    : size(boxSize)
{
    m_Name = "BoxColliderComponent";
}

void BoxColliderComponent::CreateOrUpdateShape()
{
    if (collisionShape) {
        delete collisionShape;
        collisionShape = nullptr;
    }
    collisionShape = new btBoxShape(btVector3(size.x * 0.5f, size.y * 0.5f, size.z * 0.5f));
    collisionShape->setMargin(0.01f);
    if (m_Owner) {
        collisionShape->setLocalScaling(btVector3(m_Owner->scale.x, m_Owner->scale.y, m_Owner->scale.z));
    }
    if (rigidBody) {
        rigidBody->setCollisionShape(collisionShape);
        if (rigidBody->getInvMass() > 0.0f) {
            btVector3 localInertia(0, 0, 0);
            auto rb = m_Owner ? m_Owner->GetComponent<RigidBodyComponent>() : nullptr;
            float m = rb ? rb->mass : 1.0f;
            collisionShape->calculateLocalInertia(m, localInertia);
            rigidBody->setMassProps(m, localInertia);
            rigidBody->updateInertiaTensor();
        }
    }
}

void BoxColliderComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// SphereColliderComponent
// ---------------------------------------------------------
SphereColliderComponent::SphereColliderComponent()
{
    m_Name = "SphereColliderComponent";
}

SphereColliderComponent::SphereColliderComponent(float r)
    : radius(r)
{
    m_Name = "SphereColliderComponent";
}

void SphereColliderComponent::CreateOrUpdateShape()
{
    if (collisionShape) {
        delete collisionShape;
        collisionShape = nullptr;
    }
    collisionShape = new btSphereShape(radius);
    collisionShape->setMargin(0.01f);
    if (m_Owner) {
        collisionShape->setLocalScaling(btVector3(m_Owner->scale.x, m_Owner->scale.y, m_Owner->scale.z));
    }
    if (rigidBody) {
        rigidBody->setCollisionShape(collisionShape);
        if (rigidBody->getInvMass() > 0.0f) {
            btVector3 localInertia(0, 0, 0);
            auto rb = m_Owner ? m_Owner->GetComponent<RigidBodyComponent>() : nullptr;
            float m = rb ? rb->mass : 1.0f;
            collisionShape->calculateLocalInertia(m, localInertia);
            rigidBody->setMassProps(m, localInertia);
            rigidBody->updateInertiaTensor();
        }
    }
}

void SphereColliderComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// CapsuleColliderComponent
// ---------------------------------------------------------
CapsuleColliderComponent::CapsuleColliderComponent()
{
    m_Name = "CapsuleColliderComponent";
}

CapsuleColliderComponent::CapsuleColliderComponent(float r, float h)
    : radius(r), height(h)
{
    m_Name = "CapsuleColliderComponent";
}

void CapsuleColliderComponent::CreateOrUpdateShape()
{
    if (collisionShape) {
        delete collisionShape;
        collisionShape = nullptr;
    }
    collisionShape = new btCapsuleShape(radius, height);
    collisionShape->setMargin(0.01f);
    if (m_Owner) {
        collisionShape->setLocalScaling(btVector3(m_Owner->scale.x, m_Owner->scale.y, m_Owner->scale.z));
    }
    if (rigidBody) {
        rigidBody->setCollisionShape(collisionShape);
        if (rigidBody->getInvMass() > 0.0f) {
            btVector3 localInertia(0, 0, 0);
            auto rb = m_Owner ? m_Owner->GetComponent<RigidBodyComponent>() : nullptr;
            float m = rb ? rb->mass : 1.0f;
            collisionShape->calculateLocalInertia(m, localInertia);
            rigidBody->setMassProps(m, localInertia);
            rigidBody->updateInertiaTensor();
        }
    }
}

void CapsuleColliderComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// MeshColliderComponent
// ---------------------------------------------------------
MeshColliderComponent::MeshColliderComponent()
{
    m_Name = "MeshColliderComponent";
}

MeshColliderComponent::~MeshColliderComponent()
{
    if (collisionShape) {
        delete collisionShape;
        collisionShape = nullptr;
    }
    if (m_TriangleMesh) {
        delete m_TriangleMesh;
        m_TriangleMesh = nullptr;
    }
}

void MeshColliderComponent::CreateOrUpdateShape()
{
    if (collisionShape) {
        delete collisionShape;
        collisionShape = nullptr;
    }
    if (m_TriangleMesh) {
        delete m_TriangleMesh;
        m_TriangleMesh = nullptr;
    }

    if (!m_Owner) return;
    auto meshComp = m_Owner->GetComponent<MeshComponent>();
    if (!meshComp || !meshComp->model) return;

    if (isConvex) {
        btConvexHullShape* convexShape = new btConvexHullShape();
        for (const auto& m : meshComp->model->meshes) {
            for (const auto& v : m.vertices) {
                convexShape->addPoint(btVector3(v.Position.x, v.Position.y, v.Position.z));
            }
        }
        collisionShape = convexShape;
    } else {
        m_TriangleMesh = new btTriangleMesh();
        size_t triCount = 0;
        for (const auto& m : meshComp->model->meshes) {
            for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
                uint32_t i0 = m.indices[i];
                uint32_t i1 = m.indices[i + 1];
                uint32_t i2 = m.indices[i + 2];
                if (i0 >= m.vertices.size() || i1 >= m.vertices.size() || i2 >= m.vertices.size()) continue;

                const auto& v1 = m.vertices[i0];
                const auto& v2 = m.vertices[i1];
                const auto& v3 = m.vertices[i2];
                m_TriangleMesh->addTriangle(
                    btVector3(v1.Position.x, v1.Position.y, v1.Position.z),
                    btVector3(v2.Position.x, v2.Position.y, v2.Position.z),
                    btVector3(v3.Position.x, v3.Position.y, v3.Position.z)
                );
                triCount++;
            }
        }
        if (triCount > 0) {
            collisionShape = new btBvhTriangleMeshShape(m_TriangleMesh, true, true);
        } else {
            delete m_TriangleMesh;
            m_TriangleMesh = nullptr;
            collisionShape = nullptr;
        }
    }

    if (collisionShape && m_Owner) {
        collisionShape->setLocalScaling(btVector3(m_Owner->scale.x, m_Owner->scale.y, m_Owner->scale.z));
        collisionShape->setMargin(0.01f);
    }

    if (rigidBody && collisionShape) {
        rigidBody->setCollisionShape(collisionShape);
        if (rigidBody->getInvMass() > 0.0f) {
            btVector3 localInertia(0, 0, 0);
            auto rb = m_Owner ? m_Owner->GetComponent<RigidBodyComponent>() : nullptr;
            float m = rb ? rb->mass : 1.0f;
            collisionShape->calculateLocalInertia(m, localInertia);
            rigidBody->setMassProps(m, localInertia);
            rigidBody->updateInertiaTensor();
        }
    }
}

void MeshColliderComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// RigidBodyComponent
// ---------------------------------------------------------
RigidBodyComponent::RigidBodyComponent()
{
    m_Name = "RigidBodyComponent";
}

void RigidBodyComponent::OnInit()
{
    ApplyToCollider();
}

btRigidBody* RigidBodyComponent::GetBulletRigidBody() const
{
    if (!m_Owner) return nullptr;
    auto collider = m_Owner->GetComponent<ColliderComponent>();
    return collider ? collider->rigidBody : nullptr;
}

void RigidBodyComponent::ApplyToCollider()
{
    if (!m_Owner) return;

    auto collider = m_Owner->GetComponent<ColliderComponent>();
    if (collider) {
        collider->RecreateRigidBody(isKinematic ? 0.0f : mass, isKinematic);
        if (collider->rigidBody) {
            collider->rigidBody->setFriction(friction);
            collider->rigidBody->setRollingFriction(rollingFriction);
            collider->rigidBody->setRestitution(restitution);

            if (!useGravity) {
                collider->rigidBody->setGravity(btVector3(0, 0, 0));
            } else {
                glm::vec3 g = SlipPhysics::Get().getGravity();
                collider->rigidBody->setGravity(btVector3(g.x, g.y, g.z));
            }

            collider->rigidBody->setDamping(linearDrag, angularDrag);

            btVector3 linFactor(freezePositionX ? 0.0f : 1.0f,
                                freezePositionY ? 0.0f : 1.0f,
                                freezePositionZ ? 0.0f : 1.0f);
            collider->rigidBody->setLinearFactor(linFactor);

            btVector3 angFactor(freezeRotationX ? 0.0f : 1.0f,
                                freezeRotationY ? 0.0f : 1.0f,
                                freezeRotationZ ? 0.0f : 1.0f);
            collider->rigidBody->setAngularFactor(angFactor);

            if (isKinematic) {
                collider->rigidBody->setCollisionFlags(collider->rigidBody->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
                collider->rigidBody->setActivationState(DISABLE_DEACTIVATION);
            }
        }
    }
}

void RigidBodyComponent::AddForce(const glm::vec3& force)
{
    btRigidBody* rb = GetBulletRigidBody();
    if (rb) {
        rb->activate(true);
        rb->applyCentralForce(btVector3(force.x, force.y, force.z));
    }
}

void RigidBodyComponent::AddImpulse(const glm::vec3& impulse)
{
    btRigidBody* rb = GetBulletRigidBody();
    if (rb) {
        rb->activate(true);
        rb->applyCentralImpulse(btVector3(impulse.x, impulse.y, impulse.z));
    }
}

void RigidBodyComponent::AddTorque(const glm::vec3& torque)
{
    btRigidBody* rb = GetBulletRigidBody();
    if (rb) {
        rb->activate(true);
        rb->applyTorque(btVector3(torque.x, torque.y, torque.z));
    }
}

void RigidBodyComponent::SetLinearVelocity(const glm::vec3& velocity)
{
    btRigidBody* rb = GetBulletRigidBody();
    if (rb) {
        rb->activate(true);
        rb->setLinearVelocity(btVector3(velocity.x, velocity.y, velocity.z));
    }
}

glm::vec3 RigidBodyComponent::GetLinearVelocity() const
{
    btRigidBody* rb = GetBulletRigidBody();
    if (rb) {
        const btVector3& vel = rb->getLinearVelocity();
        return glm::vec3(vel.x(), vel.y(), vel.z());
    }
    return glm::vec3(0.0f);
}

void RigidBodyComponent::SetAngularVelocity(const glm::vec3& velocity)
{
    btRigidBody* rb = GetBulletRigidBody();
    if (rb) {
        rb->activate(true);
        rb->setAngularVelocity(btVector3(velocity.x, velocity.y, velocity.z));
    }
}

glm::vec3 RigidBodyComponent::GetAngularVelocity() const
{
    btRigidBody* rb = GetBulletRigidBody();
    if (rb) {
        const btVector3& vel = rb->getAngularVelocity();
        return glm::vec3(vel.x(), vel.y(), vel.z());
    }
    return glm::vec3(0.0f);
}

void RigidBodyComponent::SetRotation(const glm::vec3& rotation)
{
    btRigidBody* rb = GetBulletRigidBody();
    if (rb) {
        rb->activate(true);
        btTransform trans = rb->getWorldTransform();
        glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), glm::radians(rotation.x), glm::vec3(1, 0, 0)) *
                           glm::rotate(glm::mat4(1.0f), glm::radians(rotation.y), glm::vec3(0, 1, 0)) *
                           glm::rotate(glm::mat4(1.0f), glm::radians(rotation.z), glm::vec3(0, 0, 1));
        btMatrix3x3 basis(
            rotMat[0][0], rotMat[1][0], rotMat[2][0],
            rotMat[0][1], rotMat[1][1], rotMat[2][1],
            rotMat[0][2], rotMat[1][2], rotMat[2][2]
        );
        trans.setBasis(basis);
        rb->setWorldTransform(trans);
        if (rb->getMotionState()) {
            rb->getMotionState()->setWorldTransform(trans);
        }
    }
    if (m_Owner) {
        m_Owner->rotation = rotation;
        if (m_Owner->GetTransform()) {
            m_Owner->GetTransform()->rotation = rotation;
        }
    }
}

void RigidBodyComponent::SetPosition(const glm::vec3& position)
{
    btRigidBody* rb = GetBulletRigidBody();
    if (rb) {
        rb->activate(true);
        btTransform trans = rb->getWorldTransform();
        trans.setOrigin(btVector3(position.x, position.y, position.z));
        rb->setWorldTransform(trans);
        if (rb->getMotionState()) {
            rb->getMotionState()->setWorldTransform(trans);
        }
    }
    if (m_Owner) {
        m_Owner->position = position;
        if (m_Owner->GetTransform()) {
            m_Owner->GetTransform()->position = position;
        }
    }
}

void RigidBodyComponent::SetFriction(float f)
{
    friction = f;
    btRigidBody* rb = GetBulletRigidBody();
    if (rb) {
        rb->setFriction(f);
    }
}

float RigidBodyComponent::GetFriction() const
{
    btRigidBody* rb = GetBulletRigidBody();
    return rb ? rb->getFriction() : friction;
}

void RigidBodyComponent::SetRollingFriction(float rf)
{
    rollingFriction = rf;
    btRigidBody* rb = GetBulletRigidBody();
    if (rb) {
        rb->setRollingFriction(rf);
    }
}

float RigidBodyComponent::GetRollingFriction() const
{
    btRigidBody* rb = GetBulletRigidBody();
    return rb ? rb->getRollingFriction() : rollingFriction;
}

void RigidBodyComponent::SetRestitution(float r)
{
    restitution = r;
    btRigidBody* rb = GetBulletRigidBody();
    if (rb) {
        rb->setRestitution(r);
    }
}

float RigidBodyComponent::GetRestitution() const
{
    btRigidBody* rb = GetBulletRigidBody();
    return rb ? rb->getRestitution() : restitution;
}

void RigidBodyComponent::OnStart()
{
    ApplyToCollider();
}

void RigidBodyComponent::OnUpdate(float deltaTime)
{
}

void RigidBodyComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// Clases de compatibilidad
// ---------------------------------------------------------
void MaterialComponent::bind()
{
    if (mat) {
        glm::mat4 id(1.0f);
        mat->bind(id);
    }
}

void CollisionComponent::init()
{
    if (!collision && std::strlen(colPath) != 0) {
        collision = new SlipCollision(colPath);
        collision->init();
    }
}

// ---------------------------------------------------------
// LightComponent
// ---------------------------------------------------------
LightComponent::LightComponent(LightType lightType)
    : type(lightType)
{
    m_Name = "LightComponent";
}

void LightComponent::OnDrawInspectorUI()
{
}

DDGIVolumeComponent::DDGIVolumeComponent()
{
    m_Name = "DDGIVolumeComponent";
}

void DDGIVolumeComponent::OnDrawInspectorUI()
{
}

ReflectionProbeComponent::ReflectionProbeComponent()
{
    m_Name = "ReflectionProbeComponent";
}

ReflectionProbeComponent::~ReflectionProbeComponent()
{
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (cubemap != 0) {
        glDeleteTextures(1, &cubemap);
        cubemap = 0;
    }
#endif
}

void ReflectionProbeComponent::OnDrawInspectorUI()
{
}

bool ReflectionProbeComponent::IsInside(const glm::vec3& worldPos, const glm::vec3& probeCenter) const
{
    glm::vec3 half = glm::max(size * 0.5f, glm::vec3(0.01f));
    glm::vec3 bmin = probeCenter - half;
    glm::vec3 bmax = probeCenter + half;
    return (worldPos.x >= bmin.x && worldPos.x <= bmax.x &&
            worldPos.y >= bmin.y && worldPos.y <= bmax.y &&
            worldPos.z >= bmin.z && worldPos.z <= bmax.z);
}

void ReflectionProbeComponent::GetBoxBounds(const glm::vec3& probeCenter, glm::vec3& outMin, glm::vec3& outMax) const
{
    glm::vec3 half = glm::max(size * 0.5f, glm::vec3(0.01f));
    outMin = probeCenter - half;
    outMax = probeCenter + half;
}

PostProcessVolumeComponent::PostProcessVolumeComponent()
{
    m_Name = "PostProcessVolumeComponent";
}

void PostProcessVolumeComponent::SerializeTo(std::ostream& out) const
{
    out << "  PostProcessVolumeComponent:\n";
    out << "    Global: " << (isGlobal ? 1 : 0) << "\n";
    out << "    Priority: " << priority << "\n";
    out << "    Weight: " << weight << "\n";
    out << "    Size: " << size.x << " " << size.y << " " << size.z << "\n";
    out << "    BlendDistance: " << blendDistance << "\n";
    out << "    BloomOverride: " << (bloomOverride ? 1 : 0) << "\n";
    out << "    Bloom: " << (bloomEnabled ? 1 : 0) << "\n";
    out << "    BloomIntensity: " << bloomIntensity << "\n";
    out << "    BloomThreshold: " << bloomThreshold << "\n";
    out << "    BloomSoftKnee: " << bloomSoftKnee << "\n";
    out << "    BloomRadius: " << bloomRadius << "\n";
    out << "    BloomTint: " << bloomTint.r << " " << bloomTint.g << " " << bloomTint.b << "\n";
    out << "    LensOverride: " << (lensOverride ? 1 : 0) << "\n";
    out << "    Lens: " << (lensEnabled ? 1 : 0) << "\n";
    out << "    LensIntensity: " << lensIntensity << "\n";
    out << "    LensThreshold: " << lensThreshold << "\n";
    out << "    LensStreak: " << lensStreak << "\n";
    out << "    LensHalo: " << lensHalo << "\n";
    out << "    AoOverride: " << (aoOverride ? 1 : 0) << "\n";
    out << "    Ao: " << (aoEnabled ? 1 : 0) << "\n";
    out << "    AoRadius: " << aoRadius << "\n";
    out << "    AoBias: " << aoBias << "\n";
    out << "    AoIntensity: " << aoIntensity << "\n";
    out << "    AoPower: " << aoPower << "\n";
    out << "    VignetteOverride: " << (vignetteOverride ? 1 : 0) << "\n";
    out << "    Vignette: " << (vignetteEnabled ? 1 : 0) << "\n";
    out << "    VignetteIntensity: " << vignetteIntensity << "\n";
    out << "    VignetteSmoothness: " << vignetteSmoothness << "\n";
    out << "    ExposureOverride: " << (exposureOverride ? 1 : 0) << "\n";
    out << "    Exposure: " << (exposureEnabled ? 1 : 0) << "\n";
    out << "    ExposureMin: " << exposureMin << "\n";
    out << "    ExposureMax: " << exposureMax << "\n";
    out << "    ExposureCompensation: " << exposureCompensation << "\n";
    out << "    ExposureSpeed: " << exposureSpeed << "\n";
    out << "    ColorOverride: " << (colorOverride ? 1 : 0) << "\n";
    out << "    Contrast: " << contrast << "\n";
    out << "    Saturation: " << saturation << "\n";
    out << "    Temperature: " << temperature << "\n";
    out << "    Lift: " << lift.r << " " << lift.g << " " << lift.b << "\n";
    out << "    Gamma: " << gamma.r << " " << gamma.g << " " << gamma.b << "\n";
    out << "    Gain: " << gain.r << " " << gain.g << " " << gain.b << "\n";
    out << "    ColorFilter: " << colorFilter.r << " " << colorFilter.g << " " << colorFilter.b << "\n";
    out << "    Tonemap: " << tonemap << "\n";
    out << "    BlurOverride: " << (blurOverride ? 1 : 0) << "\n";
    out << "    BlurRadius: " << blurRadius << "\n";
    out << "    RadialOverride: " << (radialOverride ? 1 : 0) << "\n";
    out << "    RadialStrength: " << radialStrength << "\n";
    out << "    RadialCenter: " << radialCenter.x << " " << radialCenter.y << "\n";
    out << "    FogOverride: " << (fogOverride ? 1 : 0) << "\n";
    out << "    Fog: " << (fogEnabled ? 1 : 0) << "\n";
    out << "    FogDensity: " << fogDensity << "\n";
    out << "    FogHeightFalloff: " << fogHeightFalloff << "\n";
    out << "    FogHeight: " << fogHeight << "\n";
    out << "    FogStartDistance: " << fogStartDistance << "\n";
    out << "    FogMaxDistance: " << fogMaxDistance << "\n";
    out << "    FogColor: " << fogColor.r << " " << fogColor.g << " " << fogColor.b << "\n";
    out << "    FogSunScattering: " << fogSunScattering << "\n";
    out << "    FogSkyBlend: " << fogSkyBlend << "\n";
}

void PostProcessVolumeComponent::DeserializeKey(const std::string& key, const std::string& value)
{
    auto flag = [](const std::string& v) { return v == "1" || v == "true"; };
    auto num = [](const std::string& v) {
        float x = 0.0f;
        std::stringstream(v) >> x;
        return x;
    };
    auto vec3 = [](const std::string& v) {
        glm::vec3 out(0.0f);
        std::stringstream(v) >> out.x >> out.y >> out.z;
        return out;
    };
    if (key == "Global") isGlobal = flag(value);
    else if (key == "Priority") priority = (int)num(value);
    else if (key == "Weight") weight = num(value);
    else if (key == "Size") size = vec3(value);
    else if (key == "BlendDistance") blendDistance = num(value);
    else if (key == "BloomOverride") bloomOverride = flag(value);
    else if (key == "Bloom") bloomEnabled = flag(value);
    else if (key == "BloomIntensity") bloomIntensity = num(value);
    else if (key == "BloomThreshold") bloomThreshold = num(value);
    else if (key == "BloomSoftKnee") bloomSoftKnee = num(value);
    else if (key == "BloomRadius") bloomRadius = num(value);
    else if (key == "BloomTint") bloomTint = vec3(value);
    else if (key == "LensOverride") lensOverride = flag(value);
    else if (key == "Lens") lensEnabled = flag(value);
    else if (key == "LensIntensity") lensIntensity = num(value);
    else if (key == "LensThreshold") lensThreshold = num(value);
    else if (key == "LensStreak") lensStreak = num(value);
    else if (key == "LensHalo") lensHalo = num(value);
    else if (key == "AoOverride") aoOverride = flag(value);
    else if (key == "Ao") aoEnabled = flag(value);
    else if (key == "AoRadius") aoRadius = num(value);
    else if (key == "AoBias") aoBias = num(value);
    else if (key == "AoIntensity") aoIntensity = num(value);
    else if (key == "AoPower") aoPower = num(value);
    else if (key == "VignetteOverride") vignetteOverride = flag(value);
    else if (key == "Vignette") vignetteEnabled = flag(value);
    else if (key == "VignetteIntensity") vignetteIntensity = num(value);
    else if (key == "VignetteSmoothness") vignetteSmoothness = num(value);
    else if (key == "ExposureOverride") exposureOverride = flag(value);
    else if (key == "Exposure") exposureEnabled = flag(value);
    else if (key == "ExposureMin") exposureMin = num(value);
    else if (key == "ExposureMax") exposureMax = num(value);
    else if (key == "ExposureCompensation") exposureCompensation = num(value);
    else if (key == "ExposureSpeed") exposureSpeed = num(value);
    else if (key == "ColorOverride") colorOverride = flag(value);
    else if (key == "Contrast") contrast = num(value);
    else if (key == "Saturation") saturation = num(value);
    else if (key == "Temperature") temperature = num(value);
    else if (key == "Lift") lift = vec3(value);
    else if (key == "Gamma") gamma = vec3(value);
    else if (key == "Gain") gain = vec3(value);
    else if (key == "ColorFilter") colorFilter = vec3(value);
    else if (key == "Tonemap") tonemap = (int)num(value);
    else if (key == "BlurOverride") blurOverride = flag(value);
    else if (key == "BlurRadius") blurRadius = num(value);
    else if (key == "RadialOverride") radialOverride = flag(value);
    else if (key == "RadialStrength") radialStrength = num(value);
    else if (key == "RadialCenter") {
        std::stringstream(value) >> radialCenter.x >> radialCenter.y;
    }
    else if (key == "FogOverride") fogOverride = flag(value);
    else if (key == "Fog") fogEnabled = flag(value);
    else if (key == "FogDensity") fogDensity = num(value);
    else if (key == "FogHeightFalloff") fogHeightFalloff = num(value);
    else if (key == "FogHeight") fogHeight = num(value);
    else if (key == "FogStartDistance") fogStartDistance = num(value);
    else if (key == "FogMaxDistance") fogMaxDistance = num(value);
    else if (key == "FogColor") fogColor = vec3(value);
    else if (key == "FogSunScattering") fogSunScattering = num(value);
    else if (key == "FogSkyBlend") fogSkyBlend = num(value);
}

namespace {
float PostVolumeInfluence(const PostProcessVolumeComponent& volume, const glm::vec3& cameraPos)
{
    if (!volume.IsEnabled()) return 0.0f;
    float w = glm::clamp(volume.weight, 0.0f, 1.0f);
    if (w <= 0.0f) return 0.0f;
    if (volume.isGlobal) return w;

    glm::vec3 center = volume.GetOwner() ? volume.GetOwner()->position : glm::vec3(0.0f);
    glm::vec3 half = volume.size * 0.5f;
    half.x = half.x < 0.001f ? 0.001f : half.x;
    half.y = half.y < 0.001f ? 0.001f : half.y;
    half.z = half.z < 0.001f ? 0.001f : half.z;
    glm::vec3 delta = glm::abs(cameraPos - center);
    if (delta.x > half.x || delta.y > half.y || delta.z > half.z) return 0.0f;

    float fade = 1.0f;
    float blend = volume.blendDistance > 0.0f ? volume.blendDistance : 0.0f;
    glm::vec3 inner = half - glm::vec3(blend);
    inner.x = inner.x > 0.0f ? inner.x : 0.0f;
    inner.y = inner.y > 0.0f ? inner.y : 0.0f;
    inner.z = inner.z > 0.0f ? inner.z : 0.0f;
    for (int i = 0; i < 3; ++i) {
        float span = half[i] - inner[i];
        if (span > 0.0001f && delta[i] > inner[i]) {
            float axisFade = 1.0f - (delta[i] - inner[i]) / span;
            if (axisFade < fade) fade = axisFade;
        }
    }
    return w * fade;
}
}

PostProcessSettings PostProcessVolumeComponent::Resolve(SlipLevel* level, const glm::vec3& cameraPos)
{
    PostProcessSettings out;
    if (!level) return out;

    struct Item {
        PostProcessVolumeComponent* volume;
        float weight;
    };
    std::vector<Item> items;
    for (SlipEntity* entity : level->GetEntities()) {
        if (!entity) continue;
        auto volume = entity->GetComponent<PostProcessVolumeComponent>();
        if (!volume) continue;
        float influence = PostVolumeInfluence(*volume, cameraPos);
        if (influence <= 0.001f) continue;
        items.push_back({ volume.get(), influence });
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.volume->priority < b.volume->priority;
    });

    float exposureBlend = 0.0f;
    for (const Item& item : items) {
        const PostProcessVolumeComponent& volume = *item.volume;
        float w = item.weight;
        if (volume.bloomOverride) {
            out.bloomIntensity = glm::mix(out.bloomIntensity, volume.bloomEnabled ? volume.bloomIntensity : 0.0f, w);
            out.bloomThreshold = glm::mix(out.bloomThreshold, volume.bloomThreshold, w);
            out.bloomSoftKnee = glm::mix(out.bloomSoftKnee, volume.bloomSoftKnee, w);
            out.bloomRadius = glm::mix(out.bloomRadius, volume.bloomRadius, w);
            out.bloomTint = glm::mix(out.bloomTint, volume.bloomTint, w);
        }
        if (volume.lensOverride) {
            out.lensIntensity = glm::mix(out.lensIntensity, volume.lensEnabled ? volume.lensIntensity : 0.0f, w);
            out.lensThreshold = glm::mix(out.lensThreshold, volume.lensThreshold, w);
            out.lensStreak = glm::mix(out.lensStreak, volume.lensStreak, w);
            out.lensHalo = glm::mix(out.lensHalo, volume.lensHalo, w);
        }
        if (volume.aoOverride) {
            out.ssaoIntensity = glm::mix(out.ssaoIntensity, volume.aoEnabled ? volume.aoIntensity : 0.0f, w);
            out.ssaoRadius = glm::mix(out.ssaoRadius, volume.aoRadius, w);
            out.ssaoBias = glm::mix(out.ssaoBias, volume.aoBias, w);
            out.ssaoPower = glm::mix(out.ssaoPower, volume.aoPower, w);
        }
        if (volume.vignetteOverride) {
            out.vignetteIntensity = glm::mix(out.vignetteIntensity, volume.vignetteEnabled ? volume.vignetteIntensity : 0.0f, w);
            out.vignetteSmoothness = glm::mix(out.vignetteSmoothness, volume.vignetteSmoothness, w);
        }
        if (volume.exposureOverride) {
            exposureBlend = glm::mix(exposureBlend, volume.exposureEnabled ? 1.0f : 0.0f, w);
            out.exposureMin = glm::mix(out.exposureMin, volume.exposureMin, w);
            out.exposureMax = glm::mix(out.exposureMax, volume.exposureMax, w);
            out.exposureCompensation = glm::mix(out.exposureCompensation, volume.exposureCompensation, w);
            out.exposureSpeed = glm::mix(out.exposureSpeed, volume.exposureSpeed, w);
        }
        if (volume.colorOverride) {
            out.contrast = glm::mix(out.contrast, volume.contrast, w);
            out.saturation = glm::mix(out.saturation, volume.saturation, w);
            out.temperature = glm::mix(out.temperature, volume.temperature, w);
            out.lift = glm::mix(out.lift, volume.lift, w);
            out.gamma = glm::mix(out.gamma, volume.gamma, w);
            out.gain = glm::mix(out.gain, volume.gain, w);
            out.colorFilter = glm::mix(out.colorFilter, volume.colorFilter, w);
            if (w > 0.5f) out.tonemap = volume.tonemap;
        }
        if (volume.blurOverride) {
            out.blurRadius = glm::mix(out.blurRadius, volume.blurRadius, w);
        }
        if (volume.radialOverride) {
            out.radialBlurStrength = glm::mix(out.radialBlurStrength, volume.radialStrength, w);
            out.radialBlurCenter = glm::mix(out.radialBlurCenter, volume.radialCenter, w);
        }
        if (volume.fogOverride) {
            out.fog = volume.fogEnabled;
            out.fogDensity = glm::mix(out.fogDensity, volume.fogDensity, w);
            out.fogHeightFalloff = glm::mix(out.fogHeightFalloff, volume.fogHeightFalloff, w);
            out.fogHeight = glm::mix(out.fogHeight, volume.fogHeight, w);
            out.fogStartDistance = glm::mix(out.fogStartDistance, volume.fogStartDistance, w);
            out.fogMaxDistance = glm::mix(out.fogMaxDistance, volume.fogMaxDistance, w);
            out.fogColor = glm::mix(out.fogColor, volume.fogColor, w);
            out.fogSunScattering = glm::mix(out.fogSunScattering, volume.fogSunScattering, w);
            out.fogSkyBlend = glm::mix(out.fogSkyBlend, volume.fogSkyBlend, w);
        }
    }

    out.bloom = out.bloomIntensity > 0.001f;
    out.lensBloom = out.lensIntensity > 0.001f;
    out.ssao = out.ssaoIntensity > 0.001f;
    out.autoExposure = exposureBlend > 0.5f;
    return out;
}

// ---------------------------------------------------------
// ScriptComponent
// ---------------------------------------------------------
struct ScriptComponent::Impl {
    sol::environment env;
    sol::protected_function fnStart;
    sol::protected_function fnUpdate;
    sol::protected_function fnCollisionEnter;
    sol::protected_function fnCollisionStay;
    sol::protected_function fnCollisionExit;
    sol::protected_function fnTriggerEnter;
    sol::protected_function fnTriggerStay;
    sol::protected_function fnTriggerExit;
    sol::table scriptTable;
    bool hasTable = false;
};

ScriptComponent::ScriptComponent()
    : m_Impl(std::make_unique<Impl>())
{
    m_Name = "ScriptComponent";
}

ScriptComponent::ScriptComponent(const std::string& path)
    : scriptPath(path), m_Impl(std::make_unique<Impl>())
{
    m_Name = "ScriptComponent";
    ReloadScript();
}

ScriptComponent::~ScriptComponent() = default;

std::string ScriptComponent::GetScriptFileName() const {
    if (scriptPath.empty()) return "Ninguno";
    return SlipFileSystem::GetFileName(scriptPath);
}

std::string ScriptComponent::GetScriptNameWithoutExtension() const {
    if (scriptPath.empty()) return "Ninguno";
    return SlipFileSystem::GetFileNameWithoutExtension(scriptPath);
}

void* ScriptComponent::GetScriptTableRaw() const {
    if (m_Impl && m_Impl->hasTable && m_Impl->scriptTable.valid()) {
        return &m_Impl->scriptTable;
    }
    return nullptr;
}

void ScriptComponent::SetPropertyValue(const std::string& name, float val) {
    for (auto& p : m_Properties) {
        if (p.name == name) {
            p.floatValue = val;
            if (isCompiled && m_Impl && m_Impl->hasTable) m_Impl->scriptTable[name] = val;
            return;
        }
    }
    ScriptProperty p; p.name = name; p.type = ScriptPropertyType::Float; p.floatValue = val;
    m_Properties.push_back(p);
    if (isCompiled && m_Impl && m_Impl->hasTable) m_Impl->scriptTable[name] = val;
}

void ScriptComponent::SetPropertyValue(const std::string& name, int val) {
    for (auto& p : m_Properties) {
        if (p.name == name) {
            p.intValue = val;
            p.floatValue = static_cast<float>(val);
            if (isCompiled && m_Impl && m_Impl->hasTable) m_Impl->scriptTable[name] = val;
            return;
        }
    }
    ScriptProperty p; p.name = name; p.type = ScriptPropertyType::Int; p.intValue = val; p.floatValue = static_cast<float>(val);
    m_Properties.push_back(p);
    if (isCompiled && m_Impl && m_Impl->hasTable) m_Impl->scriptTable[name] = val;
}

void ScriptComponent::SetPropertyValue(const std::string& name, bool val) {
    for (auto& p : m_Properties) {
        if (p.name == name) {
            p.boolValue = val;
            if (isCompiled && m_Impl && m_Impl->hasTable) m_Impl->scriptTable[name] = val;
            return;
        }
    }
    ScriptProperty p; p.name = name; p.type = ScriptPropertyType::Bool; p.boolValue = val;
    m_Properties.push_back(p);
    if (isCompiled && m_Impl && m_Impl->hasTable) m_Impl->scriptTable[name] = val;
}

void ScriptComponent::SetPropertyValue(const std::string& name, const std::string& val) {
    for (auto& p : m_Properties) {
        if (p.name == name) {
            p.stringValue = val;
            if (isCompiled && m_Impl && m_Impl->hasTable) m_Impl->scriptTable[name] = val;
            return;
        }
    }
    ScriptProperty p; p.name = name; p.type = ScriptPropertyType::String; p.stringValue = val;
    m_Properties.push_back(p);
    if (isCompiled && m_Impl && m_Impl->hasTable) m_Impl->scriptTable[name] = val;
}

void ScriptComponent::SetPropertyValue(const std::string& name, const glm::vec3& val) {
    for (auto& p : m_Properties) {
        if (p.name == name) {
            p.vec3Value = val;
            if (isCompiled && m_Impl && m_Impl->hasTable) m_Impl->scriptTable[name] = val;
            return;
        }
    }
    ScriptProperty p; p.name = name; p.type = ScriptPropertyType::Vec3; p.vec3Value = val;
    m_Properties.push_back(p);
    if (isCompiled && m_Impl && m_Impl->hasTable) m_Impl->scriptTable[name] = val;
}

void ScriptComponent::SyncPropertiesFromLua() {
    if (!m_Impl || !m_Impl->hasTable) return;

    try {
        for (const auto& pair : m_Impl->scriptTable) {
            if (!pair.first.is<std::string>()) continue;
            std::string key = pair.first.as<std::string>();

            // Ignorar claves internas y funciones de ciclo de vida
            if (key.empty() || key[0] == '_' ||
                key == "entity" || key == "name" || key == "id" || key == "GetTransform" || key == "GetRigidBody" ||
                key == "OnStart" || key == "OnUpdate" ||
                key == "OnCollisionEnter" || key == "OnCollisionStay" || key == "OnCollisionExit" ||
                key == "OnTriggerEnter" || key == "OnTriggerStay" || key == "OnTriggerExit") {
                continue;
            }

            sol::object val = pair.second;
            sol::type t = val.get_type();
            if (t == sol::type::function || t == sol::type::table || t == sol::type::nil || t == sol::type::none) {
                continue;
            }

            auto it = std::find_if(m_Properties.begin(), m_Properties.end(), [&](const ScriptProperty& p) {
                return p.name == key;
            });

            if (it != m_Properties.end()) {
                continue; // Conservar el override existente
            }

            ScriptProperty prop;
            prop.name = key;

            if (t == sol::type::boolean) {
                prop.type = ScriptPropertyType::Bool;
                prop.boolValue = val.as<bool>();
            } else if (t == sol::type::number) {
                double num = val.as<double>();
                if (std::floor(num) == num && num >= -2147483648.0 && num <= 2147483647.0 && val.is<int>()) {
                    prop.type = ScriptPropertyType::Int;
                    prop.intValue = val.as<int>();
                    prop.floatValue = static_cast<float>(prop.intValue);
                } else {
                    prop.type = ScriptPropertyType::Float;
                    prop.floatValue = static_cast<float>(num);
                }
            } else if (t == sol::type::string) {
                prop.type = ScriptPropertyType::String;
                prop.stringValue = val.as<std::string>();
            } else if (t == sol::type::userdata && val.is<glm::vec3>()) {
                prop.type = ScriptPropertyType::Vec3;
                prop.vec3Value = val.as<glm::vec3>();
            } else {
                continue;
            }

            m_Properties.push_back(prop);
        }

        PushPropertiesToLua();
    } catch (const std::exception& e) {
        SlipConsole::LogWarning("[ScriptComponent] Error sincronizando propiedades: " + std::string(e.what()));
    }
}

void ScriptComponent::PushPropertiesToLua() {
    if (!m_Impl || !m_Impl->hasTable) return;
    for (const auto& prop : m_Properties) {
        switch (prop.type) {
            case ScriptPropertyType::Float:
                m_Impl->scriptTable[prop.name] = prop.floatValue;
                break;
            case ScriptPropertyType::Int:
                m_Impl->scriptTable[prop.name] = prop.intValue;
                break;
            case ScriptPropertyType::Bool:
                m_Impl->scriptTable[prop.name] = prop.boolValue;
                break;
            case ScriptPropertyType::String:
                m_Impl->scriptTable[prop.name] = prop.stringValue;
                break;
            case ScriptPropertyType::Vec3:
                m_Impl->scriptTable[prop.name] = prop.vec3Value;
                break;
        }
    }
}

void ScriptComponent::SetScriptPath(const std::string& path) {
    scriptPath = path;
    ReloadScript();
}

bool ScriptComponent::ReloadScript() {
    isCompiled = false;
    lastError.clear();

    if (scriptPath.empty()) {
        return false;
    }

    std::string fullPath = scriptPath;
    if (!SlipFileSystem::Exists(fullPath)) {
        std::string assetsDir = SlipLuaEngine::Get().GetAssetsDirectory();
        if (!assetsDir.empty()) {
            fullPath = SlipFileSystem::CombinePath(assetsDir, scriptPath);
        }
    }

    if (!SlipFileSystem::Exists(fullPath)) {
        lastError = "Archivo no encontrado: " + scriptPath;
        return false;
    }

    auto& luaEngine = SlipLuaEngine::Get();
    auto& state = luaEngine.GetState();

    try {
        m_Impl->env = sol::environment(state, sol::create, state.globals());
        m_Impl->env["self"] = m_Owner;
        m_Impl->env["this"] = m_Owner;

        auto result = state.safe_script_file(fullPath, m_Impl->env);
        if (!result.valid()) {
            sol::error err = result;
            lastError = err.what();
            luaEngine.ReportError(lastError);
            return false;
        }

        m_Impl->hasTable = false;
        if (result.get_type() == sol::type::table) {
            m_Impl->scriptTable = result.get<sol::table>();
            m_Impl->hasTable = true;
        } else {
            // Detección automática estilo Roblox: si el script definió la tabla en el entorno global/env
            std::string scriptStem = GetScriptNameWithoutExtension();
            if (!scriptStem.empty() && m_Impl->env[scriptStem].valid() && m_Impl->env[scriptStem].get_type() == sol::type::table) {
                m_Impl->scriptTable = m_Impl->env[scriptStem];
                m_Impl->hasTable = true;
            } else if (m_Impl->env["Script"].valid() && m_Impl->env["Script"].get_type() == sol::type::table) {
                m_Impl->scriptTable = m_Impl->env["Script"];
                m_Impl->hasTable = true;
            }
        }

        if (m_Impl->hasTable) {
            if (m_Owner) {
                sol::table meta = state.create_table();
                meta[sol::meta_function::index] = [this](sol::table tbl, sol::object key, sol::this_state s) -> sol::object {
                    if (!m_Owner || !key.is<std::string>()) return sol::nil;
                    std::string k = key.as<std::string>();
                    if (k == "name") return sol::make_object(s, m_Owner->GetName());
                    if (k == "id") return sol::make_object(s, m_Owner->GetID());
                    if (k == "entity") return sol::make_object(s, m_Owner);
                    return sol::nil;
                };
                m_Impl->scriptTable[sol::metatable_key] = meta;
                m_Impl->scriptTable["entity"] = m_Owner;
                m_Impl->scriptTable["name"] = m_Owner->GetName();
                m_Impl->scriptTable["GetTransform"] = [this](sol::table, sol::this_state s) -> sol::object {
                    if (!m_Owner) return sol::nil;
                    auto tc = m_Owner->GetComponent<TransformComponent>();
                    return tc ? sol::make_object(s, tc.get()) : sol::nil;
                };
                m_Impl->scriptTable["GetRigidBody"] = [this](sol::table, sol::this_state s) -> sol::object {
                    if (!m_Owner) return sol::nil;
                    auto rb = m_Owner->GetComponent<RigidBodyComponent>();
                    return rb ? sol::make_object(s, rb.get()) : sol::nil;
                };
                m_Impl->scriptTable["GetRigidBody2D"] = [this](sol::table, sol::this_state s) -> sol::object {
                    if (!m_Owner) return sol::nil;
                    auto rb2d = m_Owner->GetComponent<RigidBody2DComponent>();
                    return rb2d ? sol::make_object(s, rb2d.get()) : sol::nil;
                };
                m_Impl->scriptTable["GetSpriteRenderer"] = [this](sol::table, sol::this_state s) -> sol::object {
                    if (!m_Owner) return sol::nil;
                    auto sr = m_Owner->GetComponent<SpriteRendererComponent>();
                    return sr ? sol::make_object(s, sr.get()) : sol::nil;
                };
                m_Impl->scriptTable["GetSpriteAnimator"] = [this](sol::table, sol::this_state s) -> sol::object {
                    if (!m_Owner) return sol::nil;
                    auto sa = m_Owner->GetComponent<SpriteAnimatorComponent>();
                    return sa ? sol::make_object(s, sa.get()) : sol::nil;
                };
                m_Impl->scriptTable["GetRectTransform"] = [this](sol::table, sol::this_state s) -> sol::object {
                    if (!m_Owner) return sol::nil;
                    auto rt = m_Owner->GetComponent<RectTransformComponent>();
                    return rt ? sol::make_object(s, rt.get()) : sol::nil;
                };
                m_Impl->scriptTable["GetUIImage"] = [this](sol::table, sol::this_state s) -> sol::object {
                    if (!m_Owner) return sol::nil;
                    auto img = m_Owner->GetComponent<UIImageComponent>();
                    return img ? sol::make_object(s, img.get()) : sol::nil;
                };
                m_Impl->scriptTable["GetUIText"] = [this](sol::table, sol::this_state s) -> sol::object {
                    if (!m_Owner) return sol::nil;
                    auto txt = m_Owner->GetComponent<UITextComponent>();
                    return txt ? sol::make_object(s, txt.get()) : sol::nil;
                };
                m_Impl->scriptTable["GetUIButton"] = [this](sol::table, sol::this_state s) -> sol::object {
                    if (!m_Owner) return sol::nil;
                    auto btn = m_Owner->GetComponent<UIButtonComponent>();
                    return btn ? sol::make_object(s, btn.get()) : sol::nil;
                };
                m_Impl->scriptTable["GetUISlider"] = [this](sol::table, sol::this_state s) -> sol::object {
                    if (!m_Owner) return sol::nil;
                    auto sld = m_Owner->GetComponent<UISliderComponent>();
                    return sld ? sol::make_object(s, sld.get()) : sol::nil;
                };
                m_Impl->scriptTable["GetUIProgressBar"] = [this](sol::table, sol::this_state s) -> sol::object {
                    if (!m_Owner) return sol::nil;
                    auto pb = m_Owner->GetComponent<UIProgressBarComponent>();
                    return pb ? sol::make_object(s, pb.get()) : sol::nil;
                };
                m_Impl->scriptTable["GetParticleSystem"] = [this](sol::table, sol::this_state s) -> sol::object {
                    if (!m_Owner) return sol::nil;
                    auto ps = m_Owner->GetComponent<ParticleSystemComponent>();
                    return ps ? sol::make_object(s, ps.get()) : sol::nil;
                };
            }
            m_Impl->fnStart = m_Impl->scriptTable["OnStart"];
            m_Impl->fnUpdate = m_Impl->scriptTable["OnUpdate"];
            m_Impl->fnCollisionEnter = m_Impl->scriptTable["OnCollisionEnter"];
            m_Impl->fnCollisionStay = m_Impl->scriptTable["OnCollisionStay"];
            m_Impl->fnCollisionExit = m_Impl->scriptTable["OnCollisionExit"];
            m_Impl->fnTriggerEnter = m_Impl->scriptTable["OnTriggerEnter"];
            m_Impl->fnTriggerStay = m_Impl->scriptTable["OnTriggerStay"];
            m_Impl->fnTriggerExit = m_Impl->scriptTable["OnTriggerExit"];

            SyncPropertiesFromLua();
        } else {
            m_Impl->fnStart = m_Impl->env["OnStart"];
            m_Impl->fnUpdate = m_Impl->env["OnUpdate"];
            m_Impl->fnCollisionEnter = m_Impl->env["OnCollisionEnter"];
            m_Impl->fnCollisionStay = m_Impl->env["OnCollisionStay"];
            m_Impl->fnCollisionExit = m_Impl->env["OnCollisionExit"];
            m_Impl->fnTriggerEnter = m_Impl->env["OnTriggerEnter"];
            m_Impl->fnTriggerStay = m_Impl->env["OnTriggerStay"];
            m_Impl->fnTriggerExit = m_Impl->env["OnTriggerExit"];
        }

        isCompiled = true;
        return true;
    } catch (const std::exception& e) {
        lastError = e.what();
        luaEngine.ReportError(lastError);
        return false;
    }
}

void ScriptComponent::OnInit() {
    ReloadScript();
}

void ScriptComponent::OnStart() {
    if (!m_Enabled) return;
    if (!isCompiled) {
        ReloadScript();
    }

    if (!isCompiled || !m_Impl) return;

    if (m_Impl->fnStart.valid()) {
        sol::protected_function_result res;
        if (m_Impl->hasTable) {
            res = m_Impl->fnStart(m_Impl->scriptTable, m_Owner);
        } else {
            res = m_Impl->fnStart(m_Owner);
        }

        if (!res.valid()) {
            sol::error err = res;
            lastError = err.what();
            SlipLuaEngine::Get().ReportError(lastError);
        }
    }
}

void ScriptComponent::OnUpdate(float deltaTime) {
    if (!m_Enabled || !isCompiled || !m_Impl) return;

    if (m_Impl->fnUpdate.valid()) {
        sol::protected_function_result res;
        if (m_Impl->hasTable) {
            res = m_Impl->fnUpdate(m_Impl->scriptTable, deltaTime);
        } else {
            res = m_Impl->fnUpdate(deltaTime);
        }

        if (!res.valid()) {
            sol::error err = res;
            lastError = err.what();
            SlipLuaEngine::Get().ReportError(lastError);
        }
    }
}

void ScriptComponent::OnDrawInspectorUI() {
}

void ScriptComponent::OnCollisionEnter(SlipEntity* other) {
    if (!m_Enabled || !isCompiled || !m_Impl || !other) return;
    if (m_Impl->fnCollisionEnter.valid()) {
        if (m_Impl->hasTable) {
            m_Impl->fnCollisionEnter(m_Impl->scriptTable, other);
        } else {
            m_Impl->fnCollisionEnter(other);
        }
    }
}

void ScriptComponent::OnCollisionStay(SlipEntity* other) {
    if (!m_Enabled || !isCompiled || !m_Impl || !other) return;
    if (m_Impl->fnCollisionStay.valid()) {
        if (m_Impl->hasTable) {
            m_Impl->fnCollisionStay(m_Impl->scriptTable, other);
        } else {
            m_Impl->fnCollisionStay(other);
        }
    }
}

void ScriptComponent::OnCollisionExit(SlipEntity* other) {
    if (!m_Enabled || !isCompiled || !m_Impl || !other) return;
    if (m_Impl->fnCollisionExit.valid()) {
        if (m_Impl->hasTable) {
            m_Impl->fnCollisionExit(m_Impl->scriptTable, other);
        } else {
            m_Impl->fnCollisionExit(other);
        }
    }
}

void ScriptComponent::OnTriggerEnter(SlipEntity* other) {
    if (!m_Enabled || !isCompiled || !m_Impl || !other) return;
    if (m_Impl->fnTriggerEnter.valid()) {
        if (m_Impl->hasTable) {
            m_Impl->fnTriggerEnter(m_Impl->scriptTable, other);
        } else {
            m_Impl->fnTriggerEnter(other);
        }
    }
}

void ScriptComponent::OnTriggerStay(SlipEntity* other) {
    if (!m_Enabled || !isCompiled || !m_Impl || !other) return;
    if (m_Impl->fnTriggerStay.valid()) {
        if (m_Impl->hasTable) {
            m_Impl->fnTriggerStay(m_Impl->scriptTable, other);
        } else {
            m_Impl->fnTriggerStay(other);
        }
    }
}

void ScriptComponent::OnTriggerExit(SlipEntity* other) {
    if (!m_Enabled || !isCompiled || !m_Impl || !other) return;
    if (m_Impl->fnTriggerExit.valid()) {
        if (m_Impl->hasTable) {
            m_Impl->fnTriggerExit(m_Impl->scriptTable, other);
        } else {
            m_Impl->fnTriggerExit(other);
        }
    }
}

// ---------------------------------------------------------
// CameraComponent
// ---------------------------------------------------------
static glm::mat4 GetCameraWorldRotationMatrix(const SlipEntity* owner)
{
    glm::vec3 rot = owner ? owner->rotation : glm::vec3(0.0f);
    glm::mat4 localRotMat = glm::rotate(glm::mat4(1.0f), glm::radians(rot.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
                            glm::rotate(glm::mat4(1.0f), glm::radians(rot.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
                            glm::rotate(glm::mat4(1.0f), glm::radians(rot.z), glm::vec3(0.0f, 0.0f, 1.0f));

    if (owner && owner->GetParent()) {
        glm::mat4 parentMat = owner->GetParent()->getMatrix();
        glm::vec3 col0 = glm::length(glm::vec3(parentMat[0])) > 0.0001f ? glm::normalize(glm::vec3(parentMat[0])) : glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 col1 = glm::length(glm::vec3(parentMat[1])) > 0.0001f ? glm::normalize(glm::vec3(parentMat[1])) : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 col2 = glm::length(glm::vec3(parentMat[2])) > 0.0001f ? glm::normalize(glm::vec3(parentMat[2])) : glm::vec3(0.0f, 0.0f, 1.0f);
        glm::mat4 parentRot(1.0f);
        parentRot[0] = glm::vec4(col0, 0.0f);
        parentRot[1] = glm::vec4(col1, 0.0f);
        parentRot[2] = glm::vec4(col2, 0.0f);
        return parentRot * localRotMat;
    }
    return localRotMat;
}

CameraComponent::CameraComponent()
{
    m_Name = "CameraComponent";
}

glm::vec3 CameraComponent::GetPosition() const
{
    if (!m_Owner) return glm::vec3(0.0f);
    return glm::vec3(const_cast<SlipEntity*>(m_Owner)->getMatrix()[3]);
}

glm::vec3 CameraComponent::GetForward() const
{
    if (!m_Owner) return glm::vec3(0.0f, 0.0f, -1.0f);
    glm::mat4 wm = GetCameraWorldRotationMatrix(m_Owner);
    glm::vec3 zCol = glm::vec3(wm[2]);
    if (glm::length(zCol) > 0.0001f) return -glm::normalize(zCol);
    return glm::vec3(0.0f, 0.0f, -1.0f);
}

glm::vec3 CameraComponent::GetUp() const
{
    if (!m_Owner) return glm::vec3(0.0f, 1.0f, 0.0f);
    glm::mat4 wm = GetCameraWorldRotationMatrix(m_Owner);
    glm::vec3 yCol = glm::vec3(wm[1]);
    if (glm::length(yCol) > 0.0001f) return glm::normalize(yCol);
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

glm::vec3 CameraComponent::GetRight() const
{
    if (!m_Owner) return glm::vec3(1.0f, 0.0f, 0.0f);
    glm::mat4 wm = GetCameraWorldRotationMatrix(m_Owner);
    glm::vec3 xCol = glm::vec3(wm[0]);
    if (glm::length(xCol) > 0.0001f) return glm::normalize(xCol);
    return glm::normalize(glm::cross(GetForward(), GetUp()));
}

void CameraComponent::Shake(float intensity, float duration, float frequency) {
    shakeIntensity = (intensity > 0.0f) ? intensity : 0.0f;
    shakeDuration = (duration > 0.001f) ? duration : 0.001f;
    shakeTimer = 0.0f;
    shakeElapsed = 0.0f;
    shakeFrequency = (frequency > 0.0f) ? frequency : 25.0f;
}

void CameraComponent::StopShake() {
    shakeDuration = 0.0f;
    shakeTimer = 0.0f;
    currentShakeOffset = glm::vec3(0.0f);
    currentShakeRotation = glm::vec3(0.0f);
}

void CameraComponent::OnUpdate(float deltaTime) {
    if (shakeDuration > 0.0f && shakeTimer < shakeDuration) {
        shakeTimer += deltaTime;
        shakeElapsed += deltaTime;
        float ratio = shakeTimer / shakeDuration;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        float progress = 1.0f - ratio;
        float trauma = progress * progress * shakeIntensity;
        float t = shakeElapsed * shakeFrequency;

        currentShakeOffset.x = (std::sin(t * 1.0f) * 0.7f + std::sin(t * 2.3f) * 0.3f) * trauma * 0.3f;
        currentShakeOffset.y = (std::cos(t * 1.2f) * 0.7f + std::sin(t * 2.7f) * 0.3f) * trauma * 0.3f;
        currentShakeOffset.z = (std::sin(t * 0.8f) * 0.5f) * trauma * 0.15f;

        currentShakeRotation.x = (std::sin(t * 1.4f) * 0.7f + std::cos(t * 2.1f) * 0.3f) * trauma * 4.0f; // pitch deg
        currentShakeRotation.y = (std::cos(t * 1.1f) * 0.7f + std::sin(t * 1.9f) * 0.3f) * trauma * 4.0f; // yaw deg
        currentShakeRotation.z = (std::sin(t * 1.7f)) * trauma * 2.5f; // roll deg
    } else {
        shakeDuration = 0.0f;
        shakeTimer = 0.0f;
        currentShakeOffset = glm::vec3(0.0f);
        currentShakeRotation = glm::vec3(0.0f);
    }
}

glm::mat4 CameraComponent::GetViewMatrix() const
{
    glm::vec3 pos = GetPosition() + currentShakeOffset;
    glm::vec3 forward = GetForward();
    glm::vec3 up = GetUp();
    glm::vec3 right = GetRight();

    if (glm::length(currentShakeRotation) > 0.0001f) {
        glm::mat4 shakeRot = glm::mat4(1.0f);
        shakeRot = glm::rotate(shakeRot, glm::radians(currentShakeRotation.x), right);
        shakeRot = glm::rotate(shakeRot, glm::radians(currentShakeRotation.y), up);
        shakeRot = glm::rotate(shakeRot, glm::radians(currentShakeRotation.z), forward);
        forward = glm::normalize(glm::vec3(shakeRot * glm::vec4(forward, 0.0f)));
        up = glm::normalize(glm::vec3(shakeRot * glm::vec4(up, 0.0f)));
    }
    return glm::lookAt(pos, pos + forward, up);
}

glm::mat4 CameraComponent::GetProjectionMatrix(float aspectRatio) const
{
    if (aspectRatio <= 0.0001f) aspectRatio = 1.0f;
    if (projectionType == CameraProjection::Orthographic) {
        float halfHeight = orthoSize;
        float halfWidth = halfHeight * aspectRatio;
        return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearClip, farClip);
    } else {
        return glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip);
    }
}

void CameraComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// CinematicCameraComponent (Cámara Cinemática estilo Forza Horizon / NFS)
// ---------------------------------------------------------
CinematicCameraComponent::CinematicCameraComponent()
{
    m_Name = "CinematicCameraComponent";
}

void CinematicCameraComponent::OnInit()
{
    hasInitialized = false;
    currentFOV = baseFOV;
}

void CinematicCameraComponent::OnStart()
{
    ResetImmediate();
}

void CinematicCameraComponent::ResetImmediate()
{
    SlipEntity* target = GetTarget();
    if (!target) return;

    glm::vec3 carPos = target->position;

    // Obtener la orientación horizontal (Yaw) de 360 grados completa sin singularidades
    btVector3 carFwd(0.0f, 0.0f, 1.0f);
    if (auto col = target->GetComponent<ColliderComponent>()) {
        if (col->rigidBody) {
            carFwd = col->rigidBody->getWorldTransform().getBasis().getColumn(2);
        }
    } else {
        glm::mat4 m = target->getMatrix();
        carFwd = btVector3(m[2][0], m[2][1], m[2][2]);
    }
    carFwd.setY(0.0f);
    if (carFwd.length2() > 0.0001f) {
        carFwd.normalize();
    } else {
        carFwd = btVector3(0.0f, 0.0f, 1.0f);
    }
    currentYaw = glm::degrees(std::atan2(carFwd.x(), carFwd.z()));
    currentPitch = targetPitch;
    currentDriftOffset = 0.0f;
    orbitYaw = 0.0f;
    orbitPitch = 0.0f;
    lastMouseInputTimer = autoCenterDelay + 1.0f;

    float radYaw = glm::radians(currentYaw);
    glm::vec3 horizontalBackDir(-std::sin(radYaw), 0.0f, -std::cos(radYaw));

    glm::vec3 lookTarget = carPos + glm::vec3(0.0f, pivotYOffset, 0.0f);
    smoothedLookTarget = lookTarget;
    currentPosition = lookTarget + horizontalBackDir * targetDistance + glm::vec3(0.0f, targetHeight, 0.0f);

    if (m_Owner) {
        m_Owner->position = currentPosition;
        glm::vec3 lookDir = lookTarget - currentPosition;
        float lookDist = glm::length(lookDir);
        if (lookDist > 0.001f) {
            lookDir /= lookDist;
            float camYaw = glm::degrees(std::atan2(-lookDir.x, -lookDir.z));
            float camPitch = glm::degrees(std::asin(glm::clamp(lookDir.y, -1.0f, 1.0f)));
            camPitch = glm::clamp(camPitch, -45.0f, 20.0f);
            m_Owner->rotation = glm::vec3(camPitch, camYaw, 0.0f);
        } else {
            m_Owner->rotation = glm::vec3(-targetPitch, currentYaw + 180.0f, 0.0f);
        }
        if (auto t = m_Owner->GetComponent<TransformComponent>()) {
            t->position = currentPosition;
            t->rotation = m_Owner->rotation;
        }
        if (auto cam = m_Owner->GetComponent<CameraComponent>()) {
            cam->SetFOV(baseFOV);
        }
    }
    hasInitialized = true;
}

void CinematicCameraComponent::SetTarget(SlipEntity* target)
{
    targetEntity = target;
    if (target) {
        targetEntityName = target->GetName();
    }
    ResetImmediate();
}

void CinematicCameraComponent::SetTargetName(const std::string& name)
{
    targetEntityName = name;
    targetEntity = nullptr;
}

SlipEntity* CinematicCameraComponent::GetTarget() const
{
    if (targetEntity) return targetEntity;
    if (targetEntityName.empty()) return nullptr;

    const auto& entities = SlipLevel::Get().GetEntities();
    for (SlipEntity* ent : entities) {
        if (ent && ent->GetName() == targetEntityName) {
            const_cast<CinematicCameraComponent*>(this)->targetEntity = ent;
            return ent;
        }
    }
    return nullptr;
}

void CinematicCameraComponent::OnUpdate(float deltaTime)
{
    if (deltaTime <= 0.0f) return;

    SlipEntity* target = GetTarget();
    if (!target) return;

    if (!hasInitialized) {
        ResetImmediate();
    }

    glm::vec3 carPos = target->position;

    // Obtener orientación horizontal (Yaw) de 360 grados completa sin singularidades
    btVector3 carFwd(0.0f, 0.0f, 1.0f);
    if (auto col = target->GetComponent<ColliderComponent>()) {
        if (col->rigidBody) {
            btTransform trans;
            if (col->rigidBody->getMotionState()) {
                col->rigidBody->getMotionState()->getWorldTransform(trans);
            } else {
                trans = col->rigidBody->getWorldTransform();
            }
            carPos = glm::vec3(trans.getOrigin().x(), trans.getOrigin().y(), trans.getOrigin().z());
            carFwd = trans.getBasis().getColumn(2);
        }
    } else {
        glm::mat4 m = target->getMatrix();
        carFwd = btVector3(m[2][0], m[2][1], m[2][2]);
    }
    carFwd.setY(0.0f);
    if (carFwd.length2() > 0.0001f) {
        carFwd.normalize();
    } else {
        carFwd = btVector3(0.0f, 0.0f, 1.0f);
    }
    float carYaw = glm::degrees(std::atan2(carFwd.x(), carFwd.z()));

    // 1. Obtener velocidad y verificar si el vehículo está en contacto con el suelo
    float speedKmH = 0.0f;
    glm::vec3 linearVel = glm::vec3(0.0f);
    if (auto rb = target->GetComponent<RigidBodyComponent>()) {
        linearVel = rb->GetLinearVelocity();
        speedKmH = glm::length(linearVel) * 3.6f;
    }
    bool isVehicleGrounded = false;
    if (auto veh = target->GetComponent<VehicleComponent>()) {
        float vehSpeed = std::abs(veh->GetCurrentSpeedKmHour());
        if (vehSpeed > speedKmH) speedKmH = vehSpeed;
        for (int i = 0; i < veh->GetNumWheels(); ++i) {
            if (veh->IsWheelOnGround(i)) {
                isVehicleGrounded = true;
                break;
            }
        }
    } else {
        isVehicleGrounded = true;
    }

    // 2. Control Orbital con Mouse (Solo horizontal / Yaw horizontal, sin cabeceo vertical)
    orbitPitch = 0.0f;
    if (enableMouseOrbit && SlipInput::HasInstance()) {
        glm::vec2 mouseDelta = SlipInput::Get().GetMouseDelta();
        if (std::abs(mouseDelta.x) > 0.01f) {
            orbitYaw -= mouseDelta.x * mouseSensitivity;
            lastMouseInputTimer = 0.0f;
        } else {
            lastMouseInputTimer += deltaTime;
            if (lastMouseInputTimer > autoCenterDelay) {
                float centerFactor = glm::clamp(autoCenterSpeed * deltaTime, 0.0f, 1.0f);
                orbitYaw += (0.0f - orbitYaw) * centerFactor;
            }
        }
    }

    // 3. Compensación de derrape (Drift Yaw Offset estilo NFS / Forza)
    // Solo cuando el coche está en el suelo y con velocidad horizontal real (evita vibraciones en el aire o caídas libres)
    float targetDriftOffset = 0.0f;
    float horizSpeed = glm::length(glm::vec2(linearVel.x, linearVel.z));
    if (isVehicleGrounded && speedKmH > 18.0f && horizSpeed > 3.0f) {
        float velocityYaw = glm::degrees(std::atan2(linearVel.x, linearVel.z));
        float yawDiff = velocityYaw - carYaw;
        while (yawDiff > 180.0f) yawDiff -= 360.0f;
        while (yawDiff < -180.0f) yawDiff += 360.0f;

        targetDriftOffset = glm::clamp(yawDiff * driftCompensation, -maxDriftYawOffset, maxDriftYawOffset);
    }
    // Suavizado continuo de la compensación de derrape (filtro exponencial suave)
    currentDriftOffset += (targetDriftOffset - currentDriftOffset) * glm::clamp(6.0f * deltaTime, 0.0f, 1.0f);

    // 4. Suavizado de Rotación (Yaw y Pitch)
    float desiredYaw = carYaw + currentDriftOffset + orbitYaw;
    float diffYaw = desiredYaw - currentYaw;
    while (diffYaw > 180.0f) diffYaw -= 360.0f;
    while (diffYaw < -180.0f) diffYaw += 360.0f;
    float rotStep = glm::clamp(rotationLag * deltaTime, 0.0f, 1.0f);
    currentYaw += diffYaw * rotStep;

    float desiredPitch = targetPitch + orbitPitch;
    float pitchStep = glm::clamp(pitchLag * deltaTime, 0.0f, 1.0f);
    currentPitch += (desiredPitch - currentPitch) * pitchStep;

    // 5. Dimensiones dinámicas basadas en velocidad (chase cam pura y estable)
    float speedRatio = glm::clamp(speedKmH / 180.0f, 0.0f, 1.0f);
    float dynamicHeight = targetHeight + (0.25f * speedRatio);
    float dynamicDistance = targetDistance + (0.70f * speedRatio);

    float radYaw = glm::radians(currentYaw);
    glm::vec3 horizontalBackDir(-std::sin(radYaw), 0.0f, -std::cos(radYaw));

    glm::vec3 rawLookTarget = carPos + glm::vec3(0.0f, pivotYOffset, 0.0f);
    // Filtrar micro-oscilaciones de física de alta frecuencia (resortes Bullet a 120Hz)
    smoothedLookTarget += (rawLookTarget - smoothedLookTarget) * glm::clamp(22.0f * deltaTime, 0.0f, 1.0f);
    glm::vec3 lookTarget = smoothedLookTarget;
    float finalDistance = dynamicDistance;

    // 6. Anticolisión mediante Raycast Probe con Bullet
    if (enableCollisionAvoidance && SlipPhysics::IsInitialized()) {
        glm::vec3 rayTarget = lookTarget + horizontalBackDir * dynamicDistance + glm::vec3(0.0f, dynamicHeight, 0.0f);
        btVector3 bFrom(lookTarget.x, lookTarget.y, lookTarget.z);
        btVector3 bTo(rayTarget.x, rayTarget.y, rayTarget.z);

        struct CameraRayCallback : public btCollisionWorld::ClosestRayResultCallback {
            const SlipEntity* carEnt;
            CameraRayCallback(const btVector3& from, const btVector3& to, const SlipEntity* ignoreCar)
                : btCollisionWorld::ClosestRayResultCallback(from, to), carEnt(ignoreCar) {}

            btScalar addSingleResult(btCollisionWorld::LocalRayResult& rayResult, bool normalInWorldSpace) override {
                if (rayResult.m_collisionObject) {
                    if (rayResult.m_collisionObject->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE) {
                        return 1.0f; // Ignorar triggers
                    }
                    if (carEnt && static_cast<const SlipEntity*>(rayResult.m_collisionObject->getUserPointer()) == carEnt) {
                        return 1.0f; // Ignorar el propio vehículo
                    }
                }
                return ClosestRayResultCallback::addSingleResult(rayResult, normalInWorldSpace);
            }
        };

        CameraRayCallback callback(bFrom, bTo, target);
        SlipPhysics::Get().getWorld().rayTest(bFrom, bTo, callback);

        if (callback.hasHit()) {
            float hitDist = glm::length(glm::vec3(callback.m_hitPointWorld.x(), callback.m_hitPointWorld.y(), callback.m_hitPointWorld.z()) - lookTarget);
            finalDistance = (std::max)(minCollisionDistance, hitDist - collisionRadius);
        }
    }

    glm::vec3 desiredPosition = lookTarget + horizontalBackDir * finalDistance + glm::vec3(0.0f, dynamicHeight, 0.0f);

    // 7. Amortiguación de Posición (Damped Spring)
    float posStep = glm::clamp(positionLag * deltaTime, 0.0f, 1.0f);
    currentPosition += (desiredPosition - currentPosition) * posStep;

    // 8. Aplicar Transform a la entidad de la cámara enfocando DIRECTAMENTE al vehículo (Always Centered)
    if (m_Owner) {
        m_Owner->position = currentPosition;

        glm::vec3 lookDir = lookTarget - currentPosition;
        float lookDist = glm::length(lookDir);
        if (lookDist > 0.001f) {
            lookDir /= lookDist;
            float camYaw = glm::degrees(std::atan2(-lookDir.x, -lookDir.z));
            float camPitch = glm::degrees(std::asin(glm::clamp(lookDir.y, -1.0f, 1.0f)));
            camPitch = glm::clamp(camPitch, -45.0f, 20.0f);
            m_Owner->rotation = glm::vec3(camPitch, camYaw, 0.0f);
        } else {
            m_Owner->rotation = glm::vec3(-targetPitch, currentYaw + 180.0f, 0.0f);
        }

        if (auto t = m_Owner->GetComponent<TransformComponent>()) {
            t->position = currentPosition;
            t->rotation = m_Owner->rotation;
        }

        // 9. FOV Dinámico
        if (enableDynamicFOV) {
            if (auto cam = m_Owner->GetComponent<CameraComponent>()) {
                float speedFactor = glm::clamp(speedKmH / maxSpeedForFOV, 0.0f, 1.0f);
                float desiredFOV = baseFOV + (boostFOV - baseFOV) * speedFactor;
                currentFOV += (desiredFOV - currentFOV) * glm::clamp(fovLag * deltaTime, 0.0f, 1.0f);
                cam->SetFOV(currentFOV);
            }
        }
    }
}

void CinematicCameraComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// KeyframeAnimationComponent
// ---------------------------------------------------------
KeyframeAnimationComponent::KeyframeAnimationComponent() {
    m_Name = "KeyframeAnimationComponent";
    clip = std::make_shared<KeyframeClip>();
}

KeyframeAnimationComponent::KeyframeAnimationComponent(const std::string& path) {
    m_Name = "KeyframeAnimationComponent";
    clip = std::make_shared<KeyframeClip>();
    LoadClip(path);
}

bool KeyframeAnimationComponent::LoadClip(const std::string& path) {
    clipPath = path;
    if (clipPath.empty()) return false;

    std::string resolved = path;
    if (!SlipFileSystem::Exists(resolved)) {
        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
        if (assetsDir.empty()) assetsDir = "assets";
        resolved = SlipFileSystem::CombinePath(assetsDir, path);
    }
    if (!SlipFileSystem::Exists(resolved)) {
        SlipConsole::LogWarning("No se encontro clip de animacion: " + path);
        return false;
    }

    if (!clip) clip = std::make_shared<KeyframeClip>();
    if (clip->LoadFromFile(resolved)) {
        loop = clip->loop;
        currentTime = 0.0f;
        return true;
    }
    return false;
}

void KeyframeAnimationComponent::Play() {
    isPlaying = true;
    if (clip && currentTime >= clip->duration) {
        currentTime = 0.0f;
    }
}

void KeyframeAnimationComponent::Pause() {
    isPlaying = false;
}

void KeyframeAnimationComponent::Resume() {
    isPlaying = true;
}

void KeyframeAnimationComponent::Stop() {
    isPlaying = false;
    currentTime = 0.0f;
    if (clip && m_Owner) {
        clip->SampleEntity(m_Owner, 0.0f);
    }
}

void KeyframeAnimationComponent::SetTime(float time) {
    currentTime = time;
    if (clip && m_Owner) {
        clip->SampleEntity(m_Owner, currentTime);
    }
}

void KeyframeAnimationComponent::OnInit() {
    if (!clipPath.empty() && (!clip || clip->entityGroups.empty())) {
        LoadClip(clipPath);
    }
}

void KeyframeAnimationComponent::OnStart() {
    if (playOnStart) {
        Play();
    }
}

void KeyframeAnimationComponent::OnUpdate(float deltaTime) {
    if (!isPlaying || !clip || clip->duration <= 0.0f) return;

    currentTime += deltaTime * playbackSpeed;
    if (currentTime >= clip->duration) {
        if (loop) {
            currentTime = std::fmod(currentTime, clip->duration);
        } else {
            currentTime = clip->duration;
            isPlaying = false;
        }
    }

    if (m_Owner) {
        clip->SampleEntity(m_Owner, currentTime);
    }
}

void KeyframeAnimationComponent::OnDrawInspectorUI() {
}

bool ScriptComponent::InvokeLuaFunction(const std::string& functionName) {
    if (!m_Enabled || functionName.empty()) return false;
    if (!isCompiled) {
        ReloadScript();
    }
    if (!isCompiled || !m_Impl) return false;

    try {
        if (m_Impl->hasTable && m_Impl->scriptTable.valid()) {
            sol::object fnObj = m_Impl->scriptTable[functionName];
            if (fnObj.valid() && fnObj.get_type() == sol::type::function) {
                sol::protected_function fn = fnObj;
                auto res = fn(m_Impl->scriptTable, m_Owner);
                if (!res.valid()) {
                    sol::error err = res;
                    lastError = err.what();
                    SlipLuaEngine::Get().ReportError(lastError);
                    return false;
                }
                return true;
            }
        }
        if (m_Impl->env.valid()) {
            sol::object fnObj = m_Impl->env[functionName];
            if (fnObj.valid() && fnObj.get_type() == sol::type::function) {
                sol::protected_function fn = fnObj;
                auto res = fn(m_Owner);
                if (!res.valid()) {
                    sol::error err = res;
                    lastError = err.what();
                    SlipLuaEngine::Get().ReportError(lastError);
                    return false;
                }
                return true;
            }
        }
    } catch (const std::exception& e) {
        lastError = e.what();
        SlipLuaEngine::Get().ReportError(lastError);
    }
    return false;
}

bool ScriptComponent::InvokeLuaFunctionFloat(const std::string& functionName, float value) {
    if (!m_Enabled || functionName.empty()) return false;
    if (!isCompiled) {
        ReloadScript();
    }
    if (!isCompiled || !m_Impl) return false;

    try {
        if (m_Impl->hasTable && m_Impl->scriptTable.valid()) {
            sol::object fnObj = m_Impl->scriptTable[functionName];
            if (fnObj.valid() && fnObj.get_type() == sol::type::function) {
                sol::protected_function fn = fnObj;
                auto res = fn(m_Impl->scriptTable, value, m_Owner);
                if (!res.valid()) {
                    sol::error err = res;
                    lastError = err.what();
                    SlipLuaEngine::Get().ReportError(lastError);
                    return false;
                }
                return true;
            }
        }
        if (m_Impl->env.valid()) {
            sol::object fnObj = m_Impl->env[functionName];
            if (fnObj.valid() && fnObj.get_type() == sol::type::function) {
                sol::protected_function fn = fnObj;
                auto res = fn(value, m_Owner);
                if (!res.valid()) {
                    sol::error err = res;
                    lastError = err.what();
                    SlipLuaEngine::Get().ReportError(lastError);
                    return false;
                }
                return true;
            }
        }
    } catch (const std::exception& e) {
        lastError = e.what();
        SlipLuaEngine::Get().ReportError(lastError);
    }
    return false;
}

// ---------------------------------------------------------
// SpriteRendererComponent
// ---------------------------------------------------------
static std::string ResolveTextureAssetPath(const std::string& rawPath) {
    if (rawPath.empty()) return "";
    if (SlipFileSystem::Exists(rawPath)) return rawPath;

    std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
    if (!assetsDir.empty()) {
        std::string inAssets = SlipFileSystem::CombinePath(assetsDir, rawPath);
        if (SlipFileSystem::Exists(inAssets)) return inAssets;

        std::string baseName = SlipFileSystem::GetFileName(rawPath);
        if (!baseName.empty()) {
            std::string directBase = SlipFileSystem::CombinePath(assetsDir, baseName);
            if (SlipFileSystem::Exists(directBase)) return directBase;
        }
    }
    return rawPath;
}

SpriteRendererComponent::SpriteRendererComponent()
{
    m_Name = "SpriteRendererComponent";
}

SpriteRendererComponent::SpriteRendererComponent(const std::string& texPath)
{
    m_Name = "SpriteRendererComponent";
    SetTexture(texPath);
}

SpriteRendererComponent::~SpriteRendererComponent()
{
    if (texture) {
        delete texture;
        texture = nullptr;
    }
}

void SpriteRendererComponent::SetTexture(const std::string& path)
{
    if (texture) {
        delete texture;
        texture = nullptr;
    }
    texturePath = path;
    if (!path.empty()) {
        std::string resolved = ResolveTextureAssetPath(path);
        texture = SlipTexture::load(resolved.c_str());
        if (texture && isPixelArt) {
            texture->SetFilterMode(true);
        }
    }
}

glm::vec4 SpriteRendererComponent::GetUVRect() const
{
    int cols = (std::max)(1, hFrames);
    int rows = (std::max)(1, vFrames);
    int total = cols * rows;
    int frame = ((currentFrame % total) + total) % total;

    int col = frame % cols;
    int row = frame / cols;

    float uScale = 1.0f / static_cast<float>(cols);
    float vScale = 1.0f / static_cast<float>(rows);
    float uOffset = static_cast<float>(col) * uScale;
    float vOffset = static_cast<float>(row) * vScale;

    return glm::vec4(uOffset, vOffset, uScale, vScale);
}

glm::vec2 SpriteRendererComponent::GetBaseQuadSize() const
{
    float ppu = (std::max)(1.0f, pixelsPerUnit);
    if (texture && texture->width > 0 && texture->height > 0) {
        int cols = (std::max)(1, hFrames);
        int rows = (std::max)(1, vFrames);
        float frameW = static_cast<float>(texture->width) / static_cast<float>(cols);
        float frameH = static_cast<float>(texture->height) / static_cast<float>(rows);
        return glm::vec2(frameW / ppu, frameH / ppu);
    }
    return glm::vec2(1.0f, 1.0f);
}

void SpriteRendererComponent::SetNormalMap(const std::string& path)
{
    if (normalMap) {
        delete normalMap;
        normalMap = nullptr;
    }
    normalMapPath = path;
    if (!path.empty()) {
        std::string resolved = ResolveTextureAssetPath(path);
        normalMap = SlipTexture::load(resolved.c_str());
    }
}

int SpriteRendererComponent::GetSortingLayerIndex() const
{
    return SlipSortingLayers::Get().GetLayerIndex(sortingLayerName);
}

void SpriteRendererComponent::OnInit()
{
    if (!texturePath.empty() && !texture) {
        SetTexture(texturePath);
    }
    if (!normalMapPath.empty() && !normalMap) {
        SetNormalMap(normalMapPath);
    }
}

void SpriteRendererComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// SpriteAnimatorComponent
// ---------------------------------------------------------
SpriteAnimatorComponent::SpriteAnimatorComponent()
{
    m_Name = "SpriteAnimatorComponent";
}

void SpriteAnimatorComponent::OnInit()
{
    if (playOnStart && !defaultClip.empty()) {
        Play(defaultClip);
    } else if (playOnStart && !clips.empty()) {
        PlayClipByIndex(0);
    }
}

void SpriteAnimatorComponent::OnStart()
{
    OnInit();
}

void SpriteAnimatorComponent::OnUpdate(float deltaTime)
{
    if (!isPlaying || activeClipIndex < 0 || activeClipIndex >= static_cast<int>(clips.size())) return;

    const auto& clip = clips[activeClipIndex];
    float fps = (clip.frameRate > 0.0f) ? clip.frameRate : 12.0f;
    float frameDuration = 1.0f / fps;

    playbackTimer += deltaTime;
    if (playbackTimer >= frameDuration) {
        playbackTimer = std::fmod(playbackTimer, frameDuration);
        if (currentFrame < clip.endFrame) {
            currentFrame++;
        } else {
            if (clip.loop) {
                currentFrame = clip.startFrame;
            } else {
                isPlaying = false;
            }
        }

        if (m_Owner) {
            if (auto sr = m_Owner->GetComponent<SpriteRendererComponent>()) {
                sr->currentFrame = currentFrame;
            }
        }
    }
}

void SpriteAnimatorComponent::Play(const std::string& clipName)
{
    for (size_t i = 0; i < clips.size(); ++i) {
        if (clips[i].name == clipName) {
            PlayClipByIndex(static_cast<int>(i));
            return;
        }
    }
}

void SpriteAnimatorComponent::PlayClipByIndex(int index)
{
    if (index >= 0 && index < static_cast<int>(clips.size())) {
        activeClipIndex = index;
        currentFrame = clips[index].startFrame;
        playbackTimer = 0.0f;
        isPlaying = true;
        if (m_Owner) {
            if (auto sr = m_Owner->GetComponent<SpriteRendererComponent>()) {
                sr->currentFrame = currentFrame;
            }
        }
    }
}

void SpriteAnimatorComponent::Stop()
{
    isPlaying = false;
    playbackTimer = 0.0f;
    if (activeClipIndex >= 0 && activeClipIndex < static_cast<int>(clips.size())) {
        currentFrame = clips[activeClipIndex].startFrame;
        if (m_Owner) {
            if (auto sr = m_Owner->GetComponent<SpriteRendererComponent>()) {
                sr->currentFrame = currentFrame;
            }
        }
    }
}

void SpriteAnimatorComponent::Pause()
{
    isPlaying = false;
}

void SpriteAnimatorComponent::Resume()
{
    isPlaying = true;
}

void SpriteAnimatorComponent::AddClip(const std::string& name, int startFrame, int endFrame, float fps, bool loop)
{
    SpriteAnimationClip clip;
    clip.name = name;
    clip.startFrame = startFrame;
    clip.endFrame = endFrame;
    clip.frameRate = fps;
    clip.loop = loop;
    clips.push_back(clip);
    if (clips.size() == 1) {
        defaultClip = name;
    }
}

void SpriteAnimatorComponent::RemoveClip(int index)
{
    if (index >= 0 && index < static_cast<int>(clips.size())) {
        clips.erase(clips.begin() + index);
        if (activeClipIndex == index) {
            activeClipIndex = -1;
            isPlaying = false;
        } else if (activeClipIndex > index) {
            activeClipIndex--;
        }
    }
}

SpriteAnimationClip* SpriteAnimatorComponent::GetClip(const std::string& name)
{
    for (auto& clip : clips) {
        if (clip.name == name) return &clip;
    }
    return nullptr;
}

SpriteAnimationClip* SpriteAnimatorComponent::GetActiveClip()
{
    if (activeClipIndex >= 0 && activeClipIndex < static_cast<int>(clips.size())) {
        return &clips[activeClipIndex];
    }
    return nullptr;
}

std::string SpriteAnimatorComponent::GetCurrentClipName() const
{
    if (activeClipIndex >= 0 && activeClipIndex < static_cast<int>(clips.size())) {
        return clips[activeClipIndex].name;
    }
    return "";
}

void SpriteAnimatorComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// Colisiones 2D
// ---------------------------------------------------------
BoxCollider2DComponent::BoxCollider2DComponent()
{
    m_Name = "BoxCollider2DComponent";
}

BoxCollider2DComponent::BoxCollider2DComponent(const glm::vec2& s, const glm::vec2& off)
    : size(s), offset(off)
{
    m_Name = "BoxCollider2DComponent";
}

BoxCollider2DComponent::~BoxCollider2DComponent()
{
    DestroyStaticBody();
}

void BoxCollider2DComponent::OnInit()
{
    if (m_Owner) {
        if (auto rb2d = m_Owner->GetComponent<RigidBody2DComponent>()) {
            rb2d->RebuildPhysicsBody();
        } else {
            RecreateStaticBody();
        }
    }
}

void BoxCollider2DComponent::OnUpdate(float deltaTime)
{
    if (!m_Owner) return;

    if (m_Owner->HasComponent<RigidBody2DComponent>()) {
        if (staticBody) DestroyStaticBody();
        return;
    }

    if (!staticBody) {
        RecreateStaticBody();
        return;
    }

    glm::mat4 curMat = m_Owner->getMatrix();
    if (curMat != lastSyncedMatrix) {
        lastSyncedMatrix = curMat;
        RecreateStaticBody();
    }
}

void BoxCollider2DComponent::RecreateStaticBody()
{
    DestroyStaticBody();
    if (!m_Owner || m_Owner->HasComponent<RigidBody2DComponent>()) return;
    if (!SlipPhysics::IsInitialized() || !SlipPhysics::Get().GetDynamicsWorld()) return;

    glm::vec3 pos = m_Owner->position;
    glm::vec3 curScale = m_Owner->scale;
    glm::vec3 rotEuler = m_Owner->rotation;
    if (auto tc = m_Owner->GetTransform()) {
        if (m_Owner->scale != glm::vec3(1.0f) && tc->scale == glm::vec3(1.0f)) tc->scale = m_Owner->scale;
        else if (tc->scale != glm::vec3(1.0f) && m_Owner->scale == glm::vec3(1.0f)) m_Owner->scale = tc->scale;
        curScale = m_Owner->scale;

        if (m_Owner->position != glm::vec3(0.0f) && tc->position == glm::vec3(0.0f)) tc->position = m_Owner->position;
        else if (tc->position != glm::vec3(0.0f) && m_Owner->position == glm::vec3(0.0f)) m_Owner->position = tc->position;
        pos = m_Owner->position;

        if (m_Owner->rotation != glm::vec3(0.0f) && tc->rotation == glm::vec3(0.0f)) tc->rotation = m_Owner->rotation;
        else if (tc->rotation != glm::vec3(0.0f) && m_Owner->rotation == glm::vec3(0.0f)) m_Owner->rotation = tc->rotation;
        rotEuler = m_Owner->rotation;
    }

    float halfX = (std::max)(0.01f, std::abs(size.x * curScale.x) * 0.5f);
    float halfY = (std::max)(0.01f, std::abs(size.y * curScale.y) * 0.5f);
    staticShape = new btBoxShape(btVector3(halfX, halfY, 5.0f));

    glm::vec2 worldOff = offset * glm::vec2(curScale.x, curScale.y);

    btTransform trans;
    trans.setIdentity();
    trans.setOrigin(btVector3(pos.x + worldOff.x, pos.y + worldOff.y, 0.0f));
    btQuaternion rot;
    rot.setEulerZYX(0.0f, 0.0f, glm::radians(rotEuler.z));
    trans.setRotation(rot);

    auto* motionState = new btDefaultMotionState(trans);
    btRigidBody::btRigidBodyConstructionInfo rbInfo(0.0f, motionState, staticShape, btVector3(0, 0, 0));
    staticBody = new btRigidBody(rbInfo);
    staticBody->setUserPointer(m_Owner);
    staticBody->setFriction(friction);
    staticBody->setRestitution(restitution);

    if (isTrigger) {
        staticBody->setCollisionFlags(staticBody->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
    }

    SlipPhysics::Get().GetDynamicsWorld()->addRigidBody(staticBody);
    lastSyncedMatrix = m_Owner->getMatrix();
}

void BoxCollider2DComponent::DestroyStaticBody()
{
    if (staticBody) {
        if (SlipPhysics::IsInitialized() && SlipPhysics::Get().GetDynamicsWorld()) {
            SlipPhysics::Get().GetDynamicsWorld()->removeRigidBody(staticBody);
        }
        if (staticBody->getMotionState()) delete staticBody->getMotionState();
        delete staticBody;
        staticBody = nullptr;
    }
    if (staticShape) {
        delete staticShape;
        staticShape = nullptr;
    }
}

void BoxCollider2DComponent::OnDrawInspectorUI()
{
}

CircleCollider2DComponent::CircleCollider2DComponent()
{
    m_Name = "CircleCollider2DComponent";
}

CircleCollider2DComponent::CircleCollider2DComponent(float r, const glm::vec2& off)
    : radius(r), offset(off)
{
    m_Name = "CircleCollider2DComponent";
}

CircleCollider2DComponent::~CircleCollider2DComponent()
{
    DestroyStaticBody();
}

void CircleCollider2DComponent::OnInit()
{
    if (m_Owner) {
        if (auto rb2d = m_Owner->GetComponent<RigidBody2DComponent>()) {
            rb2d->RebuildPhysicsBody();
        } else {
            RecreateStaticBody();
        }
    }
}

void CircleCollider2DComponent::OnUpdate(float deltaTime)
{
    if (!m_Owner) return;

    if (m_Owner->HasComponent<RigidBody2DComponent>()) {
        if (staticBody) DestroyStaticBody();
        return;
    }

    if (!staticBody) {
        RecreateStaticBody();
        return;
    }

    glm::mat4 curMat = m_Owner->getMatrix();
    if (curMat != lastSyncedMatrix) {
        lastSyncedMatrix = curMat;
        RecreateStaticBody();
    }
}

void CircleCollider2DComponent::RecreateStaticBody()
{
    DestroyStaticBody();
    if (!m_Owner || m_Owner->HasComponent<RigidBody2DComponent>()) return;
    if (!SlipPhysics::IsInitialized() || !SlipPhysics::Get().GetDynamicsWorld()) return;

    glm::vec3 pos = m_Owner->position;
    glm::vec3 curScale = m_Owner->scale;
    glm::vec3 rotEuler = m_Owner->rotation;
    if (auto tc = m_Owner->GetTransform()) {
        if (m_Owner->scale != glm::vec3(1.0f) && tc->scale == glm::vec3(1.0f)) tc->scale = m_Owner->scale;
        else if (tc->scale != glm::vec3(1.0f) && m_Owner->scale == glm::vec3(1.0f)) m_Owner->scale = tc->scale;
        curScale = m_Owner->scale;

        if (m_Owner->position != glm::vec3(0.0f) && tc->position == glm::vec3(0.0f)) tc->position = m_Owner->position;
        else if (tc->position != glm::vec3(0.0f) && m_Owner->position == glm::vec3(0.0f)) m_Owner->position = tc->position;
        pos = m_Owner->position;

        if (m_Owner->rotation != glm::vec3(0.0f) && tc->rotation == glm::vec3(0.0f)) tc->rotation = m_Owner->rotation;
        else if (tc->rotation != glm::vec3(0.0f) && m_Owner->rotation == glm::vec3(0.0f)) m_Owner->rotation = tc->rotation;
        rotEuler = m_Owner->rotation;
    }

    float r = (std::max)(0.01f, radius * (std::max)(std::abs(curScale.x), std::abs(curScale.y)));
    staticShape = new btSphereShape(r);

    glm::vec2 worldOff = offset * glm::vec2(curScale.x, curScale.y);

    btTransform trans;
    trans.setIdentity();
    trans.setOrigin(btVector3(pos.x + worldOff.x, pos.y + worldOff.y, 0.0f));
    btQuaternion rot;
    rot.setEulerZYX(0.0f, 0.0f, glm::radians(rotEuler.z));
    trans.setRotation(rot);

    auto* motionState = new btDefaultMotionState(trans);
    btRigidBody::btRigidBodyConstructionInfo rbInfo(0.0f, motionState, staticShape, btVector3(0, 0, 0));
    staticBody = new btRigidBody(rbInfo);
    staticBody->setUserPointer(m_Owner);
    staticBody->setFriction(friction);
    staticBody->setRestitution(restitution);

    if (isTrigger) {
        staticBody->setCollisionFlags(staticBody->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
    }

    SlipPhysics::Get().GetDynamicsWorld()->addRigidBody(staticBody);
    lastSyncedMatrix = m_Owner->getMatrix();
}

void CircleCollider2DComponent::DestroyStaticBody()
{
    if (staticBody) {
        if (SlipPhysics::IsInitialized() && SlipPhysics::Get().GetDynamicsWorld()) {
            SlipPhysics::Get().GetDynamicsWorld()->removeRigidBody(staticBody);
        }
        if (staticBody->getMotionState()) delete staticBody->getMotionState();
        delete staticBody;
        staticBody = nullptr;
    }
    if (staticShape) {
        delete staticShape;
        staticShape = nullptr;
    }
}

void CircleCollider2DComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// RigidBody2DComponent
// ---------------------------------------------------------
RigidBody2DComponent::RigidBody2DComponent()
{
    m_Name = "RigidBody2DComponent";
}

RigidBody2DComponent::~RigidBody2DComponent()
{
    if (bulletBody) {
        if (SlipPhysics::IsInitialized() && SlipPhysics::Get().GetDynamicsWorld()) {
            SlipPhysics::Get().GetDynamicsWorld()->removeRigidBody(bulletBody);
        }
        if (bulletBody->getMotionState()) {
            delete bulletBody->getMotionState();
        }
        delete bulletBody;
        bulletBody = nullptr;
    }
    if (bulletShape) {
        delete bulletShape;
        bulletShape = nullptr;
    }
}

void RigidBody2DComponent::OnInit()
{
    RebuildPhysicsBody();
}

void RigidBody2DComponent::OnStart()
{
    RebuildPhysicsBody();
}

void RigidBody2DComponent::RebuildPhysicsBody()
{
    if (!m_Owner) return;

    if (bulletBody) {
        if (SlipPhysics::IsInitialized() && SlipPhysics::Get().GetDynamicsWorld()) {
            SlipPhysics::Get().GetDynamicsWorld()->removeRigidBody(bulletBody);
        }
        if (bulletBody->getMotionState()) delete bulletBody->getMotionState();
        delete bulletBody;
        bulletBody = nullptr;
    }
    if (bulletShape) {
        delete bulletShape;
        bulletShape = nullptr;
    }

    if (!SlipPhysics::IsInitialized() || !SlipPhysics::Get().GetDynamicsWorld()) return;

    // Asegurar que los colliders 2D de esta entidad no tengan cuerpo estático duplicado
    if (auto box2d = m_Owner->GetComponent<BoxCollider2DComponent>()) {
        box2d->DestroyStaticBody();
    }
    if (auto circ2d = m_Owner->GetComponent<CircleCollider2DComponent>()) {
        circ2d->DestroyStaticBody();
    }

    // Si la entidad tiene un ColliderComponent 3D previo (ej. un Cubo 3D primitivo),
    // remover su body estático para evitar auto-colisión interna que impida la caída
    if (auto col3d = m_Owner->GetComponent<ColliderComponent>()) {
        if (col3d->rigidBody) {
            SlipPhysics::Get().GetDynamicsWorld()->removeRigidBody(col3d->rigidBody);
        }
    }

    glm::vec3 currentPos = m_Owner->position;
    glm::vec3 currentScale = m_Owner->scale;
    glm::vec3 currentRot = m_Owner->rotation;
    if (auto tc = m_Owner->GetTransform()) {
        if (m_Owner->scale != glm::vec3(1.0f) && tc->scale == glm::vec3(1.0f)) tc->scale = m_Owner->scale;
        else if (tc->scale != glm::vec3(1.0f) && m_Owner->scale == glm::vec3(1.0f)) m_Owner->scale = tc->scale;
        currentScale = m_Owner->scale;

        if (m_Owner->position != glm::vec3(0.0f) && tc->position == glm::vec3(0.0f)) tc->position = m_Owner->position;
        else if (tc->position != glm::vec3(0.0f) && m_Owner->position == glm::vec3(0.0f)) m_Owner->position = tc->position;
        currentPos = m_Owner->position;

        if (m_Owner->rotation != glm::vec3(0.0f) && tc->rotation == glm::vec3(0.0f)) tc->rotation = m_Owner->rotation;
        else if (tc->rotation != glm::vec3(0.0f) && m_Owner->rotation == glm::vec3(0.0f)) m_Owner->rotation = tc->rotation;
        currentRot = m_Owner->rotation;
    }

    glm::vec2 colOffset(0.0f);
    if (auto box = m_Owner->GetComponent<BoxCollider2DComponent>()) {
        float halfX = (std::max)(0.01f, std::abs(box->size.x * currentScale.x) * 0.5f);
        float halfY = (std::max)(0.01f, std::abs(box->size.y * currentScale.y) * 0.5f);
        bulletShape = new btBoxShape(btVector3(halfX, halfY, 5.0f));
        colOffset = box->offset * glm::vec2(currentScale.x, currentScale.y);
    } else if (auto circle = m_Owner->GetComponent<CircleCollider2DComponent>()) {
        float r = (std::max)(0.01f, circle->radius * (std::max)(std::abs(currentScale.x), std::abs(currentScale.y)));
        bulletShape = new btSphereShape(r);
        colOffset = circle->offset * glm::vec2(currentScale.x, currentScale.y);
    } else if (auto box3d = m_Owner->GetComponent<BoxColliderComponent>()) {
        // Adaptación automática si es un Cubo / Objeto 3D con BoxCollider convencional
        float halfX = (std::max)(0.01f, std::abs(box3d->size.x * currentScale.x) * 0.5f);
        float halfY = (std::max)(0.01f, std::abs(box3d->size.y * currentScale.y) * 0.5f);
        bulletShape = new btBoxShape(btVector3(halfX, halfY, 5.0f));
        colOffset = glm::vec2(box3d->centerOffset.x * currentScale.x, box3d->centerOffset.y * currentScale.y);
    } else if (auto sph3d = m_Owner->GetComponent<SphereColliderComponent>()) {
        float r = (std::max)(0.01f, sph3d->radius * (std::max)(std::abs(currentScale.x), std::abs(currentScale.y)));
        bulletShape = new btSphereShape(r);
        colOffset = glm::vec2(sph3d->centerOffset.x * currentScale.x, sph3d->centerOffset.y * currentScale.y);
    } else if (auto sr = m_Owner->GetComponent<SpriteRendererComponent>()) {
        glm::vec2 qSize = sr->GetBaseQuadSize();
        float halfX = (std::max)(0.01f, qSize.x * std::abs(currentScale.x) * 0.5f);
        float halfY = (std::max)(0.01f, qSize.y * std::abs(currentScale.y) * 0.5f);
        bulletShape = new btBoxShape(btVector3(halfX, halfY, 5.0f));
    } else {
        float halfX = (std::max)(0.01f, 0.5f * std::abs(currentScale.x));
        float halfY = (std::max)(0.01f, 0.5f * std::abs(currentScale.y));
        bulletShape = new btBoxShape(btVector3(halfX, halfY, 5.0f));
    }

    float m = (bodyType == BodyType2D::Dynamic) ? mass : 0.0f;
    btVector3 inertia(0, 0, 0);
    if (m > 0.0f) {
        bulletShape->calculateLocalInertia(m, inertia);
    }

    btTransform trans;
    trans.setIdentity();
    trans.setOrigin(btVector3(currentPos.x + colOffset.x, currentPos.y + colOffset.y, 0.0f));
    btQuaternion rot;
    rot.setEulerZYX(0.0f, 0.0f, glm::radians(currentRot.z));
    trans.setRotation(rot);

    auto* motionState = new btDefaultMotionState(trans);
    btRigidBody::btRigidBodyConstructionInfo rbInfo(m, motionState, bulletShape, inertia);
    bulletBody = new btRigidBody(rbInfo);

    // Restricciones 2D nativas: plano XY sin desplazamientos en Z y rotación sólo en Z
    bulletBody->setLinearFactor(btVector3(1.0f, 1.0f, 0.0f));
    bulletBody->setAngularFactor(btVector3(0.0f, 0.0f, freezeRotation ? 0.0f : 1.0f));
    bulletBody->setDamping(linearDamping, angularDamping);
    bulletBody->setUserPointer(m_Owner);

    // Asignar fricción, rebote y trigger desde los colliders
    if (auto box = m_Owner->GetComponent<BoxCollider2DComponent>()) {
        bulletBody->setFriction(box->friction);
        bulletBody->setRestitution(box->restitution);
        if (box->isTrigger) {
            bulletBody->setCollisionFlags(bulletBody->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
        }
    } else if (auto circle = m_Owner->GetComponent<CircleCollider2DComponent>()) {
        bulletBody->setFriction(circle->friction);
        bulletBody->setRestitution(circle->restitution);
        if (circle->isTrigger) {
            bulletBody->setCollisionFlags(bulletBody->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
        }
    }

    if (bodyType == BodyType2D::Kinematic) {
        bulletBody->setCollisionFlags(bulletBody->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
        bulletBody->setActivationState(DISABLE_DEACTIVATION);
    }

    // Prevenir efecto túnel (CCD - Continuous Collision Detection)
    bulletBody->setCcdMotionThreshold(0.05f);
    bulletBody->setCcdSweptSphereRadius(0.1f);

    SlipPhysics::Get().GetDynamicsWorld()->addRigidBody(bulletBody);

    if (bodyType == BodyType2D::Dynamic) {
        glm::vec3 g = SlipPhysics::Get().getGravity();
        bulletBody->setGravity(btVector3(g.x, g.y * gravityScale, 0.0f));
        bulletBody->activate(true);
    }
}

void RigidBody2DComponent::OnUpdate(float deltaTime)
{
    if (!bulletBody || !m_Owner) return;

    if (SlipPhysics::Get().m_Simulating) {
        if (bodyType == BodyType2D::Dynamic) {
            SyncTransformFromPhysics();
        } else if (bodyType == BodyType2D::Kinematic) {
            SyncPhysicsFromTransform();
        }
    } else {
        // En modo Edición, sincronizar la posición del cuerpo desde el Transform editado
        SyncPhysicsFromTransform();
    }
}

void RigidBody2DComponent::SyncPhysicsFromTransform()
{
    if (!bulletBody || !m_Owner) return;

    glm::vec3 currentPos = m_Owner->position;
    glm::vec3 currentScale = m_Owner->scale;
    glm::vec3 currentRot = m_Owner->rotation;
    if (auto tc = m_Owner->GetTransform()) {
        currentPos = tc->position;
        currentScale = tc->scale;
        currentRot = tc->rotation;
        m_Owner->position = tc->position;
        m_Owner->scale = tc->scale;
        m_Owner->rotation = tc->rotation;
    }

    glm::vec2 colOffset(0.0f);
    if (auto box = m_Owner->GetComponent<BoxCollider2DComponent>()) colOffset = box->offset * glm::vec2(currentScale.x, currentScale.y);
    else if (auto circle = m_Owner->GetComponent<CircleCollider2DComponent>()) colOffset = circle->offset * glm::vec2(currentScale.x, currentScale.y);
    else if (auto box3d = m_Owner->GetComponent<BoxColliderComponent>()) colOffset = glm::vec2(box3d->centerOffset.x * currentScale.x, box3d->centerOffset.y * currentScale.y);

    btTransform trans;
    trans.setIdentity();
    trans.setOrigin(btVector3(currentPos.x + colOffset.x, currentPos.y + colOffset.y, 0.0f));
    btQuaternion rot;
    rot.setEulerZYX(0.0f, 0.0f, glm::radians(currentRot.z));
    trans.setRotation(rot);

    bulletBody->setWorldTransform(trans);
    if (bulletBody->getMotionState()) {
        bulletBody->getMotionState()->setWorldTransform(trans);
    }
}

void RigidBody2DComponent::SyncTransformFromPhysics()
{
    if (!bulletBody || !m_Owner) return;

    btTransform trans;
    if (bulletBody->getMotionState()) {
        bulletBody->getMotionState()->getWorldTransform(trans);
    } else {
        trans = bulletBody->getWorldTransform();
    }

    glm::vec3 curScale = m_Owner->scale;
    if (auto tc = m_Owner->GetTransform()) {
        curScale = tc->scale;
    }

    glm::vec2 colOffset(0.0f);
    if (auto box = m_Owner->GetComponent<BoxCollider2DComponent>()) colOffset = box->offset * glm::vec2(curScale.x, curScale.y);
    else if (auto circle = m_Owner->GetComponent<CircleCollider2DComponent>()) colOffset = circle->offset * glm::vec2(curScale.x, curScale.y);
    else if (auto box3d = m_Owner->GetComponent<BoxColliderComponent>()) colOffset = glm::vec2(box3d->centerOffset.x * curScale.x, box3d->centerOffset.y * curScale.y);

    btVector3 origin = trans.getOrigin();
    m_Owner->position.x = origin.x() - colOffset.x;
    m_Owner->position.y = origin.y() - colOffset.y;
    m_Owner->position.z = 0.0f;

    btScalar yawZ, pitchY, rollX;
    trans.getRotation().getEulerZYX(yawZ, pitchY, rollX);
    m_Owner->rotation.z = glm::degrees(yawZ);

    if (auto tc = m_Owner->GetTransform()) {
        tc->position = m_Owner->position;
        tc->rotation = m_Owner->rotation;
    }

    btVector3 vel = bulletBody->getLinearVelocity();
    linearVelocity = glm::vec2(vel.x(), vel.y());
    angularVelocity = bulletBody->getAngularVelocity().z();
}

void RigidBody2DComponent::ApplyForce(const glm::vec2& force)
{
    if (bulletBody) {
        bulletBody->activate(true);
        bulletBody->applyCentralForce(btVector3(force.x, force.y, 0.0f));
    }
}

void RigidBody2DComponent::ApplyImpulse(const glm::vec2& impulse)
{
    if (bulletBody) {
        bulletBody->activate(true);
        bulletBody->applyCentralImpulse(btVector3(impulse.x, impulse.y, 0.0f));
    }
}

void RigidBody2DComponent::SetLinearVelocity(const glm::vec2& vel)
{
    linearVelocity = vel;
    if (bulletBody) {
        bulletBody->activate(true);
        bulletBody->setLinearVelocity(btVector3(vel.x, vel.y, 0.0f));
    }
}

glm::vec2 RigidBody2DComponent::GetLinearVelocity() const
{
    if (bulletBody) {
        btVector3 v = bulletBody->getLinearVelocity();
        return glm::vec2(v.x(), v.y());
    }
    return linearVelocity;
}

void RigidBody2DComponent::SetAngularVelocity(float omega)
{
    angularVelocity = omega;
    if (bulletBody) {
        bulletBody->activate(true);
        bulletBody->setAngularVelocity(btVector3(0.0f, 0.0f, omega));
    }
}

float RigidBody2DComponent::GetAngularVelocity() const
{
    if (bulletBody) {
        return bulletBody->getAngularVelocity().z();
    }
    return angularVelocity;
}

void RigidBody2DComponent::SetGravityScale(float scale)
{
    gravityScale = scale;
    if (bulletBody) {
        glm::vec3 g = SlipPhysics::Get().getGravity();
        bulletBody->setGravity(btVector3(g.x, g.y * gravityScale, 0.0f));
    }
}

void RigidBody2DComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// Luces 2D y Colisión de Luces / Sombras 2D
// ---------------------------------------------------------
Light2DComponent::Light2DComponent()
{
    m_Name = "Light2DComponent";
}

void Light2DComponent::OnDrawInspectorUI()
{
}

ShadowCaster2DComponent::ShadowCaster2DComponent()
{
    m_Name = "ShadowCaster2DComponent";
}

void ShadowCaster2DComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// TileGridComponent
// ---------------------------------------------------------
TileGridComponent::TileGridComponent()
{
    m_Name = "TileGridComponent";
}

TileGridComponent::~TileGridComponent()
{
    if (tileSheet) {
        delete tileSheet;
        tileSheet = nullptr;
    }
}

void TileGridComponent::OnInit()
{
    if (!tileSheetPath.empty() && !tileSheet) {
        SetTileSheet(tileSheetPath);
    }
}

void TileGridComponent::SetTileSheet(const std::string& path)
{
    if (tileSheet) {
        delete tileSheet;
        tileSheet = nullptr;
    }
    tileSheetPath = path;
    if (!path.empty()) {
        std::string resolved = ResolveTextureAssetPath(path);
        tileSheet = SlipTexture::load(resolved.c_str());
        if (tileSheet && isPixelArt) {
            tileSheet->SetFilterMode(true);
        }
    }
}

void TileGridComponent::SetTile(int x, int y, int tileIndex)
{
    tiles[std::make_pair(x, y)] = tileIndex;
}

void TileGridComponent::RemoveTile(int x, int y)
{
    tiles.erase(std::make_pair(x, y));
}

int TileGridComponent::GetTile(int x, int y) const
{
    auto it = tiles.find(std::make_pair(x, y));
    if (it != tiles.end()) return it->second;
    return -1;
}

bool TileGridComponent::HasTile(int x, int y) const
{
    return tiles.find(std::make_pair(x, y)) != tiles.end();
}

void TileGridComponent::Clear()
{
    tiles.clear();
}

glm::vec2 TileGridComponent::GetAnchorOffset() const
{
    switch (anchor) {
        case TileGridAnchor::Center:
            return glm::vec2(0.0f, 0.0f);
        case TileGridAnchor::BottomLeft:
            return glm::vec2(cellSize.x * 0.5f, cellSize.y * 0.5f);
        case TileGridAnchor::TopLeft:
            return glm::vec2(cellSize.x * 0.5f, -cellSize.y * 0.5f);
        case TileGridAnchor::BottomCenter:
            return glm::vec2(0.0f, cellSize.y * 0.5f);
        default:
            return glm::vec2(0.0f, 0.0f);
    }
}

glm::vec2 TileGridComponent::CellToWorld(int x, int y) const
{
    glm::vec2 origin(0.0f);
    if (m_Owner) {
        origin = glm::vec2(m_Owner->position.x, m_Owner->position.y);
    }
    glm::vec2 cellPos = glm::vec2(static_cast<float>(x) * cellSize.x, static_cast<float>(y) * cellSize.y);
    return origin + cellPos + GetAnchorOffset();
}

std::pair<int, int> TileGridComponent::WorldToCell(const glm::vec2& worldPos) const
{
    glm::vec2 origin(0.0f);
    if (m_Owner) {
        origin = glm::vec2(m_Owner->position.x, m_Owner->position.y);
    }
    glm::vec2 local = worldPos - origin - GetAnchorOffset();
    int x = static_cast<int>(std::floor((local.x + cellSize.x * 0.5f) / cellSize.x));
    int y = static_cast<int>(std::floor((local.y + cellSize.y * 0.5f) / cellSize.y));
    return std::make_pair(x, y);
}

glm::vec4 TileGridComponent::GetTileUV(int tileIndex) const
{
    int cols = (std::max)(1, sheetHFrames);
    int rows = (std::max)(1, sheetVFrames);
    int total = cols * rows;
    int frame = ((tileIndex % total) + total) % total;

    int col = frame % cols;
    int row = frame / cols;

    float uScale = 1.0f / static_cast<float>(cols);
    float vScale = 1.0f / static_cast<float>(rows);
    float uOffset = static_cast<float>(col) * uScale;
    float vOffset = static_cast<float>(row) * vScale;

    return glm::vec4(uOffset, vOffset, uScale, vScale);
}

int TileGridComponent::GetSortingLayerIndex() const
{
    return SlipSortingLayers::Get().GetLayerIndex(sortingLayerName);
}

void TileGridComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// CanvasComponent
// ---------------------------------------------------------
CanvasComponent::CanvasComponent()
{
    m_Name = "CanvasComponent";
}

void CanvasComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// RectTransformComponent
// ---------------------------------------------------------
RectTransformComponent::RectTransformComponent()
{
    m_Name = "RectTransformComponent";
    ApplyAnchorPreset(UIAnchorPreset::Center);
}

void RectTransformComponent::ApplyAnchorPreset(UIAnchorPreset preset)
{
    anchorPreset = preset;
    switch (preset) {
        case UIAnchorPreset::TopLeft:
            anchorMin = glm::vec2(0.0f, 1.0f);
            anchorMax = glm::vec2(0.0f, 1.0f);
            break;
        case UIAnchorPreset::TopCenter:
            anchorMin = glm::vec2(0.5f, 1.0f);
            anchorMax = glm::vec2(0.5f, 1.0f);
            break;
        case UIAnchorPreset::TopRight:
            anchorMin = glm::vec2(1.0f, 1.0f);
            anchorMax = glm::vec2(1.0f, 1.0f);
            break;
        case UIAnchorPreset::MiddleLeft:
            anchorMin = glm::vec2(0.0f, 0.5f);
            anchorMax = glm::vec2(0.0f, 0.5f);
            break;
        case UIAnchorPreset::Center:
            anchorMin = glm::vec2(0.5f, 0.5f);
            anchorMax = glm::vec2(0.5f, 0.5f);
            break;
        case UIAnchorPreset::MiddleRight:
            anchorMin = glm::vec2(1.0f, 0.5f);
            anchorMax = glm::vec2(1.0f, 0.5f);
            break;
        case UIAnchorPreset::BottomLeft:
            anchorMin = glm::vec2(0.0f, 0.0f);
            anchorMax = glm::vec2(0.0f, 0.0f);
            break;
        case UIAnchorPreset::BottomCenter:
            anchorMin = glm::vec2(0.5f, 0.0f);
            anchorMax = glm::vec2(0.5f, 0.0f);
            break;
        case UIAnchorPreset::BottomRight:
            anchorMin = glm::vec2(1.0f, 0.0f);
            anchorMax = glm::vec2(1.0f, 0.0f);
            break;
        case UIAnchorPreset::StretchAll:
            anchorMin = glm::vec2(0.0f, 0.0f);
            anchorMax = glm::vec2(1.0f, 1.0f);
            break;
    }
}

void RectTransformComponent::SetAnchorPresetByName(const std::string& presetName)
{
    if (presetName == "TopLeft") ApplyAnchorPreset(UIAnchorPreset::TopLeft);
    else if (presetName == "TopCenter") ApplyAnchorPreset(UIAnchorPreset::TopCenter);
    else if (presetName == "TopRight") ApplyAnchorPreset(UIAnchorPreset::TopRight);
    else if (presetName == "MiddleLeft") ApplyAnchorPreset(UIAnchorPreset::MiddleLeft);
    else if (presetName == "MiddleRight") ApplyAnchorPreset(UIAnchorPreset::MiddleRight);
    else if (presetName == "BottomLeft") ApplyAnchorPreset(UIAnchorPreset::BottomLeft);
    else if (presetName == "BottomCenter") ApplyAnchorPreset(UIAnchorPreset::BottomCenter);
    else if (presetName == "BottomRight") ApplyAnchorPreset(UIAnchorPreset::BottomRight);
    else if (presetName == "StretchAll") ApplyAnchorPreset(UIAnchorPreset::StretchAll);
    else ApplyAnchorPreset(UIAnchorPreset::Center);
}

std::string RectTransformComponent::GetAnchorPresetName() const
{
    switch (anchorPreset) {
        case UIAnchorPreset::TopLeft: return "TopLeft";
        case UIAnchorPreset::TopCenter: return "TopCenter";
        case UIAnchorPreset::TopRight: return "TopRight";
        case UIAnchorPreset::MiddleLeft: return "MiddleLeft";
        case UIAnchorPreset::MiddleRight: return "MiddleRight";
        case UIAnchorPreset::BottomLeft: return "BottomLeft";
        case UIAnchorPreset::BottomCenter: return "BottomCenter";
        case UIAnchorPreset::BottomRight: return "BottomRight";
        case UIAnchorPreset::StretchAll: return "StretchAll";
        case UIAnchorPreset::Center:
        default:
            return "Center";
    }
}

glm::vec4 RectTransformComponent::CalculateCanvasRect(const glm::vec4& parentRect) const
{
    if (anchorPreset == UIAnchorPreset::StretchAll) {
        return parentRect;
    }

    float anchorX = 0.5f * (anchorMin.x + anchorMax.x);
    float anchorY = 0.5f * (anchorMin.y + anchorMax.y);

    float refPointX = parentRect.x + anchorX * parentRect.z;
    float refPointY = parentRect.y + (1.0f - anchorY) * parentRect.w;

    float pivotScreenX = refPointX + anchoredPosition.x;
    float pivotScreenY = refPointY - anchoredPosition.y;

    float w = (std::max)(1.0f, size.x);
    float h = (std::max)(1.0f, size.y);

    float topLeftX = pivotScreenX - pivot.x * w;
    float topLeftY = pivotScreenY - (1.0f - pivot.y) * h;

    return glm::vec4(topLeftX, topLeftY, w, h);
}

void RectTransformComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// UIImageComponent
// ---------------------------------------------------------
UIImageComponent::UIImageComponent()
{
    m_Name = "UIImageComponent";
}

UIImageComponent::UIImageComponent(const std::string& path)
{
    m_Name = "UIImageComponent";
    SetTexture(path);
}

UIImageComponent::~UIImageComponent()
{
    if (texture) {
        delete texture;
        texture = nullptr;
    }
}

void UIImageComponent::SetTexture(const std::string& path)
{
    if (texture) {
        delete texture;
        texture = nullptr;
    }
    texturePath = path;
    if (!path.empty()) {
        std::string resolved = ResolveTextureAssetPath(path);
        texture = SlipTexture::load(resolved.c_str());
    }
}

void UIImageComponent::OnInit()
{
    if (!texturePath.empty() && !texture) {
        SetTexture(texturePath);
    }
}

void UIImageComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// UITextComponent
// ---------------------------------------------------------
UITextComponent::UITextComponent()
{
    m_Name = "UITextComponent";
}

UITextComponent::UITextComponent(const std::string& initialText)
    : text(initialText)
{
    m_Name = "UITextComponent";
}

void UITextComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// UIButtonComponent
// ---------------------------------------------------------
struct UIButtonComponent::CallbackImpl {
    sol::protected_function luaCallback;
};

UIButtonComponent::UIButtonComponent()
    : m_CallbackImpl(std::make_unique<CallbackImpl>())
{
    m_Name = "UIButtonComponent";
}

UIButtonComponent::~UIButtonComponent() = default;

glm::vec4 UIButtonComponent::GetCurrentStateColor() const
{
    if (!interactable) return disabledColor;
    if (isPressed) return pressedColor;
    if (isHovered) return hoverColor;
    return normalColor;
}

void UIButtonComponent::SetLuaCallback(void* solFunctionPtr)
{
    if (!m_CallbackImpl) m_CallbackImpl = std::make_unique<CallbackImpl>();
    if (solFunctionPtr) {
        m_CallbackImpl->luaCallback = *static_cast<sol::protected_function*>(solFunctionPtr);
    } else {
        m_CallbackImpl->luaCallback = sol::lua_nil;
    }
}

void UIButtonComponent::TriggerClick()
{
    if (!m_Enabled || !interactable) return;
    clickedThisFrame = true;

    if (m_CallbackImpl && m_CallbackImpl->luaCallback.valid()) {
        auto res = m_CallbackImpl->luaCallback(m_Owner);
        if (!res.valid()) {
            sol::error err = res;
            SlipLuaEngine::Get().ReportError(err.what());
        }
    }

    if (!onClickFunction.empty()) {
        SlipEntity* target = m_Owner;
        if (!targetEntityName.empty()) {
            target = nullptr;
            for (SlipEntity* ent : SlipLevel::Get().GetEntities()) {
                if (ent && ent->GetName() == targetEntityName) {
                    target = ent;
                    break;
                }
            }
        }
        if (target) {
            for (auto& sc : target->GetScripts()) {
                if (sc && sc->IsEnabled()) {
                    sc->InvokeLuaFunction(onClickFunction);
                }
            }
        }
    }
}

void UIButtonComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// UISliderComponent
// ---------------------------------------------------------
struct UISliderComponent::CallbackImpl {
    sol::protected_function luaCallback;
};

UISliderComponent::UISliderComponent()
    : m_CallbackImpl(std::make_unique<CallbackImpl>())
{
    m_Name = "UISliderComponent";
}

UISliderComponent::~UISliderComponent() = default;

float UISliderComponent::GetNormalizedValue() const
{
    if (std::abs(maxValue - minValue) < 0.00001f) return 0.0f;
    return glm::clamp((value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
}

void UISliderComponent::SetNormalizedValue(float norm, bool notify)
{
    norm = glm::clamp(norm, 0.0f, 1.0f);
    SetValue(minValue + norm * (maxValue - minValue), notify);
}

void UISliderComponent::SetLuaCallback(void* solFunctionPtr)
{
    if (!m_CallbackImpl) m_CallbackImpl = std::make_unique<CallbackImpl>();
    if (solFunctionPtr) {
        m_CallbackImpl->luaCallback = *static_cast<sol::protected_function*>(solFunctionPtr);
    } else {
        m_CallbackImpl->luaCallback = sol::lua_nil;
    }
}

void UISliderComponent::SetValue(float newValue, bool notify)
{
    float lo = (std::min)(minValue, maxValue);
    float hi = (std::max)(minValue, maxValue);
    float clamped = glm::clamp(newValue, lo, hi);
    if (wholeNumbers) {
        clamped = std::round(clamped);
    }

    bool changed = std::abs(clamped - value) > 0.0001f;
    value = clamped;

    if (notify && changed && m_Enabled && interactable) {
        if (m_CallbackImpl && m_CallbackImpl->luaCallback.valid()) {
            auto res = m_CallbackImpl->luaCallback(value, m_Owner);
            if (!res.valid()) {
                sol::error err = res;
                SlipLuaEngine::Get().ReportError(err.what());
            }
        }

        if (!onValueChangedFunction.empty()) {
            SlipEntity* target = m_Owner;
            if (!targetEntityName.empty()) {
                target = nullptr;
                for (SlipEntity* ent : SlipLevel::Get().GetEntities()) {
                    if (ent && ent->GetName() == targetEntityName) {
                        target = ent;
                        break;
                    }
                }
            }
            if (target) {
                for (auto& sc : target->GetScripts()) {
                    if (sc && sc->IsEnabled()) {
                        sc->InvokeLuaFunctionFloat(onValueChangedFunction, value);
                    }
                }
            }
        }
    }
}

void UISliderComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// UIProgressBarComponent
// ---------------------------------------------------------
UIProgressBarComponent::UIProgressBarComponent()
{
    m_Name = "UIProgressBarComponent";
}

float UIProgressBarComponent::GetNormalizedValue() const
{
    if (std::abs(maxValue - minValue) < 0.00001f) return 0.0f;
    return glm::clamp((value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
}

void UIProgressBarComponent::SetValue(float newValue)
{
    float lo = (std::min)(minValue, maxValue);
    float hi = (std::max)(minValue, maxValue);
    value = glm::clamp(newValue, lo, hi);
}

UIJoystickComponent::UIJoystickComponent()
{
    m_Name = "UIJoystickComponent";
}

void UIJoystickComponent::OnDrawInspectorUI()
{
}

UIActionButtonComponent::UIActionButtonComponent()
{
    m_Name = "UIActionButtonComponent";
}

void UIActionButtonComponent::OnDrawInspectorUI()
{
}

void UIProgressBarComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// ParticleSystemComponent
// ---------------------------------------------------------
ParticleSystemComponent::ParticleSystemComponent()
{
    m_Name = "ParticleSystemComponent";
    gradientStops = {
        { 0.0f, glm::vec4(1.0f, 0.95f, 0.65f, 1.0f) },
        { 0.35f, glm::vec4(1.0f, 0.55f, 0.12f, 0.95f) },
        { 0.75f, glm::vec4(0.85f, 0.15f, 0.05f, 0.60f) },
        { 1.0f, glm::vec4(0.20f, 0.20f, 0.20f, 0.0f) }
    };
}

ParticleSystemComponent::ParticleSystemComponent(ParticleRenderMode mode)
    : ParticleSystemComponent()
{
    renderMode = mode;
    if (mode == ParticleRenderMode::PointNiagara) {
        ApplyPreset("NiagaraSparks");
    } else {
        ApplyPreset("Fire");
    }
}

ParticleSystemComponent::~ParticleSystemComponent()
{
    if (texture) {
        delete texture;
        texture = nullptr;
    }
}

uint32_t ParticleSystemComponent::NextRandom()
{
    uint32_t x = m_RngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    m_RngState = (x == 0u) ? 0x1337BEEFu : x;
    return m_RngState;
}

float ParticleSystemComponent::RandomRange(float minVal, float maxVal)
{
    if (std::abs(maxVal - minVal) < 0.00001f) return minVal;
    float t = static_cast<float>(NextRandom() & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
    return minVal + t * (maxVal - minVal);
}

void ParticleSystemComponent::Play()
{
    m_IsPlaying = true;
    m_IsPaused = false;
}

void ParticleSystemComponent::Pause()
{
    if (m_IsPlaying) {
        m_IsPaused = true;
    }
}

void ParticleSystemComponent::Stop()
{
    m_IsPlaying = false;
    m_IsPaused = false;
    m_SystemTime = 0.0f;
    m_EmissionAccumulator = 0.0f;
    m_Particles.clear();
    for (auto& b : bursts) {
        b.triggered = false;
    }
}

void ParticleSystemComponent::Restart()
{
    Stop();
    Play();
}

void ParticleSystemComponent::SetTexture(const std::string& path)
{
    if (texture) {
        delete texture;
        texture = nullptr;
    }
    texturePath = path;
    if (!path.empty()) {
        std::string resolved = ResolveTextureAssetPath(path);
        texture = SlipTexture::load(resolved.c_str());
    }
}

glm::vec4 ParticleSystemComponent::GetUVRectForNormalizedAge(float normAge, uint32_t seed) const
{
    int cols = (std::max)(1, hFrames);
    int rows = (std::max)(1, vFrames);
    int total = cols * rows;
    if (total <= 1) {
        return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    }

    int frame = 0;
    if (animateSpriteSheet) {
        float clampedAge = glm::clamp(normAge, 0.0f, 0.9999f);
        frame = static_cast<int>(clampedAge * static_cast<float>(total));
    } else {
        frame = static_cast<int>(seed % static_cast<uint32_t>(total));
    }

    int col = frame % cols;
    int row = frame / cols;
    float uScale = 1.0f / static_cast<float>(cols);
    float vScale = 1.0f / static_cast<float>(rows);
    float uOffset = static_cast<float>(col) * uScale;
    float vOffset = static_cast<float>(row) * vScale;
    return glm::vec4(uOffset, vOffset, uScale, vScale);
}

glm::vec4 ParticleSystemComponent::EvaluateGradient(float normalizedTime) const
{
    if (gradientStops.empty()) {
        return startColor;
    }
    if (gradientStops.size() == 1) {
        return gradientStops.front().color;
    }

    float t = glm::clamp(normalizedTime, 0.0f, 1.0f);
    if (t <= gradientStops.front().t) {
        return gradientStops.front().color;
    }
    if (t >= gradientStops.back().t) {
        return gradientStops.back().color;
    }

    for (size_t i = 0; i + 1 < gradientStops.size(); ++i) {
        const auto& a = gradientStops[i];
        const auto& b = gradientStops[i + 1];
        if (t >= a.t && t <= b.t) {
            float span = b.t - a.t;
            if (span <= 0.00001f) return b.color;
            float localT = (t - a.t) / span;
            return glm::mix(a.color, b.color, localT);
        }
    }
    return gradientStops.back().color;
}

glm::vec4 ParticleSystemComponent::EvaluateParticleColor(const ParticleInstance& p) const
{
    float normAge = (p.lifetime > 0.0001f) ? glm::clamp(p.age / p.lifetime, 0.0f, 1.0f) : 1.0f;
    switch (colorMode) {
        case ParticleColorMode::Fixed:
            return startColor;
        case ParticleColorMode::RandomBetweenTwo:
            return p.baseStartColor;
        case ParticleColorMode::GradientOverLifetime:
        default:
            return EvaluateGradient(normAge);
    }
}

void ParticleSystemComponent::Emit(int count)
{
    if (count <= 0) return;

    glm::mat4 worldMat(1.0f);
    glm::mat3 rotMat(1.0f);
    if (m_Owner) {
        worldMat = m_Owner->getMatrix();
        glm::vec3 r = m_Owner->rotation;
        if (auto tc = m_Owner->GetComponent<TransformComponent>()) {
            r = tc->rotation;
        }
        glm::mat4 rm = glm::rotate(glm::mat4(1.0f), glm::radians(r.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
                       glm::rotate(glm::mat4(1.0f), glm::radians(r.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
                       glm::rotate(glm::mat4(1.0f), glm::radians(r.z), glm::vec3(0.0f, 0.0f, 1.0f));
        rotMat = glm::mat3(rm);
    }

    int limit = (std::max)(1, maxParticles);
    for (int i = 0; i < count; ++i) {
        if (static_cast<int>(m_Particles.size()) >= limit) {
            break;
        }
        SpawnSingleParticle(worldMat, rotMat);
    }
}

void ParticleSystemComponent::SpawnSingleParticle(const glm::mat4& emitterWorldMat, const glm::mat3& emitterRotMat)
{
    ParticleInstance p;
    p.randomSeed = NextRandom();
    p.age = 0.0f;
    p.lifetime = (std::max)(0.05f, RandomRange(lifetimeMin, lifetimeMax));
    p.startSize = (std::max)(0.005f, RandomRange(startSizeMin, startSizeMax));
    p.endSize = (std::max)(0.0f, p.startSize * endSizeMultiplier);
    p.size = p.startSize;
    p.rotation = RandomRange(startRotationMin, startRotationMax);
    p.angularVelocity = RandomRange(angularVelocityMin, angularVelocityMax);

    if (colorMode == ParticleColorMode::RandomBetweenTwo) {
        float lerpColor = RandomRange(0.0f, 1.0f);
        p.baseStartColor = glm::mix(startColor, startColor2, lerpColor);
    } else {
        p.baseStartColor = startColor;
    }

    // Calcular posición y dirección local según la forma del emisor
    glm::vec3 localPos(0.0f);
    glm::vec3 localDir(0.0f, 1.0f, 0.0f);
    const float kTwoPi = 6.28318530718f;

    switch (emitterShape) {
        case ParticleEmitterShape::Point: {
            localPos = glm::vec3(0.0f);
            float theta = RandomRange(0.0f, kTwoPi);
            float z = RandomRange(-1.0f, 1.0f);
            float r = std::sqrt((std::max)(0.0f, 1.0f - z * z));
            localDir = glm::vec3(r * std::cos(theta), std::abs(z), r * std::sin(theta));
            if (glm::length(localDir) < 0.0001f) localDir = glm::vec3(0.0f, 1.0f, 0.0f);
            else localDir = glm::normalize(localDir);
            break;
        }
        case ParticleEmitterShape::Sphere: {
            float theta = RandomRange(0.0f, kTwoPi);
            float u = RandomRange(-1.0f, 1.0f);
            float s = std::sqrt((std::max)(0.0f, 1.0f - u * u));
            glm::vec3 unitDir(s * std::cos(theta), u, s * std::sin(theta));
            float rad = emitFromShell ? shapeRadius : (shapeRadius * std::cbrt(RandomRange(0.0f, 1.0f)));
            localPos = unitDir * rad;
            localDir = unitDir;
            break;
        }
        case ParticleEmitterShape::Cone: {
            float angleRad = glm::radians(glm::clamp(coneAngle, 0.0f, 89.0f));
            float phi = RandomRange(0.0f, kTwoPi);
            float baseR = emitFromShell ? shapeRadius : (shapeRadius * std::sqrt(RandomRange(0.0f, 1.0f)));
            localPos = glm::vec3(baseR * std::cos(phi), 0.0f, baseR * std::sin(phi));

            float tanAngle = std::tan(angleRad);
            float radialFrac = (shapeRadius > 0.0001f) ? (baseR / shapeRadius) : RandomRange(0.0f, 1.0f);
            glm::vec3 coneSlope(std::cos(phi) * tanAngle * radialFrac, 1.0f, std::sin(phi) * tanAngle * radialFrac);
            localDir = glm::normalize(coneSlope);
            break;
        }
        case ParticleEmitterShape::Box: {
            glm::vec3 half = boxSize * 0.5f;
            localPos = glm::vec3(
                RandomRange(-half.x, half.x),
                RandomRange(-half.y, half.y),
                RandomRange(-half.z, half.z)
            );
            localDir = glm::vec3(0.0f, 1.0f, 0.0f);
            break;
        }
        case ParticleEmitterShape::Circle: {
            float phi = RandomRange(0.0f, kTwoPi);
            float r = emitFromShell ? shapeRadius : (shapeRadius * std::sqrt(RandomRange(0.0f, 1.0f)));
            localPos = glm::vec3(r * std::cos(phi), 0.0f, r * std::sin(phi));
            localDir = glm::normalize(glm::vec3(std::cos(phi) * 0.35f, 1.0f, std::sin(phi) * 0.35f));
            break;
        }
    }

    float speed = RandomRange(initialSpeedMin, initialSpeedMax);
    glm::vec3 localVel = localDir * speed + initialVelocityOffset;

    if (simulationSpaceLocal) {
        p.position = localPos;
        p.velocity = localVel;
    } else {
        p.position = glm::vec3(emitterWorldMat * glm::vec4(localPos, 1.0f));
        p.velocity = emitterRotMat * localVel;
    }

    p.color = EvaluateParticleColor(p);
    m_Particles.push_back(p);
}

void ParticleSystemComponent::Simulate(float deltaTime)
{
    if (!m_Enabled || !m_IsPlaying || m_IsPaused) return;

    float dt = deltaTime * simulationSpeed;
    if (dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f; // Evitar saltos extremos de frame

    float prevTime = m_SystemTime;
    m_SystemTime += dt;

    bool canEmitNow = emissionEnabled;
    if (duration > 0.0001f && m_SystemTime >= duration) {
        if (looping) {
            m_SystemTime = std::fmod(m_SystemTime, duration);
            for (auto& b : bursts) {
                b.triggered = false;
            }
        } else {
            canEmitNow = false;
            if (m_Particles.empty()) {
                m_IsPlaying = false;
            }
        }
    }

    glm::mat4 worldMat(1.0f);
    glm::mat4 invWorldMat(1.0f);
    glm::mat3 rotMat(1.0f);
    glm::mat3 invRotMat(1.0f);
    glm::vec3 emitterWorldPos(0.0f);

    if (m_Owner) {
        worldMat = m_Owner->getMatrix();
        invWorldMat = glm::inverse(worldMat);
        emitterWorldPos = glm::vec3(worldMat[3]);

        glm::vec3 r = m_Owner->rotation;
        if (auto tc = m_Owner->GetComponent<TransformComponent>()) {
            r = tc->rotation;
        }
        glm::mat4 rm = glm::rotate(glm::mat4(1.0f), glm::radians(r.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
                       glm::rotate(glm::mat4(1.0f), glm::radians(r.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
                       glm::rotate(glm::mat4(1.0f), glm::radians(r.z), glm::vec3(0.0f, 0.0f, 1.0f));
        rotMat = glm::mat3(rm);
        invRotMat = glm::transpose(rotMat);
    }

    int limit = (std::max)(1, maxParticles);

    // 1. Evaluar Bursts y Emisión Continua
    if (canEmitNow) {
        for (auto& b : bursts) {
            if (!b.triggered && ((prevTime <= b.time && m_SystemTime >= b.time) || (prevTime == 0.0f && b.time <= 0.0001f))) {
                b.triggered = true;
                for (int k = 0; k < b.count && static_cast<int>(m_Particles.size()) < limit; ++k) {
                    SpawnSingleParticle(worldMat, rotMat);
                }
            }
        }

        if (emissionRate > 0.0f) {
            m_EmissionAccumulator += emissionRate * dt;
            int toSpawn = static_cast<int>(m_EmissionAccumulator);
            if (toSpawn > 0) {
                m_EmissionAccumulator -= static_cast<float>(toSpawn);
                for (int k = 0; k < toSpawn && static_cast<int>(m_Particles.size()) < limit; ++k) {
                    SpawnSingleParticle(worldMat, rotMat);
                }
            }
        }
    }

    // 2. Actualizar partículas activas (Física, Turbulencia, Colisiones Bullet, Ciclo de vida)
    glm::vec3 worldGravity = glm::vec3(0.0f, -9.81f, 0.0f);
    bool bulletReady = SlipPhysics::IsInitialized();
    if (bulletReady) {
        worldGravity = SlipPhysics::Get().getGravity();
    }

    size_t writeIdx = 0;
    for (size_t i = 0; i < m_Particles.size(); ++i) {
        ParticleInstance p = m_Particles[i];
        p.age += dt;
        if (p.age >= p.lifetime) {
            continue;
        }

        float normAge = glm::clamp(p.age / p.lifetime, 0.0f, 1.0f);
        glm::vec3 oldPos = p.position;

        if (physicsEnabled) {
            glm::vec3 accel(0.0f);
            if (useGravity) {
                glm::vec3 g = worldGravity * gravityMultiplier;
                accel += simulationSpaceLocal ? (invRotMat * g) : g;
            }
            accel += constantForce;

            if (turbulenceEnabled && turbulenceStrength > 0.0001f) {
                float freq = (std::max)(0.05f, turbulenceFrequency);
                float phase = m_SystemTime * 2.5f + static_cast<float>(p.randomSeed & 0xFFu) * 0.05f;
                glm::vec3 samplePos = p.position * freq;
                glm::vec3 noiseVec(
                    std::sin(samplePos.y + phase) + std::cos(samplePos.z * 1.27f - phase * 0.7f),
                    std::sin(samplePos.z + phase * 1.13f) + std::cos(samplePos.x * 1.19f + phase * 0.5f),
                    std::sin(samplePos.x - phase * 0.9f) + std::cos(samplePos.y * 1.31f + phase)
                );
                accel += noiseVec * (0.5f * turbulenceStrength);
            }

            if (std::abs(vortexStrength) > 0.0001f) {
                glm::vec3 rel = simulationSpaceLocal ? p.position : (p.position - emitterWorldPos);
                glm::vec3 tangent(-rel.z, 0.0f, rel.x);
                float len = glm::length(tangent);
                if (len > 0.001f) {
                    accel += (tangent / len) * vortexStrength;
                }
            }

            p.velocity += accel * dt;

            if (linearDrag > 0.0001f) {
                float damping = std::exp(-linearDrag * dt);
                p.velocity *= damping;
            }
        }

        glm::vec3 newPos = oldPos + p.velocity * dt;

        // Colisiones físicas contra el mundo Bullet Physics (Colliders, Terreno, BSP, RigidBodies)
        if (physicsEnabled && collisionEnabled && bulletReady) {
            glm::vec3 worldOld = simulationSpaceLocal ? glm::vec3(worldMat * glm::vec4(oldPos, 1.0f)) : oldPos;
            glm::vec3 worldNew = simulationSpaceLocal ? glm::vec3(worldMat * glm::vec4(newPos, 1.0f)) : newPos;
            glm::vec3 rayDelta = worldNew - worldOld;
            float moveDist = glm::length(rayDelta);

            if (moveDist > 0.00001f) {
                glm::vec3 rayDir = rayDelta / moveDist;
                glm::vec3 rayEnd = worldNew + rayDir * (std::max)(0.0f, particleRadius);

                btVector3 btFrom(worldOld.x, worldOld.y, worldOld.z);
                btVector3 btTo(rayEnd.x, rayEnd.y, rayEnd.z);
                btCollisionWorld::ClosestRayResultCallback rayCallback(btFrom, btTo);

                SlipPhysics::Get().getWorld().rayTest(btFrom, btTo, rayCallback);

                if (rayCallback.hasHit()) {
                    glm::vec3 hitNormal(
                        rayCallback.m_hitNormalWorld.x(),
                        rayCallback.m_hitNormalWorld.y(),
                        rayCallback.m_hitNormalWorld.z()
                    );
                    if (glm::length(hitNormal) > 0.0001f) {
                        hitNormal = glm::normalize(hitNormal);
                    } else {
                        hitNormal = glm::vec3(0.0f, 1.0f, 0.0f);
                    }

                    glm::vec3 hitPoint(
                        rayCallback.m_hitPointWorld.x(),
                        rayCallback.m_hitPointWorld.y(),
                        rayCallback.m_hitPointWorld.z()
                    );

                    glm::vec3 worldVel = simulationSpaceLocal ? (rotMat * p.velocity) : p.velocity;
                    float vnScalar = glm::dot(worldVel, hitNormal);

                    if (vnScalar < 0.0f) {
                        glm::vec3 vn = vnScalar * hitNormal;
                        glm::vec3 vt = worldVel - vn;

                        float restitution = glm::clamp(bounciness, 0.0f, 1.0f);
                        float friction = glm::clamp(collisionFriction, 0.0f, 1.0f);
                        glm::vec3 bouncedWorldVel = (-restitution) * vn + (1.0f - friction) * vt;

                        // Aplicar impulso a RigidBody dinámico si está habilitado
                        if (pushRigidBodies && rayCallback.m_collisionObject) {
                            const btCollisionObject* colObj = rayCallback.m_collisionObject;
                            if (!colObj->isStaticOrKinematicObject()) {
                                btRigidBody* hitBody = const_cast<btRigidBody*>(btRigidBody::upcast(colObj));
                                if (hitBody && hitBody->getInvMass() > 0.0f) {
                                    glm::vec3 deltaV = worldVel - bouncedWorldVel;
                                    glm::vec3 impulseVec = deltaV * (std::max)(0.001f, mass);
                                    hitBody->activate(true);
                                    btVector3 relPos = rayCallback.m_hitPointWorld - hitBody->getCenterOfMassPosition();
                                    hitBody->applyImpulse(btVector3(impulseVec.x, impulseVec.y, impulseVec.z), relPos);
                                }
                            }
                        }

                        glm::vec3 resolvedWorldPos = hitPoint + hitNormal * (std::max)(0.005f, particleRadius);
                        if (simulationSpaceLocal) {
                            newPos = glm::vec3(invWorldMat * glm::vec4(resolvedWorldPos, 1.0f));
                            p.velocity = invRotMat * bouncedWorldVel;
                        } else {
                            newPos = resolvedWorldPos;
                            p.velocity = bouncedWorldVel;
                        }

                        p.collisionCount++;
                        if (lifetimeLossOnCollision > 0.0f) {
                            p.age += p.lifetime * glm::clamp(lifetimeLossOnCollision, 0.0f, 1.0f);
                            if (p.age >= p.lifetime) {
                                continue;
                            }
                            normAge = glm::clamp(p.age / p.lifetime, 0.0f, 1.0f);
                        }
                    }
                }
            }
        }

        p.position = newPos;
        p.rotation += p.angularVelocity * dt;
        p.size = glm::mix(p.startSize, p.endSize, normAge);
        p.color = EvaluateParticleColor(p);

        m_Particles[writeIdx++] = p;
    }
    m_Particles.resize(writeIdx);
}

void ParticleSystemComponent::GetWorldBounds(const glm::mat4& emitterWorld, glm::vec3& outMin, glm::vec3& outMax) const
{
    if (m_Particles.empty()) {
        glm::vec3 origin = glm::vec3(emitterWorld[3]);
        outMin = origin - glm::vec3(0.5f);
        outMax = origin + glm::vec3(0.5f);
        return;
    }
    outMin = glm::vec3(1e9f);
    outMax = glm::vec3(-1e9f);
    for (const auto& p : m_Particles) {
        glm::vec3 wPos = simulationSpaceLocal ? glm::vec3(emitterWorld * glm::vec4(p.position, 1.0f)) : p.position;
        float r = (std::max)(0.15f, p.size * 0.75f);
        outMin.x = (std::min)(outMin.x, wPos.x - r);
        outMin.y = (std::min)(outMin.y, wPos.y - r);
        outMin.z = (std::min)(outMin.z, wPos.z - r);
        outMax.x = (std::max)(outMax.x, wPos.x + r);
        outMax.y = (std::max)(outMax.y, wPos.y + r);
        outMax.z = (std::max)(outMax.z, wPos.z + r);
    }
}

void ParticleSystemComponent::ApplyPreset(const std::string& presetName)
{
    bursts.clear();
    if (presetName == "NiagaraSparks") {
        renderMode = ParticleRenderMode::PointNiagara;
        pointShape = ParticlePointShape::LitSphere;
        pointBaseScale = 1.25f;
        maxParticles = 4000;
        duration = 5.0f;
        looping = true;
        emissionEnabled = true;
        emissionRate = 350.0f;
        emitterShape = ParticleEmitterShape::Cone;
        shapeRadius = 0.15f;
        coneAngle = 28.0f;
        initialSpeedMin = 5.5f;
        initialSpeedMax = 11.0f;
        initialVelocityOffset = glm::vec3(0.0f, 1.5f, 0.0f);
        lifetimeMin = 1.2f;
        lifetimeMax = 2.6f;
        startSizeMin = 0.05f;
        startSizeMax = 0.11f;
        endSizeMultiplier = 0.2f;
        colorMode = ParticleColorMode::GradientOverLifetime;
        gradientStops = {
            { 0.0f, glm::vec4(1.0f, 0.98f, 0.80f, 1.0f) },
            { 0.25f, glm::vec4(1.0f, 0.65f, 0.15f, 1.0f) },
            { 0.75f, glm::vec4(1.0f, 0.20f, 0.02f, 0.85f) },
            { 1.0f, glm::vec4(0.35f, 0.05f, 0.0f, 0.0f) }
        };
        physicsEnabled = true;
        useGravity = true;
        gravityMultiplier = 1.2f;
        mass = 0.05f;
        linearDrag = 0.1f;
        turbulenceEnabled = true;
        turbulenceStrength = 1.8f;
        turbulenceFrequency = 2.0f;
        vortexStrength = 0.0f;
        collisionEnabled = true;
        particleRadius = 0.03f;
        bounciness = 0.72f;
        collisionFriction = 0.15f;
        lifetimeLossOnCollision = 0.05f;
        pushRigidBodies = true;
        materialMode = ParticleMaterialMode::Emissive;
        blendMode = ParticleBlendMode::Additive;
        emissiveColor = glm::vec3(1.0f, 0.65f, 0.2f);
        emissiveIntensity = 3.5f;
        castShadows = false;
        receiveShadows = false;
    } else if (presetName == "Fire") {
        renderMode = ParticleRenderMode::Billboard;
        maxParticles = 600;
        duration = 5.0f;
        looping = true;
        emissionEnabled = true;
        emissionRate = 65.0f;
        emitterShape = ParticleEmitterShape::Cone;
        shapeRadius = 0.35f;
        coneAngle = 15.0f;
        initialSpeedMin = 1.5f;
        initialSpeedMax = 3.2f;
        initialVelocityOffset = glm::vec3(0.0f);
        lifetimeMin = 0.8f;
        lifetimeMax = 1.6f;
        startSizeMin = 0.35f;
        startSizeMax = 0.65f;
        endSizeMultiplier = 0.15f;
        colorMode = ParticleColorMode::GradientOverLifetime;
        gradientStops = {
            { 0.0f, glm::vec4(1.0f, 0.95f, 0.50f, 0.95f) },
            { 0.35f, glm::vec4(1.0f, 0.45f, 0.08f, 0.85f) },
            { 0.75f, glm::vec4(0.75f, 0.10f, 0.02f, 0.45f) },
            { 1.0f, glm::vec4(0.15f, 0.15f, 0.15f, 0.0f) }
        };
        physicsEnabled = true;
        useGravity = true;
        gravityMultiplier = -0.25f; // Flotabilidad hacia arriba
        linearDrag = 0.2f;
        turbulenceEnabled = true;
        turbulenceStrength = 1.5f;
        turbulenceFrequency = 1.8f;
        vortexStrength = 0.5f;
        collisionEnabled = false;
        materialMode = ParticleMaterialMode::Emissive;
        blendMode = ParticleBlendMode::Additive;
        emissiveColor = glm::vec3(1.0f, 0.5f, 0.1f);
        emissiveIntensity = 2.8f;
        castShadows = false;
        lightEmissionEnabled = true;
        lightColor = glm::vec3(1.0f, 0.6f, 0.2f);
        lightIntensity = 3.5f;
        lightRange = 7.0f;
        maxLights = 1;
    } else if (presetName == "Smoke") {
        renderMode = ParticleRenderMode::Billboard;
        maxParticles = 500;
        duration = 6.0f;
        looping = true;
        emissionEnabled = true;
        emissionRate = 25.0f;
        emitterShape = ParticleEmitterShape::Cone;
        shapeRadius = 0.4f;
        coneAngle = 22.0f;
        initialSpeedMin = 0.8f;
        initialSpeedMax = 1.8f;
        initialVelocityOffset = glm::vec3(0.0f);
        lifetimeMin = 2.5f;
        lifetimeMax = 4.5f;
        startSizeMin = 0.4f;
        startSizeMax = 0.7f;
        endSizeMultiplier = 2.4f;
        colorMode = ParticleColorMode::GradientOverLifetime;
        gradientStops = {
            { 0.0f, glm::vec4(0.55f, 0.55f, 0.58f, 0.0f) },
            { 0.2f, glm::vec4(0.65f, 0.65f, 0.68f, 0.55f) },
            { 0.7f, glm::vec4(0.45f, 0.45f, 0.48f, 0.35f) },
            { 1.0f, glm::vec4(0.30f, 0.30f, 0.32f, 0.0f) }
        };
        physicsEnabled = true;
        useGravity = true;
        gravityMultiplier = -0.1f;
        linearDrag = 0.35f;
        turbulenceEnabled = true;
        turbulenceStrength = 0.9f;
        collisionEnabled = false;
        materialMode = ParticleMaterialMode::Lit;
        blendMode = ParticleBlendMode::AlphaBlend;
        receiveLights = true;
        receiveShadows = true;
        castShadows = false;
    } else if (presetName == "BouncingFountain") {
        renderMode = ParticleRenderMode::PointNiagara;
        pointShape = ParticlePointShape::LitSphere;
        pointBaseScale = 1.5f;
        maxParticles = 3000;
        duration = 5.0f;
        looping = true;
        emissionEnabled = true;
        emissionRate = 220.0f;
        emitterShape = ParticleEmitterShape::Cone;
        shapeRadius = 0.2f;
        coneAngle = 18.0f;
        initialSpeedMin = 4.5f;
        initialSpeedMax = 7.5f;
        initialVelocityOffset = glm::vec3(2.5f, 3.0f, 0.0f);
        lifetimeMin = 2.5f;
        lifetimeMax = 4.0f;
        startSizeMin = 0.08f;
        startSizeMax = 0.15f;
        endSizeMultiplier = 0.7f;
        colorMode = ParticleColorMode::GradientOverLifetime;
        gradientStops = {
            { 0.0f, glm::vec4(0.25f, 0.80f, 1.0f, 1.0f) },
            { 0.5f, glm::vec4(0.15f, 0.55f, 0.98f, 0.95f) },
            { 1.0f, glm::vec4(0.08f, 0.25f, 0.75f, 0.0f) }
        };
        physicsEnabled = true;
        useGravity = true;
        gravityMultiplier = 1.0f;
        mass = 0.15f;
        linearDrag = 0.05f;
        turbulenceEnabled = false;
        collisionEnabled = true;
        particleRadius = 0.06f;
        bounciness = 0.75f;
        collisionFriction = 0.1f;
        pushRigidBodies = true;
        materialMode = ParticleMaterialMode::Lit;
        blendMode = ParticleBlendMode::AlphaBlend;
        receiveLights = true;
        receiveShadows = true;
        castShadows = true;
    } else if (presetName == "Explosion") {
        renderMode = ParticleRenderMode::PointNiagara;
        pointShape = ParticlePointShape::SoftCircle;
        pointBaseScale = 1.4f;
        maxParticles = 2000;
        duration = 2.5f;
        looping = true;
        emissionEnabled = true;
        emissionRate = 0.0f;
        bursts.push_back({ 0.0f, 350, false });
        emitterShape = ParticleEmitterShape::Sphere;
        shapeRadius = 0.2f;
        emitFromShell = true;
        initialSpeedMin = 6.0f;
        initialSpeedMax = 16.0f;
        initialVelocityOffset = glm::vec3(0.0f, 2.0f, 0.0f);
        lifetimeMin = 0.8f;
        lifetimeMax = 1.8f;
        startSizeMin = 0.10f;
        startSizeMax = 0.22f;
        endSizeMultiplier = 0.1f;
        colorMode = ParticleColorMode::GradientOverLifetime;
        gradientStops = {
            { 0.0f, glm::vec4(1.0f, 1.0f, 0.85f, 1.0f) },
            { 0.2f, glm::vec4(1.0f, 0.6f, 0.1f, 1.0f) },
            { 0.6f, glm::vec4(0.9f, 0.15f, 0.03f, 0.8f) },
            { 1.0f, glm::vec4(0.15f, 0.15f, 0.15f, 0.0f) }
        };
        physicsEnabled = true;
        useGravity = true;
        gravityMultiplier = 0.85f;
        linearDrag = 1.2f;
        collisionEnabled = true;
        bounciness = 0.6f;
        pushRigidBodies = true;
        materialMode = ParticleMaterialMode::Emissive;
        blendMode = ParticleBlendMode::Additive;
        emissiveColor = glm::vec3(1.0f, 0.55f, 0.12f);
        emissiveIntensity = 4.0f;
    }

    Restart();
}

void ParticleSystemComponent::SerializeTo(std::ostream& out) const
{
    out << "  ParticleSystemComponent:\n";
    out << "    RenderMode: " << static_cast<int>(renderMode) << "\n";
    out << "    MaxParticles: " << maxParticles << "\n";
    out << "    Duration: " << duration << "\n";
    out << "    Looping: " << (looping ? 1 : 0) << "\n";
    out << "    PlayOnAwake: " << (playOnAwake ? 1 : 0) << "\n";
    out << "    SimulationSpaceLocal: " << (simulationSpaceLocal ? 1 : 0) << "\n";
    out << "    SimulationSpeed: " << simulationSpeed << "\n";
    out << "    EmissionEnabled: " << (emissionEnabled ? 1 : 0) << "\n";
    out << "    EmissionRate: " << emissionRate << "\n";
    for (const auto& b : bursts) {
        out << "    Burst: " << b.time << " " << b.count << "\n";
    }
    out << "    EmitterShape: " << static_cast<int>(emitterShape) << "\n";
    out << "    ShapeRadius: " << shapeRadius << "\n";
    out << "    ConeAngle: " << coneAngle << "\n";
    out << "    BoxSize: " << boxSize.x << " " << boxSize.y << " " << boxSize.z << "\n";
    out << "    EmitFromShell: " << (emitFromShell ? 1 : 0) << "\n";
    out << "    InitialSpeedMin: " << initialSpeedMin << "\n";
    out << "    InitialSpeedMax: " << initialSpeedMax << "\n";
    out << "    InitialVelocityOffset: " << initialVelocityOffset.x << " " << initialVelocityOffset.y << " " << initialVelocityOffset.z << "\n";
    out << "    LifetimeMin: " << lifetimeMin << "\n";
    out << "    LifetimeMax: " << lifetimeMax << "\n";
    out << "    StartSizeMin: " << startSizeMin << "\n";
    out << "    StartSizeMax: " << startSizeMax << "\n";
    out << "    EndSizeMultiplier: " << endSizeMultiplier << "\n";
    out << "    StartRotationMin: " << startRotationMin << "\n";
    out << "    StartRotationMax: " << startRotationMax << "\n";
    out << "    AngularVelocityMin: " << angularVelocityMin << "\n";
    out << "    AngularVelocityMax: " << angularVelocityMax << "\n";
    out << "    ColorMode: " << static_cast<int>(colorMode) << "\n";
    out << "    StartColor: " << startColor.r << " " << startColor.g << " " << startColor.b << " " << startColor.a << "\n";
    out << "    StartColor2: " << startColor2.r << " " << startColor2.g << " " << startColor2.b << " " << startColor2.a << "\n";
    out << "    ClearGradient: 1\n";
    for (const auto& stop : gradientStops) {
        out << "    GradientStop: " << stop.t << " " << stop.color.r << " " << stop.color.g << " " << stop.color.b << " " << stop.color.a << "\n";
    }
    out << "    PhysicsEnabled: " << (physicsEnabled ? 1 : 0) << "\n";
    out << "    UseGravity: " << (useGravity ? 1 : 0) << "\n";
    out << "    GravityMultiplier: " << gravityMultiplier << "\n";
    out << "    Mass: " << mass << "\n";
    out << "    LinearDrag: " << linearDrag << "\n";
    out << "    ConstantForce: " << constantForce.x << " " << constantForce.y << " " << constantForce.z << "\n";
    out << "    TurbulenceEnabled: " << (turbulenceEnabled ? 1 : 0) << "\n";
    out << "    TurbulenceStrength: " << turbulenceStrength << "\n";
    out << "    TurbulenceFrequency: " << turbulenceFrequency << "\n";
    out << "    VortexStrength: " << vortexStrength << "\n";
    out << "    CollisionEnabled: " << (collisionEnabled ? 1 : 0) << "\n";
    out << "    ParticleRadius: " << particleRadius << "\n";
    out << "    Bounciness: " << bounciness << "\n";
    out << "    CollisionFriction: " << collisionFriction << "\n";
    out << "    LifetimeLossOnCollision: " << lifetimeLossOnCollision << "\n";
    out << "    PushRigidBodies: " << (pushRigidBodies ? 1 : 0) << "\n";
    out << "    MaterialMode: " << static_cast<int>(materialMode) << "\n";
    out << "    BlendMode: " << static_cast<int>(blendMode) << "\n";
    out << "    TexturePath: " << texturePath << "\n";
    out << "    HFrames: " << hFrames << "\n";
    out << "    VFrames: " << vFrames << "\n";
    out << "    AnimateSpriteSheet: " << (animateSpriteSheet ? 1 : 0) << "\n";
    out << "    PointShape: " << static_cast<int>(pointShape) << "\n";
    out << "    PointBaseScale: " << pointBaseScale << "\n";
    out << "    EmissiveColor: " << emissiveColor.r << " " << emissiveColor.g << " " << emissiveColor.b << "\n";
    out << "    EmissiveIntensity: " << emissiveIntensity << "\n";
    out << "    Roughness: " << roughness << "\n";
    out << "    Metallic: " << metallic << "\n";
    out << "    ReceiveLights: " << (receiveLights ? 1 : 0) << "\n";
    out << "    ReceiveShadows: " << (receiveShadows ? 1 : 0) << "\n";
    out << "    CastShadows: " << (castShadows ? 1 : 0) << "\n";
    out << "    ShadowAlphaCutoff: " << shadowAlphaCutoff << "\n";
    out << "    LightEmissionEnabled: " << (lightEmissionEnabled ? 1 : 0) << "\n";
    out << "    LightColor: " << lightColor.r << " " << lightColor.g << " " << lightColor.b << "\n";
    out << "    LightIntensity: " << lightIntensity << "\n";
    out << "    LightRange: " << lightRange << "\n";
    out << "    MaxLights: " << maxLights << "\n";
    out << "    UseParticleColorForLight: " << (useParticleColorForLight ? 1 : 0) << "\n";
}

void ParticleSystemComponent::DeserializeKey(const std::string& key, const std::string& value)
{
    auto flag = [](const std::string& v) { return v == "1" || v == "true"; };
    auto num = [](const std::string& v) {
        float x = 0.0f;
        std::stringstream(v) >> x;
        return x;
    };
    auto vec3 = [](const std::string& v) {
        glm::vec3 out(0.0f);
        std::stringstream(v) >> out.x >> out.y >> out.z;
        return out;
    };
    auto vec4 = [](const std::string& v) {
        glm::vec4 out(1.0f);
        std::stringstream(v) >> out.x >> out.y >> out.z >> out.w;
        return out;
    };

    if (key == "RenderMode") renderMode = static_cast<ParticleRenderMode>(static_cast<int>(num(value)));
    else if (key == "MaxParticles") maxParticles = (std::max)(1, static_cast<int>(num(value)));
    else if (key == "Duration") duration = num(value);
    else if (key == "Looping") looping = flag(value);
    else if (key == "PlayOnAwake") playOnAwake = flag(value);
    else if (key == "SimulationSpaceLocal") simulationSpaceLocal = flag(value);
    else if (key == "SimulationSpeed") simulationSpeed = num(value);
    else if (key == "EmissionEnabled") emissionEnabled = flag(value);
    else if (key == "EmissionRate") emissionRate = num(value);
    else if (key == "Burst") {
        ParticleBurst b;
        std::stringstream(value) >> b.time >> b.count;
        bursts.push_back(b);
    }
    else if (key == "EmitterShape") emitterShape = static_cast<ParticleEmitterShape>(static_cast<int>(num(value)));
    else if (key == "ShapeRadius") shapeRadius = num(value);
    else if (key == "ConeAngle") coneAngle = num(value);
    else if (key == "BoxSize") boxSize = vec3(value);
    else if (key == "EmitFromShell") emitFromShell = flag(value);
    else if (key == "InitialSpeedMin") initialSpeedMin = num(value);
    else if (key == "InitialSpeedMax") initialSpeedMax = num(value);
    else if (key == "InitialVelocityOffset") initialVelocityOffset = vec3(value);
    else if (key == "LifetimeMin") lifetimeMin = num(value);
    else if (key == "LifetimeMax") lifetimeMax = num(value);
    else if (key == "StartSizeMin") startSizeMin = num(value);
    else if (key == "StartSizeMax") startSizeMax = num(value);
    else if (key == "EndSizeMultiplier") endSizeMultiplier = num(value);
    else if (key == "StartRotationMin") startRotationMin = num(value);
    else if (key == "StartRotationMax") startRotationMax = num(value);
    else if (key == "AngularVelocityMin") angularVelocityMin = num(value);
    else if (key == "AngularVelocityMax") angularVelocityMax = num(value);
    else if (key == "ColorMode") colorMode = static_cast<ParticleColorMode>(static_cast<int>(num(value)));
    else if (key == "StartColor") startColor = vec4(value);
    else if (key == "StartColor2") startColor2 = vec4(value);
    else if (key == "ClearGradient") {
        if (flag(value)) gradientStops.clear();
    }
    else if (key == "GradientStop") {
        ParticleGradientStop s;
        std::stringstream(value) >> s.t >> s.color.r >> s.color.g >> s.color.b >> s.color.a;
        gradientStops.push_back(s);
    }
    else if (key == "PhysicsEnabled") physicsEnabled = flag(value);
    else if (key == "UseGravity") useGravity = flag(value);
    else if (key == "GravityMultiplier") gravityMultiplier = num(value);
    else if (key == "Mass") mass = num(value);
    else if (key == "LinearDrag") linearDrag = num(value);
    else if (key == "ConstantForce") constantForce = vec3(value);
    else if (key == "TurbulenceEnabled") turbulenceEnabled = flag(value);
    else if (key == "TurbulenceStrength") turbulenceStrength = num(value);
    else if (key == "TurbulenceFrequency") turbulenceFrequency = num(value);
    else if (key == "VortexStrength") vortexStrength = num(value);
    else if (key == "CollisionEnabled") collisionEnabled = flag(value);
    else if (key == "ParticleRadius") particleRadius = num(value);
    else if (key == "Bounciness") bounciness = num(value);
    else if (key == "CollisionFriction") collisionFriction = num(value);
    else if (key == "LifetimeLossOnCollision") lifetimeLossOnCollision = num(value);
    else if (key == "PushRigidBodies") pushRigidBodies = flag(value);
    else if (key == "MaterialMode") materialMode = static_cast<ParticleMaterialMode>(static_cast<int>(num(value)));
    else if (key == "BlendMode") blendMode = static_cast<ParticleBlendMode>(static_cast<int>(num(value)));
    else if (key == "TexturePath") SetTexture(value);
    else if (key == "HFrames") hFrames = (std::max)(1, static_cast<int>(num(value)));
    else if (key == "VFrames") vFrames = (std::max)(1, static_cast<int>(num(value)));
    else if (key == "AnimateSpriteSheet") animateSpriteSheet = flag(value);
    else if (key == "PointShape") pointShape = static_cast<ParticlePointShape>(static_cast<int>(num(value)));
    else if (key == "PointBaseScale") pointBaseScale = num(value);
    else if (key == "EmissiveColor") emissiveColor = vec3(value);
    else if (key == "EmissiveIntensity") emissiveIntensity = num(value);
    else if (key == "Roughness") roughness = num(value);
    else if (key == "Metallic") metallic = num(value);
    else if (key == "ReceiveLights") receiveLights = flag(value);
    else if (key == "ReceiveShadows") receiveShadows = flag(value);
    else if (key == "CastShadows") castShadows = flag(value);
    else if (key == "ShadowAlphaCutoff") shadowAlphaCutoff = num(value);
    else if (key == "LightEmissionEnabled") lightEmissionEnabled = flag(value);
    else if (key == "LightColor") lightColor = vec3(value);
    else if (key == "LightIntensity") lightIntensity = num(value);
    else if (key == "LightRange") lightRange = num(value);
    else if (key == "MaxLights") maxLights = (std::max)(1, (std::min)(4, static_cast<int>(num(value))));
    else if (key == "UseParticleColorForLight") useParticleColorForLight = flag(value);
}

void ParticleSystemComponent::OnInit()
{
    if (!texturePath.empty() && !texture) {
        SetTexture(texturePath);
    }
    if (playOnAwake) {
        m_IsPlaying = true;
        m_IsPaused = false;
    }
}

void ParticleSystemComponent::OnStart()
{
    if (playOnAwake) {
        Restart();
    } else {
        Stop();
    }
}

void ParticleSystemComponent::OnUpdate(float deltaTime)
{
    Simulate(deltaTime);
}

void ParticleSystemComponent::OnDrawInspectorUI()
{
}

// ---------------------------------------------------------
// VehicleComponent (btRaycastVehicle de Bullet Physics)
// ---------------------------------------------------------
struct SlipVehicleRaycaster : public btVehicleRaycaster {
    btDynamicsWorld* m_dynamicsWorld;
    btRigidBody* m_chassisBody;
    SlipEntity* m_vehicleEntity;

    SlipVehicleRaycaster(btDynamicsWorld* world, btRigidBody* chassis, SlipEntity* vehicleEnt)
        : m_dynamicsWorld(world), m_chassisBody(chassis), m_vehicleEntity(vehicleEnt) {}

    void* castRay(const btVector3& from, const btVector3& to, btVehicleRaycasterResult& result) override {
        struct VehicleRayCallback : public btCollisionWorld::ClosestRayResultCallback {
            btRigidBody* chassis;
            SlipEntity* carEntity;

            VehicleRayCallback(const btVector3& from, const btVector3& to, btRigidBody* ignoreChassis, SlipEntity* carEnt)
                : btCollisionWorld::ClosestRayResultCallback(from, to), chassis(ignoreChassis), carEntity(carEnt) {}

            btScalar addSingleResult(btCollisionWorld::LocalRayResult& rayResult, bool normalInWorldSpace) override {
                if (rayResult.m_collisionObject == chassis) {
                    return 1.0f; // Ignorar el propio chasis del vehiculo
                }
                if (carEntity && rayResult.m_collisionObject) {
                    SlipEntity* ent = static_cast<SlipEntity*>(rayResult.m_collisionObject->getUserPointer());
                    if (ent == carEntity || (ent && ent->GetParent() == carEntity)) {
                        return 1.0f; // Ignorar subobjetos o hijos del vehiculo
                    }
                }
                if (rayResult.m_collisionObject && (rayResult.m_collisionObject->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE)) {
                    return 1.0f; // Ignorar colliders tipo trigger
                }
                return ClosestRayResultCallback::addSingleResult(rayResult, normalInWorldSpace);
            }
        };

        VehicleRayCallback rayCallback(from, to, m_chassisBody, m_vehicleEntity);
        m_dynamicsWorld->rayTest(from, to, rayCallback);

        if (rayCallback.hasHit()) {
            const btRigidBody* body = btRigidBody::upcast(rayCallback.m_collisionObject);
            if (body && body->hasContactResponse()) {
                result.m_hitPointInWorld = rayCallback.m_hitPointWorld;
                result.m_hitNormalInWorld = rayCallback.m_hitNormalWorld;
                if (result.m_hitNormalInWorld.fuzzyZero()) {
                    result.m_hitNormalInWorld = btVector3(0.0f, 1.0f, 0.0f);
                } else {
                    result.m_hitNormalInWorld.normalize();
                }
                result.m_distFraction = rayCallback.m_closestHitFraction;
                return (void*)body;
            }
        }
        return nullptr;
    }
};

VehicleComponent::VehicleComponent()
{
    m_Name = "VehicleComponent";
    SetupDefaultCarWheels();
}

VehicleComponent::~VehicleComponent()
{
    CleanVehicle();
}

void VehicleComponent::SetupDefaultCarWheels()
{
    wheels.clear();
    float halfW = chassisWidth * 0.5f;
    float halfL = chassisLength * 0.5f;
    float heightOffset = -chassisHeight * 0.5f;

    // Rueda delantera izquierda (FL)
    WheelConfig fl;
    fl.connectionPoint = glm::vec3(halfW, heightOffset, halfL);
    fl.wheelDirection = glm::vec3(0.0f, -1.0f, 0.0f);
    fl.wheelAxle = glm::vec3(-1.0f, 0.0f, 0.0f);
    fl.suspensionRestLength = 0.50f;
    fl.wheelRadius = defaultWheelRadius;
    fl.isFrontWheel = true;
    wheels.push_back(fl);

    // Rueda delantera derecha (FR)
    WheelConfig fr;
    fr.connectionPoint = glm::vec3(-halfW, heightOffset, halfL);
    fr.wheelDirection = glm::vec3(0.0f, -1.0f, 0.0f);
    fr.wheelAxle = glm::vec3(-1.0f, 0.0f, 0.0f);
    fr.suspensionRestLength = 0.50f;
    fr.wheelRadius = defaultWheelRadius;
    fr.isFrontWheel = true;
    wheels.push_back(fr);

    // Rueda trasera izquierda (RL)
    WheelConfig rl;
    rl.connectionPoint = glm::vec3(halfW, heightOffset, -halfL);
    rl.wheelDirection = glm::vec3(0.0f, -1.0f, 0.0f);
    rl.wheelAxle = glm::vec3(-1.0f, 0.0f, 0.0f);
    rl.suspensionRestLength = 0.50f;
    rl.wheelRadius = defaultWheelRadius;
    rl.isFrontWheel = false;
    wheels.push_back(rl);

    // Rueda trasera derecha (RR)
    WheelConfig rr;
    rr.connectionPoint = glm::vec3(-halfW, heightOffset, -halfL);
    rr.wheelDirection = glm::vec3(0.0f, -1.0f, 0.0f);
    rr.wheelAxle = glm::vec3(-1.0f, 0.0f, 0.0f);
    rr.suspensionRestLength = 0.50f;
    rr.wheelRadius = defaultWheelRadius;
    rr.isFrontWheel = false;
    wheels.push_back(rr);
}

void VehicleComponent::OnInit()
{
    if (!m_Vehicle) {
        InitVehicle();
    }
}

void VehicleComponent::OnStart()
{
    if (!m_Vehicle) {
        InitVehicle();
    }
}

void VehicleComponent::CleanVehicle()
{
    if (m_Vehicle) {
        if (SlipPhysics::IsInitialized() && SlipPhysics::Get().GetDynamicsWorld()) {
            SlipPhysics::Get().getWorld().removeAction(m_Vehicle);
        }
        delete m_Vehicle;
        m_Vehicle = nullptr;
    }
    if (m_VehicleRaycaster) {
        delete m_VehicleRaycaster;
        m_VehicleRaycaster = nullptr;
    }
}

void VehicleComponent::InitVehicle()
{
    CleanVehicle();

    if (!m_Owner || !SlipPhysics::IsInitialized()) return;

    auto rbComp = m_Owner->GetComponent<RigidBodyComponent>();
    if (!rbComp) return;

    btRigidBody* chassisBody = rbComp->GetBulletRigidBody();
    if (!chassisBody) return;

    // Asegurar que la base rotacional del chasis no sea degenerada/cero
    if (chassisBody->getWorldTransform().getBasis()[0].fuzzyZero() ||
        chassisBody->getWorldTransform().getBasis()[1].fuzzyZero() ||
        chassisBody->getWorldTransform().getBasis()[2].fuzzyZero()) {
        btTransform t = chassisBody->getWorldTransform();
        t.setRotation(btQuaternion::getIdentity());
        chassisBody->setWorldTransform(t);
        if (chassisBody->getMotionState()) {
            chassisBody->getMotionState()->setWorldTransform(t);
        }
    }

    if (wheels.empty()) {
        SetupDefaultCarWheels();
    }

    // Configurar tuning del vehículo
    m_Tuning.m_suspensionStiffness = suspensionStiffness;
    m_Tuning.m_suspensionDamping = suspensionDamping;
    m_Tuning.m_suspensionCompression = suspensionCompression;
    m_Tuning.m_maxSuspensionTravelCm = maxSuspensionTravelCm;
    m_Tuning.m_frictionSlip = frictionSlip;
    m_Tuning.m_maxSuspensionForce = maxSuspensionForce;

    m_VehicleRaycaster = new SlipVehicleRaycaster(&SlipPhysics::Get().getWorld(), chassisBody, m_Owner);
    m_Vehicle = new btRaycastVehicle(m_Tuning, chassisBody, m_VehicleRaycaster);

    // Sistema de coordenadas: 0=X (derecha), 1=Y (arriba), 2=Z (adelante)
    m_Vehicle->setCoordinateSystem(0, 1, 2);

    chassisBody->setActivationState(DISABLE_DEACTIVATION);

    // Desactivar CCD en el chasis del raycast vehicle para evitar que la esfera barrida colisione con el suelo a alta velocidad
    chassisBody->setCcdMotionThreshold(0.0f);
    chassisBody->setCcdSweptSphereRadius(0.0f);

    for (const auto& w : wheels) {
        btVector3 conn(w.connectionPoint.x, w.connectionPoint.y, w.connectionPoint.z);
        btVector3 dir(w.wheelDirection.x, w.wheelDirection.y, w.wheelDirection.z);
        btVector3 axle(w.wheelAxle.x, w.wheelAxle.y, w.wheelAxle.z);

        btWheelInfo& wheel = m_Vehicle->addWheel(conn, dir, axle, w.suspensionRestLength, w.wheelRadius, m_Tuning, w.isFrontWheel);
        wheel.m_suspensionStiffness = suspensionStiffness;
        wheel.m_wheelsDampingRelaxation = suspensionDamping;
        wheel.m_wheelsDampingCompression = suspensionCompression;
        wheel.m_rollInfluence = rollInfluence;
        wheel.m_maxSuspensionForce = maxSuspensionForce;
        wheel.m_maxSuspensionTravelCm = maxSuspensionTravelCm;
    }

    SlipPhysics::Get().getWorld().addAction(m_Vehicle);

    for (int i = 0; i < m_Vehicle->getNumWheels(); ++i) {
        m_Vehicle->updateWheelTransform(i, true);
    }
}

void VehicleComponent::OnUpdate(float deltaTime)
{
    auto rbComp = m_Owner ? m_Owner->GetComponent<RigidBodyComponent>() : nullptr;
    btRigidBody* chassisBody = rbComp ? rbComp->GetBulletRigidBody() : nullptr;

    if (!m_Vehicle || !chassisBody || m_Vehicle->getRigidBody() != chassisBody) {
        InitVehicle();
        if (!m_Vehicle) return;
    }

    // Actualizar propiedades en tiempo de ejecución
    for (int i = 0; i < m_Vehicle->getNumWheels(); ++i) {
        btWheelInfo& wheel = m_Vehicle->getWheelInfo(i);
        wheel.m_suspensionStiffness = suspensionStiffness;
        wheel.m_wheelsDampingRelaxation = suspensionDamping;
        wheel.m_wheelsDampingCompression = suspensionCompression;
        wheel.m_rollInfluence = rollInfluence;
        wheel.m_maxSuspensionForce = maxSuspensionForce;
        wheel.m_maxSuspensionTravelCm = maxSuspensionTravelCm;
    }

    // Sincronizar automáticamente las mallas de las ruedas hijas en el Hierarchy
    if (m_Owner && SlipPhysics::IsInitialized() && SlipPhysics::Get().IsSimulating()) {
        auto toLower = [](std::string s) {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };

        std::vector<SlipEntity*> wheelCandidates;
        for (SlipEntity* child : m_Owner->GetChildren()) {
            if (!child) continue;
            if (child->GetComponent<CameraComponent>()) continue;
            if (child->GetComponent<MeshComponent>()) {
                std::string n = toLower(child->GetName());
                // Ignorar subobjetos que formen parte del cuerpo o chasis del vehículo
                if (n.find("chasis") != std::string::npos ||
                    n.find("chassis") != std::string::npos ||
                    n.find("carroceria") != std::string::npos ||
                    n.find("body") != std::string::npos ||
                    n.find("cabina") != std::string::npos ||
                    n.find("cuerpo") != std::string::npos) {
                    continue;
                }
                wheelCandidates.push_back(child);
            }
        }

        int numW = m_Vehicle->getNumWheels();

        // Mapeo inteligente de ruedas: asignar ruedas por etiqueta de nombre o en orden secuencial
        std::vector<SlipEntity*> sortedWheels(numW, nullptr);
        std::vector<bool> candidateUsed(wheelCandidates.size(), false);

        auto matchesTag = [](const std::string& name, const std::vector<std::string>& tags) {
            for (const auto& tag : tags) {
                if (name.find(tag) != std::string::npos) return true;
            }
            return false;
        };

        const std::vector<std::vector<std::string>> wheelTags = {
            { "fl", "delantera_izq", "front_left", "frontleft", "wheel_0" },
            { "fr", "delantera_der", "front_right", "frontright", "wheel_1" },
            { "rl", "trasera_izq", "rear_left", "rearleft", "wheel_2" },
            { "rr", "trasera_der", "rear_right", "rearright", "wheel_3" }
        };

        for (int i = 0; i < numW && i < 4; ++i) {
            for (size_t c = 0; c < wheelCandidates.size(); ++c) {
                if (!candidateUsed[c]) {
                    std::string cName = toLower(wheelCandidates[c]->GetName());
                    if (matchesTag(cName, wheelTags[i])) {
                        sortedWheels[i] = wheelCandidates[c];
                        candidateUsed[c] = true;
                        break;
                    }
                }
            }
        }
        for (int i = 0; i < numW; ++i) {
            if (!sortedWheels[i]) {
                for (size_t c = 0; c < wheelCandidates.size(); ++c) {
                    if (!candidateUsed[c]) {
                        sortedWheels[i] = wheelCandidates[c];
                        candidateUsed[c] = true;
                        break;
                    }
                }
            }
        }

        // Extraer la orientación y posición del padre sin la escala no uniforme
        glm::mat4 parentMat = m_Owner->getMatrix();
        glm::vec3 pPos = glm::vec3(parentMat[3]);
        glm::vec3 c0 = glm::vec3(parentMat[0]);
        glm::vec3 c1 = glm::vec3(parentMat[1]);
        glm::vec3 c2 = glm::vec3(parentMat[2]);
        float l0 = glm::length(c0);
        float l1 = glm::length(c1);
        float l2 = glm::length(c2);
        if (l0 > 1e-6f) c0 /= l0;
        if (l1 > 1e-6f) c1 /= l1;
        if (l2 > 1e-6f) c2 /= l2;
        glm::mat4 parentRotOnly(
            glm::vec4(c0, 0.0f),
            glm::vec4(c1, 0.0f),
            glm::vec4(c2, 0.0f),
            glm::vec4(pPos, 1.0f)
        );
        glm::mat4 invParentRotOnly = glm::inverse(parentRotOnly);

        for (int i = 0; i < numW; ++i) {
            SlipEntity* wheelChild = sortedWheels[i];
            if (!wheelChild) continue;

            // Garantizar que la rueda nunca herede la escala rectangular del chasis
            if (auto wt = wheelChild->GetTransform()) {
                wt->inheritScale = false;
            }

            const btTransform& wTrans = m_Vehicle->getWheelTransformWS(i);
            glm::vec3 wPos(wTrans.getOrigin().x(), wTrans.getOrigin().y(), wTrans.getOrigin().z());

            // Base ortonormal 3D del chasis (columnas c0, c1, c2)
            btMatrix3x3 parentBasis(
                c0.x, c1.x, c2.x,
                c0.y, c1.y, c2.y,
                c0.z, c1.z, c2.z
            );
            // Rotación 3D relativa exacta de la rueda respecto al chasis en el espacio local
            btMatrix3x3 relBasis = parentBasis.transpose() * wTrans.getBasis();
            glm::vec3 relRot = BulletMatrixToEulerDegrees(relBasis);

            glm::vec3 localPos = glm::vec3(invParentRotOnly * glm::vec4(wPos, 1.0f));
            wheelChild->position = localPos;
            wheelChild->rotation = relRot;
            if (wheelChild->GetTransform()) {
                wheelChild->GetTransform()->position = wheelChild->position;
                wheelChild->GetTransform()->rotation = wheelChild->rotation;
            }
        }
    }

    // -------------------------------------------------------------
    // Sistema Antivuelco Activo (Anti-Roll Bar / Roll Stabilizer)
    // -------------------------------------------------------------
    if (enableAntiRollBar && chassisBody && chassisBody->isActive()) {
        btMatrix3x3 basis = chassisBody->getWorldTransform().getBasis();
        btVector3 carRight = basis.getColumn(0);
        btVector3 carUp = basis.getColumn(1);
        btVector3 carFwd = basis.getColumn(2);
        btVector3 worldUp(0.0f, 1.0f, 0.0f);

        // Medir inclinación lateral respecto a la vertical
        float rollTilt = carRight.dot(worldUp); // 0 = plano, >0 inclinado derecha, <0 inclinado izquierda

        // Si el chasis empieza a inclinarse más de 4.5 grados (~0.08 rad), aplicar par restaurador proporcional
        if (std::abs(rollTilt) > 0.08f) {
            float excessTilt = (std::abs(rollTilt) - 0.08f);
            float strength = antiRollStrength * excessTilt;
            // Par opuesto a la inclinación a lo largo del eje longitudinal del coche (carFwd)
            btVector3 rollTorque = carFwd * (-rollTilt * strength * chassisBody->getMass());
            chassisBody->applyTorque(rollTorque);
        }

        // Amortiguación de velocidad angular en el eje Roll para prevenir rebotes violentos al salir de curvas
        btVector3 angVel = chassisBody->getAngularVelocity();
        float rollRate = angVel.dot(carFwd);
        if (std::abs(rollRate) > 0.4f) {
            btVector3 dampTorque = carFwd * (-rollRate * 1200.0f);
            chassisBody->applyTorque(dampTorque);
        }
    }

    // -------------------------------------------------------------
    // Gizmos de Debug: Controles del coche, Ruedas, Giro y Centro de Masa
    // -------------------------------------------------------------
    if (showDebugGizmos && chassisBody && m_Vehicle) {
        // 1. Centro de Masa (CoM)
        btVector3 com = chassisBody->getCenterOfMassPosition();
        glm::vec3 comPos(com.x(), com.y(), com.z());
        btMatrix3x3 basis = chassisBody->getWorldTransform().getBasis();

        btVector3 right = basis.getColumn(0) * 0.9f;
        btVector3 up = basis.getColumn(1) * 0.9f;
        btVector3 fwd = basis.getColumn(2) * 1.4f;

        // Ejes RGB del Centro de Masa (Rojo = Derecha, Verde = Arriba, Azul = Frente)
        SlipDebug::Get().drawLines(comPos, comPos + glm::vec3(right.x(), right.y(), right.z()), Color::red);
        SlipDebug::Get().drawLines(comPos, comPos + glm::vec3(up.x(), up.y(), up.z()), Color::green);
        SlipDebug::Get().drawLines(comPos, comPos + glm::vec3(fwd.x(), fwd.y(), fwd.z()), Color::blue);

        // Rombo marcador en el Centro de Masa (Amarillo)
        float d = 0.18f;
        SlipDebug::Get().drawLines(comPos + glm::vec3(-d, 0, 0), comPos + glm::vec3(0, d, 0), Color::yellow);
        SlipDebug::Get().drawLines(comPos + glm::vec3(0, d, 0), comPos + glm::vec3(d, 0, 0), Color::yellow);
        SlipDebug::Get().drawLines(comPos + glm::vec3(d, 0, 0), comPos + glm::vec3(0, -d, 0), Color::yellow);
        SlipDebug::Get().drawLines(comPos + glm::vec3(0, -d, 0), comPos + glm::vec3(-d, 0, 0), Color::yellow);

        // 2. Vector de Velocidad Lineal
        btVector3 vel = chassisBody->getLinearVelocity();
        float vLen = vel.length();
        if (vLen > 0.4f) {
            btVector3 velDir = vel.normalized() * (std::min)(3.5f, vLen * 0.12f);
            SlipDebug::Get().drawLines(comPos, comPos + glm::vec3(velDir.x(), velDir.y(), velDir.z()), Color::blue);
        }

        // 3. Ruedas: Dirección de rodadura/giro, Eje de giro y Rayos de Suspensión
        for (int i = 0; i < m_Vehicle->getNumWheels(); ++i) {
            const btWheelInfo& w = m_Vehicle->getWheelInfo(i);
            const btTransform& wTrans = m_Vehicle->getWheelTransformWS(i);
            glm::vec3 wOrigin(wTrans.getOrigin().x(), wTrans.getOrigin().y(), wTrans.getOrigin().z());

            // Vector de avance de la rueda (incluye ángulo de giro de la dirección)
            btVector3 wFwd = wTrans.getBasis().getColumn(2) * (w.m_wheelsRadius * 1.5f);
            // Eje transversal de rotación de la rueda
            btVector3 wAxle = wTrans.getBasis().getColumn(0) * (w.m_wheelsRadius * 0.8f);

            // Flecha verde indicando la dirección de avance y giro del volante
            SlipDebug::Get().drawLines(wOrigin - glm::vec3(wFwd.x() * 0.4f, wFwd.y() * 0.4f, wFwd.z() * 0.4f),
                                       wOrigin + glm::vec3(wFwd.x(), wFwd.y(), wFwd.z()), Color::green);
            // Eje rojo indicando el eje de rotación
            SlipDebug::Get().drawLines(wOrigin - glm::vec3(wAxle.x(), wAxle.y(), wAxle.z()),
                                       wOrigin + glm::vec3(wAxle.x(), wAxle.y(), wAxle.z()), Color::red);

            // Raycast de suspensión
            btVector3 hardPoint = w.m_raycastInfo.m_hardPointWS;
            glm::vec3 hp(hardPoint.x(), hardPoint.y(), hardPoint.z());

            if (w.m_raycastInfo.m_isInContact) {
                // En contacto con el asfalto: línea amarilla desde el chasis al punto de contacto
                btVector3 cp = w.m_raycastInfo.m_contactPointWS;
                glm::vec3 contactPoint(cp.x(), cp.y(), cp.z());
                SlipDebug::Get().drawLines(hp, contactPoint, Color::yellow);

                // Cruz verde sobre el punto de apoyo en el suelo
                float cs = 0.22f;
                SlipDebug::Get().drawLines(contactPoint + glm::vec3(-cs, 0.02f, 0), contactPoint + glm::vec3(cs, 0.02f, 0), Color::green);
                SlipDebug::Get().drawLines(contactPoint + glm::vec3(0, 0.02f, -cs), contactPoint + glm::vec3(0, 0.02f, cs), Color::green);
            } else {
                // Rueda en el aire: línea roja mostrando el resorte completamente extendido
                btVector3 downDir = w.m_raycastInfo.m_wheelDirectionWS * (w.m_suspensionRestLength1 + w.m_wheelsRadius);
                glm::vec3 extendedEnd = hp + glm::vec3(downDir.x(), downDir.y(), downDir.z());
                SlipDebug::Get().drawLines(hp, extendedEnd, Color::red);
            }
        }

        // 4. Predicción de Trayectoria y Derrape (Drift Trajectory & Envelope Prediction)
        float speedKmh = std::abs(m_Vehicle->getCurrentSpeedKmHour());
        if (speedKmh > 8.0f) {
            btVector3 linVel = chassisBody->getLinearVelocity();
            glm::vec2 v2D(linVel.x(), linVel.z());
            float speedMps = glm::length(v2D);

            glm::vec2 fwd2D(fwd.x(), fwd.z());
            if (glm::length(fwd2D) > 0.001f) fwd2D = glm::normalize(fwd2D);

            glm::vec2 velDir2D = (speedMps > 0.1f) ? (v2D / speedMps) : fwd2D;

            float dotFwdVel = glm::clamp(glm::dot(fwd2D, velDir2D), -1.0f, 1.0f);
            float crossFwdVel = fwd2D.x * velDir2D.y - fwd2D.y * velDir2D.x;
            float slipAngleRad = std::atan2(crossFwdVel, dotFwdVel);
            float slipAngleDeg = glm::degrees(slipAngleRad);

            float steerAngle = m_Vehicle->getSteeringValue(0);

            // Detectar si está en derrape o iniciando transferencia lateral
            bool isDrifting = (std::abs(slipAngleDeg) > 4.5f) || (std::abs(steerAngle) > 0.10f && speedKmh > 30.0f);

            // Altura sobre el suelo (asfalto + offset para evitar z-fighting con el piso)
            float groundY = comPos.y - 0.45f;
            float sumContactY = 0.0f;
            int contactCount = 0;
            for (int i = 0; i < m_Vehicle->getNumWheels(); ++i) {
                if (m_Vehicle->getWheelInfo(i).m_raycastInfo.m_isInContact) {
                    sumContactY += m_Vehicle->getWheelInfo(i).m_raycastInfo.m_contactPointWS.y();
                    contactCount++;
                }
            }
            if (contactCount > 0) {
                groundY = (sumContactY / contactCount) + 0.06f;
            }

            // Simulación cinemático-dinámica proyectada a futuro (~1.5 segundos)
            const int numSteps = 16;
            const float dtStep = 0.095f;

            glm::vec2 simPos2D(comPos.x, comPos.z);
            glm::vec2 simVel2D = v2D;
            float simHeadingRad = std::atan2(fwd2D.x, fwd2D.y);

            float yawRate = chassisBody->getAngularVelocity().y();
            float targetYawRate = (speedMps / 2.6f) * std::tan(steerAngle);

            std::vector<glm::vec3> predictedPath;
            std::vector<glm::vec3> leftTrack;
            std::vector<glm::vec3> rightTrack;
            std::vector<float> stepSlipAngles;

            predictedPath.reserve(numSteps + 1);
            leftTrack.reserve(numSteps + 1);
            rightTrack.reserve(numSteps + 1);
            stepSlipAngles.reserve(numSteps + 1);

            const float halfCarWidth = 0.95f; // Ancho visual del corredor de barrido

            for (int s = 0; s <= numSteps; ++s) {
                predictedPath.push_back(glm::vec3(simPos2D.x, groundY, simPos2D.y));

                glm::vec2 curFwd(std::sin(simHeadingRad), std::cos(simHeadingRad));
                glm::vec2 curRight(curFwd.y, -curFwd.x);

                leftTrack.push_back(glm::vec3(simPos2D.x - curRight.x * halfCarWidth, groundY, simPos2D.y - curRight.y * halfCarWidth));
                rightTrack.push_back(glm::vec3(simPos2D.x + curRight.x * halfCarWidth, groundY, simPos2D.y + curRight.y * halfCarWidth));

                float curSpeed = glm::length(simVel2D);
                float curSlip = 0.0f;
                if (curSpeed > 0.5f) {
                    glm::vec2 curVelDir = simVel2D / curSpeed;
                    float cDot = glm::clamp(glm::dot(curFwd, curVelDir), -1.0f, 1.0f);
                    float cCross = curFwd.x * curVelDir.y - curFwd.y * curVelDir.x;
                    curSlip = std::abs(glm::degrees(std::atan2(cCross, cDot)));
                }
                stepSlipAngles.push_back(curSlip);

                if (s == numSteps) break;

                simPos2D += simVel2D * dtStep;

                float blendYaw = glm::mix(yawRate, targetYawRate, 0.40f);
                simHeadingRad += blendYaw * dtStep;

                glm::vec2 headingFwd(std::sin(simHeadingRad), std::cos(simHeadingRad));
                glm::vec2 headingRight(headingFwd.y, -headingFwd.x);

                float vFwd = glm::dot(simVel2D, headingFwd);
                float vLat = glm::dot(simVel2D, headingRight);

                float latFriction = isDrifting ? 4.5f : 12.0f;
                if (std::abs(vLat) > 0.1f) {
                    float latSign = (vLat > 0.0f) ? 1.0f : -1.0f;
                    vLat -= latSign * latFriction * dtStep;
                    if (latSign * vLat < 0.0f) vLat = 0.0f;
                }
                vFwd -= (0.8f + 0.02f * curSpeed) * dtStep;
                if (vFwd < 0.0f) vFwd = 0.0f;

                simVel2D = headingFwd * vFwd + headingRight * vLat;
            }

            // 4.1 Trazo de trayectoria central con gradiente de derrape
            for (size_t i = 0; i + 1 < predictedPath.size(); ++i) {
                float slipIntensity = glm::clamp(stepSlipAngles[i] / 22.0f, 0.0f, 1.0f);
                glm::vec3 pathCol;
                if (slipIntensity > 0.12f) {
                    // Derrape: Amarillo brillante a Naranja/Rojo fuego
                    pathCol = glm::mix(glm::vec3(1.0f, 0.95f, 0.1f), glm::vec3(1.0f, 0.15f, 0.05f), slipIntensity);
                } else {
                    // Agarre normal: Cyan eléctrico
                    pathCol = glm::vec3(0.1f, 0.85f, 1.0f);
                }
                SlipDebug::Get().drawLines(predictedPath[i], predictedPath[i + 1], pathCol);
            }

            // 4.2 Corredor de ancho del coche (huellas de ruedas proyectadas)
            for (size_t i = 0; i + 1 < leftTrack.size(); ++i) {
                float slipIntensity = glm::clamp(stepSlipAngles[i] / 22.0f, 0.0f, 1.0f);
                glm::vec3 trackCol = (slipIntensity > 0.12f) ? glm::vec3(1.0f, 0.55f, 0.05f) : glm::vec3(0.2f, 0.5f, 0.9f);
                SlipDebug::Get().drawLines(leftTrack[i], leftTrack[i + 1], trackCol);
                SlipDebug::Get().drawLines(rightTrack[i], rightTrack[i + 1], trackCol);
            }

            // 4.3 Barras de actitud y ángulo de carrocería en el derrape
            for (size_t i = 3; i < predictedPath.size(); i += 4) {
                glm::vec3 center = predictedPath[i];
                glm::vec3 left = leftTrack[i];
                glm::vec3 right = rightTrack[i];

                // Barra transversal que muestra la orientación transversal del chasis
                SlipDebug::Get().drawLines(left, right, glm::vec3(1.0f, 0.9f, 0.2f));

                // Flecha de orientación frontal del vehículo
                glm::vec3 dir = glm::cross(right - left, glm::vec3(0, 1, 0));
                if (glm::length(dir) > 0.001f) {
                    glm::vec3 futureFwd = glm::normalize(dir) * 1.1f;
                    SlipDebug::Get().drawLines(center, center + futureFwd, glm::vec3(0.2f, 1.0f, 0.35f));
                }
            }
        }
    }
}

void VehicleComponent::SetEngineForce(float force, int wheelIndex)
{
    if (!m_Vehicle) return;
    if (auto* rb = m_Vehicle->getRigidBody()) {
        rb->activate(true);
    }
    if (wheelIndex >= 0 && wheelIndex < m_Vehicle->getNumWheels()) {
        m_Vehicle->applyEngineForce(force, wheelIndex);
    } else {
        // Tracción deportiva estable: 75% fuerza a ruedas traseras, 25% a delanteras
        // Esto elimina el cabeceo/jaloneo frontal al acelerar y mantiene la tracción en cualquier cuesta
        for (int i = 0; i < m_Vehicle->getNumWheels(); ++i) {
            if (i < (int)wheels.size() && !wheels[i].isFrontWheel) {
                m_Vehicle->applyEngineForce(force * 0.75f, i);
            } else {
                m_Vehicle->applyEngineForce(force * 0.25f, i);
            }
        }
    }
}

void VehicleComponent::SetBrake(float brake, int wheelIndex)
{
    if (!m_Vehicle) return;
    if (auto* rb = m_Vehicle->getRigidBody()) {
        rb->activate(true);
    }
    if (wheelIndex >= 0 && wheelIndex < m_Vehicle->getNumWheels()) {
        m_Vehicle->setBrake(brake, wheelIndex);
    } else {
        // Reparto de frenada deportiva (Brake Bias): 75% delanteras, 25% traseras
        // Mantiene la estabilidad direccional recta y previene que el coche dé trompos de 180 grados al frenar
        for (int i = 0; i < m_Vehicle->getNumWheels(); ++i) {
            if (i < (int)wheels.size() && wheels[i].isFrontWheel) {
                m_Vehicle->setBrake(brake * 0.75f, i);
            } else {
                m_Vehicle->setBrake(brake * 0.25f, i);
            }
        }
    }
}

void VehicleComponent::SetHandBrake(float brake)
{
    if (!m_Vehicle) return;
    if (auto* rb = m_Vehicle->getRigidBody()) {
        rb->activate(true);
    }
    // Freno de mano: frena selectivamente ruedas traseras sin sobreescribir la friccion
    for (int i = 0; i < m_Vehicle->getNumWheels(); ++i) {
        if (i < (int)wheels.size() && !wheels[i].isFrontWheel) {
            m_Vehicle->setBrake(brake, i);
        } else {
            m_Vehicle->setBrake(0.0f, i);
        }
    }
}

void VehicleComponent::SetSteering(float steering, int wheelIndex)
{
    if (!m_Vehicle) return;
    if (wheelIndex >= 0 && wheelIndex < m_Vehicle->getNumWheels()) {
        m_Vehicle->setSteeringValue(steering, wheelIndex);
    } else {
        // Por defecto aplicar giro a ruedas delanteras
        for (int i = 0; i < m_Vehicle->getNumWheels(); ++i) {
            if (i < (int)wheels.size() && wheels[i].isFrontWheel) {
                m_Vehicle->setSteeringValue(steering, i);
            }
        }
    }
}

void VehicleComponent::SetTireFriction(float frontFriction, float rearFriction)
{
    if (!m_Vehicle) return;
    for (int i = 0; i < m_Vehicle->getNumWheels(); ++i) {
        if (i < (int)wheels.size() && wheels[i].isFrontWheel) {
            m_Vehicle->getWheelInfo(i).m_frictionSlip = frontFriction;
        } else {
            m_Vehicle->getWheelInfo(i).m_frictionSlip = rearFriction;
        }
    }
}

void VehicleComponent::SetWheelFriction(int wheelIndex, float friction)
{
    if (!m_Vehicle || wheelIndex < 0 || wheelIndex >= m_Vehicle->getNumWheels()) return;
    m_Vehicle->getWheelInfo(wheelIndex).m_frictionSlip = friction;
}

float VehicleComponent::GetCurrentSpeedKmHour() const
{
    return m_Vehicle ? m_Vehicle->getCurrentSpeedKmHour() : 0.0f;
}

int VehicleComponent::GetNumWheels() const
{
    return m_Vehicle ? m_Vehicle->getNumWheels() : (int)wheels.size();
}

glm::vec3 VehicleComponent::GetWheelPosition(int wheelIndex) const
{
    if (!m_Vehicle || wheelIndex < 0 || wheelIndex >= m_Vehicle->getNumWheels()) return glm::vec3(0.0f);
    const btTransform& trans = m_Vehicle->getWheelTransformWS(wheelIndex);
    return glm::vec3(trans.getOrigin().x(), trans.getOrigin().y(), trans.getOrigin().z());
}

glm::vec3 VehicleComponent::GetWheelRotation(int wheelIndex) const
{
    if (!m_Vehicle || wheelIndex < 0 || wheelIndex >= m_Vehicle->getNumWheels()) return glm::vec3(0.0f);
    const btTransform& trans = m_Vehicle->getWheelTransformWS(wheelIndex);
    return BulletMatrixToEulerDegrees(trans.getBasis());
}

bool VehicleComponent::IsWheelOnGround(int wheelIndex) const
{
    if (!m_Vehicle || wheelIndex < 0 || wheelIndex >= m_Vehicle->getNumWheels()) return false;
    return m_Vehicle->getWheelInfo(wheelIndex).m_raycastInfo.m_isInContact;
}

void VehicleComponent::ResetSuspension()
{
    if (!m_Vehicle) return;
    m_Vehicle->resetSuspension();
    for (int i = 0; i < m_Vehicle->getNumWheels(); ++i) {
        m_Vehicle->updateWheelTransform(i, true);
    }
}

void VehicleComponent::OnDrawInspectorUI()
{
}
