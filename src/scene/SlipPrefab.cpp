#include "SlipEngine/scene/SlipPrefab.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/render/SlipAnimatorController.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/core/SlipConsole.h"
#include <sstream>
#include <iostream>
#include <algorithm>
#include <map>
#include <utility>
#include <cctype>
#include <cstdio>

static void SerializeEntityRecursive(SlipEntity* ent, std::stringstream& ss, int& tempIdCounter, std::map<SlipEntity*, int>& entityToId, int parentId) {
    if (!ent) return;

    int currentId = ++tempIdCounter;
    entityToId[ent] = currentId;

    glm::vec3 entPos = ent->position;
    glm::vec3 entRot = ent->rotation;
    glm::vec3 entScale = ent->scale;
    if (auto tc = ent->GetComponent<TransformComponent>()) {
        entPos = tc->position;
        entRot = tc->rotation;
        entScale = tc->scale;
    }

    ss << "Entity:\n";
    ss << "  Id: " << currentId << "\n";
    ss << "  Name: " << ent->GetName() << "\n";
    ss << "  Position: " << entPos.x << " " << entPos.y << " " << entPos.z << "\n";
    ss << "  Rotation: " << entRot.x << " " << entRot.y << " " << entRot.z << "\n";
    ss << "  Scale: " << entScale.x << " " << entScale.y << " " << entScale.z << "\n";
    if (auto tc = ent->GetComponent<TransformComponent>()) {
        if (!tc->inheritScale) {
            ss << "  InheritScale: 0\n";
        }
    }
    if (parentId > 0) {
        ss << "  ParentId: " << parentId << "\n";
    }

    // MeshComponent
    if (auto meshComp = ent->GetComponent<MeshComponent>()) {
        ss << "  MeshComponent:\n";
        ss << "    ModelPath: " << meshComp->modelPath << "\n";
        std::string modelUid = SlipFileSystem::GetGuidForAsset(meshComp->modelPath);
        if (!modelUid.empty()) ss << "    ModelUID: " << modelUid << "\n";
        ss << "    MaterialPath: " << meshComp->materialPath << "\n";
        std::string matUid = SlipFileSystem::GetGuidForAsset(meshComp->materialPath);
        if (!matUid.empty()) ss << "    MaterialUID: " << matUid << "\n";
        for (size_t sIdx = 0; sIdx < meshComp->GetMaterialSlotCount(); ++sIdx) {
            ss << "    SubMaterial: " << sIdx << " " << meshComp->GetSubmeshMaterialPath(sIdx) << "\n";
        }
    }

    // SkinnedMeshComponent
    if (auto skinnedComp = ent->GetComponent<SkinnedMeshComponent>()) {
        ss << "  SkinnedMeshComponent:\n";
        ss << "    ModelPath: " << skinnedComp->modelPath << "\n";
        std::string modelUid = SlipFileSystem::GetGuidForAsset(skinnedComp->modelPath);
        if (!modelUid.empty()) ss << "    ModelUID: " << modelUid << "\n";
        ss << "    MaterialPath: " << skinnedComp->materialPath << "\n";
        std::string matUid = SlipFileSystem::GetGuidForAsset(skinnedComp->materialPath);
        if (!matUid.empty()) ss << "    MaterialUID: " << matUid << "\n";
        ss << "    CastShadows: " << (skinnedComp->castShadows ? "1" : "0") << "\n";
        for (size_t sIdx = 0; sIdx < skinnedComp->GetMaterialSlotCount(); ++sIdx) {
            ss << "    SubMaterial: " << sIdx << " " << skinnedComp->GetSubmeshMaterialPath(sIdx) << "\n";
        }
    }

    // AnimatorComponent
    if (auto animComp = ent->GetComponent<AnimatorComponent>()) {
        ss << "  AnimatorComponent:\n";
        ss << "    CurrentClipIndex: " << animComp->currentClipIndex << "\n";
        AnimationClip* curClip = animComp->GetCurrentClip();
        ss << "    CurrentClipName: " << (curClip ? curClip->name : "") << "\n";
        ss << "    PlaybackSpeed: " << animComp->playbackSpeed << "\n";
        ss << "    IsPlaying: " << (animComp->isPlaying ? "1" : "0") << "\n";
        ss << "    IsLooping: " << (animComp->isLooping ? "1" : "0") << "\n";
        ss << "    CurrentTime: " << animComp->currentTime << "\n";
        ss << "    ControllerPath: " << animComp->controllerPath << "\n";
        if (animComp->controller && !animComp->controllerPath.empty()) {
            animComp->controller->SaveToFile(animComp->controllerPath);
        }
    }

    // BoneComponent
    if (auto boneComp = ent->GetComponent<BoneComponent>()) {
        ss << "  BoneComponent:\n";
        ss << "    BoneIndex: " << boneComp->boneIndex << "\n";
        ss << "    BoneName: " << boneComp->boneName << "\n";
    }

    // LightComponent
    if (auto lightComp = ent->GetComponent<LightComponent>()) {
        ss << "  LightComponent:\n";
        std::string lTypeStr = "Point";
        if (lightComp->type == LightType::Directional) lTypeStr = "Directional";
        else if (lightComp->type == LightType::Spot) lTypeStr = "Spot";
        ss << "    Type: " << lTypeStr << "\n";
        ss << "    Color: " << lightComp->color.r << " " << lightComp->color.g << " " << lightComp->color.b << "\n";
        ss << "    Intensity: " << lightComp->intensity << "\n";
        ss << "    Range: " << lightComp->range << "\n";
        ss << "    SpotAngle: " << lightComp->spotAngle << "\n";
        ss << "    CastShadows: " << (lightComp->castShadows ? "1" : "0") << "\n";
    }

    if (auto ddgi = ent->GetComponent<DDGIVolumeComponent>()) {
        ss << "  DDGIVolumeComponent:\n";
        ss << "    Size: " << ddgi->size.x << " " << ddgi->size.y << " " << ddgi->size.z << "\n";
        ss << "    ProbesX: " << ddgi->probesX << "\n";
        ss << "    ProbesY: " << ddgi->probesY << "\n";
        ss << "    ProbesZ: " << ddgi->probesZ << "\n";
        ss << "    Preview: " << (ddgi->previewProbes ? 1 : 0) << "\n";
    }

    if (auto probe = ent->GetComponent<ReflectionProbeComponent>()) {
        ss << "  ReflectionProbeComponent:\n";
        ss << "    Size: " << probe->size.x << " " << probe->size.y << " " << probe->size.z << "\n";
        ss << "    Resolution: " << probe->resolution << "\n";
        ss << "    Intensity: " << probe->intensity << "\n";
        ss << "    RoughnessCutoff: " << probe->roughnessCutoff << "\n";
        ss << "    BoxProjection: " << (probe->boxProjection ? 1 : 0) << "\n";
        ss << "    IncludeDynamic: " << (probe->includeDynamic ? 1 : 0) << "\n";
    }

    if (auto post = ent->GetComponent<PostProcessVolumeComponent>()) {
        post->SerializeTo(ss);
    }

    // Colliders
    if (auto boxCol = ent->GetComponent<BoxColliderComponent>()) {
        ss << "  BoxColliderComponent:\n";
        ss << "    Size: " << boxCol->size.x << " " << boxCol->size.y << " " << boxCol->size.z << "\n";
        ss << "    CenterOffset: " << boxCol->centerOffset.x << " " << boxCol->centerOffset.y << " " << boxCol->centerOffset.z << "\n";
        ss << "    IsTrigger: " << (boxCol->isTrigger ? 1 : 0) << "\n";
    }
    if (auto sphereCol = ent->GetComponent<SphereColliderComponent>()) {
        ss << "  SphereColliderComponent:\n";
        ss << "    Radius: " << sphereCol->radius << "\n";
        ss << "    CenterOffset: " << sphereCol->centerOffset.x << " " << sphereCol->centerOffset.y << " " << sphereCol->centerOffset.z << "\n";
        ss << "    IsTrigger: " << (sphereCol->isTrigger ? 1 : 0) << "\n";
    }
    if (auto capCol = ent->GetComponent<CapsuleColliderComponent>()) {
        ss << "  CapsuleColliderComponent:\n";
        ss << "    Radius: " << capCol->radius << "\n";
        ss << "    Height: " << capCol->height << "\n";
        ss << "    CenterOffset: " << capCol->centerOffset.x << " " << capCol->centerOffset.y << " " << capCol->centerOffset.z << "\n";
        ss << "    IsTrigger: " << (capCol->isTrigger ? 1 : 0) << "\n";
    }
    if (auto meshCol = ent->GetComponent<MeshColliderComponent>()) {
        ss << "  MeshColliderComponent:\n";
        ss << "    IsConvex: " << (meshCol->isConvex ? 1 : 0) << "\n";
        ss << "    CenterOffset: " << meshCol->centerOffset.x << " " << meshCol->centerOffset.y << " " << meshCol->centerOffset.z << "\n";
        ss << "    IsTrigger: " << (meshCol->isTrigger ? 1 : 0) << "\n";
    }

    // RigidBodyComponent
    if (auto rbComp = ent->GetComponent<RigidBodyComponent>()) {
        ss << "  RigidBodyComponent:\n";
        ss << "    Mass: " << rbComp->mass << "\n";
        ss << "    LinearDrag: " << rbComp->linearDrag << "\n";
        ss << "    AngularDrag: " << rbComp->angularDrag << "\n";
        ss << "    IsKinematic: " << (rbComp->isKinematic ? 1 : 0) << "\n";
        ss << "    UseGravity: " << (rbComp->useGravity ? 1 : 0) << "\n";
        ss << "    Friction: " << rbComp->friction << "\n";
        ss << "    RollingFriction: " << rbComp->rollingFriction << "\n";
        ss << "    Restitution: " << rbComp->restitution << "\n";
        ss << "    FreezePosition: " << (rbComp->freezePositionX ? 1 : 0) << " " << (rbComp->freezePositionY ? 1 : 0) << " " << (rbComp->freezePositionZ ? 1 : 0) << "\n";
        ss << "    FreezeRotation: " << (rbComp->freezeRotationX ? 1 : 0) << " " << (rbComp->freezeRotationY ? 1 : 0) << " " << (rbComp->freezeRotationZ ? 1 : 0) << "\n";
    }

    // VehicleComponent
    if (auto vehComp = ent->GetComponent<VehicleComponent>()) {
        ss << "  VehicleComponent:\n";
        ss << "    SuspensionStiffness: " << vehComp->suspensionStiffness << "\n";
        ss << "    SuspensionDamping: " << vehComp->suspensionDamping << "\n";
        ss << "    SuspensionCompression: " << vehComp->suspensionCompression << "\n";
        ss << "    MaxSuspensionTravelCm: " << vehComp->maxSuspensionTravelCm << "\n";
        ss << "    FrictionSlip: " << vehComp->frictionSlip << "\n";
        ss << "    MaxSuspensionForce: " << vehComp->maxSuspensionForce << "\n";
        ss << "    RollInfluence: " << vehComp->rollInfluence << "\n";
        ss << "    ChassisDimensions: " << vehComp->chassisWidth << " " << vehComp->chassisHeight << " " << vehComp->chassisLength << " " << vehComp->defaultWheelRadius << "\n";
    }

    // ScriptComponents (soporte de múltiples scripts y sus variables públicas expuestas)
    for (auto scriptComp : ent->GetScripts()) {
        if (!scriptComp) continue;
        ss << "  ScriptComponent:\n";
        ss << "    ScriptPath: " << scriptComp->scriptPath << "\n";
        ss << "    Enabled: " << (scriptComp->IsEnabled() ? 1 : 0) << "\n";
        for (const auto& prop : scriptComp->GetProperties()) {
            ss << "    Property: " << prop.name << " ";
            switch (prop.type) {
                case ScriptPropertyType::Float:
                    ss << "float " << prop.floatValue << "\n";
                    break;
                case ScriptPropertyType::Int:
                    ss << "int " << prop.intValue << "\n";
                    break;
                case ScriptPropertyType::Bool:
                    ss << "bool " << (prop.boolValue ? 1 : 0) << "\n";
                    break;
                case ScriptPropertyType::String:
                    ss << "string " << prop.stringValue << "\n";
                    break;
                case ScriptPropertyType::Vec3:
                    ss << "vec3 " << prop.vec3Value.x << " " << prop.vec3Value.y << " " << prop.vec3Value.z << "\n";
                    break;
            }
        }
    }

    // CameraComponent
    if (auto camComp = ent->GetComponent<CameraComponent>()) {
        ss << "  CameraComponent:\n";
        ss << "    Projection: " << (camComp->projectionType == CameraProjection::Orthographic ? "Orthographic" : "Perspective") << "\n";
        ss << "    FOV: " << camComp->fov << "\n";
        ss << "    NearClip: " << camComp->nearClip << "\n";
        ss << "    FarClip: " << camComp->farClip << "\n";
        ss << "    OrthoSize: " << camComp->orthoSize << "\n";
        ss << "    IsPrimary: " << (camComp->isPrimary ? 1 : 0) << "\n";
        ss << "    ClearColor: " << camComp->clearColor.r << " " << camComp->clearColor.g << " " << camComp->clearColor.b << " " << camComp->clearColor.a << "\n";
        ss << "    Letterbox: " << camComp->letterbox << "\n";
        ss << "    Pillarbox: " << camComp->pillarbox << "\n";
    }

    // CinematicCameraComponent
    if (auto ccamComp = ent->GetComponent<CinematicCameraComponent>()) {
        ss << "  CinematicCameraComponent:\n";
        ss << "    Target: " << ccamComp->targetEntityName << "\n";
        ss << "    Distance: " << ccamComp->targetDistance << "\n";
        ss << "    Height: " << ccamComp->targetHeight << "\n";
        ss << "    Pitch: " << ccamComp->targetPitch << "\n";
        ss << "    PositionLag: " << ccamComp->positionLag << "\n";
        ss << "    RotationLag: " << ccamComp->rotationLag << "\n";
        ss << "    DriftCompensation: " << ccamComp->driftCompensation << "\n";
        ss << "    DynamicFOV: " << (ccamComp->enableDynamicFOV ? 1 : 0) << "\n";
        ss << "    BaseFOV: " << ccamComp->baseFOV << "\n";
        ss << "    BoostFOV: " << ccamComp->boostFOV << "\n";
    }

    // KeyframeAnimationComponent
    if (auto kfaComp = ent->GetComponent<KeyframeAnimationComponent>()) {
        ss << "  KeyframeAnimationComponent:\n";
        ss << "    ClipPath: " << kfaComp->clipPath << "\n";
        ss << "    PlayOnStart: " << (kfaComp->playOnStart ? 1 : 0) << "\n";
        ss << "    Loop: " << (kfaComp->loop ? 1 : 0) << "\n";
        ss << "    Speed: " << kfaComp->playbackSpeed << "\n";
    }

    if (auto spriteComp = ent->GetComponent<SpriteRendererComponent>()) {
        ss << "  SpriteRendererComponent:\n";
        ss << "    TexturePath: " << spriteComp->texturePath << "\n";
        ss << "    Color: " << spriteComp->color.r << " " << spriteComp->color.g << " " << spriteComp->color.b << " " << spriteComp->color.a << "\n";
        ss << "    FlipX: " << (spriteComp->flipX ? 1 : 0) << "\n";
        ss << "    FlipY: " << (spriteComp->flipY ? 1 : 0) << "\n";
        ss << "    SortingOrder: " << spriteComp->sortingOrder << "\n";
        ss << "    SortingLayer: " << spriteComp->sortingLayerName << "\n";
        ss << "    PixelsPerUnit: " << spriteComp->pixelsPerUnit << "\n";
        ss << "    HFrames: " << spriteComp->hFrames << "\n";
        ss << "    VFrames: " << spriteComp->vFrames << "\n";
        ss << "    CurrentFrame: " << spriteComp->currentFrame << "\n";
        ss << "    IsLit: " << (spriteComp->isLit ? 1 : 0) << "\n";
        ss << "    IsPixelArt: " << (spriteComp->isPixelArt ? 1 : 0) << "\n";
        ss << "    NormalMapPath: " << spriteComp->normalMapPath << "\n";
    }

    if (auto saComp = ent->GetComponent<SpriteAnimatorComponent>()) {
        ss << "  SpriteAnimatorComponent:\n";
        ss << "    PlayOnStart: " << (saComp->playOnStart ? 1 : 0) << "\n";
        ss << "    DefaultClip: " << saComp->defaultClip << "\n";
        ss << "    ClipCount: " << saComp->clips.size() << "\n";
        for (size_t ci = 0; ci < saComp->clips.size(); ++ci) {
            const auto& c = saComp->clips[ci];
            ss << "    ClipData: " << c.name << " " << c.startFrame << " " << c.endFrame << " " << c.frameRate << " " << (c.loop ? 1 : 0) << "\n";
        }
    }

    if (auto box2d = ent->GetComponent<BoxCollider2DComponent>()) {
        ss << "  BoxCollider2DComponent:\n";
        ss << "    Size: " << box2d->size.x << " " << box2d->size.y << "\n";
        ss << "    Offset: " << box2d->offset.x << " " << box2d->offset.y << "\n";
        ss << "    IsTrigger: " << (box2d->isTrigger ? 1 : 0) << "\n";
        ss << "    Density: " << box2d->density << "\n";
        ss << "    Friction: " << box2d->friction << "\n";
        ss << "    Restitution: " << box2d->restitution << "\n";
    }

    if (auto circ2d = ent->GetComponent<CircleCollider2DComponent>()) {
        ss << "  CircleCollider2DComponent:\n";
        ss << "    Radius: " << circ2d->radius << "\n";
        ss << "    Offset: " << circ2d->offset.x << " " << circ2d->offset.y << "\n";
        ss << "    IsTrigger: " << (circ2d->isTrigger ? 1 : 0) << "\n";
        ss << "    Density: " << circ2d->density << "\n";
        ss << "    Friction: " << circ2d->friction << "\n";
        ss << "    Restitution: " << circ2d->restitution << "\n";
    }

    if (auto rb2d = ent->GetComponent<RigidBody2DComponent>()) {
        ss << "  RigidBody2DComponent:\n";
        ss << "    BodyType: " << static_cast<int>(rb2d->bodyType) << "\n";
        ss << "    Mass: " << rb2d->mass << "\n";
        ss << "    GravityScale: " << rb2d->gravityScale << "\n";
        ss << "    FreezeRotation: " << (rb2d->freezeRotation ? 1 : 0) << "\n";
        ss << "    LinearDamping: " << rb2d->linearDamping << "\n";
        ss << "    AngularDamping: " << rb2d->angularDamping << "\n";
    }

    if (auto l2d = ent->GetComponent<Light2DComponent>()) {
        ss << "  Light2DComponent:\n";
        ss << "    LightType: " << static_cast<int>(l2d->lightType) << "\n";
        ss << "    Color: " << l2d->color.r << " " << l2d->color.g << " " << l2d->color.b << "\n";
        ss << "    Intensity: " << l2d->intensity << "\n";
        ss << "    Radius: " << l2d->radius << "\n";
        ss << "    InnerRadius: " << l2d->innerRadius << "\n";
        ss << "    SpotAngle: " << l2d->spotAngle << "\n";
        ss << "    CastShadows: " << (l2d->castShadows ? 1 : 0) << "\n";
        ss << "    ShadowIntensity: " << l2d->shadowIntensity << "\n";
    }

    if (auto sc2d = ent->GetComponent<ShadowCaster2DComponent>()) {
        ss << "  ShadowCaster2DComponent:\n";
        ss << "    Shape: " << static_cast<int>(sc2d->shape) << "\n";
        ss << "    Size: " << sc2d->size.x << " " << sc2d->size.y << "\n";
        ss << "    Radius: " << sc2d->radius << "\n";
        ss << "    Offset: " << sc2d->offset.x << " " << sc2d->offset.y << "\n";
        ss << "    CastShadows: " << (sc2d->castShadows ? 1 : 0) << "\n";
    }

    if (auto tgComp = ent->GetComponent<TileGridComponent>()) {
        ss << "  TileGridComponent:\n";
        ss << "    CellSize: " << tgComp->cellSize.x << " " << tgComp->cellSize.y << "\n";
        ss << "    Anchor: " << static_cast<int>(tgComp->anchor) << "\n";
        ss << "    TileSheetPath: " << tgComp->tileSheetPath << "\n";
        ss << "    SheetHFrames: " << tgComp->sheetHFrames << "\n";
        ss << "    SheetVFrames: " << tgComp->sheetVFrames << "\n";
        ss << "    SortingLayer: " << tgComp->sortingLayerName << "\n";
        ss << "    SortingOrder: " << tgComp->orderInLayer << "\n";
        ss << "    IsPixelArt: " << (tgComp->isPixelArt ? 1 : 0) << "\n";
        ss << "    TileCount: " << tgComp->tiles.size() << "\n";
        for (const auto& kv : tgComp->tiles) {
            ss << "    TileData: " << kv.first.first << " " << kv.first.second << " " << kv.second << "\n";
        }
    }

    if (auto canvasComp = ent->GetComponent<CanvasComponent>()) {
        ss << "  CanvasComponent:\n";
        ss << "    RenderMode: " << (canvasComp->renderMode == CanvasRenderMode::WorldSpace ? "WorldSpace" : "ScreenSpaceOverlay") << "\n";
        ss << "    ReferenceResolution: " << canvasComp->referenceResolution.x << " " << canvasComp->referenceResolution.y << "\n";
        ss << "    ScaleWithScreenSize: " << (canvasComp->scaleWithScreenSize ? 1 : 0) << "\n";
        ss << "    SortingOrder: " << canvasComp->sortingOrder << "\n";
    }

    if (auto rectComp = ent->GetComponent<RectTransformComponent>()) {
        ss << "  RectTransformComponent:\n";
        ss << "    AnchorPreset: " << rectComp->GetAnchorPresetName() << "\n";
        ss << "    AnchoredPosition: " << rectComp->anchoredPosition.x << " " << rectComp->anchoredPosition.y << "\n";
        ss << "    Size: " << rectComp->size.x << " " << rectComp->size.y << "\n";
        ss << "    Pivot: " << rectComp->pivot.x << " " << rectComp->pivot.y << "\n";
    }

    if (auto uiImg = ent->GetComponent<UIImageComponent>()) {
        ss << "  UIImageComponent:\n";
        ss << "    TexturePath: " << uiImg->texturePath << "\n";
        ss << "    Color: " << uiImg->color.r << " " << uiImg->color.g << " " << uiImg->color.b << " " << uiImg->color.a << "\n";
        ss << "    PreserveAspect: " << (uiImg->preserveAspect ? 1 : 0) << "\n";
    }

    if (auto uiTxt = ent->GetComponent<UITextComponent>()) {
        ss << "  UITextComponent:\n";
        ss << "    Text: " << uiTxt->text << "\n";
        ss << "    FontPath: " << uiTxt->fontPath << "\n";
        ss << "    FontSize: " << uiTxt->fontSize << "\n";
        ss << "    Color: " << uiTxt->color.r << " " << uiTxt->color.g << " " << uiTxt->color.b << " " << uiTxt->color.a << "\n";
        ss << "    AlignH: " << static_cast<int>(uiTxt->alignH) << "\n";
        ss << "    AlignV: " << static_cast<int>(uiTxt->alignV) << "\n";
    }

    if (auto uiBtn = ent->GetComponent<UIButtonComponent>()) {
        ss << "  UIButtonComponent:\n";
        ss << "    Interactable: " << (uiBtn->interactable ? 1 : 0) << "\n";
        ss << "    NormalColor: " << uiBtn->normalColor.r << " " << uiBtn->normalColor.g << " " << uiBtn->normalColor.b << " " << uiBtn->normalColor.a << "\n";
        ss << "    HoverColor: " << uiBtn->hoverColor.r << " " << uiBtn->hoverColor.g << " " << uiBtn->hoverColor.b << " " << uiBtn->hoverColor.a << "\n";
        ss << "    PressedColor: " << uiBtn->pressedColor.r << " " << uiBtn->pressedColor.g << " " << uiBtn->pressedColor.b << " " << uiBtn->pressedColor.a << "\n";
        ss << "    DisabledColor: " << uiBtn->disabledColor.r << " " << uiBtn->disabledColor.g << " " << uiBtn->disabledColor.b << " " << uiBtn->disabledColor.a << "\n";
        ss << "    TargetEntity: " << uiBtn->targetEntityName << "\n";
        ss << "    OnClickFunction: " << uiBtn->onClickFunction << "\n";
    }

    if (auto uiSld = ent->GetComponent<UISliderComponent>()) {
        ss << "  UISliderComponent:\n";
        ss << "    Interactable: " << (uiSld->interactable ? 1 : 0) << "\n";
        ss << "    MinValue: " << uiSld->minValue << "\n";
        ss << "    MaxValue: " << uiSld->maxValue << "\n";
        ss << "    Value: " << uiSld->value << "\n";
        ss << "    WholeNumbers: " << (uiSld->wholeNumbers ? 1 : 0) << "\n";
        ss << "    BackgroundColor: " << uiSld->backgroundColor.r << " " << uiSld->backgroundColor.g << " " << uiSld->backgroundColor.b << " " << uiSld->backgroundColor.a << "\n";
        ss << "    FillColor: " << uiSld->fillColor.r << " " << uiSld->fillColor.g << " " << uiSld->fillColor.b << " " << uiSld->fillColor.a << "\n";
        ss << "    HandleColor: " << uiSld->handleColor.r << " " << uiSld->handleColor.g << " " << uiSld->handleColor.b << " " << uiSld->handleColor.a << "\n";
        ss << "    TargetEntity: " << uiSld->targetEntityName << "\n";
        ss << "    OnValueChangedFunction: " << uiSld->onValueChangedFunction << "\n";
    }

    if (auto uiPb = ent->GetComponent<UIProgressBarComponent>()) {
        ss << "  UIProgressBarComponent:\n";
        ss << "    MinValue: " << uiPb->minValue << "\n";
        ss << "    MaxValue: " << uiPb->maxValue << "\n";
        ss << "    Value: " << uiPb->value << "\n";
        ss << "    BackgroundColor: " << uiPb->backgroundColor.r << " " << uiPb->backgroundColor.g << " " << uiPb->backgroundColor.b << " " << uiPb->backgroundColor.a << "\n";
        ss << "    FillColor: " << uiPb->fillColor.r << " " << uiPb->fillColor.g << " " << uiPb->fillColor.b << " " << uiPb->fillColor.a << "\n";
        ss << "    ShowPercentageText: " << (uiPb->showPercentageText ? 1 : 0) << "\n";
    }

    if (auto joy = ent->GetComponent<UIJoystickComponent>()) {
        ss << "  UIJoystickComponent:\n";
        ss << "    StickName: " << joy->stickName << "\n";
        ss << "    Mode: " << static_cast<int>(joy->mode) << "\n";
        ss << "    TravelRadius: " << joy->travelRadius << "\n";
        ss << "    ActivationRadius: " << joy->activationRadius << "\n";
        ss << "    BackgroundColor: " << joy->backgroundColor.r << " " << joy->backgroundColor.g << " " << joy->backgroundColor.b << " " << joy->backgroundColor.a << "\n";
        ss << "    KnobColor: " << joy->knobColor.r << " " << joy->knobColor.g << " " << joy->knobColor.b << " " << joy->knobColor.a << "\n";
        ss << "    Interactable: " << (joy->interactable ? 1 : 0) << "\n";
    }

    if (auto actionBtn = ent->GetComponent<UIActionButtonComponent>()) {
        ss << "  UIActionButtonComponent:\n";
        ss << "    ActionName: " << actionBtn->actionName << "\n";
        ss << "    Interactable: " << (actionBtn->interactable ? 1 : 0) << "\n";
        ss << "    NormalColor: " << actionBtn->normalColor.r << " " << actionBtn->normalColor.g << " " << actionBtn->normalColor.b << " " << actionBtn->normalColor.a << "\n";
        ss << "    PressedColor: " << actionBtn->pressedColor.r << " " << actionBtn->pressedColor.g << " " << actionBtn->pressedColor.b << " " << actionBtn->pressedColor.a << "\n";
    }

    if (auto ps = ent->GetComponent<ParticleSystemComponent>()) {
        ps->SerializeTo(ss);
    }

    // Serializar recursivamente a todos los hijos
    for (SlipEntity* child : ent->GetChildren()) {
        if (child) {
            SerializeEntityRecursive(child, ss, tempIdCounter, entityToId, currentId);
        }
    }
}

