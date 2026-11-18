#pragma once

#ifndef SLIP_COMPONENT_H
#define SLIP_COMPONENT_H

#include "SlipEngine.h"
#include "SlipMesh.h"
#include "SlipMaterial.h"
#include "SlipCollision.h"
#include "SlipEngine/render/SlipPostProcess.h"
#include "SlipEngine/render/SlipSkeleton.h"
#include "SlipEngine/render/SlipAnimation.h"
#include "SlipEngine/scene/SlipKeyframeAnimation.h"
#include "SlipEngine/scene/SlipSortingLayers.h"
#include <cstdint>
#include <ostream>
#include <btBulletDynamicsCommon.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <utility>

class SlipEntity;

class SLIPENGINE_API SlipComponent
{
public:
    virtual ~SlipComponent() = default;

    virtual void OnInit() {}
    virtual void OnStart() {}
    virtual void OnUpdate(float deltaTime) {}
    virtual void OnDraw(const glm::mat4& parentTransform) {}
    virtual void OnDrawInspectorUI() {}

    SlipEntity* GetOwner() const { return m_Owner; }
    void SetOwner(SlipEntity* owner) { m_Owner = owner; }

    const std::string& GetName() const { return m_Name; }
    void SetName(const std::string& name) { m_Name = name; }

    bool IsEnabled() const { return m_Enabled; }
    void SetEnabled(bool enabled) { m_Enabled = enabled; }

protected:
    SlipEntity* m_Owner = nullptr;
    std::string m_Name = "Component";
    bool m_Enabled = true;
};

// ---------------------------------------------------------
// TransformComponent
// ---------------------------------------------------------
class SLIPENGINE_API TransformComponent : public SlipComponent
{
public:
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    bool inheritScale = true; // Si es falso, no hereda la escala no-uniforme del padre (evita deformación de ruedas)

    TransformComponent();

    void Translate(float x, float y, float z);
    void Translate(const glm::vec3& delta);
    void Rotate(float x, float y, float z);
    void Rotate(const glm::vec3& delta);

    glm::mat4 GetLocalMatrix() const;
    glm::mat4 GetWorldMatrix() const;

    void OnDrawInspectorUI() override;
};

// ---------------------------------------------------------
// MeshComponent
// ---------------------------------------------------------
class SLIPENGINE_API MeshComponent : public SlipComponent
{
public:
    std::string modelPath;
    SlipMesh* model = nullptr;

    std::string materialPath;
    SlipMaterial* material = nullptr;

    std::vector<std::string> submeshMaterialPaths;
    std::vector<SlipMaterial*> submeshMaterials;

    bool ownsModel = true;

    // --- Soporte de Lightmaps para mallas estáticas ---
    glm::vec4 lightmapScaleOffset{ 1.0f, 1.0f, 0.0f, 0.0f };
    int lightmapIndex = -1;

    MeshComponent();
    MeshComponent(const std::string& path);
    ~MeshComponent() override;

    void SetModel(const std::string& path);
    void SetSharedModel(SlipMesh* sharedMesh, const std::string& path);
    void SetMaterial(const std::string& path);
    void SetSubmeshMaterial(size_t slotIndex, const std::string& path);
    SlipMaterial* GetSubmeshMaterial(size_t slotIndex) const;
    size_t GetMaterialSlotCount() const;
    std::string GetMaterialSlotName(size_t slotIndex) const;
    std::string GetSubmeshMaterialPath(size_t slotIndex) const;

    void init();
    void draw();

    void OnInit() override;
    void OnDraw(const glm::mat4& parentTransform) override;
    void OnDrawInspectorUI() override;
};

// ---------------------------------------------------------
// BoneComponent
// ---------------------------------------------------------
class SLIPENGINE_API BoneComponent : public SlipComponent
{
public:
    int boneIndex = -1;
    std::string boneName;
    glm::mat4 inverseBindMatrix = glm::mat4(1.0f);
    glm::mat4 localBindTransform = glm::mat4(1.0f);
    glm::quat orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    BoneComponent() { m_Name = "BoneComponent"; }
    BoneComponent(int idx, const std::string& name, const glm::mat4& invBind = glm::mat4(1.0f))
        : boneIndex(idx), boneName(name), inverseBindMatrix(invBind) {
        m_Name = "BoneComponent";
    }
    ~BoneComponent() override;

    void OnDrawInspectorUI() override;
};

// ---------------------------------------------------------
// SkinnedMeshComponent
// ---------------------------------------------------------
class SLIPENGINE_API SkinnedMeshComponent : public SlipComponent
{
public:
    std::string modelPath;
    SlipMesh* model = nullptr;

    std::string materialPath;
    SlipMaterial* material = nullptr;

    std::vector<std::string> submeshMaterialPaths;
    std::vector<SlipMaterial*> submeshMaterials;

    std::shared_ptr<SlipSkeleton> skeleton;
    std::vector<glm::mat4> boneTransforms; // 100 matrices uniformes
    std::vector<SlipEntity*> boneEntities;
    bool castShadows = true;

    using SkeletonExtractCallback = std::function<bool(const std::string& modelPath, SlipSkeleton& outSkeleton, std::vector<std::shared_ptr<AnimationClip>>& outClips)>;
    static void SetSkeletonExtractCallback(SkeletonExtractCallback cb);

    SkinnedMeshComponent();
    SkinnedMeshComponent(const std::string& path);
    ~SkinnedMeshComponent() override;

    void SetModel(const std::string& path);
    void SetMaterial(const std::string& path);
    void SetSubmeshMaterial(size_t slotIndex, const std::string& path);
    SlipMaterial* GetSubmeshMaterial(size_t slotIndex) const;
    size_t GetMaterialSlotCount() const;
    std::string GetMaterialSlotName(size_t slotIndex) const;
    std::string GetSubmeshMaterialPath(size_t slotIndex) const;

    void BuildBoneHierarchy(class SlipLevel* level);
    void UpdateBoneMatrices();
    void MarkBoneMatricesDirty() { m_BoneMatricesDirty = true; }
    void draw(const glm::mat4& worldTransform);

    void OnInit() override;
    void OnUpdate(float deltaTime) override;
    void OnDraw(const glm::mat4& parentTransform) override;
    void OnDrawInspectorUI() override;

private:
    bool m_BoneMatricesDirty = true;
};

class SlipAnimatorController;

// ---------------------------------------------------------
// AnimatorComponent
// ---------------------------------------------------------
class SLIPENGINE_API AnimatorComponent : public SlipComponent
{
public:
    std::vector<std::shared_ptr<AnimationClip>> clips;
    int currentClipIndex = -1;
    float currentTime = 0.0f;
    float playbackSpeed = 1.0f;
    bool isPlaying = true;
    bool isLooping = true;

