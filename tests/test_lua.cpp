#include "SlipEngine/scripting/SlipLuaEngine.h"
#include "SlipEngine/platform/SlipInputMap.h"
#include "SlipEngine/platform/SlipInput.h"
#include "SlipEngine/editor/texteditor/SlipScriptIndexer.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/scene/SlipPrefab.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/physics/SlipPhysics.h"
#include <iostream>
#include <cassert>

int main(int argc, char* argv[]) {
    std::cout << "=== Running SlipEngine Lua & Intellisense Tests ===" << std::endl;

    // 1. Test SlipLuaEngine initialization
    auto& lua = SlipLuaEngine::Get();
    lua.Init();
    std::cout << "[PASS] SlipLuaEngine initialized." << std::endl;

    // 2. Test basic execution and Slip.Log
    bool execOk = lua.ExecuteString(R"(
        Slip.Log("Hola desde test en Lua!")
        testVar = 42
    )", "test_basic");
    assert(execOk);
    int testVar = lua.GetState()["testVar"];
    assert(testVar == 42);
    std::cout << "[PASS] Basic string execution and global reading: testVar = " << testVar << std::endl;

    // 3. Test Vec3 operations in Lua
    execOk = lua.ExecuteString(R"(
        local v1 = Vec3.new(1, 2, 3)
        local v2 = Vec3.new(4, 5, 6)
        local v3 = v1 + v2
        assert(v3.x == 5 and v3.y == 7 and v3.z == 9, "Vec3 addition mismatch")
        vLength = v1:Length()
    )", "test_vec3");
    assert(execOk);
    std::cout << "[PASS] Vec3 math bindings in Lua." << std::endl;

    // 4. Test SlipEntity and Transform manipulation from Lua
    SlipEntity ent("PlayerEntity");
    auto transform = ent.GetComponent<TransformComponent>();
    transform->position = glm::vec3(10.0f, 0.0f, -5.0f);

    lua.GetState()["testEnt"] = &ent;
    execOk = lua.ExecuteString(R"(
        local t = testEnt:GetTransform()
        t:Translate(1.0, 2.0, 3.0)
        t:Rotate(0.0, 45.0, 0.0)
    )", "test_entity");
    assert(execOk);
    assert(transform->position.x == 11.0f);
    assert(transform->position.y == 2.0f);
    assert(transform->position.z == -2.0f);
    assert(transform->rotation.y == 45.0f);
    std::cout << "[PASS] Entity & Transform manipulation: pos=(" 
              << transform->position.x << ", " << transform->position.y << ", " << transform->position.z << ")" << std::endl;

    // 5. Test ScriptComponent with sample script
    std::string sampleScriptPath = "test_script_lifecycle.lua";
    std::string sampleCode = R"(
        local MyScript = {}
        function MyScript:OnStart()
            Slip.Log("OnStart ejecutado en " .. self.name)
            self:GetTransform():Translate(10, 0, 0)
        end
        function MyScript:OnUpdate(dt)
            self:GetTransform():Rotate(0, 10 * dt, 0)
        end
        return MyScript
    )";
    SlipFileSystem::WriteAllText(sampleScriptPath, sampleCode);

    auto scriptComp = ent.AddComponent<ScriptComponent>(sampleScriptPath);
    assert(scriptComp->isCompiled);
    scriptComp->OnStart();
    assert(transform->position.x == 21.0f); // 11 + 10
    scriptComp->OnUpdate(0.5f);
    assert(transform->rotation.y == 50.0f); // 45 + 10 * 0.5
    std::cout << "[PASS] ScriptComponent lifecycle: OnStart & OnUpdate executed correctly." << std::endl;

    // Test NuevoScript.lua directly
    std::cout << "Testing NuevoScript.lua..." << std::endl;
    std::string nuevoScriptPath = "C:/Projects/SlipEngine/build/Debug/NuevoProyecto/assets/NuevoScript.lua";
    if (SlipFileSystem::Exists(nuevoScriptPath)) {
        SlipEntity testEnt2("Cube");
        auto sc2 = testEnt2.AddComponent<ScriptComponent>(nuevoScriptPath);
        std::cout << "sc2 isCompiled: " << sc2->isCompiled << std::endl;
        sc2->OnStart();
        sc2->OnUpdate(0.016f);
        std::cout << "[PASS] NuevoScript.lua executed successfully!" << std::endl;
    }

    // 6. Test SlipScriptIndexer (parsing & cross-script references)
    std::string scriptBPath = "test_module_b.lua";
    std::string scriptBCode = R"(
        -- Modulo de utilidad matematico
        local MathUtil = {}
        -- Calcula la distancia euclidiana entre dos puntos
        function MathUtil.Distance(p1, p2)
            return (p1 - p2):Length()
        end
        -- Funcion global de salud
        function PlayerGlobalHeal(amount)
            Slip.Log("Curando jugador: " .. tostring(amount))
        end
        return MathUtil
    )";
    SlipFileSystem::WriteAllText(scriptBPath, scriptBCode);

    auto& indexer = SlipScriptIndexer::Get();
    indexer.IndexScript(sampleScriptPath, sampleCode);
    indexer.IndexScript(scriptBPath, scriptBCode);

    // Query symbols with prefix "MathUtil"
    auto results = indexer.QuerySymbols("MathUtil");
    assert(!results.empty());
    std::cout << "[PASS] Indexer query 'MathUtil' returned " << results.size() << " match(es): " << results[0].displayName << std::endl;

    // Query global function "PlayerGlobalHeal"
    auto healResults = indexer.QuerySymbols("PlayerGlobal");
    assert(!healResults.empty());
    assert(healResults[0].name == "PlayerGlobalHeal");
    std::cout << "[PASS] Indexer query 'PlayerGlobal' found: " << healResults[0].displayName << std::endl;

    // Test 'FindDefinition' (Go to Definition)
    const ScriptSymbol* def = indexer.FindDefinition("PlayerGlobalHeal");
    assert(def != nullptr);
    assert(def->sourceFile == scriptBPath);
    std::cout << "[PASS] FindDefinition('PlayerGlobalHeal') -> " << def->sourceFile << ":" << def->line << std::endl;

    // 7. Test Script Properties (Public exposed, Private hidden)
    std::string propScriptPath = "test_script_props.lua";
    std::string propScriptCode = R"(
        local HealthScript = {}
        HealthScript.maxHealth = 100.0
        HealthScript.speed = 5
        HealthScript.isAlive = true
        HealthScript.tag = "Player"
        HealthScript._privateVal = 999 -- Variable privada oculta por '_'

        function HealthScript:TakeDamage(amount)
            self.maxHealth = self.maxHealth - amount
            if self.maxHealth <= 0 then
                self.isAlive = false
            end
            return self.maxHealth
        end
        return HealthScript
    )";
    SlipFileSystem::WriteAllText(propScriptPath, propScriptCode);

    SlipEntity warriorEnt("Warrior");
    auto warriorSc = warriorEnt.AddComponent<ScriptComponent>(propScriptPath);
    assert(warriorSc->isCompiled);

    // Verificar que las variables públicas se reflejaron y la privada se ocultó
    const auto& props = warriorSc->GetProperties();
    bool foundMaxHealth = false;
    bool foundPrivate = false;
    for (const auto& p : props) {
        if (p.name == "maxHealth") {
            foundMaxHealth = true;
            assert(p.floatValue == 100.0f);
        }
        if (p.name == "_privateVal") foundPrivate = true;
    }
    assert(foundMaxHealth);
    assert(!foundPrivate); // La privada debe estar oculta
    std::cout << "[PASS] Script Properties: public properties exposed and private '_privateVal' hidden." << std::endl;

    // Modificar propiedad desde C++ e inspeccionar sincronización en Lua
    warriorSc->SetPropertyValue("maxHealth", 250.0f);
    warriorSc->PushPropertiesToLua();
    warriorSc->OnStart();

    // 8. Test Inter-Script Communication vía entity:GetScript("...")
    std::string callerScriptPath = "test_caller_script.lua";
    std::string callerScriptCode = R"(
        local AttackScript = {}
        AttackScript.damage = 30.0

        function AttackScript:AttackTarget(targetEntity)
            local healthComp = targetEntity:GetScript("test_script_props")
            if healthComp then
                return healthComp:TakeDamage(self.damage)
            end
            return -1
        end
        return AttackScript
    )";
    SlipFileSystem::WriteAllText(callerScriptPath, callerScriptCode);

    SlipEntity enemyEnt("EnemyBoss");
    auto enemySc = enemyEnt.AddComponent<ScriptComponent>(callerScriptPath);
    assert(enemySc->isCompiled);
    enemySc->OnStart();

    // Invocar función de daño entre scripts
    lua.GetState()["warrior"] = &warriorEnt;
    lua.GetState()["enemy"] = &enemyEnt;
    bool attackOk = lua.ExecuteString(R"(
        local atk = enemy:GetScript("test_caller_script")
        assert(atk ~= nil, "Attack script not found")
        remainingHp = atk:AttackTarget(warrior)
        assert(remainingHp == 220.0, "Expected HP 220 after 30 damage from 250")
    )", "test_interscript");
    assert(attackOk);
    std::cout << "[PASS] Inter-Script function call: AttackTarget -> HealthScript:TakeDamage succeeded! remainingHp=220" << std::endl;

    // 9. Test Prefab System (Serialize, Deserialize, Clone, Instantiate)
    auto warriorMc = warriorEnt.AddComponent<MeshColliderComponent>();
    warriorMc->isConvex = true;
    warriorMc->isTrigger = true;
    warriorMc->centerOffset = glm::vec3(1.0f, 2.0f, 3.0f);

    std::string prefabSavePath = "test_warrior.prefab";
    bool savedPrefab = SlipPrefab::SavePrefab(prefabSavePath, &warriorEnt);
    assert(savedPrefab);
    assert(SlipFileSystem::Exists(prefabSavePath));

    SlipEntity* loadedPrefabEnt = SlipPrefab::LoadPrefab(prefabSavePath, nullptr);
    assert(loadedPrefabEnt != nullptr);
    assert(loadedPrefabEnt->GetName() == "Warrior");
    auto loadedSc = loadedPrefabEnt->GetScript("test_script_props");
    assert(loadedSc != nullptr);
    auto loadedMc = loadedPrefabEnt->GetComponent<MeshColliderComponent>();
    assert(loadedMc != nullptr);
    assert(loadedMc->isConvex == true);
    assert(loadedMc->isTrigger == true);
    assert(loadedMc->centerOffset.x == 1.0f && loadedMc->centerOffset.y == 2.0f && loadedMc->centerOffset.z == 3.0f);
    std::cout << "[PASS] Prefab Save & Load with MeshCollider succeeded." << std::endl;

    // Test Prefab Instantiate en Lua
    bool instOk = lua.ExecuteString(R"(
        local newEnt = Slip.Scene.Instantiate("test_warrior.prefab", Vec3.new(5, 0, 5), Vec3.new(0, 90, 0))
        assert(newEnt ~= nil, "Prefab failed to instantiate")
        assert(newEnt:GetName() == "Warrior", "Prefab entity name mismatch")
        local t = newEnt:GetTransform()
        assert(t.position.x == 5 and t.position.z == 5, "Instantiated position mismatch")
        Slip.Scene.Destroy(newEnt)
    )", "test_instantiate_lua");
    assert(instOk);
    std::cout << "[PASS] Slip.Scene.Instantiate and Slip.Scene.Destroy executed in Lua." << std::endl;

    // 10. Input map roundtrip (sin ventana SDL)
    auto& inputMap = SlipInputMap::Get();
    inputMap.ResetToDefaults();
    std::string inputPath = "test_input_settings.ini";
    assert(inputMap.SaveToFile(inputPath));
    std::string inputText;
    assert(SlipFileSystem::ReadAllText(inputPath, inputText));
    assert(inputText.find("[Action.Jump]") != std::string::npos);
    assert(inputText.find("Key:Space") != std::string::npos);
    assert(inputText.find("[Axis.Horizontal]") != std::string::npos);
    assert(inputText.find("GamepadAxis=LeftX") != std::string::npos);
    inputMap.GetActions().clear();
    inputMap.GetAxes().clear();
    assert(inputMap.LoadFromFile(inputPath));
    assert(!inputMap.GetActions().empty());
    assert(inputMap.GetActions()[0].name == "Jump");
    assert(!inputMap.GetActions()[0].bindings.empty());
    assert(SlipInputMap::BindingToString(inputMap.GetActions()[0].bindings[0]) == "Key:Space");
    assert(!inputMap.GetAxes().empty());
    assert(inputMap.GetAxes()[0].name == "Horizontal");
    assert(inputMap.GetAxis("Horizontal") == 0.0f);
    assert(inputMap.GetAxis("NoExiste") == 0.0f);
    assert(!inputMap.GetAction("NoExiste"));
    bool inputLuaOk = lua.ExecuteString(R"(
        assert(Slip.Input.GetAction("NoExiste") == false, "accion desconocida")
        assert(Slip.Input.GetAxis("Horizontal") == 0, "eje sin teclado")
        assert(Slip.Input.GetAxis("NoExiste") == 0, "eje desconocido")
    )", "test_input_map");
    assert(inputLuaOk);
    std::cout << "[PASS] Input map save/load and Lua GetAction/GetAxis." << std::endl;

    {
        SlipInput input;
        assert(input.GetMouseDelta() == glm::vec2(0.0f));
        input.SetCursorLocked(true);
        input.SetCursorVisible(false);
        assert(input.IsCursorLocked());
        assert(!input.IsCursorVisible());
        input.ToggleCursorRelease();
        assert(!input.IsCursorLocked());
        assert(input.IsCursorVisible());
        assert(input.IsCursorReleasedByUser());
        input.ToggleCursorRelease();
        assert(input.IsCursorLocked());
        input.ResetCursor();
        assert(!input.IsCursorLocked());
        assert(input.IsCursorVisible());
        assert(!input.WantsCursorCapture());
    }
    bool cursorLuaOk = lua.ExecuteString(R"(
        local delta = Slip.Input.GetMouseDelta()
        assert(delta.x == 0 and delta.y == 0, "delta sin movimiento")
        assert(Slip.Input.IsCursorLocked() == false, "cursor libre")
        assert(Slip.Input.IsCursorVisible() == true, "cursor visible")
    )", "test_cursor");
    assert(cursorLuaOk);
    std::cout << "[PASS] Cursor lock/hide and mouse delta." << std::endl;

    // -------------------------------------------------------------
    // 11. Test 2D SpriteRenderer & Unity-style UI System (Canvas, RectTransform, Button, Slider, Text, ProgressBar)
    // -------------------------------------------------------------
    std::cout << "[TEST 11] Testing 2D SpriteRenderer & Unity-style UI System..." << std::endl;
    SlipEntity* canvasEnt = new SlipEntity("MainCanvas");
    auto canvasComp = canvasEnt->AddComponent<CanvasComponent>();
    canvasComp->referenceResolution = glm::vec2(1920.0f, 1080.0f);
    SlipLevel::Get().AddEntity(canvasEnt);

    SlipEntity* heroSpriteEnt = new SlipEntity("Hero2D");
    auto srComp = heroSpriteEnt->AddComponent<SpriteRendererComponent>();
    srComp->hFrames = 4;
    srComp->vFrames = 2;
    srComp->currentFrame = 5;
    srComp->sortingOrder = 3;
    SlipLevel::Get().AddEntity(heroSpriteEnt);

    glm::vec4 uv = srComp->GetUVRect();
    assert(std::abs(uv.z - 0.25f) < 0.001f && std::abs(uv.w - 0.5f) < 0.001f);

    SlipEntity* uiPanelEnt = new SlipEntity("HUDManager");
    auto rtComp = uiPanelEnt->AddComponent<RectTransformComponent>();
    rtComp->ApplyAnchorPreset(UIAnchorPreset::TopLeft);
    rtComp->anchoredPosition = glm::vec2(100.0f, -50.0f);
    rtComp->size = glm::vec2(300.0f, 80.0f);
    auto txtComp = uiPanelEnt->AddComponent<UITextComponent>("Puntos: 0");
    auto btnComp = uiPanelEnt->AddComponent<UIButtonComponent>();
    auto sldComp = uiPanelEnt->AddComponent<UISliderComponent>();
    sldComp->minValue = 0.0f;
    sldComp->maxValue = 100.0f;
    sldComp->value = 25.0f;
    auto pbComp = uiPanelEnt->AddComponent<UIProgressBarComponent>();
    pbComp->SetValue(40.0f);
    SlipLevel::Get().AddEntity(uiPanelEnt);
    uiPanelEnt->SetParent(canvasEnt);

    std::string uiScriptPath = "UIManagerTest.lua";
    std::string uiScriptCode = R"(
