#pragma once

#ifndef SLIP_ANIMATOR_CONTROLLER_H
#define SLIP_ANIMATOR_CONTROLLER_H

#include "SlipEngine/core/SlipExport.h"
#include "SlipEngine/render/SlipAnimation.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

enum class AnimatorParamType {
    Float = 0,
    Int = 1,
    Bool = 2,
    Trigger = 3
};

struct SLIPENGINE_API AnimatorParameter {
    std::string name;
    AnimatorParamType type = AnimatorParamType::Float;
    float floatValue = 0.0f;
    int intValue = 0;
    bool boolValue = false;
};

enum class AnimatorConditionMode {
    Greater = 0,
    Less = 1,
    Equals = 2,
    NotEqual = 3,
    IfTrue = 4,
    IfFalse = 5
};

struct SLIPENGINE_API AnimatorCondition {
    std::string parameter;
    AnimatorConditionMode mode = AnimatorConditionMode::Greater;
    float thresholdFloat = 0.0f;
    int thresholdInt = 0;

    bool Evaluate(const AnimatorParameter& param) const;
};

struct SLIPENGINE_API AnimatorTransition {
    std::string destinationState;
    float duration = 0.25f; // Segundos de crossfade
    bool hasExitTime = false;
    float exitTime = 0.9f;  // Fracción 0.0..1.0 de la animación
    std::vector<AnimatorCondition> conditions;

    bool CanTransition(const std::unordered_map<std::string, AnimatorParameter>& params, float normalizedTime) const;
};

struct SLIPENGINE_API AnimatorState {
    std::string name;
    std::string clipName;    // Nombre del clip en el Animator o nombre del .anim
    float speed = 1.0f;
    bool isLooping = true;
    float posX = 200.0f;
    float posY = 150.0f;
    std::vector<AnimatorTransition> transitions;
};

class SLIPENGINE_API SlipAnimatorController {
public:
    std::string name = "NewController";
    std::string path;
    std::vector<AnimatorState> states;
    std::unordered_map<std::string, AnimatorParameter> parameters;
    std::string defaultState;

    // Runtime state
    int currentStateIndex = -1;
    float currentStateTime = 0.0f;

    bool isTransitioning = false;
    int targetStateIndex = -1;
    float targetStateTime = 0.0f;
    float transitionProgress = 0.0f; // 0.0 -> 1.0
    float transitionDuration = 0.25f;

    SlipAnimatorController();
    ~SlipAnimatorController() = default;

    // Gestión de Parámetros
    void SetFloat(const std::string& paramName, float val);
    void SetBool(const std::string& paramName, bool val);
    void SetInt(const std::string& paramName, int val);
    void SetTrigger(const std::string& paramName);
    void ResetTrigger(const std::string& paramName);

    float GetFloat(const std::string& paramName) const;
    bool GetBool(const std::string& paramName) const;
    int GetInt(const std::string& paramName) const;
    bool HasParameter(const std::string& paramName) const;
    void AddParameter(const std::string& paramName, AnimatorParamType type);
    void RemoveParameter(const std::string& paramName);
    void RenameParameter(const std::string& oldName, const std::string& newName);

    // Gestión de Estados y Transiciones
    int AddState(const std::string& stateName, const std::string& clipName = "");
    void RemoveState(const std::string& stateName);
    void RenameState(const std::string& oldName, const std::string& newName);
    bool AddTransition(const std::string& fromState, const std::string& toState, float duration = 0.25f);
    void RemoveTransition(const std::string& fromState, int transitionIndex);
    AnimatorState* GetState(const std::string& stateName);
    AnimatorState* GetState(int index);
    int GetStateIndex(const std::string& stateName) const;

    // Actualización de máquina de estados
    void Update(float deltaTime, const std::vector<std::shared_ptr<AnimationClip>>& availableClips);

    // Obtener información de pose actual para aplicar
    struct ActivePoseInfo {
        AnimationClip* clipA = nullptr;
        float timeA = 0.0f;
        AnimationClip* clipB = nullptr;
        float timeB = 0.0f;
        float blendWeight = 0.0f; // 0.0 = 100% A, 1.0 = 100% B
    };
    ActivePoseInfo GetActivePose(const std::vector<std::shared_ptr<AnimationClip>>& availableClips) const;

    // Serialización / Guardado
    bool SaveToFile(const std::string& filePath = "");
    bool LoadFromFile(const std::string& filePath);

    void Reset();
};

#endif // !SLIP_ANIMATOR_CONTROLLER_H

