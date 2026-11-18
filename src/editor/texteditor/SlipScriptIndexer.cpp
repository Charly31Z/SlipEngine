#include "SlipEngine/editor/texteditor/SlipScriptIndexer.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include <regex>
#include <sstream>
#include <algorithm>

SlipScriptIndexer& SlipScriptIndexer::Get() {
    if (!s_Instance) {
        static SlipScriptIndexer instance;
        s_Instance = &instance;
    }
    return *s_Instance;
}

SlipScriptIndexer::SlipScriptIndexer() {
    RegisterEngineSymbols();
    m_AllSymbols = m_EngineSymbols;
}

void SlipScriptIndexer::RegisterEngineSymbols() {
    m_EngineSymbols.clear();

    auto addEngine = [this](const std::string& name, const std::string& display, const std::string& sig, const std::string& doc, SymbolType type) {
        ScriptSymbol sym;
        sym.name = name;
        sym.displayName = display;
        sym.signature = sig;
        sym.description = doc;
        sym.sourceFile = "[SlipEngine C++ Native]";
        sym.line = 0;
        sym.type = type;
        m_EngineSymbols.push_back(sym);
    };

    // Logging
    addEngine("Slip.Log", "Slip.Log(msg)", "(msg: string)", "Registra un mensaje informativo en la consola de SlipEngine.", SymbolType::EngineFunction);
    addEngine("Slip.LogWarning", "Slip.LogWarning(msg)", "(msg: string)", "Registra una advertencia en la consola de SlipEngine.", SymbolType::EngineFunction);
    addEngine("Slip.LogError", "Slip.LogError(msg)", "(msg: string)", "Registra un error crítico en la consola de SlipEngine.", SymbolType::EngineFunction);

    // Time
    addEngine("Slip.Time.deltaTime", "Slip.Time.deltaTime", "number", "Tiempo transcurrido desde el último frame en segundos.", SymbolType::EngineProperty);
    addEngine("Slip.Time.GetDeltaTime", "Slip.Time.GetDeltaTime()", "() -> number", "Devuelve deltaTime en segundos.", SymbolType::EngineFunction);

    // Input
    addEngine("Slip.Input.GetKey", "Slip.Input.GetKey(key)", "(key: int) -> bool", "Comprueba si una tecla SDL está presionada.", SymbolType::EngineFunction);
    addEngine("Slip.Input.IsKeyPressed", "Slip.Input.IsKeyPressed(key)", "(key: int) -> bool", "Verdadero mientras la tecla especificada permanezca pulsada.", SymbolType::EngineFunction);
    addEngine("Slip.Input.GetMouseButton", "Slip.Input.GetMouseButton(button)", "(button: int) -> bool", "Comprueba estado de botón del ratón (0: Izq, 1: Der, 2: Medio).", SymbolType::EngineFunction);
    addEngine("Slip.Input.GetMousePosition", "Slip.Input.GetMousePosition()", "() -> Vec2", "Devuelve la posición absoluta del cursor en pantalla.", SymbolType::EngineFunction);
    addEngine("Slip.Input.GetMouseDelta", "Slip.Input.GetMouseDelta()", "() -> Vec2", "Movimiento del ratón en este frame, en píxeles. X a la derecha, Y hacia abajo.", SymbolType::EngineFunction);
    addEngine("Slip.Input.GetMouseWheel", "Slip.Input.GetMouseWheel()", "() -> Vec2", "Giros de la rueda en este frame. Y positivo aleja del usuario.", SymbolType::EngineFunction);
    addEngine("Slip.Input.SetCursorLocked", "Slip.Input.SetCursorLocked(locked)", "(locked: bool)", "Bloquea el cursor al centro y lo esconde (modo relativo, como la cámara en primera persona).", SymbolType::EngineFunction);
    addEngine("Slip.Input.SetCursorVisible", "Slip.Input.SetCursorVisible(visible)", "(visible: bool)", "Muestra u oculta el cursor. Si está bloqueado, permanece oculto hasta liberarlo.", SymbolType::EngineFunction);
    addEngine("Slip.Input.IsCursorLocked", "Slip.Input.IsCursorLocked()", "() -> bool", "Verdadero si el cursor está bloqueado en este momento.", SymbolType::EngineFunction);
    addEngine("Slip.Input.IsCursorVisible", "Slip.Input.IsCursorVisible()", "() -> bool", "Verdadero si el cursor se está dibujando.", SymbolType::EngineFunction);
    addEngine("Slip.Input.GetAction", "Slip.Input.GetAction(name)", "(name: string) -> bool", "Verdadero mientras la accion del proyecto (por ejemplo 'Jump') permanezca pulsada.", SymbolType::EngineFunction);
    addEngine("Slip.Input.GetActionDown", "Slip.Input.GetActionDown(name)", "(name: string) -> bool", "Verdadero solo en el frame en que la accion empieza a pulsarse.", SymbolType::EngineFunction);
    addEngine("Slip.Input.GetActionUp", "Slip.Input.GetActionUp(name)", "(name: string) -> bool", "Verdadero solo en el frame en que la accion se suelta.", SymbolType::EngineFunction);
    addEngine("Slip.Input.GetAxis", "Slip.Input.GetAxis(axisName)", "(axisName: string) -> number (-1.0 a 1.0)", "Devuelve un eje de un solo eje definido en la configuracion del proyecto.", SymbolType::EngineFunction);

    // Scene
    addEngine("Slip.Scene.FindEntity", "Slip.Scene.FindEntity(name)", "(name: string) -> Entity", "Busca y devuelve una entidad activa por su nombre en la escena.", SymbolType::EngineFunction);
    addEngine("Slip.Scene.CreateEntity", "Slip.Scene.CreateEntity(name)", "(name: string) -> Entity", "Crea una nueva entidad vacía en la escena.", SymbolType::EngineFunction);
    addEngine("Slip.Scene.Destroy", "Slip.Scene.Destroy(entity)", "(entity: Entity)", "Elimina la entidad especificada de la escena activa.", SymbolType::EngineFunction);
    addEngine("Slip.Scene.Instantiate", "Slip.Scene.Instantiate(prefab, pos, rot)", "(prefabName: string, pos: Vec3, rot: Vec3) -> Entity", "Instancia un prefab guardado en assets/Prefabs/.", SymbolType::EngineFunction);

    // Math Types
    addEngine("Vec2", "Vec2.new(x, y)", "(x: number, y: number) -> Vec2", "Estructura vectorial 2D para coordenadas y físicas.", SymbolType::EngineClass);
    addEngine("Vec3", "Vec3.new(x, y, z)", "(x: number, y: number, z: number) -> Vec3", "Estructura vectorial 3D para posición, rotación y escala.", SymbolType::EngineClass);
    addEngine("Vec4", "Vec4.new(r, g, b, a)", "(r: number, g: number, b: number, a: number) -> Vec4", "Estructura vectorial 4D para colores RGBA y rectángulos.", SymbolType::EngineClass);

    // 2D & UI Components (Unity3D style)
    addEngine("GetSpriteRenderer", "Entity:GetSpriteRenderer()", "() -> SpriteRenderer", "Obtiene el componente SpriteRenderer 2D de la entidad.", SymbolType::EngineFunction);
    addEngine("GetRectTransform", "Entity:GetRectTransform()", "() -> RectTransform", "Obtiene el RectTransform de un elemento UI.", SymbolType::EngineFunction);
    addEngine("GetUIImage", "Entity:GetUIImage()", "() -> UIImage", "Obtiene el componente de imagen UI.", SymbolType::EngineFunction);
    addEngine("GetUIText", "Entity:GetUIText()", "() -> UIText", "Obtiene el componente de texto UI.", SymbolType::EngineFunction);
    addEngine("GetUIButton", "Entity:GetUIButton()", "() -> UIButton", "Obtiene el componente de botón interactivo UI.", SymbolType::EngineFunction);
    addEngine("GetUISlider", "Entity:GetUISlider()", "() -> UISlider", "Obtiene el componente de control deslizante (Slider) UI.", SymbolType::EngineFunction);
    addEngine("GetUIProgressBar", "Entity:GetUIProgressBar()", "() -> UIProgressBar", "Obtiene el componente de barra de progreso UI.", SymbolType::EngineFunction);
    addEngine("SetText", "UIText:SetText(text)", "(text: string)", "Actualiza el texto mostrado en un UIText.", SymbolType::EngineFunction);
    addEngine("SetTexture", "SpriteRenderer:SetTexture(path)", "(path: string)", "Carga y asigna una nueva textura al SpriteRenderer o UIImage.", SymbolType::EngineFunction);
    addEngine("OnClick", "UIButton:OnClick(callback)", "(callback: function())", "Registra un callback Lua ejecutado al hacer clic en el botón.", SymbolType::EngineFunction);
    addEngine("OnValueChanged", "UISlider:OnValueChanged(callback)", "(callback: function(value))", "Registra un callback Lua ejecutado al cambiar el valor del slider.", SymbolType::EngineFunction);
    addEngine("SetValue", "UIProgressBar:SetValue(val)", "(val: number)", "Actualiza el valor numérico de un UISlider o UIProgressBar.", SymbolType::EngineFunction);

    // TransformComponent Methods & Properties
    addEngine("Translate", "Transform:Translate(x, y, z)", "(x: number, y: number, z: number) o (delta: Vec3)", "Desplaza la entidad en el espacio local.", SymbolType::EngineFunction);
    addEngine("Rotate", "Transform:Rotate(x, y, z)", "(x: number, y: number, z: number) o (angles: Vec3)", "Aplica rotación en grados a la entidad.", SymbolType::EngineFunction);
    addEngine("position", "Transform.position", "Vec3", "Posición actual de la entidad en el espacio del mundo.", SymbolType::EngineProperty);
    addEngine("rotation", "Transform.rotation", "Vec3", "Ángulos de rotación Euler (en grados).", SymbolType::EngineProperty);
    addEngine("scale", "Transform.scale", "Vec3", "Escala de la entidad en los ejes X, Y, Z.", SymbolType::EngineProperty);

    // RigidBodyComponent Methods & Properties
    addEngine("AddForce", "RigidBody:AddForce(x, y, z)", "(x: number, y: number, z: number) o (force: Vec3)", "Aplica una fuerza continua al cuerpo rígido.", SymbolType::EngineFunction);
    addEngine("AddImpulse", "RigidBody:AddImpulse(x, y, z)", "(x: number, y: number, z: number) o (impulse: Vec3)", "Aplica un impulso instantáneo modificando la velocidad.", SymbolType::EngineFunction);
    addEngine("AddTorque", "RigidBody:AddTorque(x, y, z)", "(x: number, y: number, z: number) o (torque: Vec3)", "Aplica torque angular al cuerpo rígido.", SymbolType::EngineFunction);
    addEngine("SetLinearVelocity", "RigidBody:SetLinearVelocity(vel)", "(vel: Vec3)", "Establece la velocidad lineal directa del cuerpo físico.", SymbolType::EngineFunction);
    addEngine("GetLinearVelocity", "RigidBody:GetLinearVelocity()", "() -> Vec3", "Devuelve la velocidad lineal actual.", SymbolType::EngineFunction);
    addEngine("SetAngularVelocity", "RigidBody:SetAngularVelocity(vel)", "(vel: Vec3)", "Establece la velocidad de rotación angular.", SymbolType::EngineFunction);
    addEngine("GetAngularVelocity", "RigidBody:GetAngularVelocity()", "() -> Vec3", "Devuelve la velocidad angular actual.", SymbolType::EngineFunction);
    addEngine("mass", "RigidBody.mass", "number", "Masa del cuerpo rígido en kilogramos.", SymbolType::EngineProperty);

    // Entity Methods
    addEngine("GetName", "Entity:GetName()", "() -> string", "Devuelve el nombre de la entidad.", SymbolType::EngineFunction);
    addEngine("SetName", "Entity:SetName(name)", "(name: string)", "Cambia el nombre de la entidad.", SymbolType::EngineFunction);
    addEngine("GetID", "Entity:GetID()", "() -> int", "Devuelve el identificador único entero de la entidad.", SymbolType::EngineFunction);
    addEngine("GetTransform", "Entity:GetTransform()", "() -> Transform", "Devuelve el componente TransformComponent asociado.", SymbolType::EngineFunction);
    addEngine("GetRigidBody", "Entity:GetRigidBody()", "() -> RigidBody", "Devuelve el componente RigidBodyComponent si existe.", SymbolType::EngineFunction);
    addEngine("GetParent", "Entity:GetParent()", "() -> Entity", "Devuelve la entidad padre en la jerarquía.", SymbolType::EngineFunction);
    addEngine("SetParent", "Entity:SetParent(parent)", "(parent: Entity)", "Establece una nueva entidad padre.", SymbolType::EngineFunction);
    addEngine("FindChild", "Entity:FindChild(childName)", "(childName: string) -> Entity", "Busca y devuelve una entidad hija directa por su nombre.", SymbolType::EngineFunction);
    addEngine("GetChildren", "Entity:GetChildren()", "() -> table", "Devuelve la lista de entidades hijas.", SymbolType::EngineFunction);
    addEngine("GetScript", "Entity:GetScript(scriptName)", "(scriptName: string) -> table", "Obtiene la instancia de script Lua asociada a la entidad.", SymbolType::EngineFunction);
    addEngine("GetScripts", "Entity:GetScripts()", "() -> table", "Obtiene una tabla con todos los scripts asignados a la entidad.", SymbolType::EngineFunction);
    addEngine("GetComponent", "Entity:GetComponent(typeName)", "(typeName: string) -> Component", "Obtiene un componente por nombre (ej: 'Transform', 'RigidBody', 'BoxCollider').", SymbolType::EngineFunction);
    addEngine("Destroy", "Entity:Destroy()", "()", "Destruye esta entidad y la retira de la escena.", SymbolType::EngineFunction);

    // Script Lifecycle Callbacks
    addEngine("OnStart", "function Script:OnStart()", "()", "Método llamado al iniciar la simulación (Play Mode).", SymbolType::GlobalFunction);
    addEngine("OnUpdate", "function Script:OnUpdate(dt)", "(dt: number)", "Método llamado en cada fotograma con el tiempo delta transcurrido.", SymbolType::GlobalFunction);
    addEngine("OnCollisionEnter", "function Script:OnCollisionEnter(other)", "(other: Entity)", "Evento llamado cuando dos colliders comienzan a colisionar.", SymbolType::GlobalFunction);
    addEngine("OnCollisionStay", "function Script:OnCollisionStay(other)", "(other: Entity)", "Evento llamado mientras dos colliders permanezcan en contacto.", SymbolType::GlobalFunction);
    addEngine("OnCollisionExit", "function Script:OnCollisionExit(other)", "(other: Entity)", "Evento llamado cuando dos colliders dejan de colisionar.", SymbolType::GlobalFunction);
    addEngine("OnTriggerEnter", "function Script:OnTriggerEnter(other)", "(other: Entity)", "Evento disparado cuando un collider entra en el trigger.", SymbolType::GlobalFunction);
    addEngine("OnTriggerStay", "function Script:OnTriggerStay(other)", "(other: Entity)", "Evento disparado mientras permanezca dentro del trigger.", SymbolType::GlobalFunction);
    addEngine("OnTriggerExit", "function Script:OnTriggerExit(other)", "(other: Entity)", "Evento disparado al salir del volumen del trigger.", SymbolType::GlobalFunction);
}