    // Animator Controller (Máquina de estados / Locomotion)
    std::string controllerPath;
    std::shared_ptr<SlipAnimatorController> controller;

    AnimatorComponent();
    ~AnimatorComponent() override = default;

    void Play(const std::string& clipName, float speed = 1.0f, bool loop = true);
    void Play(int clipIndex, float speed = 1.0f, bool loop = true);
    void Pause() { isPlaying = false; }
    void Resume() { isPlaying = true; }
    void Stop() { isPlaying = false; currentTime = 0.0f; }
    void SetTime(float time);
    void ApplyPose(float time);
    void ApplyBlendedPose(const AnimationClip* clipA, float timeA, const AnimationClip* clipB, float timeB, float blendWeight);

    void SetController(const std::string& path);
    void SetFloat(const std::string& name, float value);
    void SetBool(const std::string& name, bool value);
    void SetInt(const std::string& name, int value);
    void SetTrigger(const std::string& name);
    float GetFloat(const std::string& name) const;
    bool GetBool(const std::string& name) const;
    int GetInt(const std::string& name) const;

    float GetCurrentTime() const { return currentTime; }
    float GetCurrentDuration() const;
    AnimationClip* GetCurrentClip() const;
    AnimationClip* GetClip(const std::string& name) const;
    AnimationClip* GetClip(int index) const;
    int GetClipCount() const { return static_cast<int>(clips.size()); }
    void AddClip(std::shared_ptr<AnimationClip> clip);

    void InvalidateEntityCache() { m_CachedTrackClip = nullptr; m_TrackEntities.clear(); }

    void OnInit() override;
    void OnUpdate(float deltaTime) override;
    void OnDrawInspectorUI() override;

private:
    std::vector<SlipEntity*> m_TrackEntities;
    const AnimationClip* m_CachedTrackClip = nullptr;
};

// ---------------------------------------------------------
// KeyframeAnimationComponent (Animaciones por Keyframe / Cinemáticas)
// ---------------------------------------------------------
class SLIPENGINE_API KeyframeAnimationComponent : public SlipComponent
{
public:
    std::string clipPath;
    std::shared_ptr<KeyframeClip> clip;
    bool playOnStart = true;
    bool loop = false;
    float playbackSpeed = 1.0f;
    float currentTime = 0.0f;
    bool isPlaying = false;

    KeyframeAnimationComponent();
    explicit KeyframeAnimationComponent(const std::string& path);
    ~KeyframeAnimationComponent() override = default;

    bool LoadClip(const std::string& path);
    void Play();
    void Pause();
    void Resume();
    void Stop();
    void SetTime(float time);

    void OnInit() override;
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDrawInspectorUI() override;
};

// ---------------------------------------------------------
// Jerarquía de Colliders
// ---------------------------------------------------------
class SLIPENGINE_API ColliderComponent : public SlipComponent
{
public:
    glm::vec3 centerOffset = glm::vec3(0.0f);
    bool isTrigger = false;

    btCollisionShape* collisionShape = nullptr;
    btRigidBody* rigidBody = nullptr;
    btDefaultMotionState* motionState = nullptr;
    glm::mat4 lastSyncedMatrix = glm::mat4(0.0f);

    ColliderComponent();
    ~ColliderComponent() override;

    virtual void CreateOrUpdateShape() = 0;
    void RecreateRigidBody(float mass = 0.0f, bool isKinematic = false);
    void SyncTransformFromEntity();

    void OnInit() override;
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
};

class SLIPENGINE_API BoxColliderComponent : public ColliderComponent
{
public:
    glm::vec3 size = glm::vec3(1.0f);

    BoxColliderComponent();
    BoxColliderComponent(glm::vec3 boxSize);

    void CreateOrUpdateShape() override;
    void OnDrawInspectorUI() override;
};

class SLIPENGINE_API SphereColliderComponent : public ColliderComponent
{
public:
    float radius = 0.5f;

    SphereColliderComponent();
    SphereColliderComponent(float r);

    void CreateOrUpdateShape() override;
    void OnDrawInspectorUI() override;
};

class SLIPENGINE_API CapsuleColliderComponent : public ColliderComponent
{
public:
    float radius = 0.5f;
    float height = 1.0f;

    CapsuleColliderComponent();
    CapsuleColliderComponent(float r, float h);

    void CreateOrUpdateShape() override;
    void OnDrawInspectorUI() override;
};

class SLIPENGINE_API MeshColliderComponent : public ColliderComponent
{
public:
    bool isConvex = false;
    std::string meshPath;
    class btTriangleMesh* m_TriangleMesh = nullptr;

    MeshColliderComponent();
    ~MeshColliderComponent() override;

    void CreateOrUpdateShape() override;
    void OnDrawInspectorUI() override;
};

// ---------------------------------------------------------
// RigidBodyComponent
// ---------------------------------------------------------
class SLIPENGINE_API RigidBodyComponent : public SlipComponent
{
public:
    float mass = 1.0f;
    float linearDrag = 0.0f;
    float angularDrag = 0.05f;
    bool useGravity = true;
    bool isKinematic = false;

    // Fricción y restitución para evitar bloqueos contra paredes y controlar deslizamiento
    float friction = 0.5f;
    float rollingFriction = 0.0f;
    float restitution = 0.0f;

    // Constraints estilo Unity (Freeze Position & Freeze Rotation)
    bool freezePositionX = false;
    bool freezePositionY = false;
    bool freezePositionZ = false;
    bool freezeRotationX = false;
    bool freezeRotationY = false;
    bool freezeRotationZ = false;

    RigidBodyComponent();

    void ApplyToCollider();
    btRigidBody* GetBulletRigidBody() const;

    // Métodos de interacción física estilo Unity
    void AddForce(const glm::vec3& force);
    void AddImpulse(const glm::vec3& impulse);
    void AddTorque(const glm::vec3& torque);
    void SetLinearVelocity(const glm::vec3& velocity);
    glm::vec3 GetLinearVelocity() const;
    void SetAngularVelocity(const glm::vec3& velocity);
    glm::vec3 GetAngularVelocity() const;
    void SetRotation(const glm::vec3& rotation);
    void SetPosition(const glm::vec3& position);

    void SetFriction(float f);
    float GetFriction() const;
    void SetRollingFriction(float rf);
    float GetRollingFriction() const;
    void SetRestitution(float r);
    float GetRestitution() const;

    void OnInit() override;
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDrawInspectorUI() override;
};

// ---------------------------------------------------------
// LightComponent
// ---------------------------------------------------------
enum class LightType {
    Directional,
    Point,
    Spot
};

