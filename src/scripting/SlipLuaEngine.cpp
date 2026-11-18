#include "SlipEngine/scripting/SlipLuaEngine.h"
#include "SlipEngine/core/SlipConsole.h"
#include "SlipEngine/core/SlipGlobals.h"
#include "SlipEngine/platform/SlipInput.h"
#include "SlipEngine/platform/SlipInputMap.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/scene/SlipPrefab.h"
#include "SlipEngine/physics/SlipPhysics.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/tools/SlipDebug.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <iostream>

SlipLuaEngine& SlipLuaEngine::Get() {
    if (!s_Instance) {
        static SlipLuaEngine instance;
        s_Instance = &instance;
    }
    return *s_Instance;
}

SlipLuaEngine::SlipLuaEngine() {
    Init();
}

SlipLuaEngine::~SlipLuaEngine() {
    Shutdown();
}

void SlipLuaEngine::Init() {
    if (m_Initialized) return;

    try {
        m_Lua.open_libraries(
            sol::lib::base,
            sol::lib::package,
            sol::lib::string,
            sol::lib::table,
            sol::lib::math,
            sol::lib::debug,
            sol::lib::coroutine
        );

        RegisterEngineBindings();
        m_Initialized = true;
    } catch (const std::exception& e) {
        ReportError(std::string("Error al inicializar SlipLuaEngine: ") + e.what());
    }
}

void SlipLuaEngine::Shutdown() {
    if (!m_Initialized) return;
    m_Lua = sol::state();
    m_Initialized = false;
}

void SlipLuaEngine::ResetState() {
    Shutdown();
    Init();
    if (!m_AssetsDirectory.empty()) {
        SetAssetsDirectory(m_AssetsDirectory);
    }
}

void SlipLuaEngine::SetAssetsDirectory(const std::string& assetsPath) {
    m_AssetsDirectory = assetsPath;
    if (m_AssetsDirectory.empty()) return;

    std::string normalized = m_AssetsDirectory;
    for (char& c : normalized) {
        if (c == '\\') c = '/';
    }
    if (normalized.back() != '/') {
        normalized += '/';
    }

    std::string newPackagePath = normalized + "?.lua;" + normalized + "?/init.lua;" + "./assets/?.lua;";
    try {
        sol::table package = m_Lua["package"];
        std::string currentPath = package["path"];
        package["path"] = newPackagePath + currentPath;
    } catch (const std::exception& e) {
        ReportError(std::string("Error configurando package.path: ") + e.what());
    }
}

void SlipLuaEngine::ReportError(const std::string& errorMessage) {
    SlipConsole::LogError("[Lua Error] " + errorMessage);
    std::cerr << "[Lua Error] " << errorMessage << std::endl;
}

