#include "SlipEngine/scene/SlipKeyframeAnimation.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/render/SlipAnimation.h"
#include "SlipEngine/render/SlipSkeleton.h"
#include "SlipEngine/core/SlipConsole.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <glm/gtx/quaternion.hpp>

// -------------------------------------------------------------
// KeyframeTrack
// -------------------------------------------------------------
void KeyframeTrack::SortKeyframes() {
    std::sort(keyframes.begin(), keyframes.end(), [](const FloatKeyframe& a, const FloatKeyframe& b) {
        return a.time < b.time;
    });
}

float KeyframeTrack::Evaluate(float time) const {
    if (keyframes.empty()) return 0.0f;
    if (keyframes.size() == 1) return keyframes[0].value;

    if (time <= keyframes.front().time) return keyframes.front().value;
    if (time >= keyframes.back().time) return keyframes.back().value;

    // Localizar segmento [k0, k1]
    size_t k0 = 0;
    for (size_t i = 0; i < keyframes.size() - 1; ++i) {
        if (time < keyframes[i + 1].time) {
            k0 = i;
            break;
        }
    }
    size_t k1 = k0 + 1;
    if (k1 >= keyframes.size()) return keyframes.back().value;

    const auto& key0 = keyframes[k0];
    const auto& key1 = keyframes[k1];

    float dt = key1.time - key0.time;
    if (dt <= 1e-6f) return key0.value;

    float u = glm::clamp((time - key0.time) / dt, 0.0f, 1.0f);

    if (key0.interpolation == KeyframeInterpolation::Step) {
        return key0.value;
    } else if (key0.interpolation == KeyframeInterpolation::Linear) {
        return glm::mix(key0.value, key1.value, u);
    } else {
        // Hermite cúbico suave (Smoothstep): 3u^2 - 2u^3
        float smoothFactor = u * u * (3.0f - 2.0f * u);
        return glm::mix(key0.value, key1.value, smoothFactor);
    }
}

void KeyframeTrack::AddKeyframe(float time, float value, KeyframeInterpolation interp) {
    int existingIdx = FindKeyframeAt(time, 0.03f);
    if (existingIdx >= 0) {
        keyframes[existingIdx].value = value;
        keyframes[existingIdx].interpolation = interp;
    } else {
        keyframes.emplace_back(time, value, interp);
        SortKeyframes();
    }
}

bool KeyframeTrack::RemoveKeyframe(int index) {
    if (index >= 0 && index < static_cast<int>(keyframes.size())) {
        keyframes.erase(keyframes.begin() + index);
        return true;
    }
    return false;
}