static std::string ToLowerString(std::string str) {
    for (char& c : str) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return str;
}

static std::string ExtractBaseScriptKey(const std::string& scriptNameOrPath) {
    std::string filename = SlipFileSystem::GetFileName(scriptNameOrPath);
    if (filename.size() >= 4 && ToLowerString(filename.substr(filename.size() - 4)) == ".lua") {
        filename = filename.substr(0, filename.size() - 4);
    }
    return ToLowerString(filename);
}

void SlipScriptIndexer::RefreshIndex(const std::string& assetsPath) {
    m_FileSymbols.clear();
    m_ScriptFileRegistry.clear();

    if (!assetsPath.empty() && SlipFileSystem::Exists(assetsPath)) {
        std::vector<std::string> stack;
        stack.push_back(assetsPath);

        while (!stack.empty()) {
            std::string currentDir = stack.back();
            stack.pop_back();

            auto entries = SlipFileSystem::ListDirectory(currentDir);
            for (const auto& entry : entries) {
                if (entry.isDirectory) {
                    stack.push_back(entry.fullPath);
                } else if (entry.extension == ".lua") {
                    RegisterScriptFile(entry.fullPath);
                    std::string content;
                    if (SlipFileSystem::ReadAllText(entry.fullPath, content)) {
                        IndexScript(entry.fullPath, content);
                    }
                }
            }
        }
    }

    // Reconstruir lista consolidada
    m_AllSymbols = m_EngineSymbols;
    for (const auto& [path, symList] : m_FileSymbols) {
        m_AllSymbols.insert(m_AllSymbols.end(), symList.begin(), symList.end());
    }
}