enum class LightMobility {
    Static,   // Se hornea totalmente en lightmaps; inactivo en runtime
    Mixed,    // Hornea rebotes indirectos en lightmaps; luz directa activa en runtime
    Dynamic   // 100% dinámico en runtime vía Clustered Forward Shading
};

class SLIPENGINE_API LightComponent : public SlipComponent
{
public:
    LightType type = LightType::Point;
    LightMobility mobility = LightMobility::Dynamic;
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    float range = 10.0f;
    float spotAngle = 30.0f;
    bool castShadows = true;

    LightComponent(LightType lightType = LightType::Point);

    void OnDrawInspectorUI() override;
};

// ---------------------------------------------------------
// Volumen fijo de sondas DDGI (opcional, solo tiene efecto en PC)
// ---------------------------------------------------------
class SLIPENGINE_API DDGIVolumeComponent : public SlipComponent
{
public:
    glm::vec3 size = glm::vec3(24.0f, 12.0f, 24.0f);
    int probesX = 16;
    int probesY = 8;
    int probesZ = 16;
    bool previewProbes = true;

    DDGIVolumeComponent();

    void OnDrawInspectorUI() override;
};

// ---------------------------------------------------------
// Sonda / Volumen de Reflexión Horneado (Reflection Probe)
// ---------------------------------------------------------
class SLIPENGINE_API ReflectionProbeComponent : public SlipComponent
{
public:
    glm::vec3 size = glm::vec3(16.0f, 8.0f, 16.0f);
    int resolution = 256;             // 128, 256, 512
    float intensity = 1.0f;
    float roughnessCutoff = 0.5f;
    bool boxProjection = true;        // Parallax Box Projection
    bool includeDynamic = false;
    bool isBaked = false;

    unsigned int cubemap = 0;         // GL_TEXTURE_CUBE_MAP

    ReflectionProbeComponent();
    ~ReflectionProbeComponent() override;

    void OnDrawInspectorUI() override;
    bool IsInside(const glm::vec3& worldPos, const glm::vec3& probeCenter) const;
    void GetBoxBounds(const glm::vec3& probeCenter, glm::vec3& outMin, glm::vec3& outMax) const;
};

// ---------------------------------------------------------
// Volumen de post-proceso (global o caja), estilo Unreal
// ---------------------------------------------------------
class SLIPENGINE_API PostProcessVolumeComponent : public SlipComponent
{
public:
    bool isGlobal = true;
    int priority = 0;
    float weight = 1.0f;
    glm::vec3 size = glm::vec3(16.0f);
    float blendDistance = 2.0f;

    bool bloomOverride = true;
    bool bloomEnabled = true;
    float bloomIntensity = 0.55f;
    float bloomThreshold = 1.0f;
    float bloomSoftKnee = 0.5f;
    float bloomRadius = 0.85f;
    glm::vec3 bloomTint = glm::vec3(1.0f);

    bool lensOverride = true;
    bool lensEnabled = false;
    float lensIntensity = 0.45f;
    float lensThreshold = 2.5f;
    float lensStreak = 0.55f;
    float lensHalo = 0.35f;

    bool aoOverride = true;
    bool aoEnabled = false;
    float aoRadius = 0.45f;
    float aoBias = 0.025f;
    float aoIntensity = 1.0f;
    float aoPower = 1.4f;

    bool vignetteOverride = true;
    bool vignetteEnabled = false;
    float vignetteIntensity = 0.35f;
    float vignetteSmoothness = 0.45f;

    bool exposureOverride = true;
    bool exposureEnabled = false;
    float exposureMin = 0.05f;
    float exposureMax = 8.0f;
    float exposureCompensation = 0.0f;
    float exposureSpeed = 2.0f;

    bool colorOverride = true;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float temperature = 0.0f;
    glm::vec3 lift = glm::vec3(0.0f);
    glm::vec3 gamma = glm::vec3(1.0f);
    glm::vec3 gain = glm::vec3(1.0f);
    glm::vec3 colorFilter = glm::vec3(1.0f);
    int tonemap = 0;

    bool blurOverride = true;
    float blurRadius = 0.0f;

    bool radialOverride = true;
    float radialStrength = 0.0f;
    glm::vec2 radialCenter = glm::vec2(0.5f);

    bool fogOverride = true;
    bool fogEnabled = false;
    float fogDensity = 0.003f;
    float fogHeightFalloff = 0.035f;
    float fogHeight = 0.0f;
    float fogStartDistance = 5.0f;
    float fogMaxDistance = 12000.0f;
    glm::vec3 fogColor = glm::vec3(0.65f, 0.72f, 0.85f);
    float fogSunScattering = 0.65f;
    float fogSkyBlend = 0.90f;

    PostProcessVolumeComponent();

    void SerializeTo(std::ostream& out) const;
    void DeserializeKey(const std::string& key, const std::string& value);

    static PostProcessSettings Resolve(class SlipLevel* level, const glm::vec3& cameraPos);
};

// ---------------------------------------------------------
// ScriptComponent (Lua)
// ---------------------------------------------------------
enum class ScriptPropertyType {
    Float,
    Int,
    Bool,
    String,
    Vec3
};

struct ScriptProperty {
    std::string name;
    ScriptPropertyType type = ScriptPropertyType::Float;
    float floatValue = 0.0f;
    int intValue = 0;
    bool boolValue = false;
    std::string stringValue;
    glm::vec3 vec3Value{ 0.0f };
};

class SLIPENGINE_API ScriptComponent : public SlipComponent
{
public:
    std::string scriptPath;
    bool isCompiled = false;
    std::string lastError;

    ScriptComponent();
    ScriptComponent(const std::string& path);
    ~ScriptComponent() override;

    void SetScriptPath(const std::string& path);
    bool ReloadScript();

    std::string GetScriptFileName() const;
    std::string GetScriptNameWithoutExtension() const;

    const std::vector<ScriptProperty>& GetProperties() const { return m_Properties; }
    std::vector<ScriptProperty>& GetProperties() { return m_Properties; }
    void SetPropertyValue(const std::string& name, float val);
    void SetPropertyValue(const std::string& name, int val);
    void SetPropertyValue(const std::string& name, bool val);
    void SetPropertyValue(const std::string& name, const std::string& val);
    void SetPropertyValue(const std::string& name, const glm::vec3& val);

    void SyncPropertiesFromLua();
    void PushPropertiesToLua();

    void OnInit() override;
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDrawInspectorUI() override;

    // Callbacks físicos (despachados a Lua)
    void OnCollisionEnter(SlipEntity* other);
    void OnCollisionStay(SlipEntity* other);
    void OnCollisionExit(SlipEntity* other);
    void OnTriggerEnter(SlipEntity* other);
    void OnTriggerStay(SlipEntity* other);
    void OnTriggerExit(SlipEntity* other);