std::string SlipPrefab::SerializeEntityToString(SlipEntity* root) {
    if (!root) return "";
    std::stringstream ss;
    ss << "# SlipEngine Prefab File\n";
    ss << "Version: 1\n\n";

    int tempIdCounter = 0;
    std::map<SlipEntity*, int> entityToId;
    SerializeEntityRecursive(root, ss, tempIdCounter, entityToId, 0);

    return ss.str();
}

SlipEntity* SlipPrefab::DeserializeEntityFromString(const std::string& data, SlipLevel* level) {
    if (data.empty()) return nullptr;

    std::stringstream ss(data);
    std::string line;

    struct SerializedScript {
        std::string path;
        bool enabled = true;
        std::vector<ScriptProperty> properties;
    };

    struct EntityData {
        int id = 0;
        int parentId = 0;
        std::string name = "Entity";
        glm::vec3 pos = glm::vec3(0.0f);
        glm::vec3 rot = glm::vec3(0.0f);
        glm::vec3 scale = glm::vec3(1.0f);
        bool inheritScale = true;

        bool hasMesh = false;
        std::string meshPath;
        std::string matPath;
        std::vector<std::pair<size_t, std::string>> subMaterials;

        bool hasSkinned = false;
        std::string skinnedModelPath;
        std::string skinnedMatPath;
        bool skinnedCastShadows = true;
        std::vector<std::pair<size_t, std::string>> skinnedSubMaterials;

        bool hasAnimator = false;
        int animClipIdx = 0;
        std::string animClipName;
        float animSpeed = 1.0f;
        bool animPlaying = true;
        bool animLooping = true;
        float animTime = 0.0f;
        std::string animControllerPath;

        bool hasBone = false;
        int boneIdx = -1;
        std::string boneName;

        bool hasLight = false;
        LightType lightType = LightType::Point;
        glm::vec3 lightColor = glm::vec3(1.0f);
        float lightIntensity = 1.0f;
        float lightRange = 10.0f;
        float lightSpotAngle = 45.0f;
        bool lightCastShadows = true;

        bool hasDDGI = false;
        glm::vec3 ddgiSize = glm::vec3(24.0f, 12.0f, 24.0f);
        int ddgiProbesX = 8;
        int ddgiProbesY = 4;
        int ddgiProbesZ = 8;
        bool ddgiPreview = true;

        bool hasReflectionProbe = false;
        glm::vec3 probeSize = glm::vec3(16.0f, 8.0f, 16.0f);
        int probeResolution = 256;
        float probeIntensity = 1.0f;
        float probeRoughnessCutoff = 0.5f;
        bool probeBoxProjection = true;
        bool probeIncludeDynamic = false;

        bool hasPost = false;
        std::vector<std::pair<std::string, std::string>> postKeys;

        bool hasBoxCol = false;
        glm::vec3 boxSize = glm::vec3(1.0f);
        glm::vec3 boxOffset = glm::vec3(0.0f);
        bool boxTrigger = false;

        bool hasSphereCol = false;
        float sphereRadius = 0.5f;
        glm::vec3 sphereOffset = glm::vec3(0.0f);
        bool sphereTrigger = false;

        bool hasCapCol = false;
        float capRadius = 0.5f;
        float capHeight = 2.0f;
        glm::vec3 capOffset = glm::vec3(0.0f);
        bool capTrigger = false;

        bool hasMeshCol = false;
        bool meshConvex = false;
        glm::vec3 meshOffset = glm::vec3(0.0f);
        bool meshTrigger = false;

        bool hasRb = false;
        float rbMass = 1.0f;
        float rbLinearDrag = 0.0f;
        float rbAngularDrag = 0.05f;
        bool rbKinematic = false;
        bool rbUseGravity = true;
        float rbFriction = 0.5f;
        float rbRollingFriction = 0.0f;
        float rbRestitution = 0.0f;
        bool rbFreezePosX = false, rbFreezePosY = false, rbFreezePosZ = false;
        bool rbFreezeRotX = false, rbFreezeRotY = false, rbFreezeRotZ = false;

        // Vehicle
        bool hasVehicle = false;
        float vehStiffness = 20.0f;
        float vehDamping = 2.3f;
        float vehCompression = 4.4f;
        float vehMaxTravel = 500.0f;
        float vehFrictionSlip = 10.5f;
        float vehMaxForce = 6000.0f;
        float vehRollInfluence = 0.1f;
        float vehWidth = 1.8f;
        float vehHeight = 0.4f;
        float vehLength = 3.6f;
        float vehWheelRadius = 0.35f;

        std::vector<SerializedScript> scripts;

        bool hasCam = false;
        std::string camProj = "Perspective";
        float camFov = 60.0f;
        float camNear = 0.1f;
        float camFar = 1000.0f;
        float camOrthoSize = 5.0f;
        bool camPrimary = true;
        glm::vec4 camClearColor = glm::vec4(0.12f, 0.14f, 0.18f, 1.0f);
        float camLetterbox = 0.0f;
        float camPillarbox = 0.0f;

        // CinematicCamera
        bool hasCinematicCam = false;
        std::string cinematicCamTarget = "";
        float cinematicCamDist = 5.8f;
        float cinematicCamHeight = 1.5f;
        float cinematicCamPitch = 9.0f;
        float cinematicCamPosLag = 8.5f;
        float cinematicCamRotLag = 6.5f;
        float cinematicCamDriftComp = 0.40f;
        bool cinematicCamDynamicFOV = true;
        float cinematicCamBaseFOV = 60.0f;
        float cinematicCamBoostFOV = 76.0f;

        bool hasKeyframeAnim = false;
        std::string keyframeClipPath;
        bool keyframePlayOnStart = true;
        bool keyframeLoop = false;
        float keyframeSpeed = 1.0f;

        bool hasSprite = false;
        std::string spriteTexPath;
        glm::vec4 spriteColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        bool spriteFlipX = false, spriteFlipY = false;
        int spriteSortingOrder = 0;
        std::string spriteSortingLayer = "Default";
        float spritePPU = 100.0f;
        int spriteHFrames = 1, spriteVFrames = 1, spriteCurrentFrame = 0;
        bool spriteIsLit = false;
        bool spriteIsPixelArt = true;
        std::string spriteNormalMapPath;

        bool hasSpriteAnimator = false;
        bool saPlayOnStart = true;
        std::string saDefaultClip;
        std::vector<SpriteAnimationClip> saClips;

        bool hasBoxCollider2D = false;
        glm::vec2 box2dSize{ 1.0f, 1.0f };
        glm::vec2 box2dOffset{ 0.0f, 0.0f };
        bool box2dIsTrigger = false;
        float box2dDensity = 1.0f, box2dFriction = 0.2f, box2dRestitution = 0.0f;

        bool hasCircleCollider2D = false;
        float circ2dRadius = 0.5f;
        glm::vec2 circ2dOffset{ 0.0f, 0.0f };
        bool circ2dIsTrigger = false;
        float circ2dDensity = 1.0f, circ2dFriction = 0.2f, circ2dRestitution = 0.0f;

        bool hasRigidBody2D = false;
        int rb2dBodyType = 0;
        float rb2dMass = 1.0f;
        float rb2dGravityScale = 1.0f;
        bool rb2dFreezeRotation = false;
        float rb2dLinearDamping = 0.0f, rb2dAngularDamping = 0.05f;

        bool hasLight2D = false;
        int l2dType = 0;
        glm::vec3 l2dColor{ 1.0f, 1.0f, 0.9f };
        float l2dIntensity = 1.0f, l2dRadius = 5.0f, l2dInnerRadius = 0.0f, l2dSpotAngle = 45.0f;
        bool l2dCastShadows = true;
        float l2dShadowIntensity = 0.85f;

        bool hasShadowCaster2D = false;
        int sc2dShape = 0;
        glm::vec2 sc2dSize{ 1.0f, 1.0f };
        float sc2dRadius = 0.5f;
        glm::vec2 sc2dOffset{ 0.0f, 0.0f };
        bool sc2dCastShadows = true;

        bool hasTileGrid = false;
        glm::vec2 tgCellSize{ 1.0f, 1.0f };
        int tgAnchor = 0;
        std::string tgTileSheetPath;
        int tgSheetHFrames = 1, tgSheetVFrames = 1;
        std::string tgSortingLayer = "Default";
        int tgSortingOrder = 0;
        bool tgIsPixelArt = true;
        std::map<std::pair<int, int>, int> tgTiles;

        bool hasCanvas = false;
        std::string canvasRenderMode = "ScreenSpaceOverlay";
        glm::vec2 canvasRefRes{ 1920.0f, 1080.0f };
        bool canvasScaleWithScreen = true;
        int canvasSortingOrder = 0;

        bool hasRect = false;
        std::string rectAnchorPreset = "Center";
        glm::vec2 rectAnchoredPos{ 0.0f, 0.0f };
        glm::vec2 rectSize{ 160.0f, 50.0f };
        glm::vec2 rectPivot{ 0.5f, 0.5f };

        bool hasUIImage = false;
        std::string uiImgTexPath;
        glm::vec4 uiImgColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        bool uiImgPreserveAspect = false;

        bool hasUIText = false;
        std::string uiTxtText = "Texto UI";
        std::string uiTxtFontPath;
        float uiTxtFontSize = 24.0f;
        glm::vec4 uiTxtColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        int uiTxtAlignH = 1, uiTxtAlignV = 1;

        bool hasUIButton = false;
        bool uiBtnInteractable = true;
        glm::vec4 uiBtnNormalCol{ 0.20f, 0.45f, 0.82f, 1.0f };
        glm::vec4 uiBtnHoverCol{ 0.28f, 0.56f, 0.95f, 1.0f };
        glm::vec4 uiBtnPressedCol{ 0.14f, 0.32f, 0.62f, 1.0f };
        glm::vec4 uiBtnDisabledCol{ 0.40f, 0.42f, 0.45f, 0.65f };
        std::string uiBtnTargetEnt, uiBtnOnClickFn;

        bool hasUISlider = false;
        bool uiSldInteractable = true;
        float uiSldMin = 0.0f, uiSldMax = 1.0f, uiSldVal = 0.5f;
        bool uiSldWhole = false;
        glm::vec4 uiSldBgCol{ 0.16f, 0.18f, 0.22f, 1.0f };
        glm::vec4 uiSldFillCol{ 0.22f, 0.62f, 0.96f, 1.0f };
        glm::vec4 uiSldHandleCol{ 0.95f, 0.96f, 0.98f, 1.0f };
        std::string uiSldTargetEnt, uiSldOnValChangedFn;

        bool hasUIProgressBar = false;
        float uiPbMin = 0.0f, uiPbMax = 100.0f, uiPbVal = 75.0f;
        glm::vec4 uiPbBgCol{ 0.15f, 0.17f, 0.21f, 1.0f };
        glm::vec4 uiPbFillCol{ 0.22f, 0.78f, 0.40f, 1.0f };
        bool uiPbShowPct = true;

        bool hasUIJoystick = false;
        std::string uiJoyStickName = "Move";
        int uiJoyMode = 0;
        float uiJoyTravel = 90.0f;
        float uiJoyActivation = 120.0f;
        glm::vec4 uiJoyBg{ 1.0f, 1.0f, 1.0f, 0.28f };
        glm::vec4 uiJoyKnob{ 1.0f, 1.0f, 1.0f, 0.85f };
        bool uiJoyInteractable = true;

        bool hasUIActionButton = false;
        std::string uiActName = "Jump";
        bool uiActInteractable = true;
        glm::vec4 uiActNormal{ 0.20f, 0.45f, 0.82f, 0.85f };
        glm::vec4 uiActPressed{ 0.14f, 0.32f, 0.62f, 1.0f };

        bool hasParticle = false;
        std::vector<std::pair<std::string, std::string>> particleKeys;
    };

    std::vector<EntityData> parsedEntities;
    EntityData currentData;
    bool inEntity = false;
    std::string currentSubBlock;

    auto finishEntity = [&]() {
        if (inEntity) {
            parsedEntities.push_back(currentData);
            currentData = EntityData();
            inEntity = false;
            currentSubBlock.clear();
        }
    };

    while (std::getline(ss, line)) {
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') continue;
        size_t last = line.find_last_not_of(" \t\r\n");
        std::string trimmed = line.substr(first, last - first + 1);

        if (trimmed == "Entity:") {
            finishEntity();
            inEntity = true;
            continue;
        }

        if (!inEntity) continue;

        if (trimmed == "MeshComponent:") { currentSubBlock = "Mesh"; currentData.hasMesh = true; continue; }
        if (trimmed == "SkinnedMeshComponent:") { currentSubBlock = "SkinnedMesh"; currentData.hasSkinned = true; continue; }
        if (trimmed == "AnimatorComponent:") { currentSubBlock = "Animator"; currentData.hasAnimator = true; continue; }
        if (trimmed == "BoneComponent:") { currentSubBlock = "Bone"; currentData.hasBone = true; continue; }
        if (trimmed == "LightComponent:") { currentSubBlock = "Light"; currentData.hasLight = true; continue; }
        if (trimmed == "DDGIVolumeComponent:") { currentSubBlock = "DDGI"; currentData.hasDDGI = true; continue; }
        if (trimmed == "ReflectionProbeComponent:") { currentSubBlock = "ReflectionProbe"; currentData.hasReflectionProbe = true; continue; }
        if (trimmed == "PostProcessVolumeComponent:") { currentSubBlock = "Post"; currentData.hasPost = true; continue; }
        if (trimmed == "BoxColliderComponent:") { currentSubBlock = "BoxCol"; currentData.hasBoxCol = true; continue; }
        if (trimmed == "SphereColliderComponent:") { currentSubBlock = "SphereCol"; currentData.hasSphereCol = true; continue; }
        if (trimmed == "CapsuleColliderComponent:") { currentSubBlock = "CapCol"; currentData.hasCapCol = true; continue; }
        if (trimmed == "MeshColliderComponent:") { currentSubBlock = "MeshCol"; currentData.hasMeshCol = true; continue; }
        if (trimmed == "RigidBodyComponent:") { currentSubBlock = "Rb"; currentData.hasRb = true; continue; }
        if (trimmed == "VehicleComponent:") { currentSubBlock = "Vehicle"; currentData.hasVehicle = true; continue; }
        if (trimmed == "ScriptComponent:") {
            currentSubBlock = "Script";
            SerializedScript sc;
            currentData.scripts.push_back(sc);
            continue;
        }
        if (trimmed == "CameraComponent:") { currentSubBlock = "Cam"; currentData.hasCam = true; continue; }
        if (trimmed == "CinematicCameraComponent:" || trimmed == "CinematicCamera:") { currentSubBlock = "CinematicCam"; currentData.hasCinematicCam = true; continue; }
        if (trimmed == "KeyframeAnimationComponent:" || trimmed == "KeyframeAnimation:") { currentSubBlock = "KeyframeAnim"; currentData.hasKeyframeAnim = true; continue; }
        if (trimmed == "SpriteRendererComponent:") { currentSubBlock = "Sprite"; currentData.hasSprite = true; continue; }
        if (trimmed == "SpriteAnimatorComponent:") { currentSubBlock = "SpriteAnimator"; currentData.hasSpriteAnimator = true; continue; }
        if (trimmed == "BoxCollider2DComponent:") { currentSubBlock = "Box2D"; currentData.hasBoxCollider2D = true; continue; }
        if (trimmed == "CircleCollider2DComponent:") { currentSubBlock = "Circle2D"; currentData.hasCircleCollider2D = true; continue; }
        if (trimmed == "RigidBody2DComponent:") { currentSubBlock = "RigidBody2D"; currentData.hasRigidBody2D = true; continue; }
        if (trimmed == "Light2DComponent:") { currentSubBlock = "Light2D"; currentData.hasLight2D = true; continue; }
        if (trimmed == "ShadowCaster2DComponent:") { currentSubBlock = "ShadowCaster2D"; currentData.hasShadowCaster2D = true; continue; }
        if (trimmed == "TileGridComponent:") { currentSubBlock = "TileGrid"; currentData.hasTileGrid = true; continue; }
        if (trimmed == "CanvasComponent:") { currentSubBlock = "Canvas"; currentData.hasCanvas = true; continue; }
        if (trimmed == "RectTransformComponent:") { currentSubBlock = "Rect"; currentData.hasRect = true; continue; }
        if (trimmed == "UIImageComponent:") { currentSubBlock = "UIImage"; currentData.hasUIImage = true; continue; }
        if (trimmed == "UITextComponent:") { currentSubBlock = "UIText"; currentData.hasUIText = true; continue; }
        if (trimmed == "UIButtonComponent:") { currentSubBlock = "UIButton"; currentData.hasUIButton = true; continue; }
        if (trimmed == "UISliderComponent:") { currentSubBlock = "UISlider"; currentData.hasUISlider = true; continue; }
        if (trimmed == "UIProgressBarComponent:") { currentSubBlock = "UIProgressBar"; currentData.hasUIProgressBar = true; continue; }
        if (trimmed == "UIJoystickComponent:") { currentSubBlock = "UIJoystick"; currentData.hasUIJoystick = true; continue; }
        if (trimmed == "UIActionButtonComponent:") { currentSubBlock = "UIActionButton"; currentData.hasUIActionButton = true; continue; }
        if (trimmed == "ParticleSystemComponent:") { currentSubBlock = "Particle"; currentData.hasParticle = true; continue; }

        size_t colon = trimmed.find(':');
        if (colon == std::string::npos) continue;

        std::string key = trimmed.substr(0, colon);
        std::string val = trimmed.substr(colon + 1);
        size_t vf = val.find_first_not_of(" \t\r\n");
        if (vf != std::string::npos) val = val.substr(vf); else val = "";
        size_t vl = val.find_last_not_of(" \t\r\n");
        if (vl != std::string::npos) val = val.substr(0, vl + 1);

        if (currentSubBlock.empty()) {
            if (key == "Id") currentData.id = std::stoi(val);
            else if (key == "ParentId") currentData.parentId = std::stoi(val);
            else if (key == "Name") currentData.name = val;
            else if (key == "Position") {
                std::stringstream vs(val);
                vs >> currentData.pos.x >> currentData.pos.y >> currentData.pos.z;
            } else if (key == "Rotation") {
                std::stringstream vs(val);
                vs >> currentData.rot.x >> currentData.rot.y >> currentData.rot.z;
            } else if (key == "Scale") {
                std::stringstream vs(val);
                vs >> currentData.scale.x >> currentData.scale.y >> currentData.scale.z;
            } else if (key == "InheritScale") {
                int isVal = 1;
                std::stringstream vs(val);
                if (vs >> isVal) currentData.inheritScale = (isVal != 0);
            }
        } else if (currentSubBlock == "Mesh") {
            if (key == "ModelPath") currentData.meshPath = val;
            else if (key == "MaterialPath") currentData.matPath = val;
            else if (key == "SubMaterial") {
                std::stringstream vs(val);
                size_t slotIdx = 0;
                if (vs >> slotIdx) {
                    std::string smPath;
                    std::getline(vs, smPath);
                    size_t sf = smPath.find_first_not_of(" \t\r\n");
                    if (sf != std::string::npos) smPath = smPath.substr(sf);
                    else smPath.clear();
                    size_t sl = smPath.find_last_not_of(" \t\r\n");
                    if (sl != std::string::npos) smPath = smPath.substr(0, sl + 1);
                    currentData.subMaterials.push_back({ slotIdx, smPath });
                }
            }
        } else if (currentSubBlock == "SkinnedMesh") {
            if (key == "ModelPath") currentData.skinnedModelPath = val;
            else if (key == "MaterialPath") currentData.skinnedMatPath = val;
            else if (key == "CastShadows") currentData.skinnedCastShadows = (val == "1" || val == "true");
            else if (key == "SubMaterial") {
                std::stringstream vs(val);
                size_t slotIdx = 0;
                if (vs >> slotIdx) {
                    std::string smPath;
                    std::getline(vs, smPath);
                    size_t sf = smPath.find_first_not_of(" \t\r\n");
                    if (sf != std::string::npos) smPath = smPath.substr(sf);
                    else smPath.clear();
                    size_t sl = smPath.find_last_not_of(" \t\r\n");
                    if (sl != std::string::npos) smPath = smPath.substr(0, sl + 1);
                    currentData.skinnedSubMaterials.push_back({ slotIdx, smPath });
                }
            }
        } else if (currentSubBlock == "Animator") {
            if (key == "CurrentClipIndex") std::stringstream(val) >> currentData.animClipIdx;
            else if (key == "CurrentClipName") currentData.animClipName = val;
            else if (key == "PlaybackSpeed") std::stringstream(val) >> currentData.animSpeed;
            else if (key == "IsPlaying") currentData.animPlaying = (val == "1" || val == "true");
            else if (key == "IsLooping") currentData.animLooping = (val == "1" || val == "true");
            else if (key == "CurrentTime") std::stringstream(val) >> currentData.animTime;
            else if (key == "ControllerPath") currentData.animControllerPath = val;
        } else if (currentSubBlock == "Bone") {
            if (key == "BoneIndex") std::stringstream(val) >> currentData.boneIdx;
            else if (key == "BoneName") currentData.boneName = val;
        } else if (currentSubBlock == "Light") {
            if (key == "Type") {
                if (val == "Directional") currentData.lightType = LightType::Directional;
                else if (val == "Spot") currentData.lightType = LightType::Spot;
                else currentData.lightType = LightType::Point;
            } else if (key == "Color") {
                std::stringstream vs(val);
                vs >> currentData.lightColor.r >> currentData.lightColor.g >> currentData.lightColor.b;
            } else if (key == "Intensity") currentData.lightIntensity = std::stof(val);
            else if (key == "Range") currentData.lightRange = std::stof(val);
            else if (key == "SpotAngle") currentData.lightSpotAngle = std::stof(val);
            else if (key == "CastShadows") currentData.lightCastShadows = (val == "1" || val == "true");
        } else if (currentSubBlock == "DDGI") {
            if (key == "Size") {
                std::stringstream vs(val);
                vs >> currentData.ddgiSize.x >> currentData.ddgiSize.y >> currentData.ddgiSize.z;
            } else if (key == "ProbesX") currentData.ddgiProbesX = std::stoi(val);
            else if (key == "ProbesY") currentData.ddgiProbesY = std::stoi(val);
            else if (key == "ProbesZ") currentData.ddgiProbesZ = std::stoi(val);
            else if (key == "Preview") currentData.ddgiPreview = (val == "1" || val == "true");
        } else if (currentSubBlock == "ReflectionProbe") {
            if (key == "Size") {
                std::stringstream vs(val);
                vs >> currentData.probeSize.x >> currentData.probeSize.y >> currentData.probeSize.z;
            } else if (key == "Resolution") currentData.probeResolution = std::stoi(val);
            else if (key == "Intensity") currentData.probeIntensity = std::stof(val);
            else if (key == "RoughnessCutoff") currentData.probeRoughnessCutoff = std::stof(val);
            else if (key == "BoxProjection") currentData.probeBoxProjection = (val == "1" || val == "true");
            else if (key == "IncludeDynamic") currentData.probeIncludeDynamic = (val == "1" || val == "true");
        } else if (currentSubBlock == "Post") {
            currentData.postKeys.push_back({ key, val });
        } else if (currentSubBlock == "Particle") {
            currentData.particleKeys.push_back({ key, val });
        } else if (currentSubBlock == "BoxCol") {
            if (key == "Size") {
                std::stringstream vs(val);
                vs >> currentData.boxSize.x >> currentData.boxSize.y >> currentData.boxSize.z;
            } else if (key == "CenterOffset") {
                std::stringstream vs(val);
                vs >> currentData.boxOffset.x >> currentData.boxOffset.y >> currentData.boxOffset.z;
            } else if (key == "IsTrigger") currentData.boxTrigger = (std::stoi(val) != 0);
        } else if (currentSubBlock == "SphereCol") {
            if (key == "Radius") currentData.sphereRadius = std::stof(val);
            else if (key == "CenterOffset") {
                std::stringstream vs(val);
                vs >> currentData.sphereOffset.x >> currentData.sphereOffset.y >> currentData.sphereOffset.z;
            } else if (key == "IsTrigger") currentData.sphereTrigger = (std::stoi(val) != 0);
        } else if (currentSubBlock == "CapCol") {
            if (key == "Radius") currentData.capRadius = std::stof(val);
            else if (key == "Height") currentData.capHeight = std::stof(val);
            else if (key == "CenterOffset") {
                std::stringstream vs(val);
                vs >> currentData.capOffset.x >> currentData.capOffset.y >> currentData.capOffset.z;
            } else if (key == "IsTrigger") currentData.capTrigger = (std::stoi(val) != 0);
        } else if (currentSubBlock == "MeshCol") {
            if (key == "IsConvex") currentData.meshConvex = (std::stoi(val) != 0);
            else if (key == "CenterOffset") {
                std::stringstream vs(val);
                vs >> currentData.meshOffset.x >> currentData.meshOffset.y >> currentData.meshOffset.z;
            } else if (key == "IsTrigger") currentData.meshTrigger = (std::stoi(val) != 0);
        } else if (currentSubBlock == "Rb") {
            if (key == "Mass") currentData.rbMass = std::stof(val);
            else if (key == "LinearDrag") currentData.rbLinearDrag = std::stof(val);
            else if (key == "AngularDrag") currentData.rbAngularDrag = std::stof(val);
            else if (key == "IsKinematic") currentData.rbKinematic = (std::stoi(val) != 0);
            else if (key == "UseGravity") currentData.rbUseGravity = (std::stoi(val) != 0);
            else if (key == "Friction") currentData.rbFriction = std::stof(val);
            else if (key == "RollingFriction") currentData.rbRollingFriction = std::stof(val);
            else if (key == "Restitution") currentData.rbRestitution = std::stof(val);
            else if (key == "FreezePosition") {
                std::stringstream vs(val);
                int fx, fy, fz;
                vs >> fx >> fy >> fz;
                currentData.rbFreezePosX = (fx != 0);
                currentData.rbFreezePosY = (fy != 0);
                currentData.rbFreezePosZ = (fz != 0);
            } else if (key == "FreezeRotation") {
                std::stringstream vs(val);
                int rx, ry, rz;
                vs >> rx >> ry >> rz;
                currentData.rbFreezeRotX = (rx != 0);
                currentData.rbFreezeRotY = (ry != 0);
                currentData.rbFreezeRotZ = (rz != 0);
            }
        } else if (currentSubBlock == "Vehicle") {
            if (key == "SuspensionStiffness") currentData.vehStiffness = std::stof(val);
            else if (key == "SuspensionDamping") currentData.vehDamping = std::stof(val);
            else if (key == "SuspensionCompression") currentData.vehCompression = std::stof(val);
            else if (key == "MaxSuspensionTravelCm") currentData.vehMaxTravel = std::stof(val);
            else if (key == "FrictionSlip") currentData.vehFrictionSlip = std::stof(val);
            else if (key == "MaxSuspensionForce") currentData.vehMaxForce = std::stof(val);
            else if (key == "RollInfluence") currentData.vehRollInfluence = std::stof(val);
            else if (key == "ChassisDimensions") {
                std::stringstream vs(val);
                vs >> currentData.vehWidth >> currentData.vehHeight >> currentData.vehLength >> currentData.vehWheelRadius;
            }
        } else if (currentSubBlock == "Script") {
            if (!currentData.scripts.empty()) {
                auto& sc = currentData.scripts.back();
                if (key == "ScriptPath") sc.path = val;
                else if (key == "Enabled") sc.enabled = (std::stoi(val) != 0);
                else if (key == "Property") {
                    std::stringstream vs(val);
                    std::string pName, pType;
                    vs >> pName >> pType;
                    ScriptProperty prop;
                    prop.name = pName;
                    if (pType == "float") {
                        prop.type = ScriptPropertyType::Float;
                        vs >> prop.floatValue;
                    } else if (pType == "int") {
                        prop.type = ScriptPropertyType::Int;
                        vs >> prop.intValue;
                        prop.floatValue = static_cast<float>(prop.intValue);
                    } else if (pType == "bool") {
                        prop.type = ScriptPropertyType::Bool;
                        int bVal = 0; vs >> bVal;
                        prop.boolValue = (bVal != 0);
                    } else if (pType == "string") {
                        prop.type = ScriptPropertyType::String;
                        vs >> prop.stringValue;
                    } else if (pType == "vec3") {
                        prop.type = ScriptPropertyType::Vec3;
                        vs >> prop.vec3Value.x >> prop.vec3Value.y >> prop.vec3Value.z;
                    }
                    sc.properties.push_back(prop);
                }
            }
        } else if (currentSubBlock == "Cam") {
            if (key == "Projection") currentData.camProj = val;
            else if (key == "FOV") currentData.camFov = std::stof(val);
            else if (key == "NearClip") currentData.camNear = std::stof(val);
            else if (key == "FarClip") currentData.camFar = std::stof(val);
            else if (key == "OrthoSize") currentData.camOrthoSize = std::stof(val);
            else if (key == "IsPrimary") currentData.camPrimary = (std::stoi(val) != 0);
            else if (key == "ClearColor") {
                std::stringstream vs(val);
                vs >> currentData.camClearColor.r >> currentData.camClearColor.g >> currentData.camClearColor.b >> currentData.camClearColor.a;
            }
            else if (key == "Letterbox") currentData.camLetterbox = std::stof(val);
            else if (key == "Pillarbox") currentData.camPillarbox = std::stof(val);
        } else if (currentSubBlock == "CinematicCam") {
            if (key == "Target") currentData.cinematicCamTarget = val;
            else if (key == "Distance") currentData.cinematicCamDist = std::stof(val);
            else if (key == "Height") currentData.cinematicCamHeight = std::stof(val);
            else if (key == "Pitch") currentData.cinematicCamPitch = std::stof(val);
            else if (key == "PositionLag") currentData.cinematicCamPosLag = std::stof(val);
            else if (key == "RotationLag") currentData.cinematicCamRotLag = std::stof(val);
            else if (key == "DriftCompensation") currentData.cinematicCamDriftComp = std::stof(val);
            else if (key == "DynamicFOV") currentData.cinematicCamDynamicFOV = (std::stoi(val) != 0);
            else if (key == "BaseFOV") currentData.cinematicCamBaseFOV = std::stof(val);
            else if (key == "BoostFOV") currentData.cinematicCamBoostFOV = std::stof(val);
        } else if (currentSubBlock == "KeyframeAnim") {
            if (key == "ClipPath") currentData.keyframeClipPath = val;
            else if (key == "PlayOnStart") currentData.keyframePlayOnStart = (std::stoi(val) != 0);
            else if (key == "Loop") currentData.keyframeLoop = (std::stoi(val) != 0);
            else if (key == "Speed") currentData.keyframeSpeed = std::stof(val);
        } else if (currentSubBlock == "Sprite") {
            if (key == "TexturePath") currentData.spriteTexPath = val;
            else if (key == "Color") { std::stringstream vs(val); vs >> currentData.spriteColor.r >> currentData.spriteColor.g >> currentData.spriteColor.b >> currentData.spriteColor.a; }
            else if (key == "FlipX") currentData.spriteFlipX = (std::stoi(val) != 0);
            else if (key == "FlipY") currentData.spriteFlipY = (std::stoi(val) != 0);
            else if (key == "SortingOrder") currentData.spriteSortingOrder = std::stoi(val);
            else if (key == "SortingLayer") currentData.spriteSortingLayer = val;
            else if (key == "PixelsPerUnit") currentData.spritePPU = std::stof(val);
            else if (key == "HFrames") currentData.spriteHFrames = std::stoi(val);
            else if (key == "VFrames") currentData.spriteVFrames = std::stoi(val);
            else if (key == "CurrentFrame") currentData.spriteCurrentFrame = std::stoi(val);
            else if (key == "IsLit") currentData.spriteIsLit = (std::stoi(val) != 0);
            else if (key == "IsPixelArt") currentData.spriteIsPixelArt = (std::stoi(val) != 0);
            else if (key == "NormalMapPath") currentData.spriteNormalMapPath = val;
        } else if (currentSubBlock == "SpriteAnimator") {
            if (key == "PlayOnStart") currentData.saPlayOnStart = (std::stoi(val) != 0);
            else if (key == "DefaultClip") currentData.saDefaultClip = val;
            else if (key == "ClipData") {
                std::stringstream vs(val);
                std::string cname;
                int sFrame = 0, eFrame = 0;
                float fps = 12.0f;
                int loop = 1;
                vs >> cname >> sFrame >> eFrame >> fps >> loop;
                if (!cname.empty()) {
                    SpriteAnimationClip c;
                    c.name = cname;
                    c.startFrame = sFrame;
                    c.endFrame = eFrame;
                    c.frameRate = fps;
                    c.loop = (loop != 0);
                    currentData.saClips.push_back(c);
                }
            }
        } else if (currentSubBlock == "Box2D") {
            if (key == "Size") { std::stringstream vs(val); vs >> currentData.box2dSize.x >> currentData.box2dSize.y; }
            else if (key == "Offset") { std::stringstream vs(val); vs >> currentData.box2dOffset.x >> currentData.box2dOffset.y; }
            else if (key == "IsTrigger") currentData.box2dIsTrigger = (std::stoi(val) != 0);
            else if (key == "Density") currentData.box2dDensity = std::stof(val);
            else if (key == "Friction") currentData.box2dFriction = std::stof(val);
            else if (key == "Restitution") currentData.box2dRestitution = std::stof(val);
        } else if (currentSubBlock == "Circle2D") {
            if (key == "Radius") currentData.circ2dRadius = std::stof(val);
            else if (key == "Offset") { std::stringstream vs(val); vs >> currentData.circ2dOffset.x >> currentData.circ2dOffset.y; }
            else if (key == "IsTrigger") currentData.circ2dIsTrigger = (std::stoi(val) != 0);
            else if (key == "Density") currentData.circ2dDensity = std::stof(val);
            else if (key == "Friction") currentData.circ2dFriction = std::stof(val);
            else if (key == "Restitution") currentData.circ2dRestitution = std::stof(val);
        } else if (currentSubBlock == "RigidBody2D") {
            if (key == "BodyType") currentData.rb2dBodyType = std::stoi(val);
            else if (key == "Mass") currentData.rb2dMass = std::stof(val);
            else if (key == "GravityScale") currentData.rb2dGravityScale = std::stof(val);
            else if (key == "FreezeRotation") currentData.rb2dFreezeRotation = (std::stoi(val) != 0);
            else if (key == "LinearDamping") currentData.rb2dLinearDamping = std::stof(val);
            else if (key == "AngularDamping") currentData.rb2dAngularDamping = std::stof(val);
        } else if (currentSubBlock == "Light2D") {
            if (key == "LightType") currentData.l2dType = std::stoi(val);
            else if (key == "Color") { std::stringstream vs(val); vs >> currentData.l2dColor.r >> currentData.l2dColor.g >> currentData.l2dColor.b; }
            else if (key == "Intensity") currentData.l2dIntensity = std::stof(val);
            else if (key == "Radius") currentData.l2dRadius = std::stof(val);
            else if (key == "InnerRadius") currentData.l2dInnerRadius = std::stof(val);
            else if (key == "SpotAngle") currentData.l2dSpotAngle = std::stof(val);
            else if (key == "CastShadows") currentData.l2dCastShadows = (std::stoi(val) != 0);
            else if (key == "ShadowIntensity") currentData.l2dShadowIntensity = std::stof(val);
        } else if (currentSubBlock == "ShadowCaster2D") {
            if (key == "Shape") currentData.sc2dShape = std::stoi(val);
            else if (key == "Size") { std::stringstream vs(val); vs >> currentData.sc2dSize.x >> currentData.sc2dSize.y; }
            else if (key == "Radius") currentData.sc2dRadius = std::stof(val);
            else if (key == "Offset") { std::stringstream vs(val); vs >> currentData.sc2dOffset.x >> currentData.sc2dOffset.y; }
            else if (key == "CastShadows") currentData.sc2dCastShadows = (std::stoi(val) != 0);
        } else if (currentSubBlock == "TileGrid") {
            if (key == "CellSize") { std::stringstream vs(val); vs >> currentData.tgCellSize.x >> currentData.tgCellSize.y; }
            else if (key == "Anchor") currentData.tgAnchor = std::stoi(val);
            else if (key == "TileSheetPath") currentData.tgTileSheetPath = val;
            else if (key == "SheetHFrames") currentData.tgSheetHFrames = std::stoi(val);
            else if (key == "SheetVFrames") currentData.tgSheetVFrames = std::stoi(val);
            else if (key == "SortingLayer") currentData.tgSortingLayer = val;
            else if (key == "SortingOrder") currentData.tgSortingOrder = std::stoi(val);
            else if (key == "IsPixelArt") currentData.tgIsPixelArt = (std::stoi(val) != 0);
            else if (key == "TileData") {
                std::stringstream vs(val);
                int tx = 0, ty = 0, tid = 0;
                if (vs >> tx >> ty >> tid) currentData.tgTiles[std::make_pair(tx, ty)] = tid;
            }
        } else if (currentSubBlock == "Canvas") {
            if (key == "RenderMode") currentData.canvasRenderMode = val;
            else if (key == "ReferenceResolution") { std::stringstream vs(val); vs >> currentData.canvasRefRes.x >> currentData.canvasRefRes.y; }
            else if (key == "ScaleWithScreenSize") currentData.canvasScaleWithScreen = (std::stoi(val) != 0);
            else if (key == "SortingOrder") currentData.canvasSortingOrder = std::stoi(val);
        } else if (currentSubBlock == "Rect") {
            if (key == "AnchorPreset") currentData.rectAnchorPreset = val;
            else if (key == "AnchoredPosition") { std::stringstream vs(val); vs >> currentData.rectAnchoredPos.x >> currentData.rectAnchoredPos.y; }
            else if (key == "Size") { std::stringstream vs(val); vs >> currentData.rectSize.x >> currentData.rectSize.y; }
            else if (key == "Pivot") { std::stringstream vs(val); vs >> currentData.rectPivot.x >> currentData.rectPivot.y; }
        } else if (currentSubBlock == "UIImage") {
            if (key == "TexturePath") currentData.uiImgTexPath = val;
            else if (key == "Color") { std::stringstream vs(val); vs >> currentData.uiImgColor.r >> currentData.uiImgColor.g >> currentData.uiImgColor.b >> currentData.uiImgColor.a; }
            else if (key == "PreserveAspect") currentData.uiImgPreserveAspect = (std::stoi(val) != 0);
        } else if (currentSubBlock == "UIText") {
            if (key == "Text") currentData.uiTxtText = val;
            else if (key == "FontPath") currentData.uiTxtFontPath = val;
            else if (key == "FontSize") currentData.uiTxtFontSize = std::stof(val);
            else if (key == "Color") { std::stringstream vs(val); vs >> currentData.uiTxtColor.r >> currentData.uiTxtColor.g >> currentData.uiTxtColor.b >> currentData.uiTxtColor.a; }
            else if (key == "AlignH") currentData.uiTxtAlignH = std::stoi(val);
            else if (key == "AlignV") currentData.uiTxtAlignV = std::stoi(val);
        } else if (currentSubBlock == "UIButton") {
            if (key == "Interactable") currentData.uiBtnInteractable = (std::stoi(val) != 0);
            else if (key == "NormalColor") { std::stringstream vs(val); vs >> currentData.uiBtnNormalCol.r >> currentData.uiBtnNormalCol.g >> currentData.uiBtnNormalCol.b >> currentData.uiBtnNormalCol.a; }
            else if (key == "HoverColor") { std::stringstream vs(val); vs >> currentData.uiBtnHoverCol.r >> currentData.uiBtnHoverCol.g >> currentData.uiBtnHoverCol.b >> currentData.uiBtnHoverCol.a; }
            else if (key == "PressedColor") { std::stringstream vs(val); vs >> currentData.uiBtnPressedCol.r >> currentData.uiBtnPressedCol.g >> currentData.uiBtnPressedCol.b >> currentData.uiBtnPressedCol.a; }
            else if (key == "DisabledColor") { std::stringstream vs(val); vs >> currentData.uiBtnDisabledCol.r >> currentData.uiBtnDisabledCol.g >> currentData.uiBtnDisabledCol.b >> currentData.uiBtnDisabledCol.a; }
            else if (key == "TargetEntity") currentData.uiBtnTargetEnt = val;
            else if (key == "OnClickFunction") currentData.uiBtnOnClickFn = val;
        } else if (currentSubBlock == "UISlider") {
            if (key == "Interactable") currentData.uiSldInteractable = (std::stoi(val) != 0);
            else if (key == "MinValue") currentData.uiSldMin = std::stof(val);
            else if (key == "MaxValue") currentData.uiSldMax = std::stof(val);
            else if (key == "Value") currentData.uiSldVal = std::stof(val);
            else if (key == "WholeNumbers") currentData.uiSldWhole = (std::stoi(val) != 0);
            else if (key == "BackgroundColor") { std::stringstream vs(val); vs >> currentData.uiSldBgCol.r >> currentData.uiSldBgCol.g >> currentData.uiSldBgCol.b >> currentData.uiSldBgCol.a; }
            else if (key == "FillColor") { std::stringstream vs(val); vs >> currentData.uiSldFillCol.r >> currentData.uiSldFillCol.g >> currentData.uiSldFillCol.b >> currentData.uiSldFillCol.a; }
            else if (key == "HandleColor") { std::stringstream vs(val); vs >> currentData.uiSldHandleCol.r >> currentData.uiSldHandleCol.g >> currentData.uiSldHandleCol.b >> currentData.uiSldHandleCol.a; }
            else if (key == "TargetEntity") currentData.uiSldTargetEnt = val;
            else if (key == "OnValueChangedFunction") currentData.uiSldOnValChangedFn = val;
        } else if (currentSubBlock == "UIProgressBar") {
            if (key == "MinValue") currentData.uiPbMin = std::stof(val);
            else if (key == "MaxValue") currentData.uiPbMax = std::stof(val);
            else if (key == "Value") currentData.uiPbVal = std::stof(val);
            else if (key == "BackgroundColor") { std::stringstream vs(val); vs >> currentData.uiPbBgCol.r >> currentData.uiPbBgCol.g >> currentData.uiPbBgCol.b >> currentData.uiPbBgCol.a; }
            else if (key == "FillColor") { std::stringstream vs(val); vs >> currentData.uiPbFillCol.r >> currentData.uiPbFillCol.g >> currentData.uiPbFillCol.b >> currentData.uiPbFillCol.a; }
            else if (key == "ShowPercentageText") currentData.uiPbShowPct = (std::stoi(val) != 0);
        } else if (currentSubBlock == "UIJoystick") {
            if (key == "StickName") currentData.uiJoyStickName = val;
            else if (key == "Mode") currentData.uiJoyMode = std::stoi(val);
            else if (key == "TravelRadius") currentData.uiJoyTravel = std::stof(val);
            else if (key == "ActivationRadius") currentData.uiJoyActivation = std::stof(val);
            else if (key == "BackgroundColor") { std::stringstream vs(val); vs >> currentData.uiJoyBg.r >> currentData.uiJoyBg.g >> currentData.uiJoyBg.b >> currentData.uiJoyBg.a; }
            else if (key == "KnobColor") { std::stringstream vs(val); vs >> currentData.uiJoyKnob.r >> currentData.uiJoyKnob.g >> currentData.uiJoyKnob.b >> currentData.uiJoyKnob.a; }
            else if (key == "Interactable") currentData.uiJoyInteractable = (std::stoi(val) != 0);
        } else if (currentSubBlock == "UIActionButton") {
            if (key == "ActionName") currentData.uiActName = val;
            else if (key == "Interactable") currentData.uiActInteractable = (std::stoi(val) != 0);
            else if (key == "NormalColor") { std::stringstream vs(val); vs >> currentData.uiActNormal.r >> currentData.uiActNormal.g >> currentData.uiActNormal.b >> currentData.uiActNormal.a; }
            else if (key == "PressedColor") { std::stringstream vs(val); vs >> currentData.uiActPressed.r >> currentData.uiActPressed.g >> currentData.uiActPressed.b >> currentData.uiActPressed.a; }
        }
    }
    finishEntity();

    if (parsedEntities.empty()) return nullptr;

    std::map<int, SlipEntity*> createdEntities;
    SlipEntity* rootResult = nullptr;

    for (const auto& data : parsedEntities) {
        SlipEntity* ent = new SlipEntity(data.name);
        ent->position = data.pos;
        ent->rotation = data.rot;
        ent->scale = data.scale;

        if (auto tc = ent->GetComponent<TransformComponent>()) {
            tc->position = data.pos;
            tc->rotation = data.rot;
            tc->scale = data.scale;
            tc->inheritScale = data.inheritScale;
        }

        if (data.hasMesh && !data.meshPath.empty()) {
            auto mesh = ent->AddComponent<MeshComponent>(data.meshPath);
            if (!data.matPath.empty()) {
                mesh->SetMaterial(data.matPath);
            }
            for (const auto& sm : data.subMaterials) {
                mesh->SetSubmeshMaterial(sm.first, sm.second);
            }
        }

        if (data.hasSkinned && !data.skinnedModelPath.empty()) {
            auto skinned = ent->AddComponent<SkinnedMeshComponent>(data.skinnedModelPath);
            if (!data.skinnedMatPath.empty()) {
                skinned->SetMaterial(data.skinnedMatPath);
            }
            for (const auto& sm : data.skinnedSubMaterials) {
                skinned->SetSubmeshMaterial(sm.first, sm.second);
            }
            skinned->castShadows = data.skinnedCastShadows;
        }

        if (data.hasAnimator) {
            auto anim = ent->GetComponent<AnimatorComponent>();
            if (!anim) anim = ent->AddComponent<AnimatorComponent>();
            anim->playbackSpeed = data.animSpeed;
            anim->isLooping = data.animLooping;
            anim->isPlaying = data.animPlaying;
            anim->currentTime = data.animTime;
            if (!data.animControllerPath.empty()) {
                anim->SetController(data.animControllerPath);
            }
            if (data.animClipIdx >= 0) {
                anim->currentClipIndex = data.animClipIdx;
            }
            if (!data.animClipName.empty()) {
                for (size_t c = 0; c < anim->clips.size(); ++c) {
                    if (anim->clips[c] && anim->clips[c]->name == data.animClipName) {
                        anim->currentClipIndex = static_cast<int>(c);
                        break;
                    }
                }
            }
        }

        if (data.hasBone) {
            auto bone = ent->AddComponent<BoneComponent>(data.boneIdx, data.boneName);
        }

        if (data.hasLight) {
            auto light = ent->AddComponent<LightComponent>(data.lightType);
            light->color = data.lightColor;
            light->intensity = data.lightIntensity;
            light->range = data.lightRange;
            light->spotAngle = data.lightSpotAngle;
            light->castShadows = data.lightCastShadows;
        }

        if (data.hasDDGI) {
            auto volume = ent->AddComponent<DDGIVolumeComponent>();
            volume->size = data.ddgiSize;
            volume->probesX = data.ddgiProbesX;
            volume->probesY = data.ddgiProbesY;
            volume->probesZ = data.ddgiProbesZ;
            volume->previewProbes = data.ddgiPreview;
        }

        if (data.hasReflectionProbe) {
            auto probe = ent->AddComponent<ReflectionProbeComponent>();
            probe->size = data.probeSize;
            probe->resolution = data.probeResolution;
            probe->intensity = data.probeIntensity;
            probe->roughnessCutoff = data.probeRoughnessCutoff;
            probe->boxProjection = data.probeBoxProjection;
            probe->includeDynamic = data.probeIncludeDynamic;
        }

        if (data.hasPost) {
            auto post = ent->AddComponent<PostProcessVolumeComponent>();
            for (const auto& kv : data.postKeys) {
                post->DeserializeKey(kv.first, kv.second);
            }
        }

        if (data.hasBoxCol) {
            auto box = ent->AddComponent<BoxColliderComponent>(data.boxSize);
            box->centerOffset = data.boxOffset;
            box->isTrigger = data.boxTrigger;
        }
        if (data.hasSphereCol) {
            auto sphere = ent->AddComponent<SphereColliderComponent>(data.sphereRadius);
            sphere->centerOffset = data.sphereOffset;
            sphere->isTrigger = data.sphereTrigger;
        }
        if (data.hasCapCol) {
            auto cap = ent->AddComponent<CapsuleColliderComponent>(data.capRadius, data.capHeight);
            cap->centerOffset = data.capOffset;
            cap->isTrigger = data.capTrigger;
        }
        if (data.hasMeshCol) {
            auto meshCol = ent->AddComponent<MeshColliderComponent>();
            meshCol->isConvex = data.meshConvex;
            meshCol->centerOffset = data.meshOffset;
            meshCol->isTrigger = data.meshTrigger;
            meshCol->CreateOrUpdateShape();
            if (!meshCol->rigidBody) {
                meshCol->RecreateRigidBody(0.0f, false);
            }
        }

        if (data.hasRb) {
            auto rb = ent->AddComponent<RigidBodyComponent>();
            rb->mass = data.rbMass;
            rb->linearDrag = data.rbLinearDrag;
            rb->angularDrag = data.rbAngularDrag;
            rb->isKinematic = data.rbKinematic;
            rb->useGravity = data.rbUseGravity;
            rb->friction = data.rbFriction;
            rb->rollingFriction = data.rbRollingFriction;
            rb->restitution = data.rbRestitution;
            rb->freezePositionX = data.rbFreezePosX;
            rb->freezePositionY = data.rbFreezePosY;
            rb->freezePositionZ = data.rbFreezePosZ;
            rb->freezeRotationX = data.rbFreezeRotX;
            rb->freezeRotationY = data.rbFreezeRotY;
            rb->freezeRotationZ = data.rbFreezeRotZ;
            rb->ApplyToCollider();
        }

        if (data.hasVehicle) {
            auto veh = ent->AddComponent<VehicleComponent>();
            veh->suspensionStiffness = data.vehStiffness;
            veh->suspensionDamping = data.vehDamping;
            veh->suspensionCompression = data.vehCompression;
            veh->maxSuspensionTravelCm = data.vehMaxTravel;
            veh->frictionSlip = data.vehFrictionSlip;
            veh->maxSuspensionForce = data.vehMaxForce;
            veh->rollInfluence = data.vehRollInfluence;
            veh->chassisWidth = data.vehWidth;
            veh->chassisHeight = data.vehHeight;
            veh->chassisLength = data.vehLength;
            veh->defaultWheelRadius = data.vehWheelRadius;
            veh->SetupDefaultCarWheels();
        }

        for (const auto& scData : data.scripts) {
            if (scData.path.empty()) continue;
            auto sc = ent->AddComponent<ScriptComponent>(scData.path);
            sc->SetEnabled(scData.enabled);
            for (const auto& prop : scData.properties) {
                switch (prop.type) {
                    case ScriptPropertyType::Float: sc->SetPropertyValue(prop.name, prop.floatValue); break;
                    case ScriptPropertyType::Int: sc->SetPropertyValue(prop.name, prop.intValue); break;
                    case ScriptPropertyType::Bool: sc->SetPropertyValue(prop.name, prop.boolValue); break;
                    case ScriptPropertyType::String: sc->SetPropertyValue(prop.name, prop.stringValue); break;
                    case ScriptPropertyType::Vec3: sc->SetPropertyValue(prop.name, prop.vec3Value); break;
                }
            }
            sc->PushPropertiesToLua();
        }

        if (data.hasCam) {
            auto cam = ent->AddComponent<CameraComponent>();
            cam->projectionType = (data.camProj == "Orthographic") ? CameraProjection::Orthographic : CameraProjection::Perspective;
            cam->fov = data.camFov;
            cam->nearClip = data.camNear;
            cam->farClip = data.camFar;
            cam->orthoSize = data.camOrthoSize;
            cam->isPrimary = data.camPrimary;
            cam->clearColor = data.camClearColor;
            cam->letterbox = data.camLetterbox;
            cam->pillarbox = data.camPillarbox;
        }

        if (data.hasCinematicCam) {
            auto ccam = ent->AddComponent<CinematicCameraComponent>();
            ccam->targetEntityName = data.cinematicCamTarget;
            ccam->targetDistance = data.cinematicCamDist;
            ccam->targetHeight = data.cinematicCamHeight;
            ccam->targetPitch = data.cinematicCamPitch;
            ccam->positionLag = data.cinematicCamPosLag;
            ccam->rotationLag = data.cinematicCamRotLag;
            ccam->driftCompensation = data.cinematicCamDriftComp;
            ccam->enableDynamicFOV = data.cinematicCamDynamicFOV;
            ccam->baseFOV = data.cinematicCamBaseFOV;
            ccam->boostFOV = data.cinematicCamBoostFOV;
        }

        if (data.hasKeyframeAnim) {
            auto kfa = ent->AddComponent<KeyframeAnimationComponent>();
            kfa->playOnStart = data.keyframePlayOnStart;
            kfa->loop = data.keyframeLoop;
            kfa->playbackSpeed = data.keyframeSpeed;
            if (!data.keyframeClipPath.empty()) {
                kfa->LoadClip(data.keyframeClipPath);
            }
        }

        if (data.hasSprite) {
            auto sr = ent->AddComponent<SpriteRendererComponent>();
            sr->color = data.spriteColor;
            sr->flipX = data.spriteFlipX;
            sr->flipY = data.spriteFlipY;
            sr->sortingOrder = data.spriteSortingOrder;
            sr->orderInLayer = data.spriteSortingOrder;
            sr->sortingLayerName = data.spriteSortingLayer;
            sr->pixelsPerUnit = data.spritePPU;
            sr->hFrames = data.spriteHFrames;
            sr->vFrames = data.spriteVFrames;
            sr->currentFrame = data.spriteCurrentFrame;
            sr->isLit = data.spriteIsLit;
            sr->isPixelArt = data.spriteIsPixelArt;
            if (!data.spriteTexPath.empty()) sr->SetTexture(data.spriteTexPath);
            if (!data.spriteNormalMapPath.empty()) sr->SetNormalMap(data.spriteNormalMapPath);
        }

        if (data.hasSpriteAnimator) {
            auto sa = ent->AddComponent<SpriteAnimatorComponent>();
            sa->playOnStart = data.saPlayOnStart;
            sa->defaultClip = data.saDefaultClip;
            sa->clips = data.saClips;
            if (sa->playOnStart && !sa->defaultClip.empty()) sa->Play(sa->defaultClip);
        }

        if (data.hasBoxCollider2D) {
            auto box2d = ent->AddComponent<BoxCollider2DComponent>();
            box2d->size = data.box2dSize;
            box2d->offset = data.box2dOffset;
            box2d->isTrigger = data.box2dIsTrigger;
            box2d->density = data.box2dDensity;
            box2d->friction = data.box2dFriction;
            box2d->restitution = data.box2dRestitution;
            if (!data.hasRigidBody2D) {
                box2d->RecreateStaticBody();
            }
        }

        if (data.hasCircleCollider2D) {
            auto circ2d = ent->AddComponent<CircleCollider2DComponent>();
            circ2d->radius = data.circ2dRadius;
            circ2d->offset = data.circ2dOffset;
            circ2d->isTrigger = data.circ2dIsTrigger;
            circ2d->density = data.circ2dDensity;
            circ2d->friction = data.circ2dFriction;
            circ2d->restitution = data.circ2dRestitution;
            if (!data.hasRigidBody2D) {
                circ2d->RecreateStaticBody();
            }
        }

        if (data.hasRigidBody2D) {
            auto rb2d = ent->AddComponent<RigidBody2DComponent>();
            rb2d->bodyType = static_cast<BodyType2D>(data.rb2dBodyType);
            rb2d->mass = data.rb2dMass;
            rb2d->gravityScale = data.rb2dGravityScale;
            rb2d->freezeRotation = data.rb2dFreezeRotation;
            rb2d->linearDamping = data.rb2dLinearDamping;
            rb2d->angularDamping = data.rb2dAngularDamping;
            rb2d->RebuildPhysicsBody();
        }

        if (data.hasLight2D) {
            auto l2d = ent->AddComponent<Light2DComponent>();
            l2d->lightType = static_cast<Light2DType>(data.l2dType);
            l2d->color = data.l2dColor;
            l2d->intensity = data.l2dIntensity;
            l2d->radius = data.l2dRadius;
            l2d->innerRadius = data.l2dInnerRadius;
            l2d->spotAngle = data.l2dSpotAngle;
            l2d->castShadows = data.l2dCastShadows;
            l2d->shadowIntensity = data.l2dShadowIntensity;
        }

        if (data.hasShadowCaster2D) {
            auto sc2d = ent->AddComponent<ShadowCaster2DComponent>();
            sc2d->shape = static_cast<ShadowCasterShape2D>(data.sc2dShape);
            sc2d->size = data.sc2dSize;
            sc2d->radius = data.sc2dRadius;
            sc2d->offset = data.sc2dOffset;
            sc2d->castShadows = data.sc2dCastShadows;
        }

        if (data.hasTileGrid) {
            auto tg = ent->AddComponent<TileGridComponent>();
            tg->cellSize = data.tgCellSize;
            tg->anchor = static_cast<TileGridAnchor>(data.tgAnchor);
            tg->sheetHFrames = data.tgSheetHFrames;
            tg->sheetVFrames = data.tgSheetVFrames;
            tg->sortingLayerName = data.tgSortingLayer;
            tg->orderInLayer = data.tgSortingOrder;
            tg->tiles = data.tgTiles;
            tg->isPixelArt = data.tgIsPixelArt;
            if (!data.tgTileSheetPath.empty()) tg->SetTileSheet(data.tgTileSheetPath);
        }

        if (data.hasCanvas) {
            auto cv = ent->AddComponent<CanvasComponent>();
            cv->renderMode = (data.canvasRenderMode == "WorldSpace") ? CanvasRenderMode::WorldSpace : CanvasRenderMode::ScreenSpaceOverlay;
            cv->referenceResolution = data.canvasRefRes;
            cv->scaleWithScreenSize = data.canvasScaleWithScreen;
            cv->sortingOrder = data.canvasSortingOrder;
        }

        if (data.hasRect) {
            auto rt = ent->AddComponent<RectTransformComponent>();
            rt->SetAnchorPresetByName(data.rectAnchorPreset);
            rt->anchoredPosition = data.rectAnchoredPos;
            rt->size = data.rectSize;
            rt->pivot = data.rectPivot;
        }

        if (data.hasUIImage) {
            auto img = ent->AddComponent<UIImageComponent>();
            img->color = data.uiImgColor;
            img->preserveAspect = data.uiImgPreserveAspect;
            if (!data.uiImgTexPath.empty()) img->SetTexture(data.uiImgTexPath);
        }

        if (data.hasUIText) {
            auto txt = ent->AddComponent<UITextComponent>(data.uiTxtText);
            txt->fontPath = data.uiTxtFontPath;
            txt->fontSize = data.uiTxtFontSize;
            txt->color = data.uiTxtColor;
            txt->alignH = static_cast<UITextAlignH>(std::clamp(data.uiTxtAlignH, 0, 2));
            txt->alignV = static_cast<UITextAlignV>(std::clamp(data.uiTxtAlignV, 0, 2));
        }

        if (data.hasUIButton) {
            auto btn = ent->AddComponent<UIButtonComponent>();
            btn->interactable = data.uiBtnInteractable;
            btn->normalColor = data.uiBtnNormalCol;
            btn->hoverColor = data.uiBtnHoverCol;
            btn->pressedColor = data.uiBtnPressedCol;
            btn->disabledColor = data.uiBtnDisabledCol;
            btn->targetEntityName = data.uiBtnTargetEnt;
            btn->onClickFunction = data.uiBtnOnClickFn;
        }

        if (data.hasUISlider) {
            auto sld = ent->AddComponent<UISliderComponent>();
            sld->interactable = data.uiSldInteractable;
            sld->minValue = data.uiSldMin;
            sld->maxValue = data.uiSldMax;
            sld->value = data.uiSldVal;
            sld->wholeNumbers = data.uiSldWhole;
            sld->backgroundColor = data.uiSldBgCol;
            sld->fillColor = data.uiSldFillCol;
            sld->handleColor = data.uiSldHandleCol;
            sld->targetEntityName = data.uiSldTargetEnt;
            sld->onValueChangedFunction = data.uiSldOnValChangedFn;
        }

        if (data.hasUIProgressBar) {
            auto pb = ent->AddComponent<UIProgressBarComponent>();
            pb->minValue = data.uiPbMin;
            pb->maxValue = data.uiPbMax;
            pb->value = data.uiPbVal;
            pb->backgroundColor = data.uiPbBgCol;
            pb->fillColor = data.uiPbFillCol;
            pb->showPercentageText = data.uiPbShowPct;
        }

        if (data.hasUIJoystick) {
            auto joy = ent->AddComponent<UIJoystickComponent>();
            joy->stickName = data.uiJoyStickName;
            int mode = data.uiJoyMode;
            if (mode < 0) mode = 0;
            if (mode > 2) mode = 2;
            joy->mode = static_cast<UIJoystickMode>(mode);
            joy->travelRadius = data.uiJoyTravel;
            joy->activationRadius = data.uiJoyActivation;
            joy->backgroundColor = data.uiJoyBg;
            joy->knobColor = data.uiJoyKnob;
            joy->interactable = data.uiJoyInteractable;
        }

        if (data.hasUIActionButton) {
            auto actionBtn = ent->AddComponent<UIActionButtonComponent>();
            actionBtn->actionName = data.uiActName;
            actionBtn->interactable = data.uiActInteractable;
            actionBtn->normalColor = data.uiActNormal;
            actionBtn->pressedColor = data.uiActPressed;
        }

        if (data.hasParticle) {
            auto ps = ent->AddComponent<ParticleSystemComponent>();
            for (const auto& kv : data.particleKeys) {
                ps->DeserializeKey(kv.first, kv.second);
            }
            ps->OnInit();
        }

        if (level) {
            level->AddEntity(ent);
        }

        createdEntities[data.id] = ent;
        if (!rootResult) {
            rootResult = ent;
        }
    }

    // Reconstruir la jerarquía padre/hijo exacta
    for (const auto& data : parsedEntities) {
        if (data.parentId > 0 && createdEntities.count(data.parentId) && createdEntities.count(data.id)) {
            SlipEntity* child = createdEntities[data.id];
            SlipEntity* parent = createdEntities[data.parentId];
            child->SetParent(parent);
        }
    }

    // Restaurar jerarquía de huesos en SkinnedMeshComponents
    for (const auto& pair : createdEntities) {
        if (auto smc = pair.second->GetComponent<SkinnedMeshComponent>()) {
            smc->BuildBoneHierarchy(level);
        }
    }

    return rootResult;
}

