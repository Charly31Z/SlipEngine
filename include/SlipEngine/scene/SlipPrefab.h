#pragma once

#ifndef SLIP_PREFAB_H
#define SLIP_PREFAB_H

#include "SlipEngine/core/SlipExport.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

class SlipEntity;
class SlipLevel;

class SLIPENGINE_API SlipPrefab {
public:
    // Serializar una jerarquía completa de entidades a texto
    static std::string SerializeEntityToString(SlipEntity* root);

    // Deserializar una jerarquía completa de entidades desde texto e insertarla en el nivel
    static SlipEntity* DeserializeEntityFromString(const std::string& data, SlipLevel* level);

    // Guardar una entidad y todos sus hijos como un archivo .prefab
    static bool SavePrefab(const std::string& filePath, SlipEntity* rootEntity);

    // Cargar e instanciar un archivo .prefab en el nivel
    static SlipEntity* LoadPrefab(const std::string& filePath, SlipLevel* level);

    // Clonar una entidad y toda su jerarquía en el nivel (para Duplicar / Copiar / Pegar)
    static SlipEntity* CloneEntity(SlipEntity* source, SlipLevel* level);

    // Instanciar un prefab por nombre o ruta, ubicándolo en una posición y rotación dadas
    static SlipEntity* Instantiate(const std::string& prefabNameOrPath, SlipLevel* level, 
                                   const glm::vec3& position = glm::vec3(0.0f), 
                                   const glm::vec3& rotation = glm::vec3(0.0f));

    // Buscar la ruta completa de un prefab dentro de assets/ por su nombre (ej. "Player" o "Player.prefab")
    static std::string FindPrefabPathByName(const std::string& prefabName);

    // Genera un nombre único en el nivel usando sufijo numérico "_001", "_002", ...
    // Si el nombre ya termina en "_NNN" ese sufijo se reemplaza por el primero libre.
    // 'ignore' permite excluir una entidad (ej. la entidad que se está renombrando a sí misma).
    static std::string MakeUniqueName(const std::string& baseName, SlipLevel* level, const SlipEntity* ignore = nullptr);
};

#endif // !SLIP_PREFAB_H