bool SlipLuaEngine::ExecuteFile(const std::string& filePath) {
    if (!SlipFileSystem::Exists(filePath)) {
        ReportError("Archivo Lua no encontrado: " + filePath);
        return false;
    }

    try {
        auto result = m_Lua.safe_script_file(filePath);
        if (!result.valid()) {
            sol::error err = result;
            ReportError(err.what());
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        ReportError(e.what());
        return false;
    }
}

bool SlipLuaEngine::ExecuteString(const std::string& scriptCode, const std::string& chunkName) {
    try {
        auto result = m_Lua.safe_script(scriptCode, chunkName);
        if (!result.valid()) {
            sol::error err = result;
            ReportError(err.what());
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        ReportError(e.what());
        return false;
    }
}

void SlipLuaEngine::RegisterEngineBindings() {
    // -------------------------------------------------------------
    // Tipos Matemáticos (glm::vec2, glm::vec3)
    // -------------------------------------------------------------
    m_Lua.new_usertype<glm::vec2>("Vec2",
        sol::call_constructor, sol::constructors<glm::vec2(), glm::vec2(float, float)>(),
        "x", &glm::vec2::x,
        "y", &glm::vec2::y,
        sol::meta_function::addition, [](const glm::vec2& a, const glm::vec2& b) { return a + b; },
        sol::meta_function::subtraction, [](const glm::vec2& a, const glm::vec2& b) { return a - b; },
        sol::meta_function::multiplication, [](const glm::vec2& a, float val) { return a * val; },
        sol::meta_function::division, [](const glm::vec2& a, float val) { return a / val; },
        "Length", [](const glm::vec2& v) { return glm::length(v); },
        "Normalize", [](const glm::vec2& v) { return glm::length(v) > 0.0001f ? glm::normalize(v) : glm::vec2(0.0f); },
        "Dot", [](const glm::vec2& a, const glm::vec2& b) { return glm::dot(a, b); }
    );
    m_Lua["Vec2"]["new"] = sol::overload(
        []() { return glm::vec2(0.0f); },
        [](float x, float y) { return glm::vec2(x, y); }
    );

    m_Lua.new_usertype<glm::vec3>("Vec3",
        sol::call_constructor, sol::constructors<glm::vec3(), glm::vec3(float, float, float)>(),
        "x", &glm::vec3::x,
        "y", &glm::vec3::y,
        "z", &glm::vec3::z,
        sol::meta_function::addition, [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
        sol::meta_function::subtraction, [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
        sol::meta_function::multiplication, [](const glm::vec3& a, float val) { return a * val; },
        sol::meta_function::division, [](const glm::vec3& a, float val) { return a / val; },
        "Length", [](const glm::vec3& v) { return glm::length(v); },
        "Normalize", [](const glm::vec3& v) { return glm::length(v) > 0.0001f ? glm::normalize(v) : glm::vec3(0.0f); },
        "Dot", [](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); },
        "Cross", [](const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); }
    );
    m_Lua["Vec3"]["new"] = sol::overload(
        []() { return glm::vec3(0.0f); },
        [](float x, float y, float z) { return glm::vec3(x, y, z); }
    );

    m_Lua.new_usertype<glm::vec4>("Vec4",
        sol::call_constructor, sol::constructors<glm::vec4(), glm::vec4(float, float, float, float)>(),
        "x", &glm::vec4::x,
        "y", &glm::vec4::y,
        "z", &glm::vec4::z,
        "w", &glm::vec4::w,
        "r", sol::property([](const glm::vec4& v) { return v.r; }, [](glm::vec4& v, float val) { v.r = val; }),
        "g", sol::property([](const glm::vec4& v) { return v.g; }, [](glm::vec4& v, float val) { v.g = val; }),
        "b", sol::property([](const glm::vec4& v) { return v.b; }, [](glm::vec4& v, float val) { v.b = val; }),
        "a", sol::property([](const glm::vec4& v) { return v.a; }, [](glm::vec4& v, float val) { v.a = val; }),
        sol::meta_function::addition, [](const glm::vec4& a, const glm::vec4& b) { return a + b; },
        sol::meta_function::subtraction, [](const glm::vec4& a, const glm::vec4& b) { return a - b; },
        sol::meta_function::multiplication, [](const glm::vec4& a, float val) { return a * val; },
        sol::meta_function::division, [](const glm::vec4& a, float val) { return a / val; }
    );
    m_Lua["Vec4"]["new"] = sol::overload(
        []() { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); },
        [](float x, float y, float z, float w) { return glm::vec4(x, y, z, w); }
    );

    m_Lua.new_usertype<SkinnedMeshComponent>("SkinnedMesh",
        "modelPath", &SkinnedMeshComponent::modelPath,
        "materialPath", &SkinnedMeshComponent::materialPath,
        "castShadows", &SkinnedMeshComponent::castShadows,
        "SetModel", &SkinnedMeshComponent::SetModel,
        "SetMaterial", &SkinnedMeshComponent::SetMaterial,
        "BuildBoneHierarchy", [](SkinnedMeshComponent& sm) {
            sm.BuildBoneHierarchy(&SlipLevel::Get());
        }
    );

    m_Lua.new_usertype<AnimatorComponent>("Animator",
        "currentClipIndex", &AnimatorComponent::currentClipIndex,
        "currentTime", &AnimatorComponent::currentTime,
        "playbackSpeed", &AnimatorComponent::playbackSpeed,
        "isPlaying", &AnimatorComponent::isPlaying,
        "isLooping", &AnimatorComponent::isLooping,
        "Play", sol::overload(
            [](AnimatorComponent& a, const std::string& name) { a.Play(name, 1.0f, true); },
            [](AnimatorComponent& a, const std::string& name, float speed) { a.Play(name, speed, true); },
            [](AnimatorComponent& a, const std::string& name, float speed, bool loop) { a.Play(name, speed, loop); },
            [](AnimatorComponent& a, int index) { a.Play(index, 1.0f, true); },
            [](AnimatorComponent& a, int index, float speed) { a.Play(index, speed, true); },
            [](AnimatorComponent& a, int index, float speed, bool loop) { a.Play(index, speed, loop); }
        ),
        "Pause", &AnimatorComponent::Pause,
        "Resume", &AnimatorComponent::Resume,
        "Stop", &AnimatorComponent::Stop,
        "SetTime", &AnimatorComponent::SetTime,
        "GetCurrentTime", &AnimatorComponent::GetCurrentTime,
        "GetCurrentDuration", &AnimatorComponent::GetCurrentDuration,
        "GetClipCount", &AnimatorComponent::GetClipCount,
        "controllerPath", &AnimatorComponent::controllerPath,
        "SetController", &AnimatorComponent::SetController,
        "SetFloat", &AnimatorComponent::SetFloat,
        "SetBool", &AnimatorComponent::SetBool,
        "SetInt", &AnimatorComponent::SetInt,
        "SetTrigger", &AnimatorComponent::SetTrigger,
        "GetFloat", &AnimatorComponent::GetFloat,
        "GetBool", &AnimatorComponent::GetBool,
        "GetInt", &AnimatorComponent::GetInt
    );

    m_Lua.new_usertype<BoneComponent>("Bone",
        "boneIndex", &BoneComponent::boneIndex,
        "boneName", &BoneComponent::boneName
    );

    // -------------------------------------------------------------
    // CameraComponent & Efectos de Camara
    // -------------------------------------------------------------
    m_Lua.new_usertype<CameraComponent>("CameraComponent",
        "fov", sol::property(&CameraComponent::GetFOV, &CameraComponent::SetFOV),
        "GetFOV", &CameraComponent::GetFOV,
        "SetFOV", &CameraComponent::SetFOV,
        "letterbox", sol::property(&CameraComponent::GetLetterbox, &CameraComponent::SetLetterbox),
        "pillarbox", sol::property(&CameraComponent::GetPillarbox, &CameraComponent::SetPillarbox),
        "SetLetterbox", &CameraComponent::SetLetterbox,
        "SetPillarbox", &CameraComponent::SetPillarbox,
        "SetBlackBars", &CameraComponent::SetBlackBars,
        "Shake", sol::overload(
            [](CameraComponent& cam, float intensity, float duration) { cam.Shake(intensity, duration, 25.0f); },
            [](CameraComponent& cam, float intensity, float duration, float freq) { cam.Shake(intensity, duration, freq); }
        ),
        "StopShake", &CameraComponent::StopShake,
        "IsShaking", &CameraComponent::IsShaking,
        "nearClip", &CameraComponent::nearClip,
        "farClip", &CameraComponent::farClip,
        "orthoSize", &CameraComponent::orthoSize,
        "isPrimary", &CameraComponent::isPrimary,
        "GetPosition", &CameraComponent::GetPosition,
        "GetForward", &CameraComponent::GetForward,
        "GetUp", &CameraComponent::GetUp,
        "GetRight", &CameraComponent::GetRight
    );

    // -------------------------------------------------------------
    // CinematicCameraComponent (Cámara Cinemática NFS / Forza)
    // -------------------------------------------------------------
    m_Lua.new_usertype<CinematicCameraComponent>("CinematicCamera",
        "targetEntityName", &CinematicCameraComponent::targetEntityName,
        "targetDistance", &CinematicCameraComponent::targetDistance,
        "targetHeight", &CinematicCameraComponent::targetHeight,
        "targetPitch", &CinematicCameraComponent::targetPitch,
        "pivotYOffset", &CinematicCameraComponent::pivotYOffset,
        "positionLag", &CinematicCameraComponent::positionLag,
        "rotationLag", &CinematicCameraComponent::rotationLag,
        "pitchLag", &CinematicCameraComponent::pitchLag,
        "driftCompensation", &CinematicCameraComponent::driftCompensation,
        "maxDriftYawOffset", &CinematicCameraComponent::maxDriftYawOffset,
        "enableDynamicFOV", &CinematicCameraComponent::enableDynamicFOV,
        "baseFOV", &CinematicCameraComponent::baseFOV,
        "boostFOV", &CinematicCameraComponent::boostFOV,
        "fovLag", &CinematicCameraComponent::fovLag,
        "maxSpeedForFOV", &CinematicCameraComponent::maxSpeedForFOV,
        "enableCollisionAvoidance", &CinematicCameraComponent::enableCollisionAvoidance,
        "enableMouseOrbit", &CinematicCameraComponent::enableMouseOrbit,
        "mouseSensitivity", &CinematicCameraComponent::mouseSensitivity,
        "autoCenterDelay", &CinematicCameraComponent::autoCenterDelay,
        "autoCenterSpeed", &CinematicCameraComponent::autoCenterSpeed,
        "SetTarget", &CinematicCameraComponent::SetTarget,
        "SetTargetName", &CinematicCameraComponent::SetTargetName,
        "GetTarget", &CinematicCameraComponent::GetTarget,
        "ResetImmediate", &CinematicCameraComponent::ResetImmediate
    );

    // -------------------------------------------------------------
    // KeyframeAnimationComponent (Cinematicas / Secuencias de Propiedades)
    // -------------------------------------------------------------
    m_Lua.new_usertype<KeyframeAnimationComponent>("KeyframeAnimation",
        "Play", &KeyframeAnimationComponent::Play,
        "Pause", &KeyframeAnimationComponent::Pause,
        "Resume", &KeyframeAnimationComponent::Resume,
        "Stop", &KeyframeAnimationComponent::Stop,
        "SetTime", &KeyframeAnimationComponent::SetTime,
        "LoadClip", &KeyframeAnimationComponent::LoadClip,
        "currentTime", &KeyframeAnimationComponent::currentTime,
        "playbackSpeed", &KeyframeAnimationComponent::playbackSpeed,
        "loop", &KeyframeAnimationComponent::loop,
        "isPlaying", &KeyframeAnimationComponent::isPlaying,
        "playOnStart", &KeyframeAnimationComponent::playOnStart,
        "clipPath", &KeyframeAnimationComponent::clipPath
    );

    // -------------------------------------------------------------
    // SpriteRendererComponent & Sistema 2D & UI Estilo Unity3D
    // -------------------------------------------------------------
    m_Lua.new_usertype<SpriteRendererComponent>("SpriteRenderer",
        "texturePath", &SpriteRendererComponent::texturePath,
        "color", &SpriteRendererComponent::color,
        "flipX", &SpriteRendererComponent::flipX,
        "flipY", &SpriteRendererComponent::flipY,
        "sortingOrder", &SpriteRendererComponent::sortingOrder,
        "orderInLayer", &SpriteRendererComponent::orderInLayer,
        "sortingLayerName", &SpriteRendererComponent::sortingLayerName,
        "pixelsPerUnit", &SpriteRendererComponent::pixelsPerUnit,
        "hFrames", &SpriteRendererComponent::hFrames,
        "vFrames", &SpriteRendererComponent::vFrames,
        "currentFrame", &SpriteRendererComponent::currentFrame,
        "isLit", &SpriteRendererComponent::isLit,
        "normalMapPath", &SpriteRendererComponent::normalMapPath,
        "SetTexture", &SpriteRendererComponent::SetTexture,
        "SetNormalMap", &SpriteRendererComponent::SetNormalMap,
        "GetSortingLayerIndex", &SpriteRendererComponent::GetSortingLayerIndex
    );

    m_Lua.new_usertype<SpriteAnimatorComponent>("SpriteAnimator",
        "currentFrame", &SpriteAnimatorComponent::currentFrame,
        "isPlaying", &SpriteAnimatorComponent::isPlaying,
        "playOnStart", &SpriteAnimatorComponent::playOnStart,
        "defaultClip", &SpriteAnimatorComponent::defaultClip,
        "Play", &SpriteAnimatorComponent::Play,
        "Stop", &SpriteAnimatorComponent::Stop,
        "Pause", &SpriteAnimatorComponent::Pause,
        "Resume", &SpriteAnimatorComponent::Resume,
        "AddClip", &SpriteAnimatorComponent::AddClip,
        "GetCurrentClipName", &SpriteAnimatorComponent::GetCurrentClipName
    );

    m_Lua.new_usertype<BoxCollider2DComponent>("BoxCollider2D",
        "size", &BoxCollider2DComponent::size,
        "offset", &BoxCollider2DComponent::offset,
        "isTrigger", &BoxCollider2DComponent::isTrigger,
        "density", &BoxCollider2DComponent::density,
        "friction", &BoxCollider2DComponent::friction,
        "restitution", &BoxCollider2DComponent::restitution
    );

    m_Lua.new_usertype<CircleCollider2DComponent>("CircleCollider2D",
        "radius", &CircleCollider2DComponent::radius,
        "offset", &CircleCollider2DComponent::offset,
        "isTrigger", &CircleCollider2DComponent::isTrigger,
        "density", &CircleCollider2DComponent::density,
        "friction", &CircleCollider2DComponent::friction,
        "restitution", &CircleCollider2DComponent::restitution
    );

    m_Lua.new_usertype<RigidBody2DComponent>("RigidBody2D",
        "mass", &RigidBody2DComponent::mass,
        "gravityScale", &RigidBody2DComponent::gravityScale,
        "freezeRotation", &RigidBody2DComponent::freezeRotation,
        "linearVelocity", &RigidBody2DComponent::linearVelocity,
        "angularVelocity", &RigidBody2DComponent::angularVelocity,
        "linearDamping", &RigidBody2DComponent::linearDamping,
        "angularDamping", &RigidBody2DComponent::angularDamping,
        "ApplyForce", &RigidBody2DComponent::ApplyForce,
        "ApplyImpulse", &RigidBody2DComponent::ApplyImpulse,
        "SetLinearVelocity", &RigidBody2DComponent::SetLinearVelocity,
        "GetLinearVelocity", &RigidBody2DComponent::GetLinearVelocity,
        "SetAngularVelocity", &RigidBody2DComponent::SetAngularVelocity,
        "GetAngularVelocity", &RigidBody2DComponent::GetAngularVelocity,
        "SetGravityScale", &RigidBody2DComponent::SetGravityScale,
        "GetGravityScale", &RigidBody2DComponent::GetGravityScale
    );

    m_Lua.new_usertype<Light2DComponent>("Light2D",
        "color", &Light2DComponent::color,
        "intensity", &Light2DComponent::intensity,
        "radius", &Light2DComponent::radius,
        "innerRadius", &Light2DComponent::innerRadius,
        "spotAngle", &Light2DComponent::spotAngle,
        "castShadows", &Light2DComponent::castShadows,
        "shadowIntensity", &Light2DComponent::shadowIntensity
    );

    m_Lua.new_usertype<ShadowCaster2DComponent>("ShadowCaster2D",
        "size", &ShadowCaster2DComponent::size,
        "radius", &ShadowCaster2DComponent::radius,
        "offset", &ShadowCaster2DComponent::offset,
        "castShadows", &ShadowCaster2DComponent::castShadows
    );

    m_Lua.new_usertype<TileGridComponent>("TileGrid",
        "cellSize", &TileGridComponent::cellSize,
        "tileSheetPath", &TileGridComponent::tileSheetPath,
        "sheetHFrames", &TileGridComponent::sheetHFrames,
        "sheetVFrames", &TileGridComponent::sheetVFrames,
        "sortingLayerName", &TileGridComponent::sortingLayerName,
        "orderInLayer", &TileGridComponent::orderInLayer,
        "SetTileSheet", &TileGridComponent::SetTileSheet,
        "SetTile", &TileGridComponent::SetTile,
        "RemoveTile", &TileGridComponent::RemoveTile,
        "GetTile", &TileGridComponent::GetTile,
        "HasTile", &TileGridComponent::HasTile,
        "Clear", &TileGridComponent::Clear,
        "CellToWorld", &TileGridComponent::CellToWorld,
        "WorldToCell", &TileGridComponent::WorldToCell
    );

    m_Lua.new_usertype<CanvasComponent>("Canvas",
        "referenceResolution", &CanvasComponent::referenceResolution,
        "scaleWithScreenSize", &CanvasComponent::scaleWithScreenSize,
        "sortingOrder", &CanvasComponent::sortingOrder
    );

    m_Lua.new_usertype<RectTransformComponent>("RectTransform",
        "anchorMin", &RectTransformComponent::anchorMin,
        "anchorMax", &RectTransformComponent::anchorMax,
        "anchoredPosition", &RectTransformComponent::anchoredPosition,
        "size", &RectTransformComponent::size,
        "pivot", &RectTransformComponent::pivot,
        "SetAnchorPreset", &RectTransformComponent::SetAnchorPresetByName,
        "GetAnchorPreset", &RectTransformComponent::GetAnchorPresetName
    );

    m_Lua.new_usertype<UIImageComponent>("UIImage",
        "texturePath", &UIImageComponent::texturePath,
        "color", &UIImageComponent::color,
        "preserveAspect", &UIImageComponent::preserveAspect,
        "SetTexture", &UIImageComponent::SetTexture
    );

    m_Lua.new_usertype<UITextComponent>("UIText",
        "text", &UITextComponent::text,
        "fontPath", &UITextComponent::fontPath,
        "fontSize", &UITextComponent::fontSize,
        "color", &UITextComponent::color,
        "SetText", &UITextComponent::SetText,
        "GetText", &UITextComponent::GetText
    );

    m_Lua.new_usertype<UIButtonComponent>("UIButton",
        "interactable", &UIButtonComponent::interactable,
        "normalColor", &UIButtonComponent::normalColor,
        "hoverColor", &UIButtonComponent::hoverColor,
        "pressedColor", &UIButtonComponent::pressedColor,
        "disabledColor", &UIButtonComponent::disabledColor,
        "targetEntityName", &UIButtonComponent::targetEntityName,
        "onClickFunction", &UIButtonComponent::onClickFunction,
        "isHovered", sol::readonly(&UIButtonComponent::isHovered),
        "isPressed", sol::readonly(&UIButtonComponent::isPressed),
        "TriggerClick", &UIButtonComponent::TriggerClick,
        "OnClick", [](UIButtonComponent& btn, sol::protected_function fn) {
            btn.SetLuaCallback(&fn);
        }
    );

    m_Lua.new_usertype<UISliderComponent>("UISlider",
        "interactable", &UISliderComponent::interactable,
        "minValue", &UISliderComponent::minValue,
        "maxValue", &UISliderComponent::maxValue,
        "value", sol::property(&UISliderComponent::GetValue, [](UISliderComponent& s, float v) { s.SetValue(v, true); }),
        "wholeNumbers", &UISliderComponent::wholeNumbers,
        "backgroundColor", &UISliderComponent::backgroundColor,
        "fillColor", &UISliderComponent::fillColor,
        "handleColor", &UISliderComponent::handleColor,
        "targetEntityName", &UISliderComponent::targetEntityName,
        "onValueChangedFunction", &UISliderComponent::onValueChangedFunction,
        "SetValue", [](UISliderComponent& s, float v) { s.SetValue(v, true); },
        "GetValue", &UISliderComponent::GetValue,
        "GetNormalizedValue", &UISliderComponent::GetNormalizedValue,
        "OnValueChanged", [](UISliderComponent& s, sol::protected_function fn) {
            s.SetLuaCallback(&fn);
        }
    );

    m_Lua.new_usertype<UIProgressBarComponent>("UIProgressBar",
        "minValue", &UIProgressBarComponent::minValue,
        "maxValue", &UIProgressBarComponent::maxValue,
        "value", sol::property(&UIProgressBarComponent::GetValue, &UIProgressBarComponent::SetValue),
        "backgroundColor", &UIProgressBarComponent::backgroundColor,
        "fillColor", &UIProgressBarComponent::fillColor,
        "showPercentageText", &UIProgressBarComponent::showPercentageText,
        "SetValue", &UIProgressBarComponent::SetValue,
        "GetValue", &UIProgressBarComponent::GetValue,
        "GetNormalizedValue", &UIProgressBarComponent::GetNormalizedValue
    );

    m_Lua.new_enum("UIJoystickMode",
        "Fixed", UIJoystickMode::Fixed,
        "Floating", UIJoystickMode::Floating,
        "Invisible", UIJoystickMode::Invisible
    );

    m_Lua.new_usertype<UIJoystickComponent>("UIJoystick",
        "stickName", &UIJoystickComponent::stickName,
        "mode", &UIJoystickComponent::mode,
        "travelRadius", &UIJoystickComponent::travelRadius,
        "activationRadius", &UIJoystickComponent::activationRadius,
        "backgroundColor", &UIJoystickComponent::backgroundColor,
        "knobColor", &UIJoystickComponent::knobColor,
        "interactable", &UIJoystickComponent::interactable,
        "value", sol::readonly(&UIJoystickComponent::value)
    );

    m_Lua.new_usertype<UIActionButtonComponent>("UIActionButton",
        "actionName", &UIActionButtonComponent::actionName,
        "interactable", &UIActionButtonComponent::interactable,
        "normalColor", &UIActionButtonComponent::normalColor,
        "pressedColor", &UIActionButtonComponent::pressedColor,
        "isPressed", sol::readonly(&UIActionButtonComponent::isPressed)
    );

    // -------------------------------------------------------------
    // TransformComponent
    // -------------------------------------------------------------
    m_Lua.new_usertype<TransformComponent>("TransformComponent",
        "position", &TransformComponent::position,
        "rotation", &TransformComponent::rotation,
        "scale", &TransformComponent::scale,
        "Translate", sol::overload(
            static_cast<void(TransformComponent::*)(float, float, float)>(&TransformComponent::Translate),
            static_cast<void(TransformComponent::*)(const glm::vec3&)>(&TransformComponent::Translate)
        ),
        "Rotate", sol::overload(
            static_cast<void(TransformComponent::*)(float, float, float)>(&TransformComponent::Rotate),
            static_cast<void(TransformComponent::*)(const glm::vec3&)>(&TransformComponent::Rotate)
        ),
        "GetWorldMatrix", &TransformComponent::GetWorldMatrix
    );
    m_Lua["Transform"] = m_Lua["TransformComponent"];

    // -------------------------------------------------------------
    // RigidBodyComponent
    // -------------------------------------------------------------
    m_Lua.new_usertype<RigidBodyComponent>("RigidBody",
        "mass", &RigidBodyComponent::mass,
        "linearDrag", &RigidBodyComponent::linearDrag,
        "angularDrag", &RigidBodyComponent::angularDrag,
        "useGravity", &RigidBodyComponent::useGravity,
        "isKinematic", &RigidBodyComponent::isKinematic,
        "friction", &RigidBodyComponent::friction,
        "rollingFriction", &RigidBodyComponent::rollingFriction,
        "restitution", &RigidBodyComponent::restitution,
        "SetFriction", &RigidBodyComponent::SetFriction,
        "GetFriction", &RigidBodyComponent::GetFriction,
        "SetRollingFriction", &RigidBodyComponent::SetRollingFriction,
        "GetRollingFriction", &RigidBodyComponent::GetRollingFriction,
        "SetRestitution", &RigidBodyComponent::SetRestitution,
        "GetRestitution", &RigidBodyComponent::GetRestitution,
        "AddForce", sol::overload(
            [](RigidBodyComponent& rb, const glm::vec3& force) { rb.AddForce(force); },
            [](RigidBodyComponent& rb, float x, float y, float z) { rb.AddForce(glm::vec3(x, y, z)); }
        ),
        "AddImpulse", sol::overload(
            [](RigidBodyComponent& rb, const glm::vec3& impulse) { rb.AddImpulse(impulse); },
            [](RigidBodyComponent& rb, float x, float y, float z) { rb.AddImpulse(glm::vec3(x, y, z)); }
        ),
        "AddTorque", sol::overload(
            [](RigidBodyComponent& rb, const glm::vec3& torque) { rb.AddTorque(torque); },
            [](RigidBodyComponent& rb, float x, float y, float z) { rb.AddTorque(glm::vec3(x, y, z)); }
        ),
        "SetLinearVelocity", &RigidBodyComponent::SetLinearVelocity,
        "GetLinearVelocity", &RigidBodyComponent::GetLinearVelocity,
        "SetAngularVelocity", &RigidBodyComponent::SetAngularVelocity,
        "GetAngularVelocity", &RigidBodyComponent::GetAngularVelocity,
        "SetRotation", sol::overload(
            [](RigidBodyComponent& rb, const glm::vec3& r) { rb.SetRotation(r); },
            [](RigidBodyComponent& rb, float x, float y, float z) { rb.SetRotation(glm::vec3(x, y, z)); }
        ),
        "SetPosition", sol::overload(
            [](RigidBodyComponent& rb, const glm::vec3& p) { rb.SetPosition(p); },
            [](RigidBodyComponent& rb, float x, float y, float z) { rb.SetPosition(glm::vec3(x, y, z)); }
        )
    );

    // -------------------------------------------------------------
    // VehicleComponent (Bullet Raycast Vehicle)
    // -------------------------------------------------------------
    m_Lua.new_usertype<VehicleComponent>("Vehicle",
        "suspensionStiffness", &VehicleComponent::suspensionStiffness,
        "suspensionDamping", &VehicleComponent::suspensionDamping,
        "suspensionCompression", &VehicleComponent::suspensionCompression,
        "maxSuspensionTravelCm", &VehicleComponent::maxSuspensionTravelCm,
        "frictionSlip", &VehicleComponent::frictionSlip,
        "maxSuspensionForce", &VehicleComponent::maxSuspensionForce,
        "rollInfluence", &VehicleComponent::rollInfluence,
        "showDebugGizmos", &VehicleComponent::showDebugGizmos,
        "enableAntiRollBar", &VehicleComponent::enableAntiRollBar,
        "antiRollStrength", &VehicleComponent::antiRollStrength,
        "ResetSuspension", &VehicleComponent::ResetSuspension,
        "SetEngineForce", [](VehicleComponent& v, float force, sol::optional<int> wheel) {
            v.SetEngineForce(force, wheel.value_or(-1));
        },
        "SetBrake", [](VehicleComponent& v, float brake, sol::optional<int> wheel) {
            v.SetBrake(brake, wheel.value_or(-1));
        },
        "SetHandBrake", &VehicleComponent::SetHandBrake,
        "SetTireFriction", &VehicleComponent::SetTireFriction,
        "SetWheelFriction", &VehicleComponent::SetWheelFriction,
        "SetSteering", [](VehicleComponent& v, float steering, sol::optional<int> wheel) {
            v.SetSteering(steering, wheel.value_or(-1));
        },
        "GetCurrentSpeedKmHour", &VehicleComponent::GetCurrentSpeedKmHour,
        "GetSpeed", &VehicleComponent::GetCurrentSpeedKmHour,
        "GetNumWheels", &VehicleComponent::GetNumWheels,
        "GetWheelPosition", &VehicleComponent::GetWheelPosition,
        "GetWheelRotation", &VehicleComponent::GetWheelRotation,
        "IsWheelOnGround", &VehicleComponent::IsWheelOnGround,
        "SetupDefaultCarWheels", &VehicleComponent::SetupDefaultCarWheels
    );

    // -------------------------------------------------------------
    // ParticleSystemComponent (Híbrido Unity Billboards / Unreal Niagara GL_POINTS)
    // -------------------------------------------------------------
    m_Lua.new_usertype<ParticleSystemComponent>("ParticleSystem",
        "maxParticles", &ParticleSystemComponent::maxParticles,
        "emissionEnabled", &ParticleSystemComponent::emissionEnabled,
        "emissionRate", &ParticleSystemComponent::emissionRate,
        "duration", &ParticleSystemComponent::duration,
        "looping", &ParticleSystemComponent::looping,
        "playOnAwake", &ParticleSystemComponent::playOnAwake,
        "simulationSpaceLocal", &ParticleSystemComponent::simulationSpaceLocal,
        "simulationSpeed", &ParticleSystemComponent::simulationSpeed,
        "lifetimeMin", &ParticleSystemComponent::lifetimeMin,
        "lifetimeMax", &ParticleSystemComponent::lifetimeMax,
        "startSizeMin", &ParticleSystemComponent::startSizeMin,
        "startSizeMax", &ParticleSystemComponent::startSizeMax,
        "endSizeMultiplier", &ParticleSystemComponent::endSizeMultiplier,
        "shapeRadius", &ParticleSystemComponent::shapeRadius,
        "coneAngle", &ParticleSystemComponent::coneAngle,
        "boxSize", &ParticleSystemComponent::boxSize,
        "initialSpeedMin", &ParticleSystemComponent::initialSpeedMin,
        "initialSpeedMax", &ParticleSystemComponent::initialSpeedMax,
        "initialVelocityOffset", &ParticleSystemComponent::initialVelocityOffset,
        "physicsEnabled", &ParticleSystemComponent::physicsEnabled,
        "useGravity", &ParticleSystemComponent::useGravity,
        "gravityMultiplier", &ParticleSystemComponent::gravityMultiplier,
        "mass", &ParticleSystemComponent::mass,
        "linearDrag", &ParticleSystemComponent::linearDrag,
        "constantForce", &ParticleSystemComponent::constantForce,
        "turbulenceEnabled", &ParticleSystemComponent::turbulenceEnabled,
        "turbulenceStrength", &ParticleSystemComponent::turbulenceStrength,
        "turbulenceFrequency", &ParticleSystemComponent::turbulenceFrequency,
        "vortexStrength", &ParticleSystemComponent::vortexStrength,
        "collisionEnabled", &ParticleSystemComponent::collisionEnabled,
        "particleRadius", &ParticleSystemComponent::particleRadius,
        "bounciness", &ParticleSystemComponent::bounciness,
        "collisionFriction", &ParticleSystemComponent::collisionFriction,
        "lifetimeLossOnCollision", &ParticleSystemComponent::lifetimeLossOnCollision,
        "pushRigidBodies", &ParticleSystemComponent::pushRigidBodies,
        "startColor", &ParticleSystemComponent::startColor,
        "startColor2", &ParticleSystemComponent::startColor2,
        "emissiveColor", &ParticleSystemComponent::emissiveColor,
        "emissiveIntensity", &ParticleSystemComponent::emissiveIntensity,
        "receiveLights", &ParticleSystemComponent::receiveLights,
        "receiveShadows", &ParticleSystemComponent::receiveShadows,
        "castShadows", &ParticleSystemComponent::castShadows,
        "lightEmissionEnabled", &ParticleSystemComponent::lightEmissionEnabled,
        "lightColor", &ParticleSystemComponent::lightColor,
        "lightIntensity", &ParticleSystemComponent::lightIntensity,
        "lightRange", &ParticleSystemComponent::lightRange,
        "maxLights", &ParticleSystemComponent::maxLights,
        "useParticleColorForLight", &ParticleSystemComponent::useParticleColorForLight,
        "texturePath", &ParticleSystemComponent::texturePath,
        "hFrames", &ParticleSystemComponent::hFrames,
        "vFrames", &ParticleSystemComponent::vFrames,
        "animateSpriteSheet", &ParticleSystemComponent::animateSpriteSheet,
        "Play", &ParticleSystemComponent::Play,
        "Pause", &ParticleSystemComponent::Pause,
        "Stop", &ParticleSystemComponent::Stop,
        "Restart", &ParticleSystemComponent::Restart,
        "Emit", &ParticleSystemComponent::Emit,
        "IsPlaying", &ParticleSystemComponent::IsPlaying,
        "IsPaused", &ParticleSystemComponent::IsPaused,
        "GetActiveParticleCount", &ParticleSystemComponent::GetActiveParticleCount,
        "SetTexture", &ParticleSystemComponent::SetTexture,
        "ApplyPreset", &ParticleSystemComponent::ApplyPreset,
        "ClearGradient", [](ParticleSystemComponent& ps) { ps.gradientStops.clear(); },
        "AddGradientStop", [](ParticleSystemComponent& ps, float t, const glm::vec4& col) {
            ps.gradientStops.push_back({ t, col });
        },
        "SetRenderMode", [](ParticleSystemComponent& ps, const std::string& mode) {
            if (mode == "PointNiagara" || mode == "Niagara" || mode == "Points")
                ps.renderMode = ParticleRenderMode::PointNiagara;
            else
                ps.renderMode = ParticleRenderMode::Billboard;
        },
        "SetMaterialMode", [](ParticleSystemComponent& ps, const std::string& mode) {
            if (mode == "Lit") ps.materialMode = ParticleMaterialMode::Lit;
            else if (mode == "Emissive") ps.materialMode = ParticleMaterialMode::Emissive;
            else ps.materialMode = ParticleMaterialMode::Unlit;
        }
    );

    // -------------------------------------------------------------
    // SlipEntity
    // -------------------------------------------------------------
    m_Lua.new_usertype<SlipEntity>("Entity",
        "GetName", &SlipEntity::GetName,
        "SetName", &SlipEntity::SetName,
        "GetID", &SlipEntity::GetID,
        "name", sol::property(&SlipEntity::GetName, &SlipEntity::SetName),
        "GetTransform", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<TransformComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetRigidBody", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<RigidBodyComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetVehicle", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<VehicleComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetSpriteRenderer", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<SpriteRendererComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetSpriteAnimator", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<SpriteAnimatorComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetBoxCollider2D", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<BoxCollider2DComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetCircleCollider2D", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<CircleCollider2DComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetRigidBody2D", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<RigidBody2DComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetLight2D", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<Light2DComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetShadowCaster2D", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<ShadowCaster2DComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetTileGrid", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<TileGridComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetCanvas", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<CanvasComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetRectTransform", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<RectTransformComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetUIImage", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<UIImageComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetUIText", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<UITextComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetUIButton", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<UIButtonComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetUISlider", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<UISliderComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetUIProgressBar", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<UIProgressBarComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetUIJoystick", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<UIJoystickComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetUIActionButton", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<UIActionButtonComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetParticleSystem", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<ParticleSystemComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "AddParticleSystem", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto comp = e.GetComponent<ParticleSystemComponent>();
            if (!comp) comp = e.AddComponent<ParticleSystemComponent>();
            return comp ? sol::make_object(s, comp.get()) : sol::nil;
        },
        "GetParent", &SlipEntity::GetParent,
        "SetParent", &SlipEntity::SetParent,
        "GetChildren", [](SlipEntity& e, sol::this_state s) -> sol::table {
            sol::state_view lua(s);
            sol::table tbl = lua.create_table();
            int idx = 1;
            for (SlipEntity* child : e.GetChildren()) {
                if (child) tbl[idx++] = child;
            }
            return tbl;
        },
        "GetChildCount", [](SlipEntity& e) -> int {
            return static_cast<int>(e.GetChildCount());
        },
        "GetChild", [](SlipEntity& e, int index) -> SlipEntity* {
            if (index >= 1) return e.GetChild(static_cast<size_t>(index - 1));
            return e.GetChild(static_cast<size_t>(index));
        },
        "AddChild", &SlipEntity::AddChild,
        "RemoveChild", &SlipEntity::RemoveChild,
        "DetachFromParent", &SlipEntity::DetachFromParent,
        "IsChildOf", &SlipEntity::IsChildOf,
        "FindChild", [](SlipEntity& e, const std::string& childName, sol::optional<bool> recursive) -> SlipEntity* {
            return e.FindChild(childName, recursive.value_or(false));
        },
        "GetAllChildren", [](SlipEntity& e, sol::this_state s) -> sol::table {
            sol::state_view lua(s);
            sol::table tbl = lua.create_table();
            std::vector<SlipEntity*> all;
            e.GetAllChildren(all);
            for (size_t i = 0; i < all.size(); ++i) {
                tbl[static_cast<int>(i + 1)] = all[i];
            }
            return tbl;
        },
        "DontDestroyOnLoad", [](SlipEntity& e) {
            e.SetDontDestroyOnLoad(true);
        },
        "SetPersistent", &SlipEntity::SetPersistent,
        "IsPersistent", &SlipEntity::IsPersistent,
        "GetScript", [](SlipEntity& e, sol::optional<std::string> name, sol::this_state s) -> sol::object {
            std::shared_ptr<ScriptComponent> sc = e.GetScript(name.value_or(""));
            if (sc) {
                void* raw = sc->GetScriptTableRaw();
                if (raw) {
                    sol::table* tbl = static_cast<sol::table*>(raw);
                    return *tbl;
                }
            }
            return sol::nil;
        },
        "GetScripts", [](SlipEntity& e, sol::this_state s) -> sol::table {
            sol::state_view lua(s);
            sol::table result = lua.create_table();
            int idx = 1;
            for (auto& sc : e.GetScripts()) {
                if (sc) {
                    void* raw = sc->GetScriptTableRaw();
                    if (raw) {
                        sol::table* tbl = static_cast<sol::table*>(raw);
                        result[idx++] = *tbl;
                    }
                }
            }
            return result;
        },
        "GetComponent", [](SlipEntity& e, const std::string& typeName, sol::this_state s) -> sol::object {
            if (typeName == "Transform" || typeName == "TransformComponent") {
                auto tc = e.GetComponent<TransformComponent>();
                if (tc) return sol::make_object(s, tc.get());
            } else if (typeName == "RigidBody" || typeName == "RigidBodyComponent") {
                auto rb = e.GetComponent<RigidBodyComponent>();
                if (rb) return sol::make_object(s, rb.get());
            } else if (typeName == "Vehicle" || typeName == "VehicleComponent") {
                auto veh = e.GetComponent<VehicleComponent>();
                if (veh) return sol::make_object(s, veh.get());
            } else if (typeName == "BoxCollider" || typeName == "BoxColliderComponent") {
                auto bc = e.GetComponent<BoxColliderComponent>();
                if (bc) return sol::make_object(s, bc.get());
            } else if (typeName == "SphereCollider" || typeName == "SphereColliderComponent") {
                auto sc = e.GetComponent<SphereColliderComponent>();
                if (sc) return sol::make_object(s, sc.get());
            } else if (typeName == "CapsuleCollider" || typeName == "CapsuleColliderComponent") {
                auto cc = e.GetComponent<CapsuleColliderComponent>();
                if (cc) return sol::make_object(s, cc.get());
            } else if (typeName == "Camera" || typeName == "CameraComponent") {
                auto cam = e.GetComponent<CameraComponent>();
                if (cam) return sol::make_object(s, cam.get());
            } else if (typeName == "CinematicCamera" || typeName == "CinematicCameraComponent") {
                auto ccam = e.GetComponent<CinematicCameraComponent>();
                if (ccam) return sol::make_object(s, ccam.get());
            } else if (typeName == "Mesh" || typeName == "MeshComponent") {
                auto mesh = e.GetComponent<MeshComponent>();
                if (mesh) return sol::make_object(s, mesh.get());
            } else if (typeName == "SpriteRenderer" || typeName == "SpriteRendererComponent") {
                auto sr = e.GetComponent<SpriteRendererComponent>();
                if (sr) return sol::make_object(s, sr.get());
            } else if (typeName == "SpriteAnimator" || typeName == "SpriteAnimatorComponent") {
                auto sa = e.GetComponent<SpriteAnimatorComponent>();
                if (sa) return sol::make_object(s, sa.get());
            } else if (typeName == "BoxCollider2D" || typeName == "BoxCollider2DComponent") {
                auto b2d = e.GetComponent<BoxCollider2DComponent>();
                if (b2d) return sol::make_object(s, b2d.get());
            } else if (typeName == "CircleCollider2D" || typeName == "CircleCollider2DComponent") {
                auto c2d = e.GetComponent<CircleCollider2DComponent>();
                if (c2d) return sol::make_object(s, c2d.get());
            } else if (typeName == "RigidBody2D" || typeName == "RigidBody2DComponent") {
                auto rb2d = e.GetComponent<RigidBody2DComponent>();
                if (rb2d) return sol::make_object(s, rb2d.get());
            } else if (typeName == "Light2D" || typeName == "Light2DComponent") {
                auto l2d = e.GetComponent<Light2DComponent>();
                if (l2d) return sol::make_object(s, l2d.get());
            } else if (typeName == "ShadowCaster2D" || typeName == "ShadowCaster2DComponent") {
                auto sc2d = e.GetComponent<ShadowCaster2DComponent>();
                if (sc2d) return sol::make_object(s, sc2d.get());
            } else if (typeName == "TileGrid" || typeName == "TileGridComponent") {
                auto tg = e.GetComponent<TileGridComponent>();
                if (tg) return sol::make_object(s, tg.get());
            } else if (typeName == "Canvas" || typeName == "CanvasComponent") {
                auto cv = e.GetComponent<CanvasComponent>();
                if (cv) return sol::make_object(s, cv.get());
            } else if (typeName == "RectTransform" || typeName == "RectTransformComponent") {
                auto rt = e.GetComponent<RectTransformComponent>();
                if (rt) return sol::make_object(s, rt.get());
            } else if (typeName == "UIImage" || typeName == "UIImageComponent") {
                auto img = e.GetComponent<UIImageComponent>();
                if (img) return sol::make_object(s, img.get());
            } else if (typeName == "UIText" || typeName == "UITextComponent") {
                auto txt = e.GetComponent<UITextComponent>();
                if (txt) return sol::make_object(s, txt.get());
            } else if (typeName == "UIButton" || typeName == "UIButtonComponent") {
                auto btn = e.GetComponent<UIButtonComponent>();
                if (btn) return sol::make_object(s, btn.get());
            } else if (typeName == "UISlider" || typeName == "UISliderComponent") {
                auto sld = e.GetComponent<UISliderComponent>();
                if (sld) return sol::make_object(s, sld.get());
            } else if (typeName == "UIProgressBar" || typeName == "UIProgressBarComponent") {
                auto pb = e.GetComponent<UIProgressBarComponent>();
                if (pb) return sol::make_object(s, pb.get());
            } else if (typeName == "UIJoystick" || typeName == "UIJoystickComponent") {
                auto joy = e.GetComponent<UIJoystickComponent>();
                if (joy) return sol::make_object(s, joy.get());
            } else if (typeName == "UIActionButton" || typeName == "UIActionButtonComponent") {
                auto actionBtn = e.GetComponent<UIActionButtonComponent>();
                if (actionBtn) return sol::make_object(s, actionBtn.get());
            } else if (typeName == "ParticleSystem" || typeName == "ParticleSystemComponent") {
                auto ps = e.GetComponent<ParticleSystemComponent>();
                if (ps) return sol::make_object(s, ps.get());
            } else if (typeName == "SkinnedMesh" || typeName == "SkinnedMeshComponent") {
                auto sm = e.GetComponent<SkinnedMeshComponent>();
                if (sm) return sol::make_object(s, sm.get());
            } else if (typeName == "Animator" || typeName == "AnimatorComponent") {
                auto anim = e.GetComponent<AnimatorComponent>();
                if (anim) return sol::make_object(s, anim.get());
            } else if (typeName == "Bone" || typeName == "BoneComponent") {
                auto b = e.GetComponent<BoneComponent>();
                if (b) return sol::make_object(s, b.get());
            } else if (typeName == "KeyframeAnimation" || typeName == "KeyframeAnimationComponent" || typeName == "Sequence") {
                auto kfa = e.GetComponent<KeyframeAnimationComponent>();
                if (kfa) return sol::make_object(s, kfa.get());
            }
            return sol::nil;
        },
        "GetCamera", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto cam = e.GetComponent<CameraComponent>();
            return cam ? sol::make_object(s, cam.get()) : sol::nil;
        },
        "GetCinematicCamera", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto cam = e.GetComponent<CinematicCameraComponent>();
            return cam ? sol::make_object(s, cam.get()) : sol::nil;
        },
        "GetKeyframeAnimation", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto kfa = e.GetComponent<KeyframeAnimationComponent>();
            return kfa ? sol::make_object(s, kfa.get()) : sol::nil;
        },
        "GetSkinnedMesh", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto sm = e.GetComponent<SkinnedMeshComponent>();
            return sm ? sol::make_object(s, sm.get()) : sol::nil;
        },
        "GetAnimator", [](SlipEntity& e, sol::this_state s) -> sol::object {
            auto anim = e.GetComponent<AnimatorComponent>();
            return anim ? sol::make_object(s, anim.get()) : sol::nil;
        },
        "GetBone", [](SlipEntity& e, const std::string& boneName) -> SlipEntity* {
            return e.FindChild(boneName, true);
        },
        "PlayAnimation", [](SlipEntity& e, const std::string& clipName, sol::optional<float> speed, sol::optional<bool> loop) {
            auto anim = e.GetComponent<AnimatorComponent>();
            if (anim) anim->Play(clipName, speed.value_or(1.0f), loop.value_or(true));
        },
        "PauseAnimation", [](SlipEntity& e) {
            auto anim = e.GetComponent<AnimatorComponent>();
            if (anim) anim->Pause();
        },
        "ResumeAnimation", [](SlipEntity& e) {
            auto anim = e.GetComponent<AnimatorComponent>();
            if (anim) anim->Resume();
        },
        "StopAnimation", [](SlipEntity& e) {
            auto anim = e.GetComponent<AnimatorComponent>();
            if (anim) anim->Stop();
        },
        "SetAnimationSpeed", [](SlipEntity& e, float speed) {
            auto anim = e.GetComponent<AnimatorComponent>();
            if (anim) anim->playbackSpeed = speed;
        },
        "SetAnimFloat", [](SlipEntity& e, const std::string& name, float val) {
            auto anim = e.GetComponent<AnimatorComponent>();
            if (anim) anim->SetFloat(name, val);
        },
        "SetAnimBool", [](SlipEntity& e, const std::string& name, bool val) {
            auto anim = e.GetComponent<AnimatorComponent>();
            if (anim) anim->SetBool(name, val);
        },
        "SetAnimTrigger", [](SlipEntity& e, const std::string& name) {
            auto anim = e.GetComponent<AnimatorComponent>();
            if (anim) anim->SetTrigger(name);
        },
        "GetAnimFloat", [](SlipEntity& e, const std::string& name) -> float {
            auto anim = e.GetComponent<AnimatorComponent>();
            return anim ? anim->GetFloat(name) : 0.0f;
        },
        "GetAnimBool", [](SlipEntity& e, const std::string& name) -> bool {
            auto anim = e.GetComponent<AnimatorComponent>();
            return anim ? anim->GetBool(name) : false;
        },
        "Destroy", [](SlipEntity& e) {
            SlipLevel::Get().DestroyEntity(&e);
        }
    );

    // -------------------------------------------------------------
    // Tabla Global Slip
    // -------------------------------------------------------------
    sol::table slip = m_Lua.create_named_table("Slip");

    // -------------------------------------------------------------
    // Tabla Global Camera (Efectos de Camara, FOV, Shake, Letterbox)
    // -------------------------------------------------------------
    sol::table camTable = m_Lua.create_named_table("Camera");
    camTable["GetMain"] = [](sol::this_state s) -> sol::object {
        CameraComponent* cam = SlipLevel::Get().GetMainCamera();
        return cam ? sol::make_object(s, cam) : sol::nil;
    };
    camTable["SetFOV"] = [](float fov) {
        if (auto cam = SlipLevel::Get().GetMainCamera()) cam->SetFOV(fov);
    };
    camTable["GetFOV"] = []() -> float {
        if (auto cam = SlipLevel::Get().GetMainCamera()) return cam->GetFOV();
        return 60.0f;
    };
    camTable["Shake"] = [](float intensity, float duration, sol::optional<float> freq) {
        if (auto cam = SlipLevel::Get().GetMainCamera()) {
            cam->Shake(intensity, duration, freq.value_or(25.0f));
        }
    };
    camTable["StopShake"] = []() {
        if (auto cam = SlipLevel::Get().GetMainCamera()) cam->StopShake();
    };
    camTable["SetLetterbox"] = [](float amount) {
        if (auto cam = SlipLevel::Get().GetMainCamera()) cam->SetLetterbox(amount);
    };
    camTable["SetPillarbox"] = [](float amount) {
        if (auto cam = SlipLevel::Get().GetMainCamera()) cam->SetPillarbox(amount);
    };
    camTable["SetBlackBars"] = [](float h, float v) {
        if (auto cam = SlipLevel::Get().GetMainCamera()) cam->SetBlackBars(h, v);
    };
    slip["Camera"] = camTable;

    slip["Log"] = [](const std::string& message) {
        SlipConsole::LogInfo(std::string("[Lua] ") + message);
    };

    slip["LogWarning"] = [](const std::string& message) {
        SlipConsole::LogWarning(std::string("[Lua] ") + message);
    };

    slip["LogError"] = [](const std::string& message) {
        SlipConsole::LogError(std::string("[Lua] ") + message);
    };

    // Submódulo Slip.Debug
    sol::table debugTable = slip.create_named("Debug");
    debugTable["DrawLine"] = [](const glm::vec3& start, const glm::vec3& end, sol::optional<sol::object> colorObj) {
        glm::vec3 c(1.0f, 1.0f, 0.0f);
        if (colorObj.has_value() && colorObj.value().valid()) {
            sol::object obj = colorObj.value();
            if (obj.is<std::string>()) {
                std::string col = obj.as<std::string>();
                if (col == "red") c = glm::vec3(1.0f, 0.0f, 0.0f);
                else if (col == "green") c = glm::vec3(0.0f, 1.0f, 0.0f);
                else if (col == "blue") c = glm::vec3(0.0f, 0.0f, 1.0f);
                else if (col == "white") c = glm::vec3(1.0f, 1.0f, 1.0f);
                else if (col == "black") c = glm::vec3(0.0f, 0.0f, 0.0f);
                else if (col == "orange") c = glm::vec3(1.0f, 0.5f, 0.0f);
                else if (col == "cyan") c = glm::vec3(0.0f, 0.85f, 1.0f);
            } else if (obj.is<glm::vec3>()) {
                c = obj.as<glm::vec3>();
            } else if (obj.is<sol::table>()) {
                sol::table tbl = obj.as<sol::table>();
                c.r = tbl.get_or("x", tbl.get_or(1, 1.0f));
                c.g = tbl.get_or("y", tbl.get_or(2, 1.0f));
                c.b = tbl.get_or("z", tbl.get_or(3, 0.0f));
            }
        }
        SlipDebug::Get().drawLines(start, end, c);
    };

    // Submódulo Slip.Time
    sol::table timeTable = slip.create_named("Time");
    timeTable["GetDeltaTime"] = []() -> float {
        return SlipGlobals::Get().GetDeltaTime();
    };
    timeTable.set_function("deltaTime", []() -> float {
        return SlipGlobals::Get().GetDeltaTime();
    });

    // Submódulo Slip.Input
    sol::table inputTable = slip.create_named("Input");
    inputTable["GetKey"] = [](sol::object keyObj) -> bool {
        if (!SlipInput::HasInstance()) return false;
        if (keyObj.is<int>()) {
            return SlipInput::Get().GetKey(keyObj.as<int>()) != 0;
        } else if (keyObj.is<std::string>()) {
            std::string name = keyObj.as<std::string>();
            SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
            if (sc != SDL_SCANCODE_UNKNOWN) {
                return SlipInput::Get().GetKey(static_cast<int>(sc)) != 0;
            }
            SDL_Keycode kc = SDL_GetKeyFromName(name.c_str());
            if (kc != SDLK_UNKNOWN) {
                return SlipInput::Get().GetKey(static_cast<int>(kc)) != 0;
            }
        }
        return false;
    };
    inputTable["IsKeyPressed"] = inputTable["GetKey"];
    inputTable["GetMouseButton"] = [](int button) -> bool {
        return SlipInput::Get().GetMouseButton(button) != 0;
    };
    inputTable["GetMousePosition"] = []() -> glm::vec2 {
        if (!SlipInput::HasInstance()) return glm::vec2(0.0f);
        return SlipInput::Get().GetMousePos();
    };
    inputTable["GetMouseDelta"] = []() -> glm::vec2 {
        if (!SlipInput::HasInstance()) return glm::vec2(0.0f);
        return SlipInput::Get().GetMouseDelta();
    };
    inputTable["GetMouseWheel"] = []() -> glm::vec2 {
        if (!SlipInput::HasInstance()) return glm::vec2(0.0f);
        return SlipInput::Get().GetMouseWheel();
    };
    inputTable["SetCursorLocked"] = [](bool locked) {
        if (SlipInput::HasInstance()) SlipInput::Get().SetCursorLocked(locked);
    };
    inputTable["SetCursorVisible"] = [](bool visible) {
        if (SlipInput::HasInstance()) SlipInput::Get().SetCursorVisible(visible);
    };
    inputTable["IsCursorLocked"] = []() -> bool {
        return SlipInput::HasInstance() && SlipInput::Get().IsCursorLocked();
    };
    inputTable["IsCursorVisible"] = []() -> bool {
        return !SlipInput::HasInstance() || SlipInput::Get().IsCursorVisible();
    };
    inputTable["GetAction"] = [](const std::string& actionName) -> bool {
        return SlipInputMap::Get().GetAction(actionName);
    };
    inputTable["GetActionDown"] = [](const std::string& actionName) -> bool {
        return SlipInputMap::Get().GetActionDown(actionName);
    };
    inputTable["GetActionUp"] = [](const std::string& actionName) -> bool {
        return SlipInputMap::Get().GetActionUp(actionName);
    };
    inputTable["GetAxis"] = [](const std::string& axisName) -> float {
        return SlipInputMap::Get().GetAxis(axisName);
    };

    // Submódulo Slip.Key y Slip.KeyCode
    sol::table keyTable = slip.create_named("Key");
    keyTable["A"] = static_cast<int>(SDL_SCANCODE_A);
    keyTable["B"] = static_cast<int>(SDL_SCANCODE_B);
    keyTable["C"] = static_cast<int>(SDL_SCANCODE_C);
    keyTable["D"] = static_cast<int>(SDL_SCANCODE_D);
    keyTable["E"] = static_cast<int>(SDL_SCANCODE_E);
    keyTable["F"] = static_cast<int>(SDL_SCANCODE_F);
    keyTable["G"] = static_cast<int>(SDL_SCANCODE_G);
    keyTable["H"] = static_cast<int>(SDL_SCANCODE_H);
    keyTable["I"] = static_cast<int>(SDL_SCANCODE_I);
    keyTable["J"] = static_cast<int>(SDL_SCANCODE_J);
    keyTable["K"] = static_cast<int>(SDL_SCANCODE_K);
    keyTable["L"] = static_cast<int>(SDL_SCANCODE_L);
    keyTable["M"] = static_cast<int>(SDL_SCANCODE_M);
    keyTable["N"] = static_cast<int>(SDL_SCANCODE_N);
    keyTable["O"] = static_cast<int>(SDL_SCANCODE_O);
    keyTable["P"] = static_cast<int>(SDL_SCANCODE_P);
    keyTable["Q"] = static_cast<int>(SDL_SCANCODE_Q);
    keyTable["R"] = static_cast<int>(SDL_SCANCODE_R);
    keyTable["S"] = static_cast<int>(SDL_SCANCODE_S);
    keyTable["T"] = static_cast<int>(SDL_SCANCODE_T);
    keyTable["U"] = static_cast<int>(SDL_SCANCODE_U);
    keyTable["V"] = static_cast<int>(SDL_SCANCODE_V);
    keyTable["W"] = static_cast<int>(SDL_SCANCODE_W);
    keyTable["X"] = static_cast<int>(SDL_SCANCODE_X);
    keyTable["Y"] = static_cast<int>(SDL_SCANCODE_Y);
    keyTable["Z"] = static_cast<int>(SDL_SCANCODE_Z);

    keyTable["Num0"] = static_cast<int>(SDL_SCANCODE_0);
    keyTable["Num1"] = static_cast<int>(SDL_SCANCODE_1);
    keyTable["Num2"] = static_cast<int>(SDL_SCANCODE_2);
    keyTable["Num3"] = static_cast<int>(SDL_SCANCODE_3);
    keyTable["Num4"] = static_cast<int>(SDL_SCANCODE_4);
    keyTable["Num5"] = static_cast<int>(SDL_SCANCODE_5);
    keyTable["Num6"] = static_cast<int>(SDL_SCANCODE_6);
    keyTable["Num7"] = static_cast<int>(SDL_SCANCODE_7);
    keyTable["Num8"] = static_cast<int>(SDL_SCANCODE_8);
    keyTable["Num9"] = static_cast<int>(SDL_SCANCODE_9);

    keyTable["Up"] = static_cast<int>(SDL_SCANCODE_UP);
    keyTable["Down"] = static_cast<int>(SDL_SCANCODE_DOWN);
    keyTable["Left"] = static_cast<int>(SDL_SCANCODE_LEFT);
    keyTable["Right"] = static_cast<int>(SDL_SCANCODE_RIGHT);

    keyTable["Space"] = static_cast<int>(SDL_SCANCODE_SPACE);
    keyTable["Return"] = static_cast<int>(SDL_SCANCODE_RETURN);
    keyTable["Enter"] = static_cast<int>(SDL_SCANCODE_RETURN);
    keyTable["Escape"] = static_cast<int>(SDL_SCANCODE_ESCAPE);
    keyTable["Tab"] = static_cast<int>(SDL_SCANCODE_TAB);
    keyTable["Backspace"] = static_cast<int>(SDL_SCANCODE_BACKSPACE);

    keyTable["LeftShift"] = static_cast<int>(SDL_SCANCODE_LSHIFT);
    keyTable["RightShift"] = static_cast<int>(SDL_SCANCODE_RSHIFT);
    keyTable["LeftControl"] = static_cast<int>(SDL_SCANCODE_LCTRL);
    keyTable["RightControl"] = static_cast<int>(SDL_SCANCODE_RCTRL);
    keyTable["LeftAlt"] = static_cast<int>(SDL_SCANCODE_LALT);
    keyTable["RightAlt"] = static_cast<int>(SDL_SCANCODE_RALT);

    slip["KeyCode"] = keyTable;

    // Submódulo Slip.Scene
    sol::table sceneTable = slip.create_named("Scene");
    sceneTable["FindEntity"] = [](const std::string& name) -> SlipEntity* {
        for (SlipEntity* ent : SlipLevel::Get().GetEntities()) {
            if (ent && ent->GetName() == name) {
                return ent;
            }
        }
        return nullptr;
    };

    sceneTable["Destroy"] = [](SlipEntity* ent) {
        if (!ent) return;
        SlipLevel::Get().DestroyEntity(ent);
    };

    sceneTable["CreateEntity"] = [](sol::optional<std::string> name) -> SlipEntity* {
        SlipEntity* ent = new SlipEntity(name.value_or("Entity"));
        SlipLevel::Get().AddEntity(ent);
        return ent;
    };

    sceneTable["Instantiate"] = [](const std::string& prefabNameOrPath, sol::optional<glm::vec3> pos, sol::optional<glm::vec3> rot) -> SlipEntity* {
        glm::vec3 p = pos.value_or(glm::vec3(0.0f));
        glm::vec3 r = rot.value_or(glm::vec3(0.0f));
        return SlipPrefab::Instantiate(prefabNameOrPath, &SlipLevel::Get(), p, r);
    };

    sceneTable["LoadScene"] = [](const std::string& scenePathOrName) {
        std::string path = scenePathOrName;
        bool isAbsolute = (path.size() >= 2 && path[1] == ':') || (path.size() >= 1 && (path[0] == '/' || path[0] == '\\'));
        if (!isAbsolute && path.find('/') == std::string::npos && path.find('\\') == std::string::npos) {
            std::string root = SlipFileSystem::GetProjectRootPath();
            if (root.empty()) root = SlipFileSystem::GetDefaultWorkingPath();
            std::string scenesDir = SlipFileSystem::CombinePath(root, "scenes");
            if (path.size() < 6 || path.substr(path.size() - 6) != ".scene") {
                path += ".scene";
            }
            path = SlipFileSystem::CombinePath(scenesDir, path);
        }
        SlipLevel::Get().RequestLoadScene(path);
    };

    sceneTable["ReloadScene"] = []() {
        std::string cur = SlipLevel::Get().GetCurrentScenePath();
        if (!cur.empty()) {
            SlipLevel::Get().RequestLoadScene(cur);
        }
    };

    sceneTable["GetCurrentScenePath"] = []() -> std::string {
        return SlipLevel::Get().GetCurrentScenePath();
    };

    sceneTable["GetCurrentSceneName"] = []() -> std::string {
        return SlipLevel::Get().GetCurrentSceneName();
    };

    sceneTable["GetEntities"] = [](sol::this_state s) -> sol::table {
        sol::state_view lua(s);
        sol::table result = lua.create_table();
        int idx = 1;
        for (SlipEntity* ent : SlipLevel::Get().GetEntities()) {
            if (ent) result[idx++] = ent;
        }
        return result;
    };

    slip["DontDestroyOnLoad"] = [](SlipEntity* ent) {
        if (ent) ent->SetDontDestroyOnLoad(true);
    };

    // Submódulo Slip.Physics
    sol::table physicsTable = slip.create_named("Physics");
    physicsTable["Raycast"] = [](const glm::vec3& from, const glm::vec3& to, sol::optional<SlipEntity*> ignoreEnt, sol::this_state s) -> sol::object {
        if (!SlipPhysics::IsInitialized()) return sol::nil;

        btVector3 bFrom(from.x, from.y, from.z);
        btVector3 bTo(to.x, to.y, to.z);

        struct CustomRayCallback : public btCollisionWorld::ClosestRayResultCallback {
            const SlipEntity* ignore;
            CustomRayCallback(const btVector3& rayFromWorld, const btVector3& rayToWorld, const SlipEntity* ignoreEntity)
                : btCollisionWorld::ClosestRayResultCallback(rayFromWorld, rayToWorld), ignore(ignoreEntity) {}

            btScalar addSingleResult(btCollisionWorld::LocalRayResult& rayResult, bool normalInWorldSpace) override {
                if (rayResult.m_collisionObject) {
                    if (rayResult.m_collisionObject->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE) {
                        return 1.0f;
                    }
                    SlipEntity* ent = static_cast<SlipEntity*>(rayResult.m_collisionObject->getUserPointer());
                    if (ent && ent == ignore) {
                        return 1.0f;
                    }
                }
                return btCollisionWorld::ClosestRayResultCallback::addSingleResult(rayResult, normalInWorldSpace);
            }
        };

        SlipEntity* ign = ignoreEnt.value_or(nullptr);
        CustomRayCallback callback(bFrom, bTo, ign);
        SlipPhysics::Get().getWorld().rayTest(bFrom, bTo, callback);

        if (callback.hasHit()) {
            sol::state_view lua(s);
            sol::table hitResult = lua.create_table();
            hitResult["hasHit"] = true;
            hitResult["fraction"] = callback.m_closestHitFraction;
            glm::vec3 hitPoint(callback.m_hitPointWorld.x(), callback.m_hitPointWorld.y(), callback.m_hitPointWorld.z());
            glm::vec3 hitNormal(callback.m_hitNormalWorld.x(), callback.m_hitNormalWorld.y(), callback.m_hitNormalWorld.z());
            hitResult["point"] = hitPoint;
            hitResult["normal"] = hitNormal;
            hitResult["distance"] = glm::length(hitPoint - from);

            if (callback.m_collisionObject) {
                SlipEntity* hitEnt = static_cast<SlipEntity*>(callback.m_collisionObject->getUserPointer());
                if (hitEnt) hitResult["entity"] = hitEnt;
            }
            return hitResult;
        }
        return sol::nil;
    };

    physicsTable["SetSimulationBubble"] = [](bool enable, sol::optional<float> radius, sol::optional<glm::vec3> center) {
        SlipPhysics::Get().SetSimulationBubble(enable, radius.value_or(60.0f), center.value_or(glm::vec3(0.0f)));
    };
    physicsTable["IsSimulationBubbleEnabled"] = []() {
        return SlipPhysics::Get().IsSimulationBubbleEnabled();
    };
    physicsTable["SetSimulationBubbleCenter"] = [](const glm::vec3& center) {
        SlipPhysics::Get().SetSimulationBubbleCenter(center);
    };
}