bool SlipPrefab::SavePrefab(const std::string& filePath, SlipEntity* rootEntity) {
    if (!rootEntity || filePath.empty()) return false;
    std::string serialized = SerializeEntityToString(rootEntity);
    if (SlipFileSystem::WriteAllText(filePath, serialized)) {
        SlipConsole::LogSuccess("Prefab guardado correctamente: " + filePath);
        return true;
    }
    SlipConsole::LogError("Error al guardar Prefab: " + filePath);
    return false;
}

SlipEntity* SlipPrefab::LoadPrefab(const std::string& filePath, SlipLevel* level) {
    std::string content;
    if (!SlipFileSystem::ReadAllText(filePath, content)) {
        SlipConsole::LogError("No se pudo leer el archivo Prefab: " + filePath);
        return nullptr;
    }
    return DeserializeEntityFromString(content, level);
}

SlipEntity* SlipPrefab::CloneEntity(SlipEntity* source, SlipLevel* level) {
    if (!source) return nullptr;
    std::string serialized = SerializeEntityToString(source);
    SlipEntity* clone = DeserializeEntityFromString(serialized, level);
    if (clone) {
        clone->SetName(MakeUniqueName(source->GetName(), level, clone));
        // Desplazar ligeramente para que sea visible de inmediato si se duplica en el editor
        bool is2D = clone->HasComponent<SpriteRendererComponent>() ||
                    clone->HasComponent<BoxCollider2DComponent>() ||
                    clone->HasComponent<CircleCollider2DComponent>() ||
                    clone->HasComponent<RigidBody2DComponent>() ||
                    clone->HasComponent<TileGridComponent>();

        if (is2D) {
            if (auto tc = clone->GetComponent<TransformComponent>()) {
                tc->Translate(0.5f, 0.5f, 0.0f);
                clone->position = tc->position;
            } else {
                clone->position += glm::vec3(0.5f, 0.5f, 0.0f);
            }
            if (auto box2d = clone->GetComponent<BoxCollider2DComponent>()) {
                if (!clone->HasComponent<RigidBody2DComponent>()) {
                    box2d->RecreateStaticBody();
                }
            }
            if (auto circ2d = clone->GetComponent<CircleCollider2DComponent>()) {
                if (!clone->HasComponent<RigidBody2DComponent>()) {
                    circ2d->RecreateStaticBody();
                }
            }
        } else {
            if (auto tc = clone->GetComponent<TransformComponent>()) {
                tc->Translate(0.5f, 0.0f, 0.5f);
                clone->position = tc->position;
            } else {
                clone->position += glm::vec3(0.5f, 0.0f, 0.5f);
            }
        }
    }
    return clone;
}