bool SlipScriptIndexer::IsScriptNameUnique(const std::string& scriptName, const std::string& currentFilePath) const {
    std::string key = ExtractBaseScriptKey(scriptName);
    if (key.empty()) return false;

    auto it = m_ScriptFileRegistry.find(key);
    if (it == m_ScriptFileRegistry.end()) {
        return true;
    }

    if (!currentFilePath.empty()) {
        std::string currentKey = ExtractBaseScriptKey(currentFilePath);
        if (currentKey == key) {
            return true;
        }
    }

    return false;
}

std::string SlipScriptIndexer::FindScriptPathByName(const std::string& scriptName) const {
    std::string key = ExtractBaseScriptKey(scriptName);
    auto it = m_ScriptFileRegistry.find(key);
    if (it != m_ScriptFileRegistry.end()) {
        return it->second;
    }
    return "";
}

void SlipScriptIndexer::RegisterScriptFile(const std::string& filePath) {
    if (filePath.empty()) return;
    std::string key = ExtractBaseScriptKey(filePath);
    if (!key.empty()) {
        m_ScriptFileRegistry[key] = filePath;
    }
}

void SlipScriptIndexer::UnregisterScriptFile(const std::string& filePath) {
    if (filePath.empty()) return;
    std::string key = ExtractBaseScriptKey(filePath);
    m_ScriptFileRegistry.erase(key);
}

