#include "SlipEntity.h"
#include "SlipComponent.h"
#include "SlipID.h"
#include "SlipGlobals.h"

SlipEntity::SlipEntity()
{
	id = SlipID::Get().next();
	name = "Entity_" + std::to_string(id);

	m_Transform = std::make_shared<TransformComponent>();
	m_Transform->SetOwner(this);
	m_Transform->position = position;
	m_Transform->rotation = rotation;
	m_Transform->scale = scale;
	m_Components.push_back(m_Transform);
}

SlipEntity::SlipEntity(const std::string& entityName)
	: name(entityName)
{
	id = SlipID::Get().next();

	m_Transform = std::make_shared<TransformComponent>();
	m_Transform->SetOwner(this);
	m_Transform->position = position;
	m_Transform->rotation = rotation;
	m_Transform->scale = scale;
	m_Components.push_back(m_Transform);
}

SlipEntity::~SlipEntity()
{
	SetParent(nullptr);

	for (SlipEntity* child : m_Children) {
		if (child) child->m_Parent = nullptr;
	}
	m_Children.clear();

	m_Components.clear();
	m_Transform.reset();
}

void SlipEntity::SetParent(SlipEntity* newParent)
{
	if (newParent == m_Parent) return;
	if (newParent == this) return;

	// Prevenir ciclos en el árbol
	SlipEntity* ancestor = newParent;
	while (ancestor != nullptr) {
		if (ancestor == this) return;
		ancestor = ancestor->m_Parent;
	}

	// Remover de padre anterior
	if (m_Parent) {
		auto& pc = m_Parent->m_Children;
		pc.erase(std::remove(pc.begin(), pc.end(), this), pc.end());
	}

	m_Parent = newParent;

	// Añadir al nuevo padre
	if (m_Parent) {
		m_Parent->m_Children.push_back(this);
	}
}

bool SlipEntity::IsChildOf(const SlipEntity* potentialParent) const
{
	if (!potentialParent) return false;
	const SlipEntity* curr = m_Parent;
	while (curr != nullptr) {
		if (curr == potentialParent) return true;
		curr = curr->m_Parent;
	}
	return false;
}

SlipEntity* SlipEntity::FindChild(const std::string& childName, bool recursive) const
{
	for (SlipEntity* child : m_Children) {
		if (child && child->GetName() == childName) {
			return child;
		}
	}
	if (recursive) {
		for (SlipEntity* child : m_Children) {
			if (child) {
				SlipEntity* found = child->FindChild(childName, true);
				if (found) return found;
			}
		}
	}
	return nullptr;
}

void SlipEntity::GetAllChildren(std::vector<SlipEntity*>& outList) const
{
	for (SlipEntity* child : m_Children) {
		if (child) {
			outList.push_back(child);
			child->GetAllChildren(outList);
		}
	}
}

glm::mat4 SlipEntity::getMatrix()
{
	glm::mat4 localMatrix;
	auto boneComp = GetComponent<BoneComponent>();
	if (boneComp) {
		if (m_Transform) {
			position = m_Transform->position;
			scale = m_Transform->scale;
		}
		glm::mat4 translate = glm::translate(glm::mat4(1.0f), position);
		glm::mat4 rotMat = glm::mat4_cast(boneComp->orientation);
		glm::mat4 scaling = glm::scale(glm::mat4(1.0f), scale);
		localMatrix = translate * rotMat * scaling;
	} else if (m_Transform) {
		position = m_Transform->position;
		rotation = m_Transform->rotation;
		scale = m_Transform->scale;
		localMatrix = m_Transform->GetLocalMatrix();
	} else {
		glm::mat4 translate = glm::translate(glm::mat4(1.0f), position);
		glm::mat4 scaling = glm::scale(glm::mat4(1.0f), scale);

		glm::mat4 rotationX = glm::rotate(glm::mat4(1.f), glm::radians(rotation.x), glm::vec3(1.f, 0.f, 0.f));
		glm::mat4 rotationY = glm::rotate(glm::mat4(1.f), glm::radians(rotation.y), glm::vec3(0.f, 1.f, 0.f));
		glm::mat4 rotationZ = glm::rotate(glm::mat4(1.f), glm::radians(rotation.z), glm::vec3(0.f, 0.f, 1.f));

		localMatrix = translate * rotationX * rotationY * rotationZ * scaling;
	}

	if (m_Parent) {
		bool shouldInheritScale = true;
		if (m_Transform && !m_Transform->inheritScale) {
			shouldInheritScale = false;
		}

		// Si el padre tiene un VehicleComponent y este hijo es una rueda/esfera visual, proteger su escala
		if (m_Parent->GetComponent<VehicleComponent>() != nullptr) {
			std::string n = name;
			for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (n.find("chasis") == std::string::npos &&
			    n.find("chassis") == std::string::npos &&
			    n.find("carroceria") == std::string::npos &&
			    n.find("body") == std::string::npos) {
				shouldInheritScale = false;
				if (m_Transform) m_Transform->inheritScale = false;
			}
		}

		if (!shouldInheritScale) {
			// Extraer matriz del padre sin escala (solo posición y rotación ortonormalizada)
			// para no deformar a este hijo cuando el padre tiene escala no uniforme (ej. chasis de auto)
			glm::mat4 parentMat = m_Parent->getMatrix();
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
			return parentRotOnly * localMatrix;
		}
		return m_Parent->getMatrix() * localMatrix;
	}
	return localMatrix;
}