    // Invocar función personalizada en el script Lua (usado por UIButton, UISlider, etc.)
    bool InvokeLuaFunction(const std::string& functionName);
    bool InvokeLuaFunctionFloat(const std::string& functionName, float value);

    // Acceso interno a la tabla de script
    struct Impl;
    Impl* GetImpl() const { return m_Impl.get(); }
    void* GetScriptTableRaw() const;

private:
    std::unique_ptr<Impl> m_Impl;
    std::vector<ScriptProperty> m_Properties;
};

// ---------------------------------------------------------
// CameraComponent (Cámara estilo Unity3D)
// ---------------------------------------------------------
enum class CameraProjection {
    Perspective = 0,
    Orthographic = 1
};

class SLIPENGINE_API CameraComponent : public SlipComponent
{
public:
    CameraProjection projectionType = CameraProjection::Perspective;
    float fov = 60.0f; // FOV vertical en grados
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    float orthoSize = 5.0f;
    bool isPrimary = true;
    glm::vec4 clearColor = glm::vec4(0.12f, 0.14f, 0.18f, 1.0f);

    // Bordes negros cinematográficos (0.0 a 0.5)
    float letterbox = 0.0f;
    float pillarbox = 0.0f;

    // Camera Shake
    float shakeIntensity = 0.0f;
    float shakeDuration = 0.0f;
    float shakeTimer = 0.0f;
    float shakeElapsed = 0.0f;
    float shakeFrequency = 25.0f;
    glm::vec3 currentShakeOffset = glm::vec3(0.0f);
    glm::vec3 currentShakeRotation = glm::vec3(0.0f); // Pitch, Yaw, Roll en grados

    CameraComponent();
    ~CameraComponent() override = default;

    float GetFOV() const { return fov; }
    void SetFOV(float val) { fov = glm::clamp(val, 1.0f, 179.0f); }

    float GetLetterbox() const { return letterbox; }
    void SetLetterbox(float amount) { letterbox = glm::clamp(amount, 0.0f, 0.5f); }

    float GetPillarbox() const { return pillarbox; }
    void SetPillarbox(float amount) { pillarbox = glm::clamp(amount, 0.0f, 0.5f); }

    void SetBlackBars(float h, float v) { SetLetterbox(h); SetPillarbox(v); }

    void Shake(float intensity, float duration, float frequency = 25.0f);
    void StopShake();
    bool IsShaking() const { return shakeDuration > 0.0f && shakeTimer < shakeDuration; }
    float GetShakeIntensity() const { return shakeIntensity; }
    void SetShakeIntensity(float val) { shakeIntensity = val; }

    glm::vec3 GetPosition() const;
    glm::vec3 GetForward() const;
    glm::vec3 GetUp() const;
    glm::vec3 GetRight() const;

    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix(float aspectRatio) const;

    void OnUpdate(float deltaTime) override;
    void OnDrawInspectorUI() override;
};

// ---------------------------------------------------------
// CinematicCameraComponent (Cámara Cinemática estilo Forza Horizon / NFS)
// ---------------------------------------------------------
class SLIPENGINE_API CinematicCameraComponent : public SlipComponent
{
public:
    std::string targetEntityName = "";
    SlipEntity* targetEntity = nullptr;

    // Dimensiones del Spring Arm
    float targetDistance = 4.8f;      // Distancia detrás del vehículo (estilo Forza/NFS)
    float targetHeight = 0.85f;       // Altura de cámara respecto al centro del vehículo
    float targetPitch = 6.0f;         // Inclinación vertical en grados mirando hacia abajo
    float pivotYOffset = 0.40f;       // Desplazamiento del punto pivote en el objetivo (centro de masa)

    // Suavizado y Amortiguación (Damped Spring)
    float positionLag = 8.5f;         // Rapidez del resorte de posición
    float rotationLag = 6.5f;         // Rapidez del resorte de giro (Yaw)
    float pitchLag = 8.0f;            // Rapidez del resorte de cabeceo (Pitch)

    // Dinámica de Derrape estilo NFS
    float driftCompensation = 0.40f;  // Compensación angular para encuadrar derrapes
    float maxDriftYawOffset = 35.0f;  // Límite máximo en grados de desplazamiento por derrape

    // FOV Dinámico
    bool enableDynamicFOV = true;
    float baseFOV = 60.0f;
    float boostFOV = 76.0f;
    float fovLag = 4.5f;
    float maxSpeedForFOV = 220.0f;     // Velocidad en km/h a la que se alcanza el FOV máximo

    // Anticolisión (Spring Arm Probe / Raycast)
    bool enableCollisionAvoidance = true;
    float collisionRadius = 0.25f;
    float minCollisionDistance = 1.2f;

    // Control Orbital Manual
    bool enableMouseOrbit = true;
    float mouseSensitivity = 0.15f;
    float autoCenterDelay = 1.4f;     // Segundos sin input de ratón antes de volver detrás del coche
    float autoCenterSpeed = 2.5f;

    // Estado interno
    float currentYaw = 0.0f;
    float currentPitch = 9.0f;
    float currentFOV = 60.0f;
    float currentDriftOffset = 0.0f;
    float orbitYaw = 0.0f;
    float orbitPitch = 0.0f;
    float lastMouseInputTimer = 0.0f;
    bool hasInitialized = false;
    glm::vec3 currentPosition = glm::vec3(0.0f);
    glm::vec3 smoothedLookTarget = glm::vec3(0.0f);

    CinematicCameraComponent();
    ~CinematicCameraComponent() override = default;

    void OnInit() override;
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDrawInspectorUI() override;

    void SetTarget(SlipEntity* target);
    void SetTargetName(const std::string& name);
    SlipEntity* GetTarget() const;
    void ResetImmediate();
};

// ---------------------------------------------------------
// SpriteRendererComponent (Sprites 2D estilo Unity3D)
// ---------------------------------------------------------
class SLIPENGINE_API SpriteRendererComponent : public SlipComponent
{
public:
    std::string texturePath;
    SlipTexture* texture = nullptr;
    glm::vec4 color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    bool flipX = false;
    bool flipY = false;
    int sortingOrder = 0; // Orden dentro de la capa (Order in Layer)
    int orderInLayer = 0; // Alias de sortingOrder
    std::string sortingLayerName = "Default"; // Nombre de capa estilo Unity
    float pixelsPerUnit = 100.0f;
    bool isPixelArt = true; // Filtrado Point / Nearest nítido sin difuminar (Ideal para pixel art)

    // Sombreado 2D dinámico (Lit 2D) con luces 2D y sombras
    bool isLit = false;
    std::string normalMapPath;
    SlipTexture* normalMap = nullptr;

    // Soporte de Sprite Sheet / Atlas 2D (animación por frames)
    int hFrames = 1; // Columnas
    int vFrames = 1; // Filas
    int currentFrame = 0;

