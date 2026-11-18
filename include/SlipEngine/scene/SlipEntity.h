#pragma once

#ifndef SLIP_ENTITY_H
#define SLIP_ENTITY_H

#include "SlipEngine.h"

#include <string>
#include <vector>
#include <memory>
#include <type_traits>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

class SlipComponent;
class TransformComponent;

class SLIPENGINE_API SlipEntity
{
protected:
	glm::vec3 oldPos = glm::vec3(1.0f);
	glm::vec3 oldRot = glm::vec3(.0f);
	glm::vec3 oldSca = glm::vec3(1.0f);
	std::string name = "Entity";

	std::vector<std::shared_ptr<SlipComponent>> m_Components;
	std::shared_ptr<TransformComponent> m_Transform;

	SlipEntity* m_Parent = nullptr;
	std::vector<SlipEntity*> m_Children;

private:
	virtual void Start();
	bool startFunEjec = false;

	uint32_t id;
public:
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 rotation = glm::vec3(.0f);
	glm::vec3 scale = glm::vec3(1.0f);

	glm::mat4 getMatrix();

	uint32_t GetId() const { return id; }
	uint32_t GetID() const { return id; }

	const std::string& GetName() const { return name; }
	void SetName(const std::string& newName) { name = newName; }

	bool isStatic = false;
	bool IsStatic() const { return isStatic; }
	void SetStatic(bool s) { isStatic = s; }

	SlipEntity();
	SlipEntity(const std::string& entityName);
	virtual ~SlipEntity();

	void ejecStart();
	void ResetStart() { startFunEjec = false; }

	virtual void Update(float deltaTime = -1.0f);
	virtual void DrawInspectorUI();

	// Eventos de Colisión Física y Triggers
	void OnCollisionEnter(SlipEntity* other);
	void OnCollisionStay(SlipEntity* other);
	void OnCollisionExit(SlipEntity* other);
	void OnTriggerEnter(SlipEntity* other);
	void OnTriggerStay(SlipEntity* other);
	void OnTriggerExit(SlipEntity* other);

	// Acceso a Scripts
	std::shared_ptr<class ScriptComponent> GetScript(const std::string& scriptName = "");
	std::vector<std::shared_ptr<class ScriptComponent>> GetScripts();

	// --- Relación Jerárquica Padre / Hijo ---
	void SetParent(SlipEntity* parent);
	SlipEntity* GetParent() const { return m_Parent; }
	const std::vector<SlipEntity*>& GetChildren() const { return m_Children; }
	std::vector<SlipEntity*>& GetChildren() { return m_Children; }

	size_t GetChildCount() const { return m_Children.size(); }
	SlipEntity* GetChild(size_t index) const {
		if (index < m_Children.size()) return m_Children[index];
		return nullptr;
	}
	void AddChild(SlipEntity* child) {
		if (child && child != this) child->SetParent(this);
	}
	void RemoveChild(SlipEntity* child) {
		if (child && child->GetParent() == this) child->SetParent(nullptr);
	}
	void DetachFromParent() { SetParent(nullptr); }
	bool IsChildOf(const SlipEntity* potentialParent) const;
	SlipEntity* FindChild(const std::string& childName, bool recursive = false) const;
	void GetAllChildren(std::vector<SlipEntity*>& outList) const;

	// --- Persistencia entre escenas (DontDestroyOnLoad) ---
	void SetDontDestroyOnLoad(bool persistent) { m_DontDestroyOnLoad = persistent; }
	bool IsDontDestroyOnLoad() const { return m_DontDestroyOnLoad; }
	void SetPersistent(bool persistent) { m_DontDestroyOnLoad = persistent; }
	bool IsPersistent() const { return m_DontDestroyOnLoad; }
	bool IsEffectivelyPersistent() const {
		if (m_DontDestroyOnLoad) return true;
		if (m_Parent) return m_Parent->IsEffectivelyPersistent();
		return false;
	}

private:
	bool m_DontDestroyOnLoad = false;
public:

	// --- Gestión de Componentes (ECS) ---
	template<typename T, typename... Args>
	std::shared_ptr<T> AddComponent(Args&&... args) {
		static_assert(std::is_base_of<SlipComponent, T>::value, "T must derive from SlipComponent");
		auto comp = std::make_shared<T>(std::forward<Args>(args)...);
		comp->SetOwner(this);
		comp->OnInit();
		m_Components.push_back(comp);
		return comp;
	}

	template<typename T>
	std::shared_ptr<T> GetComponent() {
		for (auto& c : m_Components) {
			auto casted = std::dynamic_pointer_cast<T>(c);
			if (casted) return casted;
		}
		return nullptr;
	}

	template<typename T>
	bool HasComponent() const {
		for (const auto& c : m_Components) {
			if (std::dynamic_pointer_cast<T>(c)) return true;
		}
		return false;
	}

	template<typename T>
	bool RemoveComponent() {
		for (auto it = m_Components.begin(); it != m_Components.end(); ++it) {
			if (std::dynamic_pointer_cast<T>(*it)) {
				m_Components.erase(it);
				return true;
			}
		}
		return false;
	}

	const std::vector<std::shared_ptr<SlipComponent>>& GetComponents() const { return m_Components; }
	std::vector<std::shared_ptr<SlipComponent>>& GetComponents() { return m_Components; }
	std::shared_ptr<TransformComponent> GetTransform() const { return m_Transform; }
};

#endif // !SLIP_ENTITY_H