void SlipScriptIndexer::IndexScript(const std::string& filePath, const std::string& content) {
    RegisterScriptFile(filePath);
    std::vector<ScriptSymbol> symbols;
    std::istringstream stream(content);
    std::string line;
    int lineNum = 0;
    std::string lastComment;

    // Regex para:
    // 1. function FuncName(args)
    // 2. function Module.FuncName(args) / function Module:FuncName(args)
    // 3. local TableName = {} o TableName = {}
    static const std::regex funcRegex(R"-(function\s+([a-zA-Z0-9_\.:]+)\s*\((.*?)\))-");
    static const std::regex tableRegex(R"-((?:local\s+)?([a-zA-Z0-9_]+)\s*=\s*\{\})-");
    static const std::regex commentRegex(R"-(\s*--\s*(.*))-");

    while (std::getline(stream, line)) {
        lineNum++;

        // Capturar comentarios de documentación inmediatos previos
        std::smatch commentMatch;
        if (std::regex_match(line, commentMatch, commentRegex)) {
            if (!lastComment.empty()) lastComment += " ";
            lastComment += commentMatch[1].str();
            continue;
        }

        std::smatch match;
        if (std::regex_search(line, match, funcRegex)) {
            std::string fullName = match[1].str();
            std::string params = match[2].str();

            ScriptSymbol sym;
            sym.name = fullName;
            sym.sourceFile = filePath;
            sym.line = lineNum;
            sym.signature = "(" + params + ")";
            sym.description = lastComment.empty() ? ("Definida en " + SlipFileSystem::GetFileName(filePath)) : lastComment;

            if (fullName.find('.') != std::string::npos || fullName.find(':') != std::string::npos) {
                sym.type = SymbolType::ModuleFunction;
                sym.displayName = fullName + sym.signature;
            } else {
                sym.type = SymbolType::GlobalFunction;
                sym.displayName = fullName + sym.signature;
            }

            symbols.push_back(sym);
            lastComment.clear();
            continue;
        }

        if (std::regex_search(line, match, tableRegex)) {
            std::string tableName = match[1].str();
            ScriptSymbol sym;
            sym.name = tableName;
            sym.displayName = tableName + " = {}";
            sym.signature = "table";
            sym.sourceFile = filePath;
            sym.line = lineNum;
            sym.type = SymbolType::Variable;
            sym.description = lastComment.empty() ? ("Tabla/Módulo en " + SlipFileSystem::GetFileName(filePath)) : lastComment;
            symbols.push_back(sym);
            lastComment.clear();
            continue;
        }

        // Si la línea no es comentario ni declaración, resetear comentario previo
        if (!line.empty() && line.find_first_not_of(" \t\r\n") != std::string::npos) {
            lastComment.clear();
        }
    }

    m_FileSymbols[filePath] = symbols;

    // Actualizar consolidado
    m_AllSymbols = m_EngineSymbols;
    for (const auto& [path, symList] : m_FileSymbols) {
        m_AllSymbols.insert(m_AllSymbols.end(), symList.begin(), symList.end());
    }
}