    SpriteRendererComponent();
    SpriteRendererComponent(const std::string& texPath);
    ~SpriteRendererComponent() override;

    void SetTexture(const std::string& path);
    void SetNormalMap(const std::string& path);
    glm::vec4 GetUVRect() const; // (uOffset, vOffset, uScale, vScale)
    glm::vec2 GetBaseQuadSize() const; // Dimensiones en unidades de mundo según PPU y frame
    int GetSortingLayerIndex() const;

    void OnInit() override;
    void OnDrawInspectorUI() override;
};

// ---------------------------------------------------------
// SpriteAnimatorComponent (Animaciones por Spritesheet)
// ---------------------------------------------------------
struct SLIPENGINE_API SpriteAnimationClip {
    std::string name = "Clip";
    int startFrame = 0;
    int endFrame = 0;
    float frameRate = 12.0f; // FPS
    bool loop = true;
};

class SLIPENGINE_API SpriteAnimatorComponent : public SlipComponent
{
public:
    std::vector<SpriteAnimationClip> clips;
    int activeClipIndex = -1;
    float playbackTimer = 0.0f;
    int currentFrame = 0;
    bool isPlaying = true;
    bool playOnStart = true;
    std::string defaultClip;

    SpriteAnimatorComponent();
    ~SpriteAnimatorComponent() override = default;

    void OnInit() override;
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDrawInspectorUI() override;

    void Play(const std::string& clipName);
    void PlayClipByIndex(int index);
    void Stop();
    void Pause();
    void Resume();
    void AddClip(const std::string& name, int startFrame, int endFrame, float fps = 12.0f, bool loop = true);
    void RemoveClip(int index);
    SpriteAnimationClip* GetClip(const std::string& name);
    SpriteAnimationClip* GetActiveClip();
    std::string GetCurrentClipName() const;
};

// ---------------------------------------------------------
// Colisiones 2D (BoxCollider2D & CircleCollider2D)
// ---------------------------------------------------------
class SLIPENGINE_API BoxCollider2DComponent : public SlipComponent
{
public:
    glm::vec2 size = glm::vec2(1.0f, 1.0f);
    glm::vec2 offset = glm::vec2(0.0f, 0.0f);
    bool isTrigger = false;
    float density = 1.0f;
    float friction = 0.2f;
    float restitution = 0.0f;

    btRigidBody* staticBody = nullptr;
    btCollisionShape* staticShape = nullptr;
    glm::mat4 lastSyncedMatrix = glm::mat4(0.0f);

    BoxCollider2DComponent();
    BoxCollider2DComponent(const glm::vec2& s, const glm::vec2& off = glm::vec2(0.0f));
    ~BoxCollider2DComponent() override;

    void OnInit() override;
    void OnUpdate(float deltaTime) override;
    void OnDrawInspectorUI() override;

    void RecreateStaticBody();
    void DestroyStaticBody();
};

class SLIPENGINE_API CircleCollider2DComponent : public SlipComponent
{
public:
    float radius = 0.5f;
    glm::vec2 offset = glm::vec2(0.0f, 0.0f);
    bool isTrigger = false;
    float density = 1.0f;
    float friction = 0.2f;
    float restitution = 0.0f;

    btRigidBody* staticBody = nullptr;
    btCollisionShape* staticShape = nullptr;
    glm::mat4 lastSyncedMatrix = glm::mat4(0.0f);

    CircleCollider2DComponent();
    CircleCollider2DComponent(float r, const glm::vec2& off = glm::vec2(0.0f));
    ~CircleCollider2DComponent() override;

    void OnInit() override;
    void OnUpdate(float deltaTime) override;
    void OnDrawInspectorUI() override;

    void RecreateStaticBody();
    void DestroyStaticBody();
};

// ---------------------------------------------------------
// RigidBody2DComponent (Física 2D con simulación restringida)
// ---------------------------------------------------------
enum class BodyType2D {
    Dynamic = 0,
    Static = 1,
    Kinematic = 2
};

class SLIPENGINE_API RigidBody2DComponent : public SlipComponent
{
public:
    BodyType2D bodyType = BodyType2D::Dynamic;
    float mass = 1.0f;
    float gravityScale = 1.0f;
    bool freezeRotation = false;
    glm::vec2 linearVelocity = glm::vec2(0.0f);
    float angularVelocity = 0.0f;
    float linearDamping = 0.0f;
    float angularDamping = 0.05f;

    btRigidBody* bulletBody = nullptr;
    btCollisionShape* bulletShape = nullptr;

    RigidBody2DComponent();
    ~RigidBody2DComponent() override;

    void OnInit() override;
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDrawInspectorUI() override;

    void ApplyForce(const glm::vec2& force);
    void ApplyImpulse(const glm::vec2& impulse);
    void SetLinearVelocity(const glm::vec2& vel);
    glm::vec2 GetLinearVelocity() const;
    void SetAngularVelocity(float omega);
    float GetAngularVelocity() const;
    void SetGravityScale(float scale);
    float GetGravityScale() const { return gravityScale; }

    void SyncPhysicsFromTransform();
    void SyncTransformFromPhysics();
    void RebuildPhysicsBody();
};

// ---------------------------------------------------------
// Luces 2D y Colisión de Luces / Sombras 2D
// ---------------------------------------------------------
enum class Light2DType {
    Point = 0,
    Spot = 1,
    Directional = 2
};

class SLIPENGINE_API Light2DComponent : public SlipComponent
{
public:
    Light2DType lightType = Light2DType::Point;
    glm::vec3 color = glm::vec3(1.0f, 1.0f, 0.9f);
    float intensity = 1.0f;
    float radius = 5.0f;
    float innerRadius = 0.0f;
    float spotAngle = 45.0f; // Grados para luz focal 2D
    bool castShadows = true;
    float shadowIntensity = 0.85f;

    Light2DComponent();
    ~Light2DComponent() override = default;

    void OnDrawInspectorUI() override;
};

enum class ShadowCasterShape2D {
    Box = 0,
    Circle = 1
};

class SLIPENGINE_API ShadowCaster2DComponent : public SlipComponent
{
public:
    ShadowCasterShape2D shape = ShadowCasterShape2D::Box;
    glm::vec2 size = glm::vec2(1.0f, 1.0f);
    float radius = 0.5f;
    glm::vec2 offset = glm::vec2(0.0f, 0.0f);
    bool castShadows = true;

    ShadowCaster2DComponent();
    ~ShadowCaster2DComponent() override = default;

    void OnDrawInspectorUI() override;
};

// ---------------------------------------------------------
// TileGridComponent (Tilemap con Anclaje)
// ---------------------------------------------------------
enum class TileGridAnchor {
    Center = 0,
    BottomLeft = 1,
    TopLeft = 2,
    BottomCenter = 3
};