local UIManagerTest = {
    clicks = 0,
    lastSliderVal = 0.0
}

function UIManagerTest:OnStart()
    local sr = Slip.Scene.FindEntity("Hero2D"):GetSpriteRenderer()
    assert(sr ~= nil, "SpriteRenderer debe existir")
    sr.flipX = true
    sr.currentFrame = 2

    local rt = self.entity:GetRectTransform()
    assert(rt ~= nil, "RectTransform debe existir")
    rt:SetAnchorPreset("BottomRight")

    local btn = self.entity:GetUIButton()
    local txt = self.entity:GetUIText()
    local pb = self.entity:GetUIProgressBar()
    local sld = self.entity:GetUISlider()

    btn:OnClick(function()
        self.clicks = self.clicks + 1
        txt:SetText("Clicks: " .. tostring(self.clicks))
        pb:SetValue(85.0)
    end)

    sld:OnValueChanged(function(val)
        self.lastSliderVal = val
    end)
end

function UIManagerTest:OnInspectorClick()
    self.clicks = self.clicks + 10
end

return UIManagerTest
)";
    SlipFileSystem::WriteAllText(uiScriptPath, uiScriptCode);
    auto uiScriptComp = uiPanelEnt->AddComponent<ScriptComponent>(uiScriptPath);
    uiScriptComp->OnStart();

    assert(srComp->flipX == true);
    assert(srComp->currentFrame == 2);
    assert(rtComp->GetAnchorPresetName() == "BottomRight");

    // Disparar clic (prueba tanto el callback Lua directo como la función de Inspector)
    btnComp->onClickFunction = "OnInspectorClick";
    btnComp->TriggerClick();
    assert(txtComp->GetText() == "Clicks: 1");
    assert(std::abs(pbComp->GetValue() - 85.0f) < 0.01f);

    // Disparar cambio de valor en el Slider
    sldComp->SetValue(72.5f, true);
    assert(std::abs(sldComp->GetValue() - 72.5f) < 0.01f);

    // Verificar serialización de Prefab con componentes 2D y UI
    std::string uiPrefabStr = SlipPrefab::SerializeEntityToString(canvasEnt);
    SlipEntity* clonedCanvas = SlipPrefab::DeserializeEntityFromString(uiPrefabStr, nullptr);
    assert(clonedCanvas != nullptr);
    assert(clonedCanvas->HasComponent<CanvasComponent>());
    assert(!clonedCanvas->GetChildren().empty());
    SlipEntity* clonedHUD = clonedCanvas->GetChildren()[0];
    assert(clonedHUD->HasComponent<RectTransformComponent>());
    assert(clonedHUD->HasComponent<UITextComponent>());
    assert(clonedHUD->HasComponent<UIButtonComponent>());
    assert(clonedHUD->HasComponent<UISliderComponent>());
    assert(clonedHUD->HasComponent<UIProgressBarComponent>());
    assert(clonedHUD->GetComponent<UITextComponent>()->GetText() == "Clicks: 1");
    for (SlipEntity* ch : clonedCanvas->GetChildren()) delete ch;
    delete clonedCanvas;
    SlipFileSystem::DeleteFileOrDir(uiScriptPath);
    std::cout << "[PASS] 2D SpriteRenderer & Unity-style UI System." << std::endl;

    // -------------------------------------------------------------
    // 12. Test Hybrid Particle System (Unity Billboards + Unreal Niagara GL_POINTS + Bullet Physics Collisions + Lua + Serialization)
    // -------------------------------------------------------------
    std::cout << "[TEST 12] Testing Hybrid Particle System (Unity Billboards & Unreal Niagara GL_POINTS)..." << std::endl;
    {
        // Inicializar motor de físicas si aún no está inicializado
        if (!SlipPhysics::IsInitialized()) {
            static SlipPhysics physInstance;
        }

        // Crear piso estático en y = 0 para probar colisiones y rebote de partículas
        SlipEntity* floorEnt = new SlipEntity("ParticleTestFloor");
        floorEnt->position = glm::vec3(0.0f, -0.5f, 0.0f);
        if (auto tc = floorEnt->GetComponent<TransformComponent>()) {
            tc->position = floorEnt->position;
        }
        auto floorCol = floorEnt->AddComponent<BoxColliderComponent>(glm::vec3(20.0f, 1.0f, 20.0f));
        floorCol->OnInit();
        SlipLevel::Get().AddEntity(floorEnt);

        // Crear emisor Niagara GL_POINTS arriba del piso con velocidad hacia abajo y rebote
        SlipEntity* niagaraEnt = new SlipEntity("NiagaraSparksEmitter");
        niagaraEnt->position = glm::vec3(0.0f, 1.0f, 0.0f);
        if (auto tc = niagaraEnt->GetComponent<TransformComponent>()) {
            tc->position = niagaraEnt->position;
        }
        auto ps = niagaraEnt->AddComponent<ParticleSystemComponent>(ParticleRenderMode::PointNiagara);
        ps->ApplyPreset("NiagaraSparks");
        ps->emissionRate = 0.0f; // Control manual por Emit()
        ps->emitterShape = ParticleEmitterShape::Point;
        ps->initialSpeedMin = 0.0f;
        ps->initialSpeedMax = 0.0f;
        ps->initialVelocityOffset = glm::vec3(0.0f, -10.0f, 0.0f); // Lanza directo hacia el piso (y=0)
        ps->gravityMultiplier = 0.0f;
        ps->linearDrag = 0.0f;
        ps->turbulenceEnabled = false;
        ps->collisionEnabled = true;
        ps->bounciness = 0.8f;
        ps->collisionFriction = 0.0f;
        ps->castShadows = true;
        ps->receiveShadows = true;
        ps->materialMode = ParticleMaterialMode::Emissive;

        // Probar evaluación de gradiente multicolor sobre el ciclo de vida
        glm::vec4 cStart = ps->EvaluateGradient(0.0f);
        glm::vec4 cMid = ps->EvaluateGradient(0.5f);
        glm::vec4 cEnd = ps->EvaluateGradient(1.0f);
        assert(cStart.a > 0.9f && cEnd.a < 0.1f);
        assert(cMid.r > 0.5f);

        // Emitir 10 partículas y simular impacto contra el piso (1.0m a -10m/s cruza y=0 en 0.15s)
        ps->Emit(10);
        assert(ps->GetActiveParticleCount() == 10);
        ps->Simulate(0.15f);
        assert(ps->GetActiveParticleCount() == 10);
        // Después de rebotar contra el piso en y=0, la velocidad Y debe haberse invertido a positiva (+8.0 m/s)
        for (const auto& p : ps->GetParticles()) {
            assert(p.velocity.y > 0.0f);
            assert(p.position.y >= 0.0f);
        }

        // Probar control desde Lua y serialización en Prefab + Escena
        SlipLevel::Get().AddEntity(niagaraEnt);
        bool luaPsOk = lua.ExecuteString(R"(
            local ent = Slip.Scene.FindEntity("NiagaraSparksEmitter")
            assert(ent ~= nil, "NiagaraSparksEmitter no encontrado")
            local ps = ent:GetParticleSystem()
            assert(ps ~= nil, "GetParticleSystem() devolvio nil")
            assert(ps:GetActiveParticleCount() == 10, "Conteo de particulas incorrecto")
            ps.bounciness = 0.92
            ps.emissiveIntensity = 7.5
            ps:Emit(5)
            assert(ps:GetActiveParticleCount() == 15, "Emit(5) desde Lua fallo")
        )", "test_particle_lua");
        assert(luaPsOk);
        assert(std::abs(ps->bounciness - 0.92f) < 0.001f);
        assert(std::abs(ps->emissiveIntensity - 7.5f) < 0.001f);

        // Probar serialización en Prefab
        std::string psPrefabStr = SlipPrefab::SerializeEntityToString(niagaraEnt);
        SlipEntity* clonedPsEnt = SlipPrefab::DeserializeEntityFromString(psPrefabStr, nullptr);
        assert(clonedPsEnt != nullptr);
        auto clonedPs = clonedPsEnt->GetComponent<ParticleSystemComponent>();
        assert(clonedPs != nullptr);
        assert(clonedPs->renderMode == ParticleRenderMode::PointNiagara);
        assert(clonedPs->materialMode == ParticleMaterialMode::Emissive);
        assert(clonedPs->collisionEnabled == true);
        assert(clonedPs->castShadows == true);
        assert(std::abs(clonedPs->bounciness - 0.92f) < 0.001f);
        assert(std::abs(clonedPs->emissiveIntensity - 7.5f) < 0.001f);
        assert(clonedPs->gradientStops.size() == ps->gradientStops.size());
        delete clonedPsEnt;

        // Probar guardado y carga de Escena completa con ParticleSystemComponent
        std::string tempScenePath = "test_particles_scene.slipscene";
        assert(SlipLevel::Get().SaveScene(tempScenePath));
        assert(SlipLevel::Get().LoadScene(tempScenePath));
        SlipEntity* scenePsEnt = nullptr;
        for (SlipEntity* e : SlipLevel::Get().GetEntities()) {
            if (e && e->GetName() == "NiagaraSparksEmitter") {
                scenePsEnt = e;
                break;
            }
        }
        assert(scenePsEnt != nullptr);
        auto scenePs = scenePsEnt->GetComponent<ParticleSystemComponent>();
        assert(scenePs != nullptr);
        assert(scenePs->renderMode == ParticleRenderMode::PointNiagara);
        assert(std::abs(scenePs->bounciness - 0.92f) < 0.001f);
        SlipFileSystem::DeleteFileOrDir(tempScenePath);
    }
    std::cout << "[PASS] Hybrid Particle System (Simulation, Collisions, Bounce, Lua & Serialization)." << std::endl;

    // 12. Test Entity Hierarchy & Persistent Entities (DontDestroyOnLoad) in Lua
    {
        SlipEntity rootEnt("RootEntity");
        SlipEntity child1("ChildOne");
        SlipEntity child2("ChildTwo");
        SlipEntity grandChild("GrandChild");

        lua.GetState()["rootEnt"] = &rootEnt;
        lua.GetState()["c1"] = &child1;
        lua.GetState()["c2"] = &child2;
        lua.GetState()["gc"] = &grandChild;

        bool hierOk = lua.ExecuteString(R"(
            rootEnt:AddChild(c1)
            rootEnt:AddChild(c2)
            c1:AddChild(gc)

            assert(c1:GetParent():GetName() == "RootEntity", "c1 parent mismatch")
            assert(gc:GetParent():GetName() == "ChildOne", "gc parent mismatch")
            assert(rootEnt:GetChildCount() == 2, "rootEnt child count mismatch")

            local children = rootEnt:GetChildren()
            assert(#children == 2, "children table count mismatch")
            assert(children[1]:GetName() == "ChildOne", "first child mismatch")

            local foundGc = rootEnt:FindChild("GrandChild", true)
            assert(foundGc ~= nil, "recursive findChild failed")
            assert(foundGc:GetName() == "GrandChild", "found name mismatch")

            assert(gc:IsChildOf(rootEnt) == true, "IsChildOf ancestor check failed")

            local allDescendants = rootEnt:GetAllChildren()
            assert(#allDescendants == 3, "GetAllChildren count mismatch")

            -- DontDestroyOnLoad
            assert(rootEnt:IsPersistent() == false, "default persistent mismatch")
            Slip.DontDestroyOnLoad(rootEnt)
            assert(rootEnt:IsPersistent() == true, "rootEnt should be persistent")
            assert(gc:IsPersistent() == false, "direct persistent should be false")

            -- Cambio de escena
            Slip.Scene.LoadScene("scenes/TestNext.scene")
        )", "test_hierarchy_and_persistence");
        assert(hierOk);

        assert(SlipLevel::Get().HasPendingSceneLoad() == true);

        // Limpieza de nivel con preservacion de persistentes
        SlipEntity* nonPersist = new SlipEntity("TempDeletable");
        SlipLevel::Get().AddEntity(nonPersist);
        SlipEntity* persist = new SlipEntity("PersistentHero");
        persist->SetDontDestroyOnLoad(true);
        SlipLevel::Get().AddEntity(persist);

        SlipLevel::Get().ClearLevel(true); // preserve persistent
        assert(SlipLevel::Get().GetEntities().size() == 1);
        assert(SlipLevel::Get().GetEntities()[0]->GetName() == "PersistentHero");
        SlipLevel::Get().ClearLevel(false); // full clean
    }
    std::cout << "[PASS] Entity Hierarchy & DontDestroyOnLoad Persistence in Lua." << std::endl;

    // 13. Test SkinnedMesh, Animator and Bone bindings in Lua
    {
        SlipEntity charEnt("CharacterModel");
        auto sm = charEnt.AddComponent<SkinnedMeshComponent>();
        auto anim = charEnt.AddComponent<AnimatorComponent>();

        auto clip = std::make_shared<AnimationClip>();
        clip->name = "Walk";
        clip->duration = 2.0f;
        clip->ticksPerSecond = 30.0f;

        AnimationTrack track;
        track.boneName = "Hand_R";
        KeyPosition kp1{ glm::vec3(0.0f), 0.0f };
        KeyPosition kp2{ glm::vec3(1.0f, 0.0f, 0.0f), 1.0f };
        track.positions.push_back(kp1);
        track.positions.push_back(kp2);
        clip->AddTrack(track);

        anim->AddClip(clip);

        SlipEntity handBone("Hand_R");
        handBone.AddComponent<BoneComponent>(1, "Hand_R");
        charEnt.AddChild(&handBone);

        lua.GetState()["charEnt"] = &charEnt;

        bool animOk = lua.ExecuteString(R"(
            local animator = charEnt:GetAnimator()
            assert(animator ~= nil, "Animator component not found")
            assert(animator:GetClipCount() == 1, "Clip count mismatch")
            assert(animator:GetCurrentDuration() == 2.0, "Duration mismatch")

            charEnt:PlayAnimation("Walk", 1.5, true)
            assert(animator.isPlaying == true, "Should be playing")
            assert(animator.playbackSpeed == 1.5, "Speed mismatch")

            charEnt:PauseAnimation()
            assert(animator.isPlaying == false, "Should be paused")
            charEnt:ResumeAnimation()
            assert(animator.isPlaying == true, "Should be resumed")

            charEnt:SetAnimationSpeed(0.5)
            assert(animator.playbackSpeed == 0.5, "Speed should be 0.5")

            charEnt:StopAnimation()
            assert(animator.isPlaying == false, "Should be stopped")
            assert(animator.currentTime == 0.0, "Current time should reset to 0")

            local boneFound = charEnt:GetBone("Hand_R")
            assert(boneFound ~= nil, "Bone Hand_R not found via GetBone")
            assert(boneFound:GetName() == "Hand_R", "Bone name mismatch")

            local smComp = charEnt:GetComponent("SkinnedMesh")
            assert(smComp ~= nil, "GetComponent SkinnedMesh failed")
            local boneComp = boneFound:GetComponent("Bone")
            assert(boneComp ~= nil, "GetComponent Bone failed")
            assert(boneComp.boneName == "Hand_R", "BoneComponent boneName mismatch")

            local animComp = charEnt:GetComponent("Animator")
            assert(animComp ~= nil, "GetComponent Animator failed")
            animComp:SetFloat("Speed", 3.14)
            animComp:SetBool("IsGrounded", true)
            animComp:SetTrigger("Jump")
            assert(math.abs(animComp:GetFloat("Speed") - 3.14) < 0.001, "GetFloat Speed failed")
            assert(animComp:GetBool("IsGrounded") == true, "GetBool IsGrounded failed")
        )", "test_skinned_and_animator");
        assert(animOk);
    }
    std::cout << "[PASS] SkinnedMesh, Animator and Bone bindings in Lua." << std::endl;

    // 14. Test Camera effects (FOV, Shake, Letterbox/Pillarbox) and Keyframe Animation Clip
    {
        SlipEntity camEnt("MainCamera");
        auto camComp = camEnt.AddComponent<CameraComponent>();
        camComp->isPrimary = true;
        camComp->SetFOV(60.0f);
        camComp->SetLetterbox(0.0f);
        camComp->SetPillarbox(0.0f);

        SlipLevel::Get().AddEntity(&camEnt);
        lua.GetState()["camEnt"] = &camEnt;

        bool camOk = lua.ExecuteString(R"(
            local cam = camEnt:GetCamera()
            assert(cam ~= nil, "camEnt:GetCamera() returned nil")
            assert(cam:GetFOV() == 60.0, "initial fov mismatch")

            -- Test FOV modification
            cam:SetFOV(45.0)
            assert(cam:GetFOV() == 45.0, "cam:SetFOV(45) failed")
            assert(cam.fov == 45.0, "cam.fov property failed")
            cam.fov = 75.0
            assert(cam:GetFOV() == 75.0, "cam.fov setter failed")

            -- Test Global Camera table
            Camera.SetFOV(50.0)
            assert(math.abs(Camera.GetFOV() - 50.0) < 0.01, "Camera.SetFOV failed")

            -- Test Letterbox & Pillarbox
            Camera.SetLetterbox(0.12)
            assert(math.abs(cam.letterbox - 0.12) < 0.001, "letterbox mismatch")
            Camera.SetPillarbox(0.05)
            assert(math.abs(cam.pillarbox - 0.05) < 0.001, "pillarbox mismatch")
            Camera.SetBlackBars(0.15, 0.0)
            assert(math.abs(cam.letterbox - 0.15) < 0.001, "SetBlackBars letterbox mismatch")
            assert(cam.pillarbox == 0.0, "SetBlackBars pillarbox mismatch")

            -- Test Camera Shake
            assert(cam:IsShaking() == false, "should not be shaking initially")
            Camera.Shake(0.8, 1.2, 30.0)
            assert(cam:IsShaking() == true, "Camera.Shake should activate shake")
            Camera.StopShake()
            assert(cam:IsShaking() == false, "Camera.StopShake failed")
        )", "test_camera_scripting");
        assert(camOk);

        // Test Camera shake view matrix transformation
        camComp->Shake(1.0f, 0.5f, 25.0f);
        camComp->OnUpdate(0.1f);
        assert(camComp->IsShaking() == true);
        glm::mat4 shakeView = camComp->GetViewMatrix();
        assert(shakeView != glm::mat4(1.0f));
        camComp->StopShake();
        camComp->OnUpdate(0.0f);
        assert(camComp->IsShaking() == false);

        // Test KeyframeClip creation, evaluation, interpolation and serialization
        KeyframeClip clip("CinematicTest", 4.0f, 30.0f);
        auto& grp = clip.GetOrCreateGroup("TestActor");

        // Track 1: Posicion X (Smooth Hermite)
        grp.GetOrCreateTrack(AnimatableProperty::Transform_PosX, "Posicion X");
        grp.GetTrack(AnimatableProperty::Transform_PosX)->AddKeyframe(0.0f, 0.0f, KeyframeInterpolation::Smooth);
        grp.GetTrack(AnimatableProperty::Transform_PosX)->AddKeyframe(2.0f, 10.0f, KeyframeInterpolation::Smooth);
        grp.GetTrack(AnimatableProperty::Transform_PosX)->AddKeyframe(4.0f, 0.0f, KeyframeInterpolation::Smooth);

        // Track 2: Camera FOV (Linear)
        grp.GetOrCreateTrack(AnimatableProperty::Camera_FOV, "Camara FOV");
        grp.GetTrack(AnimatableProperty::Camera_FOV)->AddKeyframe(0.0f, 60.0f, KeyframeInterpolation::Linear);
        grp.GetTrack(AnimatableProperty::Camera_FOV)->AddKeyframe(2.0f, 30.0f, KeyframeInterpolation::Linear);
        grp.GetTrack(AnimatableProperty::Camera_FOV)->AddKeyframe(4.0f, 60.0f, KeyframeInterpolation::Linear);

        // Track 3: Camera Letterbox (Step)
        grp.GetOrCreateTrack(AnimatableProperty::Camera_Letterbox, "Camara Letterbox");
        grp.GetTrack(AnimatableProperty::Camera_Letterbox)->AddKeyframe(0.0f, 0.0f, KeyframeInterpolation::Step);
        grp.GetTrack(AnimatableProperty::Camera_Letterbox)->AddKeyframe(1.0f, 0.12f, KeyframeInterpolation::Step);
        grp.GetTrack(AnimatableProperty::Camera_Letterbox)->AddKeyframe(3.0f, 0.0f, KeyframeInterpolation::Step);

        auto* trkX = grp.GetTrack(AnimatableProperty::Transform_PosX);
        auto* trkFov = grp.GetTrack(AnimatableProperty::Camera_FOV);
        auto* trkLb = grp.GetTrack(AnimatableProperty::Camera_Letterbox);
        assert(trkX && trkFov && trkLb);

        // Verify evaluations
        assert(std::abs(trkX->Evaluate(0.0f) - 0.0f) < 0.01f);
        assert(std::abs(trkX->Evaluate(2.0f) - 10.0f) < 0.01f);
        assert(std::abs(trkX->Evaluate(1.0f) - 5.0f) < 0.2f); // Smooth midpoint is ~5.0
        assert(std::abs(trkFov->Evaluate(1.0f) - 45.0f) < 0.01f); // Linear midpoint of 60 and 30 is 45
        assert(std::abs(trkLb->Evaluate(0.5f) - 0.0f) < 0.01f);  // Step before 1.0 is 0.0
        assert(std::abs(trkLb->Evaluate(1.5f) - 0.12f) < 0.01f); // Step between 1.0 and 3.0 is 0.12

        // Save & Load clip
        std::string clipSavePath = "test_clip.clip";
        bool saveClipOk = clip.SaveToFile(clipSavePath);
        assert(saveClipOk);

        KeyframeClip loadedClip;
        bool loadClipOk = loadedClip.LoadFromFile(clipSavePath);
        assert(loadClipOk);
        assert(loadedClip.duration == 4.0f);
        assert(loadedClip.entityGroups.size() == 1);
        auto loadedGrp = loadedClip.GetGroup("TestActor");
        assert(loadedGrp != nullptr);
        assert(loadedGrp->tracks.size() == 3);
        assert(std::abs(loadedGrp->GetTrack(AnimatableProperty::Camera_FOV)->Evaluate(1.0f) - 45.0f) < 0.01f);

        // Test applying animation to entity
        SlipEntity actorEnt("TestActor");
        auto actorCam = actorEnt.AddComponent<CameraComponent>();
        loadedClip.SampleEntity(&actorEnt, 2.0f);
        assert(std::abs(actorEnt.position.x - 10.0f) < 0.01f);
        assert(std::abs(actorCam->GetFOV() - 30.0f) < 0.01f);

        // Test KeyframeAnimationComponent in Lua
        auto kfa = actorEnt.AddComponent<KeyframeAnimationComponent>();
        kfa->clip = std::make_shared<KeyframeClip>(loadedClip);
        lua.GetState()["actorEnt"] = &actorEnt;

        bool kfaLuaOk = lua.ExecuteString(R"(
            local kfa = actorEnt:GetKeyframeAnimation()
            assert(kfa ~= nil, "GetKeyframeAnimation() failed")
            kfa:Play()
            assert(kfa.isPlaying == true, "KeyframeAnimation isPlaying should be true")
            kfa:SetTime(2.0)
            assert(kfa.currentTime == 2.0, "KeyframeAnimation SetTime failed")
            kfa:Pause()
            assert(kfa.isPlaying == false, "KeyframeAnimation Pause failed")
            kfa:Stop()
            assert(kfa.isPlaying == false, "KeyframeAnimation Stop failed")
            assert(kfa.currentTime == 0.0, "KeyframeAnimation currentTime should be 0.0")
        )", "test_keyframe_lua");
        assert(kfaLuaOk);

        // --- TEST UI KEYFRAME ANIMATION ---
        {
            SlipEntity uiEnt("MenuFadePanel");
            auto rt = uiEnt.AddComponent<RectTransformComponent>();
            auto img = uiEnt.AddComponent<UIImageComponent>();
            rt->anchoredPosition = glm::vec2(-100.0f, 0.0f);
            rt->size = glm::vec2(100.0f, 50.0f);
            img->color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);

            KeyframeClip uiClip("FadeInTransition", 2.0f, 30.0f);
            auto& uiGrp = uiClip.GetOrCreateGroup("MenuFadePanel");
            uiGrp.GetOrCreateTrack(AnimatableProperty::UI_AnchoredPosX, "UI: Posicion X").AddKeyframe(0.0f, -100.0f, KeyframeInterpolation::Linear);
            uiGrp.GetTrack(AnimatableProperty::UI_AnchoredPosX)->AddKeyframe(1.0f, 0.0f, KeyframeInterpolation::Linear);
            uiGrp.GetOrCreateTrack(AnimatableProperty::UI_Width, "UI: Ancho").AddKeyframe(0.0f, 100.0f, KeyframeInterpolation::Linear);
            uiGrp.GetTrack(AnimatableProperty::UI_Width)->AddKeyframe(1.0f, 250.0f, KeyframeInterpolation::Linear);
            uiGrp.GetOrCreateTrack(AnimatableProperty::UI_Alpha, "UI: Alfa Fade").AddKeyframe(0.0f, 0.0f, KeyframeInterpolation::Linear);
            uiGrp.GetTrack(AnimatableProperty::UI_Alpha)->AddKeyframe(1.0f, 1.0f, KeyframeInterpolation::Linear);

            uiClip.SampleEntity(&uiEnt, 1.0f);
            assert(std::abs(rt->anchoredPosition.x - 0.0f) < 0.01f);
            assert(std::abs(rt->size.x - 250.0f) < 0.01f);
            assert(std::abs(img->color.a - 1.0f) < 0.01f);

            // Serialization of UI keyframes
            std::string uiClipPath = "test_ui_clip.clip";
            assert(uiClip.SaveToFile(uiClipPath));
            KeyframeClip loadedUiClip;
            assert(loadedUiClip.LoadFromFile(uiClipPath));
            auto loadedUiGrp = loadedUiClip.GetGroup("MenuFadePanel");
            assert(loadedUiGrp != nullptr);
            assert(loadedUiGrp->GetTrack(AnimatableProperty::UI_Alpha) != nullptr);
            assert(std::abs(loadedUiGrp->GetTrack(AnimatableProperty::UI_Alpha)->Evaluate(0.5f) - 0.5f) < 0.01f);
            SlipFileSystem::DeleteFileOrDir(uiClipPath);
        }

        // --- TEST BONE / SKELETAL KEYFRAME ANIMATION & EXPORT ---
        {
            SlipEntity boneEnt("mixamorig:RightArm");
            auto bc = boneEnt.AddComponent<BoneComponent>();
            bc->boneIndex = 5;

            KeyframeClip boneClip("PunchAnim", 1.0f, 30.0f);
            auto& boneGrp = boneClip.GetOrCreateGroup("mixamorig:RightArm");
            boneGrp.GetOrCreateTrack(AnimatableProperty::Bone_RotX, "Hueso: Rotacion X").AddKeyframe(0.0f, 0.0f, KeyframeInterpolation::Linear);
            boneGrp.GetTrack(AnimatableProperty::Bone_RotX)->AddKeyframe(0.5f, 45.0f, KeyframeInterpolation::Linear);
            boneGrp.GetTrack(AnimatableProperty::Bone_RotX)->AddKeyframe(1.0f, 0.0f, KeyframeInterpolation::Linear);

            boneGrp.GetOrCreateTrack(AnimatableProperty::Bone_PosX, "Hueso: Posicion X").AddKeyframe(0.0f, 0.0f, KeyframeInterpolation::Linear);
            boneGrp.GetTrack(AnimatableProperty::Bone_PosX)->AddKeyframe(0.5f, 2.5f, KeyframeInterpolation::Linear);
            boneGrp.GetTrack(AnimatableProperty::Bone_PosX)->AddKeyframe(1.0f, 0.0f, KeyframeInterpolation::Linear);

            boneClip.SampleEntity(&boneEnt, 0.5f);
            assert(std::abs(boneEnt.rotation.x - 45.0f) < 0.01f);
            assert(std::abs(boneEnt.position.x - 2.5f) < 0.01f);

            // Export to skeletal AnimationClip
            std::shared_ptr<AnimationClip> skelClip = boneClip.ToSkeletalAnimationClip();
            assert(skelClip != nullptr);
            assert(skelClip->name == "PunchAnim");
            assert(skelClip->tracks.size() == 1);
            assert(skelClip->tracks[0].boneName == "mixamorig:RightArm");
            assert(!skelClip->tracks[0].positions.empty());
            assert(!skelClip->tracks[0].rotations.empty());

            // Test evaluating skeletal animation track
            glm::vec3 sampledPos = skelClip->tracks[0].InterpolatePosition(0.5f);
            assert(std::abs(sampledPos.x - 2.5f) < 0.01f);

            // Test converting back with FromSkeletalAnimationClip
            KeyframeClip roundTripClip;
            roundTripClip.FromSkeletalAnimationClip(skelClip.get());
            assert(roundTripClip.name == "PunchAnim");
            auto rtGrp = roundTripClip.GetGroup("mixamorig:RightArm");
            assert(rtGrp != nullptr);
            assert(rtGrp->GetTrack(AnimatableProperty::Bone_PosX) != nullptr);
            float evVal = rtGrp->GetTrack(AnimatableProperty::Bone_PosX)->Evaluate(0.5f);
            assert(std::abs(evVal - 2.5f) < 0.01f);
        }

        SlipFileSystem::DeleteFileOrDir(clipSavePath);
        SlipLevel::Get().RemoveEntity(&camEnt);
    }
    std::cout << "[PASS] Camera effects (FOV, Shake, Letterbox/Pillarbox), UI/Bone Keyframing & Skeletal Clip Export." << std::endl;

    // -------------------------------------------------------------
    // TEST 15: CinematicCameraComponent & NFS Driving Prototype System
    // -------------------------------------------------------------
    std::cout << "\n[TEST 15] Testing CinematicCameraComponent & NFS Prototype..." << std::endl;
    {
        SlipEntity carEnt("NFSTestCar");
        auto carTransform = carEnt.GetComponent<TransformComponent>();
        carTransform->position = glm::vec3(0.0f, 1.0f, 0.0f);
        carTransform->rotation = glm::vec3(0.0f, 45.0f, 0.0f);

        SlipEntity camEnt("NFSTestCamera");
        auto camComp = camEnt.AddComponent<CameraComponent>();
        auto ccam = camEnt.AddComponent<CinematicCameraComponent>();
        assert(ccam != nullptr);

        ccam->SetTarget(&carEnt);
        assert(ccam->GetTarget() == &carEnt);
        assert(ccam->targetEntityName == "NFSTestCar");

        // Test Spring Arm & follow update
        ccam->targetDistance = 6.0f;
        ccam->targetHeight = 2.0f;
        ccam->positionLag = 10.0f;
        ccam->rotationLag = 10.0f;
        ccam->enableDynamicFOV = true;
        ccam->baseFOV = 60.0f;
        ccam->boostFOV = 80.0f;

        ccam->ResetImmediate();
        assert(camEnt.position != glm::vec3(0.0f));

        // Test update with delta time
        ccam->OnUpdate(0.016f);

        // Test Lua access to CinematicCamera
        lua.GetState()["testCar"] = &carEnt;
        lua.GetState()["testCam"] = &camEnt;

        bool luaOk = lua.ExecuteString(R"(
            local c = testCam:GetCinematicCamera()
            assert(c ~= nil, "GetCinematicCamera returned nil")
            assert(c.targetDistance == 6.0, "targetDistance mismatch")
            c.targetDistance = 7.5
            assert(c.targetDistance == 7.5, "targetDistance set failed")
            c:SetTargetName("NFSTestCar")
            assert(c.targetEntityName == "NFSTestCar", "SetTargetName failed")
        )", "test_cinematic_cam_lua");
        assert(luaOk);
        assert(ccam->targetDistance == 7.5f);

        // Test NFSCarController script execution in Lua
        bool nfsScriptOk = lua.ExecuteFile("examples/NFSCarController.lua");
        assert(nfsScriptOk);

        // Test VehicleComponent wheel ground raycast and contact
        {
            if (!SlipPhysics::IsInitialized()) {
                static SlipPhysics physInstance;
            }
            SlipLevel::Get().ClearLevel(false);

            // Piso estatico
            SlipEntity* ground = new SlipEntity("GroundTest");
            ground->position = glm::vec3(0.0f, 0.0f, 0.0f);
            ground->scale = glm::vec3(1.0f, 1.0f, 1.0f);
            auto groundCol = ground->AddComponent<BoxColliderComponent>(glm::vec3(300.0f, 0.5f, 300.0f));
            groundCol->centerOffset = glm::vec3(0.0f, -0.25f, 0.0f);
            auto groundRb = ground->AddComponent<RigidBodyComponent>();
            groundRb->mass = 0.0f;
            groundCol->OnInit();
            groundRb->OnInit();
            SlipLevel::Get().AddEntity(ground);

            // Coche
            SlipEntity* car = new SlipEntity("CarTest");
            car->position = glm::vec3(0.0f, 1.5f, 0.0f);
            car->scale = glm::vec3(1.8f, 0.7f, 4.2f);
            auto carCol = car->AddComponent<BoxColliderComponent>(glm::vec3(1.0f, 1.0f, 1.0f));
            auto carRb = car->AddComponent<RigidBodyComponent>();
            carRb->mass = 1250.0f;
            auto carVeh = car->AddComponent<VehicleComponent>();
            carCol->OnInit();
            carRb->OnInit();
            carVeh->OnInit();

            SlipEntity* wheelFL = new SlipEntity("Rueda_FL");
            wheelFL->AddComponent<MeshComponent>();
            car->AddChild(wheelFL);
            SlipLevel::Get().AddEntity(car);

            SlipPhysics::Get().SetSimulating(true);

            // Simular física por 60 cuadros
            for (int step = 0; step < 60; ++step) {
                SlipPhysics::Get().update(1.0f / 60.0f);
                carCol->OnUpdate(1.0f / 60.0f);
                carVeh->OnUpdate(1.0f / 60.0f);
            }

            auto* bulletVeh = carVeh->GetBulletVehicle();
            assert(bulletVeh != nullptr);
            bool inContact = bulletVeh->getWheelInfo(0).m_raycastInfo.m_isInContact;
            assert(inContact == true);
            assert(car->position.y > 0.8f); // Chasis suspendido sobre el suelo
            assert(wheelFL->position.y < 0.0f); // Rueda en posicion adecuada respecto al chasis
        }

        std::cout << "[PASS] CinematicCameraComponent spring arm, dynamic FOV & Lua bindings." << std::endl;
    }

    // Cleanup temp files
    SlipFileSystem::DeleteFileOrDir(sampleScriptPath);
    SlipFileSystem::DeleteFileOrDir(scriptBPath);
    SlipFileSystem::DeleteFileOrDir(propScriptPath);
    SlipFileSystem::DeleteFileOrDir(callerScriptPath);
    SlipFileSystem::DeleteFileOrDir(prefabSavePath);
    SlipFileSystem::DeleteFileOrDir(inputPath);
    delete loadedPrefabEnt;

    std::cout << "\n>>> ALL 15 TEST SUITES PASSED SUCCESSFULLY! <<<" << std::endl;
    return 0;
}