static std::string ToLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::vector<ScriptSymbol> SlipScriptIndexer::QuerySymbols(const std::string& prefix, const std::string& currentContext) const {
    std::vector<ScriptSymbol> matches;
    if (prefix.empty()) {
        // Al pulsar Ctrl+Espacio sin texto previo, retornar símbolos globales y de motor más relevantes
        for (const auto& sym : m_AllSymbols) {
            matches.push_back(sym);
            if (matches.size() >= 30) break;
        }
        return matches;
    }

    std::string cleanPrefix = prefix;
    bool isColon = false;
    if (!cleanPrefix.empty() && cleanPrefix.back() == ':') {
        isColon = true;
        cleanPrefix = "";
    } else if (cleanPrefix.find(':') != std::string::npos) {
        cleanPrefix = cleanPrefix.substr(cleanPrefix.find_last_of(':') + 1);
        isColon = true;
    }

    std::string lowerPrefix = ToLower(cleanPrefix);

    for (const auto& sym : m_AllSymbols) {
        if (isColon) {
            // Filtrar preferentemente funciones y métodos invocables con ':'
            if (sym.type != SymbolType::EngineFunction && sym.type != SymbolType::ModuleFunction) {
                continue;
            }
        }

        if (lowerPrefix.empty()) {
            matches.push_back(sym);
        } else {
            std::string lowerName = ToLower(sym.name);
            std::string lowerDisplay = ToLower(sym.displayName);

            // Búsqueda por prefijo o contiene
            if (lowerName.find(lowerPrefix) == 0 || lowerDisplay.find(lowerPrefix) == 0) {
                matches.push_back(sym);
            } else if (lowerName.find(lowerPrefix) != std::string::npos) {
                matches.push_back(sym);
            }
        }

        if (matches.size() >= 30) {
            break; // Limitar resultados para óptimo rendimiento UI
        }
    }

    if (!lowerPrefix.empty()) {
        // Ordenar resultados priorizando coincidencias exactas de prefijo
        std::sort(matches.begin(), matches.end(), [&lowerPrefix](const ScriptSymbol& a, const ScriptSymbol& b) {
            std::string la = ToLower(a.name);
            std::string lb = ToLower(b.name);
            bool aStarts = (la.find(lowerPrefix) == 0);
            bool bStarts = (lb.find(lowerPrefix) == 0);
            if (aStarts != bStarts) return aStarts;
            return a.name < b.name;
        });
    }

    return matches;
}

const ScriptSymbol* SlipScriptIndexer::FindDefinition(const std::string& symbolName) const {
    if (symbolName.empty()) return nullptr;

    // Buscar primero en scripts de usuario
    for (const auto& [path, symList] : m_FileSymbols) {
        for (const auto& sym : symList) {
            if (sym.name == symbolName) {
                return &sym;
            }
            // Si el nombre buscado es el método sin el prefijo del módulo
            size_t dotPos = sym.name.find_last_of(".:");
            if (dotPos != std::string::npos) {
                std::string member = sym.name.substr(dotPos + 1);
                if (member == symbolName) {
                    return &sym;
                }
            }
        }
    }

    // Buscar en símbolos del motor
    for (const auto& sym : m_EngineSymbols) {
        if (sym.name == symbolName) {
            return &sym;
        }
    }

    return nullptr;
}

