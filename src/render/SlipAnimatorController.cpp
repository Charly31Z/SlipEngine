#include "SlipEngine/render/SlipAnimatorController.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/core/SlipConsole.h"
#include <sstream>
#include <cmath>
#include <algorithm>

bool AnimatorCondition::Evaluate(const AnimatorParameter& param) const {
    switch (mode) {
        case AnimatorConditionMode::Greater:
            if (param.type == AnimatorParamType::Float) return param.floatValue > thresholdFloat;
            if (param.type == AnimatorParamType::Int) return param.intValue > thresholdInt;
            return false;
        case AnimatorConditionMode::Less:
            if (param.type == AnimatorParamType::Float) return param.floatValue < thresholdFloat;
            if (param.type == AnimatorParamType::Int) return param.intValue < thresholdInt;
            return false;
        case AnimatorConditionMode::Equals:
            if (param.type == AnimatorParamType::Float) return std::abs(param.floatValue - thresholdFloat) < 1e-4f;
            if (param.type == AnimatorParamType::Int) return param.intValue == thresholdInt;
            if (param.type == AnimatorParamType::Bool) return param.boolValue == (thresholdInt != 0);
            return false;
        case AnimatorConditionMode::NotEqual:
            if (param.type == AnimatorParamType::Float) return std::abs(param.floatValue - thresholdFloat) >= 1e-4f;
            if (param.type == AnimatorParamType::Int) return param.intValue != thresholdInt;
            if (param.type == AnimatorParamType::Bool) return param.boolValue != (thresholdInt != 0);
            return false;
        case AnimatorConditionMode::IfTrue:
            if (param.type == AnimatorParamType::Bool || param.type == AnimatorParamType::Trigger) return param.boolValue;
            return false;
        case AnimatorConditionMode::IfFalse:
            if (param.type == AnimatorParamType::Bool) return !param.boolValue;
            return false;
    }
    return false;
}

bool AnimatorTransition::CanTransition(const std::unordered_map<std::string, AnimatorParameter>& params, float normalizedTime) const {
    if (hasExitTime && normalizedTime < exitTime) {
        return false;
    }

    if (conditions.empty()) {
        return hasExitTime;
    }

    for (const auto& cond : conditions) {
        auto it = params.find(cond.parameter);
        if (it == params.end()) return false;
        if (!cond.Evaluate(it->second)) return false;
    }

    return true;
}

SlipAnimatorController::SlipAnimatorController() {
    name = "NewAnimatorController";
}

void SlipAnimatorController::SetFloat(const std::string& paramName, float val) {
    auto it = parameters.find(paramName);
    if (it != parameters.end()) {
        it->second.floatValue = val;
    } else {
        AnimatorParameter p;
        p.name = paramName;
        p.type = AnimatorParamType::Float;
        p.floatValue = val;
        parameters[paramName] = p;
    }
}

void SlipAnimatorController::SetBool(const std::string& paramName, bool val) {
    auto it = parameters.find(paramName);
    if (it != parameters.end()) {
        it->second.boolValue = val;
    } else {
        AnimatorParameter p;
        p.name = paramName;
        p.type = AnimatorParamType::Bool;
        p.boolValue = val;
        parameters[paramName] = p;
    }
}

void SlipAnimatorController::SetInt(const std::string& paramName, int val) {
    auto it = parameters.find(paramName);
    if (it != parameters.end()) {
        it->second.intValue = val;
    } else {
        AnimatorParameter p;
        p.name = paramName;
        p.type = AnimatorParamType::Int;
        p.intValue = val;
        parameters[paramName] = p;
    }
}

void SlipAnimatorController::SetTrigger(const std::string& paramName) {
    auto it = parameters.find(paramName);
    if (it != parameters.end()) {
        it->second.boolValue = true;
    } else {
        AnimatorParameter p;
        p.name = paramName;
        p.type = AnimatorParamType::Trigger;
        p.boolValue = true;
        parameters[paramName] = p;
    }
}

void SlipAnimatorController::ResetTrigger(const std::string& paramName) {
    auto it = parameters.find(paramName);
    if (it != parameters.end() && it->second.type == AnimatorParamType::Trigger) {
        it->second.boolValue = false;
    }
}