std::string SlipPrefab::FindPrefabPathByName(const std::string& prefabName) {
    if (prefabName.empty()) return "";

    std::string cleanName = prefabName;
    if (cleanName.size() >= 7 && cleanName.substr(cleanName.size() - 7) == ".prefab") {
        cleanName = cleanName.substr(0, cleanName.size() - 7);
    }

    std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
    if (assetsDir.empty()) assetsDir = "assets";

    auto files = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".prefab");
    for (const auto& f : files) {
        std::string fileName = SlipFileSystem::GetFileNameWithoutExtension(f.fullPath);
        if (fileName == cleanName) {
            return f.fullPath;
        }
    }

    // Probar ruta directa
    std::string directPath = SlipFileSystem::CombinePath(assetsDir, prefabName);
    if (SlipFileSystem::Exists(directPath)) return directPath;
    if (SlipFileSystem::Exists(directPath + ".prefab")) return directPath + ".prefab";

    return "";
}

std::string SlipPrefab::MakeUniqueName(const std::string& baseName, SlipLevel* level, const SlipEntity* ignore) {
    if (baseName.empty()) return baseName;

    // Si el nombre ya termina en "_NNN", ese sufijo se descarta para generar la numeración nueva
    std::string base = baseName;
    size_t underscorePos = baseName.find_last_of('_');
    if (underscorePos != std::string::npos && underscorePos + 1 < baseName.size()) {
        bool allDigits = true;
        for (size_t i = underscorePos + 1; i < baseName.size(); ++i) {
            if (std::isdigit(static_cast<unsigned char>(baseName[i])) == 0) {
                allDigits = false;
                break;
            }
        }
        if (allDigits) base = baseName.substr(0, underscorePos);
    }

    auto entityExists = [&](const std::string& candidate) {
        if (!level) return false;
        for (SlipEntity* e : level->GetEntities()) {
            if (e && e != ignore && e->GetName() == candidate) return true;
        }
        return false;
    };

    if (!entityExists(baseName)) return baseName;

    for (int i = 1; i <= 999999; ++i) {
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "_%03d", i);
        std::string candidate = base + suffix;
        if (!entityExists(candidate)) return candidate;
    }
    return baseName;
}

