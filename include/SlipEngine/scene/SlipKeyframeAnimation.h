#pragma once

#ifndef SLIP_KEYFRAME_ANIMATION_H
#define SLIP_KEYFRAME_ANIMATION_H

#include "SlipEngine/core/SlipExport.h"
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>

class SlipEntity;
class SlipLevel;

enum class KeyframeInterpolation {
    Linear = 0,
    Smooth = 1,
    Step = 2
};

enum class AnimatableProperty {
    // Transform estándar 3D
    Transform_PosX = 0,
    Transform_PosY,
    Transform_PosZ,
    Transform_RotX,
    Transform_RotY,
    Transform_RotZ,
    Transform_ScaleX,
    Transform_ScaleY,
    Transform_ScaleZ,

    // Cámara
    Camera_FOV,
    Camera_Letterbox,
    Camera_Pillarbox,
    Camera_ShakeIntensity,

    // Iluminación
    Light_Intensity,

    // UI & RectTransform (Transiciones, Menús, Fades, HUD)
    UI_AnchoredPosX,
    UI_AnchoredPosY,
    UI_Width,
    UI_Height,
    UI_Alpha,
    UI_ColorR,
    UI_ColorG,
    UI_ColorB,
    UI_ProgressValue,
    UI_FontSize,

    // Esqueleto / Huesos (Animaciones personalizadas de personajes)
    Bone_RotX,
    Bone_RotY,
    Bone_RotZ,
    Bone_PosX,
    Bone_PosY,
    Bone_PosZ,

    Custom
};

struct SLIPENGINE_API FloatKeyframe {
    float time = 0.0f;
    float value = 0.0f;
    KeyframeInterpolation interpolation = KeyframeInterpolation::Smooth;

    FloatKeyframe() = default;
    FloatKeyframe(float t, float v, KeyframeInterpolation interp = KeyframeInterpolation::Smooth)
        : time(t), value(v), interpolation(interp) {}
};

class SLIPENGINE_API KeyframeTrack {
public:
    std::string name;
    AnimatableProperty propertyType = AnimatableProperty::Transform_PosX;
    std::vector<FloatKeyframe> keyframes;

    KeyframeTrack() = default;
    KeyframeTrack(const std::string& trackName, AnimatableProperty propType)
        : name(trackName), propertyType(propType) {}

    float Evaluate(float time) const;
    void AddKeyframe(float time, float value, KeyframeInterpolation interp = KeyframeInterpolation::Smooth);
    bool RemoveKeyframe(int index);
    int FindKeyframeAt(float time, float tolerance = 0.05f) const;
    void SortKeyframes();
};

class SLIPENGINE_API EntityAnimationTrackGroup {
public:
    std::string entityName;
    std::vector<KeyframeTrack> tracks;

    EntityAnimationTrackGroup() = default;
    explicit EntityAnimationTrackGroup(const std::string& name) : entityName(name) {}

    KeyframeTrack* GetTrack(AnimatableProperty type);
    const KeyframeTrack* GetTrack(AnimatableProperty type) const;
    KeyframeTrack& GetOrCreateTrack(AnimatableProperty type, const std::string& trackName);
    bool RemoveTrack(AnimatableProperty type);

    void CaptureKeyframe(SlipEntity* entity, AnimatableProperty type, float time);
    void CaptureAll(SlipEntity* entity, float time);
    void Apply(SlipEntity* entity, float time);
};

class SLIPENGINE_API KeyframeClip {
public:
    std::string name = "CinematicClip";
    float duration = 5.0f;          // Duración total en segundos
    float frameRate = 30.0f;        // Fotogramas por segundo (FPS)
    bool loop = false;

    std::vector<EntityAnimationTrackGroup> entityGroups;

    KeyframeClip() = default;
    explicit KeyframeClip(const std::string& clipName, float dur = 5.0f, float fps = 30.0f)
        : name(clipName), duration(dur), frameRate(fps) {}

    EntityAnimationTrackGroup* GetGroup(const std::string& entityName);
    const EntityAnimationTrackGroup* GetGroup(const std::string& entityName) const;
    EntityAnimationTrackGroup& GetOrCreateGroup(const std::string& entityName);

    void Sample(SlipLevel* level, float time);
    void SampleEntity(SlipEntity* entity, float time);

    int GetTotalFrames() const {
        return static_cast<int>(std::ceil(duration * frameRate));
    }

    bool SaveToFile(const std::string& filePath) const;
    bool LoadFromFile(const std::string& filePath);

    // Conversión e integración con el sistema esquelético de SlipEngine
    std::shared_ptr<class AnimationClip> ToSkeletalAnimationClip() const;
    void FromSkeletalAnimationClip(const class AnimationClip* skeletalClip);

    static std::string PropertyToString(AnimatableProperty prop);
    static AnimatableProperty StringToProperty(const std::string& str);
    static const char* GetPropertyDisplayName(AnimatableProperty prop);
};

#endif // !SLIP_KEYFRAME_ANIMATION_H