float SlipAnimatorController::GetFloat(const std::string& paramName) const {
    auto it = parameters.find(paramName);
    if (it != parameters.end()) return it->second.floatValue;
    return 0.0f;
}

bool SlipAnimatorController::GetBool(const std::string& paramName) const {
    auto it = parameters.find(paramName);
    if (it != parameters.end()) return it->second.boolValue;
    return false;
}

int SlipAnimatorController::GetInt(const std::string& paramName) const {
    auto it = parameters.find(paramName);
    if (it != parameters.end()) return it->second.intValue;
    return 0;
}

bool SlipAnimatorController::HasParameter(const std::string& paramName) const {
    return parameters.find(paramName) != parameters.end();
}

void SlipAnimatorController::AddParameter(const std::string& paramName, AnimatorParamType type) {
    AnimatorParameter p;
    p.name = paramName;
    p.type = type;
    parameters[paramName] = p;
}

void SlipAnimatorController::RemoveParameter(const std::string& paramName) {
    parameters.erase(paramName);
    for (auto& st : states) {
        for (auto& tr : st.transitions) {
            auto it = tr.conditions.begin();
            while (it != tr.conditions.end()) {
                if (it->parameter == paramName) it = tr.conditions.erase(it);
                else ++it;
            }
        }
    }
}

void SlipAnimatorController::RenameParameter(const std::string& oldName, const std::string& newName) {
    if (oldName == newName || newName.empty()) return;
    auto it = parameters.find(oldName);
    if (it != parameters.end()) {
        AnimatorParameter p = it->second;
        p.name = newName;
        parameters.erase(it);
        parameters[newName] = p;
    }
    for (auto& st : states) {
        for (auto& tr : st.transitions) {
            for (auto& cond : tr.conditions) {
                if (cond.parameter == oldName) cond.parameter = newName;
            }
        }
    }
}

int SlipAnimatorController::AddState(const std::string& stateName, const std::string& clipName) {
    for (size_t i = 0; i < states.size(); ++i) {
        if (states[i].name == stateName) {
            states[i].clipName = clipName;
            return static_cast<int>(i);
        }
    }
    AnimatorState s;
    s.name = stateName;
    s.clipName = clipName;
    s.posX = 150.0f + states.size() * 180.0f;
    s.posY = 120.0f;
    states.push_back(s);
    if (defaultState.empty()) defaultState = stateName;
    return static_cast<int>(states.size() - 1);
}

void SlipAnimatorController::RemoveState(const std::string& stateName) {
    for (auto it = states.begin(); it != states.end(); ) {
        if (it->name == stateName) {
            it = states.erase(it);
        } else {
            // Eliminar transiciones que apunten a este estado
            auto trIt = it->transitions.begin();
            while (trIt != it->transitions.end()) {
                if (trIt->destinationState == stateName) trIt = it->transitions.erase(trIt);
                else ++trIt;
            }
            ++it;
        }
    }
    if (defaultState == stateName) {
        defaultState = states.empty() ? "" : states[0].name;
    }
}

void SlipAnimatorController::RenameState(const std::string& oldName, const std::string& newName) {
    if (oldName == newName || newName.empty()) return;
    for (auto& s : states) {
        if (s.name == oldName) s.name = newName;
        for (auto& tr : s.transitions) {
            if (tr.destinationState == oldName) tr.destinationState = newName;
        }
    }
    if (defaultState == oldName) defaultState = newName;
}

bool SlipAnimatorController::AddTransition(const std::string& fromState, const std::string& toState, float duration) {
    if (fromState.empty() || toState.empty()) return false;
    AnimatorState* st = GetState(fromState);
    if (!st) return false;
    for (const auto& tr : st->transitions) {
        if (tr.destinationState == toState) return false; // Ya existe
    }
    AnimatorTransition tr;
    tr.destinationState = toState;
    tr.duration = duration;
    tr.hasExitTime = false;
    st->transitions.push_back(tr);
    return true;
}