SlipEntity* SlipPrefab::Instantiate(const std::string& prefabNameOrPath, SlipLevel* level,
                                   const glm::vec3& position, const glm::vec3& rotation) {
    std::string path = prefabNameOrPath;
    if (!SlipFileSystem::Exists(path)) {
        path = FindPrefabPathByName(prefabNameOrPath);
    }

    if (path.empty() || !SlipFileSystem::Exists(path)) {
        SlipConsole::LogError("SlipPrefab::Instantiate: No se encontró el prefab '" + prefabNameOrPath + "'");
        return nullptr;
    }

    SlipLevel* targetLevel = level ? level : &SlipLevel::Get();
    SlipEntity* instance = LoadPrefab(path, targetLevel);
    if (instance) {
        instance->SetName(MakeUniqueName(instance->GetName(), targetLevel, instance));
        if (auto tc = instance->GetComponent<TransformComponent>()) {
            tc->position = position;
            tc->rotation = rotation;
            instance->position = position;
            instance->rotation = rotation;
        } else {
            instance->position = position;
            instance->rotation = rotation;
        }
        for (auto& comp : instance->GetComponents()) {
            if (comp) comp->OnInit();
        }
        instance->ResetStart();
        SlipConsole::LogInfo("Prefab instanciado: " + instance->GetName() + " en (" + 
                             std::to_string(position.x) + ", " + std::to_string(position.y) + ", " + std::to_string(position.z) + ")");
    }
    return instance;
}