class SLIPENGINE_API TileGridComponent : public SlipComponent
{
public:
    glm::vec2 cellSize = glm::vec2(1.0f, 1.0f);
    TileGridAnchor anchor = TileGridAnchor::Center;
    std::string tileSheetPath;
    SlipTexture* tileSheet = nullptr;
    int sheetHFrames = 1;
    int sheetVFrames = 1;
    std::string sortingLayerName = "Default";
    int orderInLayer = 0;
    bool isPixelArt = true; // Filtrado Point / Nearest nítido sin difuminar

    // Almacena (x, y) coordenada de celda -> índice de tile en el spritesheet
    std::map<std::pair<int, int>, int> tiles;

    TileGridComponent();
    ~TileGridComponent() override;

    void OnInit() override;
    void OnDrawInspectorUI() override;

    void SetTileSheet(const std::string& path);
    void SetTile(int x, int y, int tileIndex);
    void RemoveTile(int x, int y);
    int GetTile(int x, int y) const;
    bool HasTile(int x, int y) const;
    void Clear();

    glm::vec2 GetAnchorOffset() const;
    glm::vec2 CellToWorld(int x, int y) const;
    std::pair<int, int> WorldToCell(const glm::vec2& worldPos) const;
    glm::vec4 GetTileUV(int tileIndex) const;
    int GetSortingLayerIndex() const;
};

// ---------------------------------------------------------
// Sistema UI Estilo Unity3D (Canvas + RectTransform + Widgets)
// ---------------------------------------------------------
enum class CanvasRenderMode {
    ScreenSpaceOverlay = 0,
    WorldSpace = 1
};

class SLIPENGINE_API CanvasComponent : public SlipComponent
{
public:
    CanvasRenderMode renderMode = CanvasRenderMode::ScreenSpaceOverlay;
    glm::vec2 referenceResolution = glm::vec2(1920.0f, 1080.0f);
    bool scaleWithScreenSize = true;
    int sortingOrder = 0;

    CanvasComponent();
    ~CanvasComponent() override = default;

    void OnDrawInspectorUI() override;
};

enum class UIAnchorPreset {
    Center = 0,
    TopLeft,
    TopCenter,
    TopRight,
    MiddleLeft,
    MiddleRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
    StretchAll
};

class SLIPENGINE_API RectTransformComponent : public SlipComponent
{
public:
    UIAnchorPreset anchorPreset = UIAnchorPreset::Center;
    glm::vec2 anchorMin = glm::vec2(0.5f, 0.5f);
    glm::vec2 anchorMax = glm::vec2(0.5f, 0.5f);
    glm::vec2 anchoredPosition = glm::vec2(0.0f, 0.0f);
    glm::vec2 size = glm::vec2(160.0f, 50.0f);
    glm::vec2 pivot = glm::vec2(0.5f, 0.5f);

    RectTransformComponent();
    ~RectTransformComponent() override = default;

    void ApplyAnchorPreset(UIAnchorPreset preset);
    void SetAnchorPresetByName(const std::string& presetName);
    std::string GetAnchorPresetName() const;

    // Calcula el rectángulo final (x, y, width, height) en espacio de Canvas (origen arriba-izquierda o centro según convención de padre)
    // En este sistema, parentRect = (x, y, w, h) con (0,0) en esquina superior izquierda del Canvas y +Y hacia abajo
    // anchoredPosition: +X hacia la derecha, +Y hacia arriba (estilo Unity)
    glm::vec4 CalculateCanvasRect(const glm::vec4& parentRect) const;

    void OnDrawInspectorUI() override;
};

class SLIPENGINE_API UIImageComponent : public SlipComponent
{
public:
    std::string texturePath;
    SlipTexture* texture = nullptr;
    glm::vec4 color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    bool preserveAspect = false;

    UIImageComponent();
    UIImageComponent(const std::string& path);
    ~UIImageComponent() override;

    void SetTexture(const std::string& path);
    void OnInit() override;
    void OnDrawInspectorUI() override;
};

enum class UITextAlignH {
    Left = 0,
    Center = 1,
    Right = 2
};

enum class UITextAlignV {
    Top = 0,
    Middle = 1,
    Bottom = 2
};

class SLIPENGINE_API UITextComponent : public SlipComponent
{
public:
    std::string text = "Texto UI";
    std::string fontPath = "";
    float fontSize = 24.0f;
    glm::vec4 color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    UITextAlignH alignH = UITextAlignH::Center;
    UITextAlignV alignV = UITextAlignV::Middle;

    UITextComponent();
    UITextComponent(const std::string& initialText);
    ~UITextComponent() override = default;

    void SetText(const std::string& newText) { text = newText; }
    const std::string& GetText() const { return text; }

    void OnDrawInspectorUI() override;
};

class SLIPENGINE_API UIButtonComponent : public SlipComponent
{
public:
    bool interactable = true;
    glm::vec4 normalColor = glm::vec4(0.20f, 0.45f, 0.82f, 1.0f);
    glm::vec4 hoverColor = glm::vec4(0.28f, 0.56f, 0.95f, 1.0f);
    glm::vec4 pressedColor = glm::vec4(0.14f, 0.32f, 0.62f, 1.0f);
    glm::vec4 disabledColor = glm::vec4(0.40f, 0.42f, 0.45f, 0.65f);

    // Configuración de Evento OnClick en el Inspector
    std::string targetEntityName = ""; // Vacío = esta misma entidad
    std::string onClickFunction = "";  // Nombre de función en ScriptComponent

    // Estado en tiempo de ejecución
    bool isHovered = false;
    bool isPressed = false;
    bool clickedThisFrame = false;

    UIButtonComponent();
    ~UIButtonComponent() override;

    glm::vec4 GetCurrentStateColor() const;
    void TriggerClick();
    void SetLuaCallback(void* solFunctionPtr);

    void OnDrawInspectorUI() override;

private:
    struct CallbackImpl;
    std::unique_ptr<CallbackImpl> m_CallbackImpl;
};

class SLIPENGINE_API UISliderComponent : public SlipComponent
{
public:
    bool interactable = true;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float value = 0.5f;
    bool wholeNumbers = false;

    glm::vec4 backgroundColor = glm::vec4(0.16f, 0.18f, 0.22f, 1.0f);
    glm::vec4 fillColor = glm::vec4(0.22f, 0.62f, 0.96f, 1.0f);
    glm::vec4 handleColor = glm::vec4(0.95f, 0.96f, 0.98f, 1.0f);

    // Configuración de Evento OnValueChanged en el Inspector
    std::string targetEntityName = ""; // Vacío = esta misma entidad
    std::string onValueChangedFunction = "";