void SlipAnimatorController::RemoveTransition(const std::string& fromState, int transitionIndex) {
    AnimatorState* st = GetState(fromState);
    if (!st) return;
    if (transitionIndex >= 0 && transitionIndex < static_cast<int>(st->transitions.size())) {
        st->transitions.erase(st->transitions.begin() + transitionIndex);
    }
}

AnimatorState* SlipAnimatorController::GetState(const std::string& stateName) {
    for (auto& s : states) {
        if (s.name == stateName) return &s;
    }
    return nullptr;
}

AnimatorState* SlipAnimatorController::GetState(int index) {
    if (index >= 0 && index < static_cast<int>(states.size())) return &states[index];
    return nullptr;
}

int SlipAnimatorController::GetStateIndex(const std::string& stateName) const {
    for (size_t i = 0; i < states.size(); ++i) {
        if (states[i].name == stateName) return static_cast<int>(i);
    }
    return -1;
}

void SlipAnimatorController::Reset() {
    currentStateIndex = -1;
    currentStateTime = 0.0f;
    isTransitioning = false;
    targetStateIndex = -1;
    targetStateTime = 0.0f;
    transitionProgress = 0.0f;
}

static AnimationClip* FindClipByName(const std::string& name, const std::vector<std::shared_ptr<AnimationClip>>& clips) {
    if (name.empty()) return clips.empty() ? nullptr : clips[0].get();
    for (const auto& c : clips) {
        if (c && (c->name == name || SlipFileSystem::GetFileNameWithoutExtension(name) == c->name || SlipFileSystem::GetFileName(name) == c->name)) {
            return c.get();
        }
    }
    if (!clips.empty()) return clips[0].get();
    return nullptr;
}

void SlipAnimatorController::Update(float deltaTime, const std::vector<std::shared_ptr<AnimationClip>>& availableClips) {
    if (states.empty()) return;

    // Inicializar estado por defecto si aún no está asignado
    if (currentStateIndex < 0 || currentStateIndex >= static_cast<int>(states.size())) {
        int defIdx = GetStateIndex(defaultState);
        currentStateIndex = (defIdx >= 0) ? defIdx : 0;
        currentStateTime = 0.0f;
        isTransitioning = false;
    }

    AnimatorState& currState = states[currentStateIndex];
    AnimationClip* currClip = FindClipByName(currState.clipName, availableClips);
    float currDuration = (currClip && currClip->duration > 0.0f) ? currClip->duration : 1.0f;

    // Avanzar tiempo del estado actual
    currentStateTime += deltaTime * currState.speed;
    if (currState.isLooping) {
        currentStateTime = std::fmod(currentStateTime, currDuration);
        if (currentStateTime < 0.0f) currentStateTime += currDuration;
    } else {
        if (currentStateTime > currDuration) currentStateTime = currDuration;
    }

    float normalizedTime = currDuration > 0.0f ? (currentStateTime / currDuration) : 0.0f;

    // Si estamos en transición, avanzar la mezcla crossfade
    if (isTransitioning) {
        if (targetStateIndex >= 0 && targetStateIndex < static_cast<int>(states.size())) {
            AnimatorState& targetState = states[targetStateIndex];
            AnimationClip* targetClip = FindClipByName(targetState.clipName, availableClips);
            float targetDuration = (targetClip && targetClip->duration > 0.0f) ? targetClip->duration : 1.0f;

            targetStateTime += deltaTime * targetState.speed;
            if (targetState.isLooping) {
                targetStateTime = std::fmod(targetStateTime, targetDuration);
                if (targetStateTime < 0.0f) targetStateTime += targetDuration;
            } else {
                if (targetStateTime > targetDuration) targetStateTime = targetDuration;
            }

            float dtProgress = (transitionDuration > 0.001f) ? (deltaTime / transitionDuration) : 1.0f;
            transitionProgress += dtProgress;

            if (transitionProgress >= 1.0f) {
                // Transición completada
                currentStateIndex = targetStateIndex;
                currentStateTime = targetStateTime;
                isTransitioning = false;
                targetStateIndex = -1;
                targetStateTime = 0.0f;
                transitionProgress = 0.0f;
            }
        } else {
            isTransitioning = false;
        }
        return;
    }

    // Evaluar transiciones desde el estado actual
    for (const auto& trans : currState.transitions) {
        if (trans.CanTransition(parameters, normalizedTime)) {
            int destIdx = GetStateIndex(trans.destinationState);
            if (destIdx >= 0 && destIdx != currentStateIndex) {
                isTransitioning = true;
                targetStateIndex = destIdx;
                targetStateTime = 0.0f;
                transitionProgress = 0.0f;
                transitionDuration = (trans.duration > 0.001f) ? trans.duration : 0.001f;

                // Consumir triggers utilizados en las condiciones
                for (const auto& cond : trans.conditions) {
                    ResetTrigger(cond.parameter);
                }
                break;
            }
        }
    }
}