void SlipEntity::DrawInspectorUI()
{
	for (auto& comp : m_Components) {
		if (comp) comp->OnDrawInspectorUI();
	}
}

void SlipEntity::ejecStart()
{
	if (!startFunEjec)
	{
		Start();
		for (auto& comp : m_Components) {
			if (comp) comp->OnStart();
		}
		startFunEjec = true;
	}
}

void SlipEntity::Start()
{
}

void SlipEntity::Update(float deltaTime)
{
	float dt = (deltaTime > 0.0f) ? deltaTime : SlipGlobals::Get().GetDeltaTime();
	for (auto& comp : m_Components) {
		if (comp && comp->IsEnabled()) {
			comp->OnUpdate(dt);
		}
	}
}

std::shared_ptr<ScriptComponent> SlipEntity::GetScript(const std::string& scriptName) {
	if (scriptName.empty()) {
		return GetComponent<ScriptComponent>();
	}

	std::string cleanQuery = scriptName;
	if (cleanQuery.size() >= 4 && cleanQuery.substr(cleanQuery.size() - 4) == ".lua") {
		cleanQuery = cleanQuery.substr(0, cleanQuery.size() - 4);
	}
	for (char& c : cleanQuery) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

	for (auto& c : m_Components) {
		auto sc = std::dynamic_pointer_cast<ScriptComponent>(c);
		if (sc) {
			std::string scName = sc->GetScriptNameWithoutExtension();
			for (char& c : scName) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (scName == cleanQuery) {
				return sc;
			}
		}
	}
	return nullptr;
}

std::vector<std::shared_ptr<ScriptComponent>> SlipEntity::GetScripts() {
	std::vector<std::shared_ptr<ScriptComponent>> result;
	for (auto& c : m_Components) {
		auto sc = std::dynamic_pointer_cast<ScriptComponent>(c);
		if (sc) {
			result.push_back(sc);
		}
	}
	return result;
}

void SlipEntity::OnCollisionEnter(SlipEntity* other) {
	for (auto& c : m_Components) {
		if (!c || !c->IsEnabled()) continue;
		if (auto sc = std::dynamic_pointer_cast<ScriptComponent>(c)) {
			sc->OnCollisionEnter(other);
		}
	}
}

void SlipEntity::OnCollisionStay(SlipEntity* other) {
	for (auto& c : m_Components) {
		if (!c || !c->IsEnabled()) continue;
		if (auto sc = std::dynamic_pointer_cast<ScriptComponent>(c)) {
			sc->OnCollisionStay(other);
		}
	}
}

void SlipEntity::OnCollisionExit(SlipEntity* other) {
	for (auto& c : m_Components) {
		if (!c || !c->IsEnabled()) continue;
		if (auto sc = std::dynamic_pointer_cast<ScriptComponent>(c)) {
			sc->OnCollisionExit(other);
		}
	}
}

void SlipEntity::OnTriggerEnter(SlipEntity* other) {
	for (auto& c : m_Components) {
		if (!c || !c->IsEnabled()) continue;
		if (auto sc = std::dynamic_pointer_cast<ScriptComponent>(c)) {
			sc->OnTriggerEnter(other);
		}
	}
}

void SlipEntity::OnTriggerStay(SlipEntity* other) {
	for (auto& c : m_Components) {
		if (!c || !c->IsEnabled()) continue;
		if (auto sc = std::dynamic_pointer_cast<ScriptComponent>(c)) {
			sc->OnTriggerStay(other);
		}
	}
}

void SlipEntity::OnTriggerExit(SlipEntity* other) {
	for (auto& c : m_Components) {
		if (!c || !c->IsEnabled()) continue;
		if (auto sc = std::dynamic_pointer_cast<ScriptComponent>(c)) {
			sc->OnTriggerExit(other);
		}
	}
}