int KeyframeTrack::FindKeyframeAt(float time, float tolerance) const {
    for (size_t i = 0; i < keyframes.size(); ++i) {
        if (std::abs(keyframes[i].time - time) <= tolerance) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// -------------------------------------------------------------
// EntityAnimationTrackGroup
// -------------------------------------------------------------
KeyframeTrack* EntityAnimationTrackGroup::GetTrack(AnimatableProperty type) {
    for (auto& trk : tracks) {
        if (trk.propertyType == type) return &trk;
    }
    return nullptr;
}

const KeyframeTrack* EntityAnimationTrackGroup::GetTrack(AnimatableProperty type) const {
    for (const auto& trk : tracks) {
        if (trk.propertyType == type) return &trk;
    }
    return nullptr;
}

KeyframeTrack& EntityAnimationTrackGroup::GetOrCreateTrack(AnimatableProperty type, const std::string& trackName) {
    KeyframeTrack* existing = GetTrack(type);
    if (existing) return *existing;

    tracks.emplace_back(trackName.empty() ? KeyframeClip::GetPropertyDisplayName(type) : trackName, type);
    return tracks.back();
}

bool EntityAnimationTrackGroup::RemoveTrack(AnimatableProperty type) {
    for (auto it = tracks.begin(); it != tracks.end(); ++it) {
        if (it->propertyType == type) {
            tracks.erase(it);
            return true;
        }
    }
    return false;
}

void EntityAnimationTrackGroup::CaptureKeyframe(SlipEntity* entity, AnimatableProperty type, float time) {
    if (!entity) return;

    KeyframeTrack& trk = GetOrCreateTrack(type, KeyframeClip::GetPropertyDisplayName(type));
    float val = 0.0f;

    switch (type) {
        case AnimatableProperty::Transform_PosX: {
            if (auto tc = entity->GetComponent<TransformComponent>()) val = tc->position.x;
            else val = entity->position.x;
            break;
        }
        case AnimatableProperty::Transform_PosY: {
            if (auto tc = entity->GetComponent<TransformComponent>()) val = tc->position.y;
            else val = entity->position.y;
            break;
        }
        case AnimatableProperty::Transform_PosZ: {
            if (auto tc = entity->GetComponent<TransformComponent>()) val = tc->position.z;
            else val = entity->position.z;
            break;
        }
        case AnimatableProperty::Transform_RotX: {
            if (auto tc = entity->GetComponent<TransformComponent>()) val = tc->rotation.x;
            else val = entity->rotation.x;
            break;
        }
        case AnimatableProperty::Transform_RotY: {
            if (auto tc = entity->GetComponent<TransformComponent>()) val = tc->rotation.y;
            else val = entity->rotation.y;
            break;
        }
        case AnimatableProperty::Transform_RotZ: {
            if (auto tc = entity->GetComponent<TransformComponent>()) val = tc->rotation.z;
            else val = entity->rotation.z;
            break;
        }
        case AnimatableProperty::Transform_ScaleX: {
            if (auto tc = entity->GetComponent<TransformComponent>()) val = tc->scale.x;
            else val = entity->scale.x;
            break;
        }
        case AnimatableProperty::Transform_ScaleY: {
            if (auto tc = entity->GetComponent<TransformComponent>()) val = tc->scale.y;
            else val = entity->scale.y;
            break;
        }
        case AnimatableProperty::Transform_ScaleZ: {
            if (auto tc = entity->GetComponent<TransformComponent>()) val = tc->scale.z;
            else val = entity->scale.z;
            break;
        }
        case AnimatableProperty::Camera_FOV: {
            if (auto cam = entity->GetComponent<CameraComponent>()) val = cam->GetFOV();
            break;
        }
        case AnimatableProperty::Camera_Letterbox: {
            if (auto cam = entity->GetComponent<CameraComponent>()) val = cam->GetLetterbox();
            break;
        }
        case AnimatableProperty::Camera_Pillarbox: {
            if (auto cam = entity->GetComponent<CameraComponent>()) val = cam->GetPillarbox();
            break;
        }
        case AnimatableProperty::Camera_ShakeIntensity: {
            if (auto cam = entity->GetComponent<CameraComponent>()) val = cam->GetShakeIntensity();
            break;
        }
        case AnimatableProperty::Light_Intensity: {
            if (auto light = entity->GetComponent<LightComponent>()) val = light->intensity;
            break;
        }

        // --- PROPIEDADES DE UI ---
        case AnimatableProperty::UI_AnchoredPosX: {
            if (auto rt = entity->GetComponent<RectTransformComponent>()) val = rt->anchoredPosition.x;
            break;
        }
        case AnimatableProperty::UI_AnchoredPosY: {
            if (auto rt = entity->GetComponent<RectTransformComponent>()) val = rt->anchoredPosition.y;
            break;
        }
        case AnimatableProperty::UI_Width: {
            if (auto rt = entity->GetComponent<RectTransformComponent>()) val = rt->size.x;
            break;
        }
        case AnimatableProperty::UI_Height: {
            if (auto rt = entity->GetComponent<RectTransformComponent>()) val = rt->size.y;
            break;
        }
        case AnimatableProperty::UI_Alpha: {
            if (auto img = entity->GetComponent<UIImageComponent>()) val = img->color.a;
            else if (auto txt = entity->GetComponent<UITextComponent>()) val = txt->color.a;
            else if (auto pb = entity->GetComponent<UIProgressBarComponent>()) val = pb->fillColor.a;
            break;
        }
        case AnimatableProperty::UI_ColorR: {
            if (auto img = entity->GetComponent<UIImageComponent>()) val = img->color.r;
            else if (auto txt = entity->GetComponent<UITextComponent>()) val = txt->color.r;
            break;
        }
        case AnimatableProperty::UI_ColorG: {
            if (auto img = entity->GetComponent<UIImageComponent>()) val = img->color.g;
            else if (auto txt = entity->GetComponent<UITextComponent>()) val = txt->color.g;
            break;
        }
        case AnimatableProperty::UI_ColorB: {
            if (auto img = entity->GetComponent<UIImageComponent>()) val = img->color.b;
            else if (auto txt = entity->GetComponent<UITextComponent>()) val = txt->color.b;
            break;
        }
        case AnimatableProperty::UI_ProgressValue: {
            if (auto pb = entity->GetComponent<UIProgressBarComponent>()) val = pb->value;
            else if (auto sld = entity->GetComponent<UISliderComponent>()) val = sld->value;
            break;
        }
        case AnimatableProperty::UI_FontSize: {
            if (auto txt = entity->GetComponent<UITextComponent>()) val = txt->fontSize;
            break;
        }

        // --- PROPIEDADES DE HUESOS / ESQUELETO ---
        case AnimatableProperty::Bone_RotX: {
            if (auto bc = entity->GetComponent<BoneComponent>()) {
                val = glm::degrees(glm::eulerAngles(bc->orientation)).x;
            } else if (auto tc = entity->GetComponent<TransformComponent>()) {
                val = tc->rotation.x;
            } else {
                val = entity->rotation.x;
            }
            break;
        }
        case AnimatableProperty::Bone_RotY: {
            if (auto bc = entity->GetComponent<BoneComponent>()) {
                val = glm::degrees(glm::eulerAngles(bc->orientation)).y;
            } else if (auto tc = entity->GetComponent<TransformComponent>()) {
                val = tc->rotation.y;
            } else {
                val = entity->rotation.y;
            }
            break;
        }
        case AnimatableProperty::Bone_RotZ: {
            if (auto bc = entity->GetComponent<BoneComponent>()) {
                val = glm::degrees(glm::eulerAngles(bc->orientation)).z;
            } else if (auto tc = entity->GetComponent<TransformComponent>()) {
                val = tc->rotation.z;
            } else {
                val = entity->rotation.z;
            }
            break;
        }
        case AnimatableProperty::Bone_PosX: {
            if (auto tc = entity->GetComponent<TransformComponent>()) val = tc->position.x;
            else val = entity->position.x;
            break;
        }
        case AnimatableProperty::Bone_PosY: {
            if (auto tc = entity->GetComponent<TransformComponent>()) val = tc->position.y;
            else val = entity->position.y;
            break;
        }
        case AnimatableProperty::Bone_PosZ: {
            if (auto tc = entity->GetComponent<TransformComponent>()) val = tc->position.z;
            else val = entity->position.z;
            break;
        }

        default: break;
    }

    trk.AddKeyframe(time, val, KeyframeInterpolation::Smooth);
}

void EntityAnimationTrackGroup::CaptureAll(SlipEntity* entity, float time) {
    if (!entity) return;

    if (tracks.empty()) {
        // Detectar si la entidad es de UI
        if (entity->GetComponent<RectTransformComponent>() || entity->GetComponent<UIImageComponent>() || entity->GetComponent<UITextComponent>()) {
            CaptureKeyframe(entity, AnimatableProperty::UI_AnchoredPosX, time);
            CaptureKeyframe(entity, AnimatableProperty::UI_AnchoredPosY, time);
            CaptureKeyframe(entity, AnimatableProperty::UI_Width, time);
            CaptureKeyframe(entity, AnimatableProperty::UI_Height, time);
            CaptureKeyframe(entity, AnimatableProperty::UI_Alpha, time);
            CaptureKeyframe(entity, AnimatableProperty::Transform_ScaleX, time);
            CaptureKeyframe(entity, AnimatableProperty::Transform_ScaleY, time);
        } else if (entity->GetComponent<BoneComponent>()) {
            CaptureKeyframe(entity, AnimatableProperty::Bone_RotX, time);
            CaptureKeyframe(entity, AnimatableProperty::Bone_RotY, time);
            CaptureKeyframe(entity, AnimatableProperty::Bone_RotZ, time);
            CaptureKeyframe(entity, AnimatableProperty::Bone_PosX, time);
            CaptureKeyframe(entity, AnimatableProperty::Bone_PosY, time);
            CaptureKeyframe(entity, AnimatableProperty::Bone_PosZ, time);
        } else {
            // Crear pistas predeterminadas de entidad 3D
            CaptureKeyframe(entity, AnimatableProperty::Transform_PosX, time);
            CaptureKeyframe(entity, AnimatableProperty::Transform_PosY, time);
            CaptureKeyframe(entity, AnimatableProperty::Transform_PosZ, time);
            CaptureKeyframe(entity, AnimatableProperty::Transform_RotX, time);
            CaptureKeyframe(entity, AnimatableProperty::Transform_RotY, time);
            CaptureKeyframe(entity, AnimatableProperty::Transform_RotZ, time);
            if (entity->GetComponent<CameraComponent>()) {
                CaptureKeyframe(entity, AnimatableProperty::Camera_FOV, time);
                CaptureKeyframe(entity, AnimatableProperty::Camera_Letterbox, time);
            }
        }
    } else {
        for (auto& trk : tracks) {
            CaptureKeyframe(entity, trk.propertyType, time);
        }
    }
}

void EntityAnimationTrackGroup::Apply(SlipEntity* entity, float time) {
    if (!entity || tracks.empty()) return;

    glm::vec3 pos = entity->position;
    glm::vec3 rot = entity->rotation;
    glm::vec3 scl = entity->scale;
    bool posChanged = false, rotChanged = false, sclChanged = false;

    CameraComponent* cam = entity->GetComponent<CameraComponent>().get();
    LightComponent* light = entity->GetComponent<LightComponent>().get();

    for (const auto& trk : tracks) {
        if (trk.keyframes.empty()) continue;
        float val = trk.Evaluate(time);

        switch (trk.propertyType) {
            case AnimatableProperty::Transform_PosX: pos.x = val; posChanged = true; break;
            case AnimatableProperty::Transform_PosY: pos.y = val; posChanged = true; break;
            case AnimatableProperty::Transform_PosZ: pos.z = val; posChanged = true; break;
            case AnimatableProperty::Transform_RotX: rot.x = val; rotChanged = true; break;
            case AnimatableProperty::Transform_RotY: rot.y = val; rotChanged = true; break;
            case AnimatableProperty::Transform_RotZ: rot.z = val; rotChanged = true; break;
            case AnimatableProperty::Transform_ScaleX: scl.x = val; sclChanged = true; break;
            case AnimatableProperty::Transform_ScaleY: scl.y = val; sclChanged = true; break;
            case AnimatableProperty::Transform_ScaleZ: scl.z = val; sclChanged = true; break;
            case AnimatableProperty::Camera_FOV: {
                if (cam) cam->SetFOV(val);
                break;
            }
            case AnimatableProperty::Camera_Letterbox: {
                if (cam) cam->SetLetterbox(val);
                break;
            }
            case AnimatableProperty::Camera_Pillarbox: {
                if (cam) cam->SetPillarbox(val);
                break;
            }
            case AnimatableProperty::Camera_ShakeIntensity: {
                if (cam) {
                    cam->SetShakeIntensity(val);
                    if (val > 0.05f && !cam->IsShaking()) {
                        cam->Shake(val, 0.25f);
                    }
                }
                break;
            }
            case AnimatableProperty::Light_Intensity: {
                if (light) light->intensity = val;
                break;
            }

            // --- PROPIEDADES DE UI ---
            case AnimatableProperty::UI_AnchoredPosX: {
                if (auto rt = entity->GetComponent<RectTransformComponent>()) rt->anchoredPosition.x = val;
                break;
            }
            case AnimatableProperty::UI_AnchoredPosY: {
                if (auto rt = entity->GetComponent<RectTransformComponent>()) rt->anchoredPosition.y = val;
                break;
            }
            case AnimatableProperty::UI_Width: {
                if (auto rt = entity->GetComponent<RectTransformComponent>()) rt->size.x = val;
                break;
            }
            case AnimatableProperty::UI_Height: {
                if (auto rt = entity->GetComponent<RectTransformComponent>()) rt->size.y = val;
                break;
            }
            case AnimatableProperty::UI_Alpha: {
                float a = (val < 0.0f) ? 0.0f : ((val > 1.0f) ? 1.0f : val);
                if (auto img = entity->GetComponent<UIImageComponent>()) img->color.a = a;
                if (auto txt = entity->GetComponent<UITextComponent>()) txt->color.a = a;
                if (auto pb = entity->GetComponent<UIProgressBarComponent>()) {
                    pb->fillColor.a = a;
                    pb->backgroundColor.a = a;
                }
                break;
            }
            case AnimatableProperty::UI_ColorR: {
                if (auto img = entity->GetComponent<UIImageComponent>()) img->color.r = val;
                if (auto txt = entity->GetComponent<UITextComponent>()) txt->color.r = val;
                break;
            }
            case AnimatableProperty::UI_ColorG: {
                if (auto img = entity->GetComponent<UIImageComponent>()) img->color.g = val;
                if (auto txt = entity->GetComponent<UITextComponent>()) txt->color.g = val;
                break;
            }
            case AnimatableProperty::UI_ColorB: {
                if (auto img = entity->GetComponent<UIImageComponent>()) img->color.b = val;
                if (auto txt = entity->GetComponent<UITextComponent>()) txt->color.b = val;
                break;
            }
            case AnimatableProperty::UI_ProgressValue: {
                if (auto pb = entity->GetComponent<UIProgressBarComponent>()) pb->value = val;
                if (auto sld = entity->GetComponent<UISliderComponent>()) sld->value = val;
                break;
            }
            case AnimatableProperty::UI_FontSize: {
                if (auto txt = entity->GetComponent<UITextComponent>()) txt->fontSize = val;
                break;
            }

            // --- PROPIEDADES DE HUESOS / ESQUELETO ---
            case AnimatableProperty::Bone_RotX: rot.x = val; rotChanged = true; break;
            case AnimatableProperty::Bone_RotY: rot.y = val; rotChanged = true; break;
            case AnimatableProperty::Bone_RotZ: rot.z = val; rotChanged = true; break;
            case AnimatableProperty::Bone_PosX: pos.x = val; posChanged = true; break;
            case AnimatableProperty::Bone_PosY: pos.y = val; posChanged = true; break;
            case AnimatableProperty::Bone_PosZ: pos.z = val; posChanged = true; break;

            default: break;
        }
    }

    if (posChanged) {
        entity->position = pos;
        if (entity->GetTransform()) entity->GetTransform()->position = pos;
    }
    if (rotChanged) {
        entity->rotation = rot;
        if (entity->GetTransform()) entity->GetTransform()->rotation = rot;
    }
    if (sclChanged) {
        entity->scale = scl;
        if (entity->GetTransform()) entity->GetTransform()->scale = scl;
    }

    if (auto bc = entity->GetComponent<BoneComponent>()) {
        bc->orientation = glm::quat(glm::radians(rot));
        SlipEntity* cur = entity->GetParent();
        while (cur) {
            if (auto smc = cur->GetComponent<SkinnedMeshComponent>()) {
                smc->MarkBoneMatricesDirty();
                smc->UpdateBoneMatrices();
                break;
            }
            cur = cur->GetParent();
        }
    }
}

// -------------------------------------------------------------
// KeyframeClip
// -------------------------------------------------------------
EntityAnimationTrackGroup* KeyframeClip::GetGroup(const std::string& entityName) {
    for (auto& g : entityGroups) {
        if (g.entityName == entityName) return &g;
    }
    return nullptr;
}

const EntityAnimationTrackGroup* KeyframeClip::GetGroup(const std::string& entityName) const {
    for (const auto& g : entityGroups) {
        if (g.entityName == entityName) return &g;
    }
    return nullptr;
}

EntityAnimationTrackGroup& KeyframeClip::GetOrCreateGroup(const std::string& entityName) {
    EntityAnimationTrackGroup* existing = GetGroup(entityName);
    if (existing) return *existing;

    entityGroups.emplace_back(entityName);
    return entityGroups.back();
}

void KeyframeClip::Sample(SlipLevel* level, float time) {
    if (!level) return;

    float t = time;
    if (duration > 0.0f) {
        if (loop) t = std::fmod(time, duration);
        else t = glm::clamp(time, 0.0f, duration);
    }

    for (auto& g : entityGroups) {
        SlipEntity* targetEnt = nullptr;
        for (SlipEntity* ent : level->GetEntities()) {
            if (ent && ent->GetName() == g.entityName) {
                targetEnt = ent;
                break;
            }
        }
        if (targetEnt) {
            g.Apply(targetEnt, t);
        }
    }
}

void KeyframeClip::SampleEntity(SlipEntity* entity, float time) {
    if (!entity) return;

    float t = time;
    if (duration > 0.0f) {
        if (loop) t = std::fmod(time, duration);
        else t = glm::clamp(time, 0.0f, duration);
    }

    // Buscar grupo que coincida con el nombre de la entidad o el primer grupo disponible
    EntityAnimationTrackGroup* grp = GetGroup(entity->GetName());
    if (!grp && !entityGroups.empty()) {
        grp = &entityGroups[0];
    }

    if (grp) {
        grp->Apply(entity, t);
    }
}

std::string KeyframeClip::PropertyToString(AnimatableProperty prop) {
    switch (prop) {
        case AnimatableProperty::Transform_PosX: return "Transform.Position.X";
        case AnimatableProperty::Transform_PosY: return "Transform.Position.Y";
        case AnimatableProperty::Transform_PosZ: return "Transform.Position.Z";
        case AnimatableProperty::Transform_RotX: return "Transform.Rotation.X";
        case AnimatableProperty::Transform_RotY: return "Transform.Rotation.Y";
        case AnimatableProperty::Transform_RotZ: return "Transform.Rotation.Z";
        case AnimatableProperty::Transform_ScaleX: return "Transform.Scale.X";
        case AnimatableProperty::Transform_ScaleY: return "Transform.Scale.Y";
        case AnimatableProperty::Transform_ScaleZ: return "Transform.Scale.Z";
        case AnimatableProperty::Camera_FOV: return "Camera.FOV";
        case AnimatableProperty::Camera_Letterbox: return "Camera.Letterbox";
        case AnimatableProperty::Camera_Pillarbox: return "Camera.Pillarbox";
        case AnimatableProperty::Camera_ShakeIntensity: return "Camera.ShakeIntensity";
        case AnimatableProperty::Light_Intensity: return "Light.Intensity";

        // UI
        case AnimatableProperty::UI_AnchoredPosX: return "UI.AnchoredPosition.X";
        case AnimatableProperty::UI_AnchoredPosY: return "UI.AnchoredPosition.Y";
        case AnimatableProperty::UI_Width: return "UI.Size.Width";
        case AnimatableProperty::UI_Height: return "UI.Size.Height";
        case AnimatableProperty::UI_Alpha: return "UI.Alpha";
        case AnimatableProperty::UI_ColorR: return "UI.Color.R";
        case AnimatableProperty::UI_ColorG: return "UI.Color.G";
        case AnimatableProperty::UI_ColorB: return "UI.Color.B";
        case AnimatableProperty::UI_ProgressValue: return "UI.ProgressValue";
        case AnimatableProperty::UI_FontSize: return "UI.FontSize";

        // Huesos / Esqueleto
        case AnimatableProperty::Bone_RotX: return "Bone.Rotation.X";
        case AnimatableProperty::Bone_RotY: return "Bone.Rotation.Y";
        case AnimatableProperty::Bone_RotZ: return "Bone.Rotation.Z";
        case AnimatableProperty::Bone_PosX: return "Bone.Position.X";
        case AnimatableProperty::Bone_PosY: return "Bone.Position.Y";
        case AnimatableProperty::Bone_PosZ: return "Bone.Position.Z";

        default: return "Custom";
    }
}

AnimatableProperty KeyframeClip::StringToProperty(const std::string& str) {
    if (str == "Transform.Position.X") return AnimatableProperty::Transform_PosX;
    if (str == "Transform.Position.Y") return AnimatableProperty::Transform_PosY;
    if (str == "Transform.Position.Z") return AnimatableProperty::Transform_PosZ;
    if (str == "Transform.Rotation.X") return AnimatableProperty::Transform_RotX;
    if (str == "Transform.Rotation.Y") return AnimatableProperty::Transform_RotY;
    if (str == "Transform.Rotation.Z") return AnimatableProperty::Transform_RotZ;
    if (str == "Transform.Scale.X") return AnimatableProperty::Transform_ScaleX;
    if (str == "Transform.Scale.Y") return AnimatableProperty::Transform_ScaleY;
    if (str == "Transform.Scale.Z") return AnimatableProperty::Transform_ScaleZ;
    if (str == "Camera.FOV") return AnimatableProperty::Camera_FOV;
    if (str == "Camera.Letterbox") return AnimatableProperty::Camera_Letterbox;
    if (str == "Camera.Pillarbox") return AnimatableProperty::Camera_Pillarbox;
    if (str == "Camera.ShakeIntensity") return AnimatableProperty::Camera_ShakeIntensity;
    if (str == "Light.Intensity") return AnimatableProperty::Light_Intensity;

    // UI
    if (str == "UI.AnchoredPosition.X") return AnimatableProperty::UI_AnchoredPosX;
    if (str == "UI.AnchoredPosition.Y") return AnimatableProperty::UI_AnchoredPosY;
    if (str == "UI.Size.Width") return AnimatableProperty::UI_Width;
    if (str == "UI.Size.Height") return AnimatableProperty::UI_Height;
    if (str == "UI.Alpha") return AnimatableProperty::UI_Alpha;
    if (str == "UI.Color.R") return AnimatableProperty::UI_ColorR;
    if (str == "UI.Color.G") return AnimatableProperty::UI_ColorG;
    if (str == "UI.Color.B") return AnimatableProperty::UI_ColorB;
    if (str == "UI.ProgressValue") return AnimatableProperty::UI_ProgressValue;
    if (str == "UI.FontSize") return AnimatableProperty::UI_FontSize;

    // Huesos
    if (str == "Bone.Rotation.X") return AnimatableProperty::Bone_RotX;
    if (str == "Bone.Rotation.Y") return AnimatableProperty::Bone_RotY;
    if (str == "Bone.Rotation.Z") return AnimatableProperty::Bone_RotZ;
    if (str == "Bone.Position.X") return AnimatableProperty::Bone_PosX;
    if (str == "Bone.Position.Y") return AnimatableProperty::Bone_PosY;
    if (str == "Bone.Position.Z") return AnimatableProperty::Bone_PosZ;

    return AnimatableProperty::Custom;
}

const char* KeyframeClip::GetPropertyDisplayName(AnimatableProperty prop) {
    switch (prop) {
        case AnimatableProperty::Transform_PosX: return "Posicion X";
        case AnimatableProperty::Transform_PosY: return "Posicion Y";
        case AnimatableProperty::Transform_PosZ: return "Posicion Z";
        case AnimatableProperty::Transform_RotX: return "Rotacion X";
        case AnimatableProperty::Transform_RotY: return "Rotacion Y";
        case AnimatableProperty::Transform_RotZ: return "Rotacion Z";
        case AnimatableProperty::Transform_ScaleX: return "Escala X";
        case AnimatableProperty::Transform_ScaleY: return "Escala Y";
        case AnimatableProperty::Transform_ScaleZ: return "Escala Z";
        case AnimatableProperty::Camera_FOV: return "Camara FOV";
        case AnimatableProperty::Camera_Letterbox: return "Camara Letterbox (Bordes H)";
        case AnimatableProperty::Camera_Pillarbox: return "Camara Pillarbox (Bordes V)";
        case AnimatableProperty::Camera_ShakeIntensity: return "Camara Shake";
        case AnimatableProperty::Light_Intensity: return "Luz Intensidad";

        case AnimatableProperty::UI_AnchoredPosX: return "UI: Posicion Anclada X";
        case AnimatableProperty::UI_AnchoredPosY: return "UI: Posicion Anclada Y";
        case AnimatableProperty::UI_Width: return "UI: Ancho";
        case AnimatableProperty::UI_Height: return "UI: Alto";
        case AnimatableProperty::UI_Alpha: return "UI: Alfa (Opacidad / Fade)";
        case AnimatableProperty::UI_ColorR: return "UI: Color R";
        case AnimatableProperty::UI_ColorG: return "UI: Color G";
        case AnimatableProperty::UI_ColorB: return "UI: Color B";
        case AnimatableProperty::UI_ProgressValue: return "UI: Progreso (Value)";
        case AnimatableProperty::UI_FontSize: return "UI: Tamano Fuente";

        case AnimatableProperty::Bone_RotX: return "Hueso: Rotacion X";
        case AnimatableProperty::Bone_RotY: return "Hueso: Rotacion Y";
        case AnimatableProperty::Bone_RotZ: return "Hueso: Rotacion Z";
        case AnimatableProperty::Bone_PosX: return "Hueso: Posicion X";
        case AnimatableProperty::Bone_PosY: return "Hueso: Posicion Y";
        case AnimatableProperty::Bone_PosZ: return "Hueso: Posicion Z";

        default: return "Personalizada";
    }
}

bool KeyframeClip::SaveToFile(const std::string& filePath) const {
    std::string dir = SlipFileSystem::GetParentPath(filePath);
    if (!dir.empty()) {
        SlipFileSystem::CreateDirectoryPath(dir);
    }

    std::ofstream out(filePath);
    if (!out.is_open()) {
        SlipConsole::LogError("Error al abrir archivo para guardar clip: " + filePath);
        return false;
    }

    out << "# SlipEngine Keyframe Animation Clip\n";
    out << "ClipName: " << name << "\n";
    out << "Duration: " << duration << "\n";
    out << "FrameRate: " << frameRate << "\n";
    out << "Loop: " << (loop ? 1 : 0) << "\n\n";

    for (const auto& grp : entityGroups) {
        out << "EntityGroup: " << grp.entityName << "\n";
        for (const auto& trk : grp.tracks) {
            out << "  Track: " << PropertyToString(trk.propertyType) << " Name: \"" << trk.name << "\"\n";
            for (const auto& k : trk.keyframes) {
                out << "    Key: " << std::fixed << std::setprecision(4) << k.time
                    << " " << std::setprecision(6) << k.value
                    << " " << static_cast<int>(k.interpolation) << "\n";
            }
        }
        out << "\n";
    }

    out.close();
    SlipConsole::LogSuccess("Clip de animacion guardado: " + filePath);
    return true;
}

bool KeyframeClip::LoadFromFile(const std::string& filePath) {
    std::ifstream in(filePath);
    if (!in.is_open()) {
        SlipConsole::LogError("No se pudo abrir el archivo de clip: " + filePath);
        return false;
    }

    entityGroups.clear();
    name = SlipFileSystem::GetFileNameWithoutExtension(filePath);

    std::string line;
    EntityAnimationTrackGroup* currentGroup = nullptr;
    KeyframeTrack* currentTrack = nullptr;

    while (std::getline(in, line)) {
        // Trim whitespace
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') continue;
        size_t last = line.find_last_not_of(" \t\r\n");
        std::string trimmed = line.substr(first, (last - first + 1));

        std::istringstream iss(trimmed);
        std::string tag;
        iss >> tag;

        if (tag == "ClipName:") {
            iss >> name;
        } else if (tag == "Duration:") {
            iss >> duration;
        } else if (tag == "FrameRate:") {
            iss >> frameRate;
        } else if (tag == "Loop:") {
            int l = 0; iss >> l; loop = (l != 0);
        } else if (tag == "EntityGroup:") {
            std::string grpName;
            iss >> grpName;
            entityGroups.emplace_back(grpName);
            currentGroup = &entityGroups.back();
            currentTrack = nullptr;
        } else if (tag == "Track:") {
            if (!currentGroup) continue;
            std::string propStr;
            iss >> propStr;
            AnimatableProperty prop = StringToProperty(propStr);

            std::string nameTag;
            std::string trkName = GetPropertyDisplayName(prop);
            if (iss >> nameTag && nameTag == "Name:") {
                std::string rest;
                std::getline(iss, rest);
                size_t q1 = rest.find('\"');
                size_t q2 = rest.rfind('\"');
                if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1) {
                    trkName = rest.substr(q1 + 1, q2 - q1 - 1);
                }
            }
            currentGroup->tracks.emplace_back(trkName, prop);
            currentTrack = &currentGroup->tracks.back();
        } else if (tag == "Key:") {
            if (!currentTrack) continue;
            float t = 0.0f, v = 0.0f;
            int interp = 1;
            if (iss >> t >> v) {
                if (!(iss >> interp)) interp = 1;
                currentTrack->keyframes.emplace_back(t, v, static_cast<KeyframeInterpolation>(interp));
            }
        }
    }

    in.close();
    for (auto& grp : entityGroups) {
        for (auto& trk : grp.tracks) {
            trk.SortKeyframes();
        }
    }

    SlipConsole::LogInfo("Clip cargado exitosamente: " + filePath + " (" + std::to_string(entityGroups.size()) + " grupos de pistas)");
    return true;
}

std::shared_ptr<AnimationClip> KeyframeClip::ToSkeletalAnimationClip() const {
    auto skeletalClip = std::make_shared<AnimationClip>();
    skeletalClip->name = name.empty() ? "CustomClip" : name;
    skeletalClip->duration = (duration > 0.0f) ? duration : 1.0f;
    skeletalClip->ticksPerSecond = (frameRate > 0.0f) ? frameRate : 30.0f;

    for (const auto& grp : entityGroups) {
        const KeyframeTrack* trkPosX = grp.GetTrack(AnimatableProperty::Bone_PosX);
        if (!trkPosX) trkPosX = grp.GetTrack(AnimatableProperty::Transform_PosX);
        const KeyframeTrack* trkPosY = grp.GetTrack(AnimatableProperty::Bone_PosY);
        if (!trkPosY) trkPosY = grp.GetTrack(AnimatableProperty::Transform_PosY);
        const KeyframeTrack* trkPosZ = grp.GetTrack(AnimatableProperty::Bone_PosZ);
        if (!trkPosZ) trkPosZ = grp.GetTrack(AnimatableProperty::Transform_PosZ);

        const KeyframeTrack* trkRotX = grp.GetTrack(AnimatableProperty::Bone_RotX);
        if (!trkRotX) trkRotX = grp.GetTrack(AnimatableProperty::Transform_RotX);
        const KeyframeTrack* trkRotY = grp.GetTrack(AnimatableProperty::Bone_RotY);
        if (!trkRotY) trkRotY = grp.GetTrack(AnimatableProperty::Transform_RotY);
        const KeyframeTrack* trkRotZ = grp.GetTrack(AnimatableProperty::Bone_RotZ);
        if (!trkRotZ) trkRotZ = grp.GetTrack(AnimatableProperty::Transform_RotZ);

        const KeyframeTrack* trkSclX = grp.GetTrack(AnimatableProperty::Transform_ScaleX);
        const KeyframeTrack* trkSclY = grp.GetTrack(AnimatableProperty::Transform_ScaleY);
        const KeyframeTrack* trkSclZ = grp.GetTrack(AnimatableProperty::Transform_ScaleZ);

        if (!trkPosX && !trkPosY && !trkPosZ && !trkRotX && !trkRotY && !trkRotZ && !trkSclX && !trkSclY && !trkSclZ) {
            continue;
        }

        AnimationTrack animTrack;
        animTrack.boneName = grp.entityName;

        std::vector<float> timeStamps;
        auto addTimeStamps = [&](const KeyframeTrack* t) {
            if (!t) return;
            for (const auto& kf : t->keyframes) {
                timeStamps.push_back(kf.time);
            }
        };
        addTimeStamps(trkPosX); addTimeStamps(trkPosY); addTimeStamps(trkPosZ);
        addTimeStamps(trkRotX); addTimeStamps(trkRotY); addTimeStamps(trkRotZ);
        addTimeStamps(trkSclX); addTimeStamps(trkSclY); addTimeStamps(trkSclZ);

        timeStamps.push_back(0.0f);
        if (duration > 0.0f) timeStamps.push_back(duration);

        std::sort(timeStamps.begin(), timeStamps.end());
        timeStamps.erase(std::unique(timeStamps.begin(), timeStamps.end(), [](float a, float b) {
            return std::abs(a - b) < 1e-4f;
        }), timeStamps.end());

        for (float t : timeStamps) {
            glm::vec3 pos(0.0f);
            if (trkPosX) pos.x = trkPosX->Evaluate(t);
            if (trkPosY) pos.y = trkPosY->Evaluate(t);
            if (trkPosZ) pos.z = trkPosZ->Evaluate(t);

            glm::vec3 rotEulerDeg(0.0f);
            if (trkRotX) rotEulerDeg.x = trkRotX->Evaluate(t);
            if (trkRotY) rotEulerDeg.y = trkRotY->Evaluate(t);
            if (trkRotZ) rotEulerDeg.z = trkRotZ->Evaluate(t);

            glm::quat q = glm::quat(glm::radians(rotEulerDeg));

            glm::vec3 scl(1.0f);
            if (trkSclX) scl.x = trkSclX->Evaluate(t);
            if (trkSclY) scl.y = trkSclY->Evaluate(t);
            if (trkSclZ) scl.z = trkSclZ->Evaluate(t);

            animTrack.positions.push_back(KeyPosition{pos, t});
            animTrack.rotations.push_back(KeyRotation{q, t});
            animTrack.scales.push_back(KeyScale{scl, t});
        }

        skeletalClip->AddTrack(animTrack);
    }

    return skeletalClip;
}

void KeyframeClip::FromSkeletalAnimationClip(const AnimationClip* skeletalClip) {
    if (!skeletalClip) return;

    name = skeletalClip->name;
    duration = skeletalClip->duration;
    frameRate = (skeletalClip->ticksPerSecond > 0.0f) ? skeletalClip->ticksPerSecond : 30.0f;
    entityGroups.clear();

    for (const auto& track : skeletalClip->tracks) {
        EntityAnimationTrackGroup& grp = GetOrCreateGroup(track.boneName);

        if (!track.positions.empty()) {
            grp.GetOrCreateTrack(AnimatableProperty::Bone_PosX, "Hueso: Posicion X");
            grp.GetOrCreateTrack(AnimatableProperty::Bone_PosY, "Hueso: Posicion Y");
            grp.GetOrCreateTrack(AnimatableProperty::Bone_PosZ, "Hueso: Posicion Z");

            KeyframeTrack* tx = grp.GetTrack(AnimatableProperty::Bone_PosX);
            KeyframeTrack* ty = grp.GetTrack(AnimatableProperty::Bone_PosY);
            KeyframeTrack* tz = grp.GetTrack(AnimatableProperty::Bone_PosZ);

            for (const auto& kp : track.positions) {
                if (tx) tx->AddKeyframe(kp.timeStamp, kp.position.x, KeyframeInterpolation::Smooth);
                if (ty) ty->AddKeyframe(kp.timeStamp, kp.position.y, KeyframeInterpolation::Smooth);
                if (tz) tz->AddKeyframe(kp.timeStamp, kp.position.z, KeyframeInterpolation::Smooth);
            }
        }

        if (!track.rotations.empty()) {
            grp.GetOrCreateTrack(AnimatableProperty::Bone_RotX, "Hueso: Rotacion X");
            grp.GetOrCreateTrack(AnimatableProperty::Bone_RotY, "Hueso: Rotacion Y");
            grp.GetOrCreateTrack(AnimatableProperty::Bone_RotZ, "Hueso: Rotacion Z");

            KeyframeTrack* rx = grp.GetTrack(AnimatableProperty::Bone_RotX);
            KeyframeTrack* ry = grp.GetTrack(AnimatableProperty::Bone_RotY);
            KeyframeTrack* rz = grp.GetTrack(AnimatableProperty::Bone_RotZ);

            for (const auto& kr : track.rotations) {
                glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(kr.orientation));
                if (rx) rx->AddKeyframe(kr.timeStamp, eulerDeg.x, KeyframeInterpolation::Smooth);
                if (ry) ry->AddKeyframe(kr.timeStamp, eulerDeg.y, KeyframeInterpolation::Smooth);
                if (rz) rz->AddKeyframe(kr.timeStamp, eulerDeg.z, KeyframeInterpolation::Smooth);
            }
        }

        if (!track.scales.empty()) {
            grp.GetOrCreateTrack(AnimatableProperty::Transform_ScaleX, "Escala X");
            grp.GetOrCreateTrack(AnimatableProperty::Transform_ScaleY, "Escala Y");
            grp.GetOrCreateTrack(AnimatableProperty::Transform_ScaleZ, "Escala Z");

            KeyframeTrack* sx = grp.GetTrack(AnimatableProperty::Transform_ScaleX);
            KeyframeTrack* sy = grp.GetTrack(AnimatableProperty::Transform_ScaleY);
            KeyframeTrack* sz = grp.GetTrack(AnimatableProperty::Transform_ScaleZ);

            for (const auto& ks : track.scales) {
                if (sx) sx->AddKeyframe(ks.timeStamp, ks.scale.x, KeyframeInterpolation::Smooth);
                if (sy) sy->AddKeyframe(ks.timeStamp, ks.scale.y, KeyframeInterpolation::Smooth);
                if (sz) sz->AddKeyframe(ks.timeStamp, ks.scale.z, KeyframeInterpolation::Smooth);
            }
        }
    }
}