SlipAnimatorController::ActivePoseInfo SlipAnimatorController::GetActivePose(const std::vector<std::shared_ptr<AnimationClip>>& availableClips) const {
    ActivePoseInfo pose{};
    if (currentStateIndex < 0 || currentStateIndex >= static_cast<int>(states.size())) {
        return pose;
    }

    const AnimatorState& currState = states[currentStateIndex];
    pose.clipA = FindClipByName(currState.clipName, availableClips);
    pose.timeA = currentStateTime;

    if (isTransitioning && targetStateIndex >= 0 && targetStateIndex < static_cast<int>(states.size())) {
        const AnimatorState& targetState = states[targetStateIndex];
        pose.clipB = FindClipByName(targetState.clipName, availableClips);
        pose.timeB = targetStateTime;
        pose.blendWeight = glm::clamp(transitionProgress, 0.0f, 1.0f);
    } else {
        pose.clipB = nullptr;
        pose.timeB = 0.0f;
        pose.blendWeight = 0.0f;
    }

    return pose;
}

bool SlipAnimatorController::SaveToFile(const std::string& filePath) {
    std::string target = filePath.empty() ? path : filePath;
    if (target.empty()) {
        target = "assets/Animations/" + name + ".controller";
    }
    if (SlipFileSystem::GetFileExtension(target) != ".controller") {
        target += ".controller";
    }

    std::string parentDir = SlipFileSystem::GetParentPath(target);
    if (!parentDir.empty()) SlipFileSystem::CreateDirectoryPath(parentDir);

    std::stringstream ss;
    ss << "# SlipEngine Animator Controller\n";
    ss << "AnimatorController:\n";
    ss << "  Name: " << name << "\n";
    ss << "  DefaultState: " << defaultState << "\n";

    ss << "  Parameters:\n";
    for (const auto& pair : parameters) {
        const auto& p = pair.second;
        ss << "    Parameter: " << p.name << " ";
        switch (p.type) {
            case AnimatorParamType::Float: ss << "float " << p.floatValue << "\n"; break;
            case AnimatorParamType::Int: ss << "int " << p.intValue << "\n"; break;
            case AnimatorParamType::Bool: ss << "bool " << (p.boolValue ? 1 : 0) << "\n"; break;
            case AnimatorParamType::Trigger: ss << "trigger " << (p.boolValue ? 1 : 0) << "\n"; break;
        }
    }

    ss << "  States:\n";
    for (const auto& s : states) {
        ss << "    State:\n";
        ss << "      Name: " << s.name << "\n";
        ss << "      Clip: " << s.clipName << "\n";
        ss << "      Speed: " << s.speed << "\n";
        ss << "      Loop: " << (s.isLooping ? 1 : 0) << "\n";
        ss << "      Position: " << s.posX << " " << s.posY << "\n";
        for (const auto& t : s.transitions) {
            ss << "      Transition: " << t.destinationState << " " << t.duration << " " << (t.hasExitTime ? 1 : 0) << " " << t.exitTime << "\n";
            for (const auto& c : t.conditions) {
                ss << "        Condition: " << c.parameter << " " << static_cast<int>(c.mode) << " " << c.thresholdFloat << " " << c.thresholdInt << "\n";
            }
        }
    }

    this->path = target;
    return SlipFileSystem::WriteAllText(target, ss.str());
}