    bool isDragging = false;

    UISliderComponent();
    ~UISliderComponent() override;

    float GetNormalizedValue() const;
    void SetNormalizedValue(float norm, bool notify = true);
    void SetValue(float newValue, bool notify = true);
    float GetValue() const { return value; }
    void SetLuaCallback(void* solFunctionPtr);

    void OnDrawInspectorUI() override;

private:
    struct CallbackImpl;
    std::unique_ptr<CallbackImpl> m_CallbackImpl;
};

class SLIPENGINE_API UIProgressBarComponent : public SlipComponent
{
public:
    float minValue = 0.0f;
    float maxValue = 100.0f;
    float value = 75.0f;
    glm::vec4 backgroundColor = glm::vec4(0.15f, 0.17f, 0.21f, 1.0f);
    glm::vec4 fillColor = glm::vec4(0.22f, 0.78f, 0.40f, 1.0f);
    bool showPercentageText = true;

    UIProgressBarComponent();
    ~UIProgressBarComponent() override = default;

    float GetNormalizedValue() const;
    void SetValue(float newValue);
    float GetValue() const { return value; }

    void OnDrawInspectorUI() override;
};

enum class UIJoystickMode {
    Fixed = 0,
    Floating = 1,
    Invisible = 2
};

class SLIPENGINE_API UIJoystickComponent : public SlipComponent
{
public:
    std::string stickName = "Move";
    UIJoystickMode mode = UIJoystickMode::Fixed;
    float travelRadius = 90.0f;
    float activationRadius = 120.0f;
    glm::vec4 backgroundColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.28f);
    glm::vec4 knobColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.85f);
    bool interactable = true;

    glm::vec2 value = glm::vec2(0.0f);
    glm::vec2 knobOffset = glm::vec2(0.0f);
    glm::vec2 floatingCenter = glm::vec2(0.0f);
    bool floatingActive = false;
    int64_t capturedFinger = -1;

    UIJoystickComponent();
    ~UIJoystickComponent() override = default;

    void OnDrawInspectorUI() override;
};

class SLIPENGINE_API UIActionButtonComponent : public SlipComponent
{
public:
    std::string actionName = "Jump";
    bool interactable = true;
    glm::vec4 normalColor = glm::vec4(0.20f, 0.45f, 0.82f, 0.85f);
    glm::vec4 pressedColor = glm::vec4(0.14f, 0.32f, 0.62f, 1.0f);

    bool isPressed = false;
    int64_t capturedFinger = -1;

    UIActionButtonComponent();
    ~UIActionButtonComponent() override = default;

    void OnDrawInspectorUI() override;
};

// ---------------------------------------------------------
// Sistema de Partículas Híbrido (Unity Billboards + Unreal Niagara GL_POINTS)
// ---------------------------------------------------------
enum class ParticleRenderMode {
    Billboard = 0,
    PointNiagara = 1
};

enum class ParticlePointShape {
    SoftCircle = 0,
    LitSphere = 1,
    Square = 2
};

enum class ParticleEmitterShape {
    Point = 0,
    Sphere = 1,
    Cone = 2,
    Box = 3,
    Circle = 4
};

enum class ParticleColorMode {
    Fixed = 0,
    RandomBetweenTwo = 1,
    GradientOverLifetime = 2
};

enum class ParticleMaterialMode {
    Unlit = 0,
    Lit = 1,
    Emissive = 2
};

enum class ParticleBlendMode {
    AlphaBlend = 0,
    Additive = 1
};

struct ParticleGradientStop {
    float t = 0.0f; // [0.0, 1.0]
    glm::vec4 color = glm::vec4(1.0f);
};

struct ParticleBurst {
    float time = 0.0f;
    int count = 25;
    bool triggered = false;
};

struct ParticleInstance {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec4 color = glm::vec4(1.0f);
    glm::vec4 baseStartColor = glm::vec4(1.0f);
    float age = 0.0f;
    float lifetime = 1.0f;
    float startSize = 0.2f;
    float endSize = 0.05f;
    float size = 0.2f;
    float rotation = 0.0f;
    float angularVelocity = 0.0f;
    uint32_t randomSeed = 1u;
    int collisionCount = 0;
};

class SLIPENGINE_API ParticleSystemComponent : public SlipComponent
{
public:
    // --- Modo General y Simulación ---
    ParticleRenderMode renderMode = ParticleRenderMode::Billboard;
    int maxParticles = 1000;
    float duration = 5.0f;
    bool looping = true;
    bool playOnAwake = true;
    bool simulationSpaceLocal = false;
    float simulationSpeed = 1.0f;

    // --- Emisión ---
    bool emissionEnabled = true;
    float emissionRate = 30.0f; // Partículas por segundo
    std::vector<ParticleBurst> bursts;

    // --- Forma del Emisor y Fuerza/Velocidad Inicial ("Salir Arrojadas") ---
    ParticleEmitterShape emitterShape = ParticleEmitterShape::Cone;
    float shapeRadius = 0.5f;
    float coneAngle = 25.0f; // Grados
    glm::vec3 boxSize = glm::vec3(1.0f, 1.0f, 1.0f);
    bool emitFromShell = false;
    float initialSpeedMin = 3.0f;
    float initialSpeedMax = 6.0f;
    glm::vec3 initialVelocityOffset = glm::vec3(0.0f, 0.0f, 0.0f); // Impulso adicional al lanzar

    // --- Ciclo de Vida, Tamaño y Rotación ---
    float lifetimeMin = 1.0f;
    float lifetimeMax = 2.5f;
    float startSizeMin = 0.20f;
    float startSizeMax = 0.35f;
    float endSizeMultiplier = 0.25f; // Escala final relativa al tamaño inicial
    float startRotationMin = 0.0f;
    float startRotationMax = 360.0f;
    float angularVelocityMin = -45.0f;
    float angularVelocityMax = 45.0f;

    // --- Color y Gradiente sobre Ciclo de Vida ---
    ParticleColorMode colorMode = ParticleColorMode::GradientOverLifetime;
    glm::vec4 startColor = glm::vec4(1.0f, 0.85f, 0.35f, 1.0f);
    glm::vec4 startColor2 = glm::vec4(1.0f, 0.25f, 0.05f, 1.0f);
    std::vector<ParticleGradientStop> gradientStops;

    // --- Simulación Física y Colisiones (Estilo RigidBody / Bullet) ---
    bool physicsEnabled = true;
    bool useGravity = true;
    float gravityMultiplier = 1.0f;
    float mass = 0.1f;
    float linearDrag = 0.05f;
    glm::vec3 constantForce = glm::vec3(0.0f);