bool SlipAnimatorController::LoadFromFile(const std::string& filePath) {
    std::string content;
    if (!SlipFileSystem::ReadAllText(filePath, content)) return false;

    this->path = filePath;
    states.clear();
    parameters.clear();
    defaultState.clear();
    Reset();

    std::stringstream ss(content);
    std::string line;
    AnimatorState* currentStateBuilding = nullptr;
    AnimatorTransition* currentTransBuilding = nullptr;

    while (std::getline(ss, line)) {
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') continue;
        std::string trimmed = line.substr(first);
        size_t last = trimmed.find_last_not_of(" \t\r\n");
        if (last != std::string::npos) trimmed = trimmed.substr(0, last + 1);

        if (trimmed.rfind("Name:", 0) == 0) {
            std::string val = trimmed.substr(5);
            val.erase(0, val.find_first_not_of(" \t"));
            if (currentStateBuilding) currentStateBuilding->name = val;
            else this->name = val;
        } else if (trimmed.rfind("DefaultState:", 0) == 0) {
            std::string val = trimmed.substr(13);
            val.erase(0, val.find_first_not_of(" \t"));
            this->defaultState = val;
        } else if (trimmed.rfind("Parameter:", 0) == 0) {
            std::stringstream ps(trimmed.substr(10));
            std::string pName, pTypeStr;
            ps >> pName >> pTypeStr;
            AnimatorParameter p;
            p.name = pName;
            if (pTypeStr == "float") {
                p.type = AnimatorParamType::Float;
                ps >> p.floatValue;
            } else if (pTypeStr == "int") {
                p.type = AnimatorParamType::Int;
                ps >> p.intValue;
            } else if (pTypeStr == "bool") {
                p.type = AnimatorParamType::Bool;
                int b = 0; ps >> b; p.boolValue = (b != 0);
            } else if (pTypeStr == "trigger") {
                p.type = AnimatorParamType::Trigger;
                int b = 0; ps >> b; p.boolValue = (b != 0);
            }
            parameters[pName] = p;
        } else if (trimmed == "State:") {
            states.emplace_back();
            currentStateBuilding = &states.back();
            currentTransBuilding = nullptr;
        } else if (currentStateBuilding && trimmed.rfind("Clip:", 0) == 0) {
            std::string val = trimmed.substr(5);
            val.erase(0, val.find_first_not_of(" \t"));
            currentStateBuilding->clipName = val;
        } else if (currentStateBuilding && trimmed.rfind("Speed:", 0) == 0) {
            std::stringstream ps(trimmed.substr(6));
            ps >> currentStateBuilding->speed;
        } else if (currentStateBuilding && trimmed.rfind("Loop:", 0) == 0) {
            int lp = 1;
            std::stringstream ps(trimmed.substr(5));
            ps >> lp;
            currentStateBuilding->isLooping = (lp != 0);
        } else if (currentStateBuilding && trimmed.rfind("Position:", 0) == 0) {
            std::stringstream ps(trimmed.substr(9));
            ps >> currentStateBuilding->posX >> currentStateBuilding->posY;
        } else if (currentStateBuilding && trimmed.rfind("Transition:", 0) == 0) {
            std::stringstream ts(trimmed.substr(11));
            AnimatorTransition trans;
            int het = 0;
            ts >> trans.destinationState >> trans.duration >> het >> trans.exitTime;
            trans.hasExitTime = (het != 0);
            currentStateBuilding->transitions.push_back(trans);
            currentTransBuilding = &currentStateBuilding->transitions.back();
        } else if (currentTransBuilding && trimmed.rfind("Condition:", 0) == 0) {
            std::stringstream cs(trimmed.substr(10));
            AnimatorCondition cond;
            int m = 0;
            cs >> cond.parameter >> m >> cond.thresholdFloat >> cond.thresholdInt;
            cond.mode = static_cast<AnimatorConditionMode>(m);
            currentTransBuilding->conditions.push_back(cond);
        }
    }

    if (!defaultState.empty()) {
        currentStateIndex = GetStateIndex(defaultState);
    } else if (!states.empty()) {
        currentStateIndex = 0;
        defaultState = states[0].name;
    }
    return true;
}