    bool turbulenceEnabled = false;
    float turbulenceStrength = 2.0f;
    float turbulenceFrequency = 1.5f;
    float vortexStrength = 0.0f;

    bool collisionEnabled = false;
    float particleRadius = 0.05f;
    float bounciness = 0.6f;        // Restitución / rebote [0..1]
    float collisionFriction = 0.2f; // Fricción tangencial [0..1]
    float lifetimeLossOnCollision = 0.0f;
    bool pushRigidBodies = true;    // Aplica impulso físico a btRigidBody dinámicos

    // --- Material, Iluminación y Sombras ---
    ParticleMaterialMode materialMode = ParticleMaterialMode::Emissive;
    ParticleBlendMode blendMode = ParticleBlendMode::Additive;

    std::string texturePath = "";
    SlipTexture* texture = nullptr;
    int hFrames = 1;
    int vFrames = 1;
    bool animateSpriteSheet = false;

    ParticlePointShape pointShape = ParticlePointShape::LitSphere;
    float pointBaseScale = 1.0f;

    glm::vec3 emissiveColor = glm::vec3(1.0f, 0.6f, 0.15f);
    float emissiveIntensity = 2.5f;
    float roughness = 0.45f;
    float metallic = 0.0f;

    bool receiveLights = true;
    bool receiveShadows = true;
    bool castShadows = false;
    float shadowAlphaCutoff = 0.2f;

    // --- Emisión de Luz Real (Dynamic Light Emission) ---
    bool lightEmissionEnabled = false;
    glm::vec3 lightColor = glm::vec3(1.0f, 0.75f, 0.3f);
    float lightIntensity = 2.5f;
    float lightRange = 6.0f;
    int maxLights = 1;
    bool useParticleColorForLight = true;

    ParticleSystemComponent();
    ParticleSystemComponent(ParticleRenderMode mode);
    ~ParticleSystemComponent() override;

    // Controles de reproducción
    void Play();
    void Pause();
    void Stop();
    void Restart();
    void Emit(int count);

    bool IsPlaying() const { return m_IsPlaying && !m_IsPaused; }
    bool IsPaused() const { return m_IsPaused; }
    float GetSystemTime() const { return m_SystemTime; }
    int GetActiveParticleCount() const { return static_cast<int>(m_Particles.size()); }
    const std::vector<ParticleInstance>& GetParticles() const { return m_Particles; }
    std::vector<ParticleInstance>& GetParticles() { return m_Particles; }

    void SetTexture(const std::string& path);
    glm::vec4 GetUVRectForNormalizedAge(float normAge, uint32_t seed) const;
    glm::vec4 EvaluateGradient(float normalizedTime) const;
    glm::vec4 EvaluateParticleColor(const ParticleInstance& p) const;
    void GetWorldBounds(const glm::mat4& emitterWorld, glm::vec3& outMin, glm::vec3& outMax) const;

    void ApplyPreset(const std::string& presetName);

    void Simulate(float deltaTime);

    void SerializeTo(std::ostream& out) const;
    void DeserializeKey(const std::string& key, const std::string& value);

    void OnInit() override;
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDrawInspectorUI() override;

private:
    void SpawnSingleParticle(const glm::mat4& emitterWorldMat, const glm::mat3& emitterRotMat);
    uint32_t NextRandom();
    float RandomRange(float minVal, float maxVal);

    std::vector<ParticleInstance> m_Particles;
    bool m_IsPlaying = true;
    bool m_IsPaused = false;
    float m_SystemTime = 0.0f;
    float m_EmissionAccumulator = 0.0f;
    uint32_t m_RngState = 0x1337BEEFu;
};

// ---------------------------------------------------------
// VehicleComponent (btRaycastVehicle de Bullet Physics)
// ---------------------------------------------------------
struct WheelConfig {
    glm::vec3 connectionPoint = glm::vec3(0.0f); // Offset local respecto al chasis
    glm::vec3 wheelDirection = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 wheelAxle = glm::vec3(-1.0f, 0.0f, 0.0f);
    float suspensionRestLength = 0.5f;
    float wheelRadius = 0.35f;
    bool isFrontWheel = false;
};

class SLIPENGINE_API VehicleComponent : public SlipComponent
{
public:
    // Suspensión y física de las ruedas
    float suspensionStiffness = 20.0f;
    float suspensionDamping = 2.3f;
    float suspensionCompression = 4.4f;
    float maxSuspensionTravelCm = 500.0f;
    float frictionSlip = 10.5f;
    float maxSuspensionForce = 35000.0f;
    float rollInfluence = 0.10f;
    bool showDebugGizmos = true;
    bool enableAntiRollBar = true;
    float antiRollStrength = 5500.0f;

    // Dimensiones para generación de 4 ruedas por defecto
    float chassisWidth = 1.8f;
    float chassisHeight = 0.4f;
    float chassisLength = 3.6f;
    float defaultWheelRadius = 0.35f;

    std::vector<WheelConfig> wheels;

    VehicleComponent();
    ~VehicleComponent() override;

    void OnInit() override;
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDrawInspectorUI() override;

    void InitVehicle();
    void CleanVehicle();

    // Controles de aceleración, frenado y dirección
    void SetEngineForce(float force, int wheelIndex = -1);
    void SetBrake(float brake, int wheelIndex = -1);
    void SetHandBrake(float brake);
    void SetSteering(float steering, int wheelIndex = -1);
    void SetTireFriction(float frontFriction, float rearFriction);
    void SetWheelFriction(int wheelIndex, float friction);
    float GetCurrentSpeedKmHour() const;

    int GetNumWheels() const;
    glm::vec3 GetWheelPosition(int wheelIndex) const;
    glm::vec3 GetWheelRotation(int wheelIndex) const; // En grados de Euler
    bool IsWheelOnGround(int wheelIndex) const;

    void SetupDefaultCarWheels();
    void ResetSuspension();

    void SetShowDebugGizmos(bool show) { showDebugGizmos = show; }
    bool GetShowDebugGizmos() const { return showDebugGizmos; }

    btRaycastVehicle* GetBulletVehicle() const { return m_Vehicle; }

private:
    btRaycastVehicle::btVehicleTuning m_Tuning;
    btVehicleRaycaster* m_VehicleRaycaster = nullptr;
    btRaycastVehicle* m_Vehicle = nullptr;
};

// Clases de compatibilidad histórica
class MaterialComponent : public SlipComponent {
public:
    char matPath[192] = "";
    SlipMaterial* mat = nullptr;
    void bind();
};
class CollisionComponent : public ColliderComponent {
public:
    char colPath[192] = "";
    SlipCollision* collision = nullptr;
    void init();
    void CreateOrUpdateShape() override {}
};

#include "SlipEngine/scene/SlipSkyComponent.h"

#endif // !SLIP_COMPONENT_H
