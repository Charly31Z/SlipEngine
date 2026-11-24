#include "SlipEngine/editor/panels/InspectorPanel.h"
#include "SlipEngine/editor/panels/AnimationEditorPanel.h"
#include "SlipEngine/editor/panels/ViewportPanel.h"
#include "SlipEngine/editor/SlipEditor.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/scene/SlipPrefab.h"
#include "SlipEngine/scene/SlipTerrainComponent.h"
#include "SlipEngine/render/SlipAnimatorController.h"
#include "SlipEngine/render/SlipPrimitive.h"
#include "SlipEngine/editor/SlipModelExtract.h"
#include "SlipEngine/editor/SlipProject.h"
#include "SlipEngine/editor/SlipFileDialog.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/core/SlipConsole.h"
#include "SlipEngine/editor/SlipThumbnailManager.h"
#include "SlipEngine/render/rhi/SlipRHI.h"
#include "SlipEngine/render/shadergraph/ShaderGraph.h"
#include "SlipEngine/render/shadergraph/ShaderGraphSerializer.h"
#include "SlipEngine/platform/SlipInputMap.h"
#include <imgui.h>
#include <cstring>
#include <algorithm>
#include <memory>
#include <set>
#include <unordered_map>

namespace {
bool RenderTextureSlotControl(const char* label, const char* idSuffix, char* pathBuf, size_t bufSize, const std::vector<FileInfo>& texFiles) {
    bool changed = false;
    ImGui::PushID(idSuffix);

    std::string currentDisplay = (pathBuf[0] != '\0') ? SlipFileSystem::GetFileName(pathBuf) : "[Ninguna]";
    float availW = ImGui::GetContentRegionAvail().x;
    ImGui::Text("%s:", label);
    ImGui::SetNextItemWidth(availW - 38.0f);
    if (ImGui::BeginCombo("##TexCombo", currentDisplay.c_str())) {
        if (ImGui::Selectable("[Ninguna]", pathBuf[0] == '\0')) {
            pathBuf[0] = '\0';
            changed = true;
        }
        if (!texFiles.empty()) {
            ImGui::Separator();
            for (const auto& tf : texFiles) {
                bool isSel = (std::string(pathBuf) == tf.fullPath);
                if (ImGui::Selectable(tf.name.c_str(), isSel)) {
                    std::strncpy(pathBuf, tf.fullPath.c_str(), bufSize - 1);
                    pathBuf[bufSize - 1] = '\0';
                    changed = true;
                }
                if (isSel) ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            std::string droppedPath = (const char*)payload->Data;
            std::string ext = SlipFileSystem::GetFileExtension(droppedPath);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
                std::strncpy(pathBuf, droppedPath.c_str(), bufSize - 1);
                pathBuf[bufSize - 1] = '\0';
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsItemHovered() && pathBuf[0] != '\0') {
        ImGui::SetTooltip("Ruta: %s\n(Puedes arrastrar una textura desde el Explorador)", pathBuf);
    }

    ImGui::SameLine();
    if (ImGui::Button("X", ImVec2(28.0f, 0.0f))) {
        pathBuf[0] = '\0';
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Limpiar textura");
    }

    ImGui::PopID();
    return changed;
}

// ============================================================
// Shader Graph: cachÃ© del grafo + editor dinÃ¡mico de parÃ¡metros
// ============================================================
static SlipShaderGraph::ShaderGraph* GetShaderGraphForPath(const std::string& graphPath, bool forceReload = false) {
    static std::unordered_map<std::string, std::unique_ptr<SlipShaderGraph::ShaderGraph>> s_GraphCache;
    static std::set<std::string> s_FailedPaths;

    if (graphPath.empty()) return nullptr;
    auto it = s_GraphCache.find(graphPath);
    if (it != s_GraphCache.end() && !forceReload) return it->second.get();

    auto graph = std::make_unique<SlipShaderGraph::ShaderGraph>();
    std::string err;
    if (SlipShaderGraph::ShaderGraphSerializer::DeserializeFromFile(*graph, graphPath, &err)) {
        auto* ptr = graph.get();
        s_GraphCache[graphPath] = std::move(graph);
        s_FailedPaths.erase(graphPath);
        return ptr;
    }
    s_FailedPaths.insert(graphPath);
    return nullptr;
}

// Propaga los parÃ¡metros del material grafo a las entidades de la escena que usen el mismo .mat
static void PropagateGraphMaterialToScene(SlipLevel* level, const std::string& matPath, SlipMaterial& src) {
    if (!level || matPath.empty()) return;

    std::string baseName = SlipFileSystem::GetFileName(matPath);
    for (SlipEntity* ent : level->GetEntities()) {
        if (!ent) continue;
        auto tryApply = [&](SlipMaterial* target) -> void {
            if (!target) return;
            bool matches = (target->path == matPath) || (!baseName.empty() && SlipFileSystem::GetFileName(target->path) == baseName);
            if (!matches) return;

            if (target->shaderGraphPath != src.shaderGraphPath) {
                target->LoadShaderGraph(src.shaderGraphPath);
            }
            if (target->shaderGraphPath == src.shaderGraphPath && !src.shaderGraphPath.empty()) {
                target->graphFloatParams = src.graphFloatParams;
                target->graphVectorParams = src.graphVectorParams;
                target->graphTextureParams = src.graphTextureParams;
            }
        };

        if (auto mc = ent->GetComponent<MeshComponent>()) {
            tryApply(mc->material);
        }
        if (auto smc = ent->GetComponent<SkinnedMeshComponent>()) {
            tryApply(smc->material);
        }
    }
}

// SecciÃ³n del inspector cuando el material usa un Shader Graph
static void RenderShaderGraphMaterialSection(SlipMaterial& mat, const std::string& assetsDir, SlipLevel* contextLevel) {
    std::string displayPath = mat.shaderGraphPath;

    ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "Shader Graph: %s",
                       displayPath.empty() ? "(sin asignar)" : SlipFileSystem::GetFileName(displayPath).c_str());
    ImGui::SetNextItemWidth(-1.0f);
    char pathBuf[256];
    std::strncpy(pathBuf, displayPath.c_str(), sizeof(pathBuf) - 1);
    pathBuf[sizeof(pathBuf) - 1] = '\0';
    if (ImGui::InputText("##SGPath", pathBuf, sizeof(pathBuf))) {
        mat.LoadShaderGraph(pathBuf);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ruta del .shadergraph (o arrastra el asset desde el explorador)");
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            std::string droppedPath = reinterpret_cast<const char*>(payload->Data);
            if (SlipFileSystem::GetFileExtension(droppedPath) == ".shadergraph") {
                mat.LoadShaderGraph(droppedPath);
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (displayPath.empty()) return;

    if (ImGui::Button("Abrir en el Editor de Shader Graph", ImVec2(-1, 0))) {
        if (SlipEditor::GetInstance()) {
            SlipEditor::GetInstance()->OpenShaderGraph(displayPath);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Recompilar", ImVec2(-1, 0))) {
        if (mat.LoadShaderGraph(mat.shaderGraphPath)) {
            SlipConsole::LogSuccess("Shader Graph recompilado: " + mat.shaderGraphPath);
        } else {
            SlipConsole::LogError("Fallo al recompilar Shader Graph: " + mat.shaderGraphPath);
        }
    }
    if (ImGui::Button("Quitar Shader Graph", ImVec2(-1, 0))) {
        mat.LoadShaderGraph("");
        SlipConsole::LogInfo("Material revertido a shader Lit/Unlit integrado");
    }
    if (ImGui::Button("Refrescar Definicion (grafo)", ImVec2(-1, 0))) {
        GetShaderGraphForPath(mat.shaderGraphPath, true);
    }

    auto* graph = GetShaderGraphForPath(mat.shaderGraphPath);
    if (!graph) {
        ImGui::TextDisabled("(No se pudo leer la definicion del grafo)");
        return;
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f), "Propiedades Expuestas");
    ImGui::TextDisabled("(Los cambios se propagan a la escena en vivo)");

    bool anyChanged = false;
    for (const auto& prop : graph->properties) {
        ImGui::PushID(prop.id.c_str());

        switch (prop.type) {
            case SlipShaderGraph::PinType::Float: {
                float& val = mat.graphFloatParams[prop.name];
                if (ImGui::DragFloat(prop.name.c_str(), &val, 0.05f)) anyChanged = true;
                break;
            }
            case SlipShaderGraph::PinType::Vec2: {
                glm::vec4& val = mat.graphVectorParams[prop.name];
                if (ImGui::DragFloat2(prop.name.c_str(), &val.x, 0.05f)) anyChanged = true;
                break;
            }
            case SlipShaderGraph::PinType::Vec3: {
                glm::vec4& val = mat.graphVectorParams[prop.name];
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 1.0f, 1.0f));
                ImGui::TextUnformatted(prop.name.c_str());
                ImGui::PopStyleColor();
                if (ImGui::DragFloat3("##vec3val", &val.x, 0.05f)) anyChanged = true;
                if (ImGui::ColorEdit3("##vec3col", &val.x)) anyChanged = true;
                break;
            }
            case SlipShaderGraph::PinType::Vec4: {
                glm::vec4& val = mat.graphVectorParams[prop.name];
                if (ImGui::ColorEdit4(prop.name.c_str(), &val.x)) anyChanged = true;
                break;
            }
            case SlipShaderGraph::PinType::Texture2D: {
                std::string cur = mat.GetGraphTexture(prop.name);
                std::string display = cur.empty() ? "[Ninguna]" : SlipFileSystem::GetFileName(cur);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.0f);
                if (ImGui::BeginCombo(prop.name.c_str(), display.c_str())) {
                    if (ImGui::Selectable("[Ninguna]", cur.empty())) {
                        mat.SetGraphTexture(prop.name, "");
                        anyChanged = true;
                    }
                    std::vector<FileInfo> texFiles;
                    if (!assetsDir.empty()) {
                        texFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".png,.jpg,.jpeg,.tga,.bmp");
                    }
                    for (const auto& tf : texFiles) {
                        bool isSel = (cur == tf.fullPath);
                        if (ImGui::Selectable(tf.name.c_str(), isSel)) {
                            mat.SetGraphTexture(prop.name, tf.fullPath);
                            anyChanged = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            default:
                ImGui::TextDisabled("%s: tipo no soportado", prop.name.c_str());
                break;
        }

        ImGui::PopID();
    }

    // ActualizaciÃ³n en tiempo real de las entidades de la escena que usen este material
    if (anyChanged) {
        PropagateGraphMaterialToScene(contextLevel, mat.path, mat);
    }
}

void RenderSlipMaterialPropertiesEditor(SlipMaterial& mat, const char* idPrefix, const std::string& assetsDir, SlipLevel* contextLevel) {
    ImGui::PushID(idPrefix);

    // --- Shader Graph: si el material ya usa un grafo, mostrar solo esa seccion ---
    if (!mat.shaderGraphPath.empty()) {
        RenderShaderGraphMaterialSection(mat, assetsDir, contextLevel);
        ImGui::PopID();
        return;
    }

    std::vector<FileInfo> texFiles;
    if (!assetsDir.empty()) {
        texFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".png,.jpg,.jpeg,.tga,.bmp");
    }

    const char* shaderOptions[] = { "Lit", "Unlit" };
    int currentShaderIdx = (std::string(mat.shaderType) == "Unlit") ? 1 : 0;
    if (ImGui::Combo("Shader", &currentShaderIdx, shaderOptions, IM_ARRAYSIZE(shaderOptions))) {
        std::strncpy(mat.shaderType, shaderOptions[currentShaderIdx], sizeof(mat.shaderType) - 1);
        mat.init();
    }

    // --- Asignar un Shader Graph al material ---
    std::vector<FileInfo> sgFiles;
    if (!assetsDir.empty()) {
        sgFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".shadergraph");
    }
    if (!sgFiles.empty()) {
        if (ImGui::BeginCombo("##SGAssets", "Usar Shader Graph...")) {
            for (const auto& sg : sgFiles) {
                if (ImGui::Selectable(sg.name.c_str(), false)) {
                    if (mat.LoadShaderGraph(sg.fullPath)) {
                        SlipConsole::LogSuccess("Shader Graph asignado al material: " + sg.fullPath);
                    } else {
                        SlipConsole::LogError("Fallo al compilar Shader Graph: " + sg.fullPath);
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sg.fullPath.c_str());
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                std::string droppedPath = reinterpret_cast<const char*>(payload->Data);
                if (SlipFileSystem::GetFileExtension(droppedPath) == ".shadergraph") {
                    mat.LoadShaderGraph(droppedPath);
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    bool texChanged = false;
    if (std::string(mat.shaderType) == "Unlit") {
        ImGui::ColorEdit3("Color Base", &mat.color.r);
        if (RenderTextureSlotControl("Textura Base (Albedo)", "UnlitDiff", mat.diffPath, sizeof(mat.diffPath), texFiles)) {
            texChanged = true;
        }
        ImGui::Checkbox("Recorte Alfa (Alpha Clip)", &mat.alphaClip);
        if (mat.alphaClip) {
            ImGui::SliderFloat("Umbral Alfa (Cutoff)", &mat.alphaCutoff, 0.01f, 0.99f);
        }
    } else {
        ImGui::ColorEdit3("Albedo (Color Base)", &mat.color.r);
        ImGui::SliderFloat("Rugosidad (Roughness)", &mat.roughness, 0.0f, 1.0f);
        ImGui::SliderFloat("MetÃ¡lico (Metallic)", &mat.metallic, 0.0f, 1.0f);
        ImGui::SliderFloat("OclusiÃ³n Ambiental (AO)", &mat.ao, 0.0f, 1.0f);
        ImGui::ColorEdit3("Color Emisivo", &mat.emissiveColor.r);
        ImGui::SliderFloat("Intensidad Emisiva", &mat.emissiveIntensity, 0.0f, 20.0f);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "Mapas de Textura PBR:");

        if (RenderTextureSlotControl("Mapa Albedo / Difuso", "PBRDiff", mat.diffPath, sizeof(mat.diffPath), texFiles)) texChanged = true;
        if (RenderTextureSlotControl("Mapa de Normales (Normal Map)", "PBRNorm", mat.normalPath, sizeof(mat.normalPath), texFiles)) texChanged = true;
        if (std::strlen(mat.normalPath) > 0 || mat.normalTexture != nullptr) {
            ImGui::SliderFloat("Intensidad de Normales (Normal Scale)", &mat.normalScale, 0.0f, 5.0f, "%.2f");
        }
        if (RenderTextureSlotControl("Mapa Roughness / Specular", "PBRRough", mat.roughnessPath, sizeof(mat.roughnessPath), texFiles)) texChanged = true;
        ImGui::Checkbox("Invertir Roughness (Mapa Glossiness/Specular)", &mat.invertRoughness);
        if (RenderTextureSlotControl("Mapa MetÃ¡lico (Metallic)", "PBRMetal", mat.metallicPath, sizeof(mat.metallicPath), texFiles)) texChanged = true;
        if (RenderTextureSlotControl("Mapa OclusiÃ³n Ambiental (AO)", "PBRAO", mat.aoPath, sizeof(mat.aoPath), texFiles)) texChanged = true;
        if (RenderTextureSlotControl("Mapa Emisivo (Emissive)", "PBREmi", mat.emissivePath, sizeof(mat.emissivePath), texFiles)) texChanged = true;
        if (RenderTextureSlotControl("Mapa Opacidad / MÃ¡scara Alfa", "PBROpa", mat.opacityPath, sizeof(mat.opacityPath), texFiles)) texChanged = true;

        ImGui::Separator();
        ImGui::Checkbox("Recorte Alfa (Alpha Cutout / Follaje)", &mat.alphaClip);
        if (mat.alphaClip) {
            ImGui::SliderFloat("Umbral Alfa (Alpha Cutoff)", &mat.alphaCutoff, 0.01f, 0.99f);
        }
        ImGui::Checkbox("Doble Cara (Double Sided)", &mat.doubleSided);
    }

    if (texChanged) {
        mat.reloadTextures();
    }

    ImGui::PopID();
}
} // namespace

InspectorPanel::InspectorPanel(SelectionCallback onSelect)
    : m_OnSelectCallback(onSelect) {}

void InspectorPanel::OnImGuiRender() {
    if (!m_IsOpen) return;

    if (ImGui::Begin("Inspector", &m_IsOpen)) {

        // =========================================================================
        // 1. INSPECTOR DE ASSET (Si se seleccionÃ³ un archivo en el explorador)
        // =========================================================================
        if (!m_SelectedAssetPath.empty() && SlipFileSystem::Exists(m_SelectedAssetPath)) {
            std::string fileName = SlipFileSystem::GetFileName(m_SelectedAssetPath);
            std::string ext = SlipFileSystem::GetFileExtension(m_SelectedAssetPath);
            std::string assetType = SlipModelExtract::DeduceAssetType(ext);

            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Asset: %s", fileName.c_str());
            ImGui::TextDisabled("Ruta: %s", m_SelectedAssetPath.c_str());
            ImGui::TextDisabled("Tipo: %s", assetType.c_str());
            ImGui::Separator();

            // --- A) Modelos 3D (.fbx, .obj, .dae, .gltf) ---
            if (assetType == "Model") {
                static std::string s_CurrentInspectedModelPath;
                static AssetMetadata s_InspectedModelMeta;
                static ModelInspectionInfo s_InspectedModelDetails;

                if (s_CurrentInspectedModelPath != m_SelectedAssetPath) {
                    SlipModelExtract::EnsureMetaForFile(m_SelectedAssetPath, &s_InspectedModelMeta);
                    SlipModelExtract::InspectModelDetailed(m_SelectedAssetPath, s_InspectedModelDetails);
                    s_CurrentInspectedModelPath = m_SelectedAssetPath;
                }

                if (ImGui::CollapsingHeader("Ajustes de ImportaciÃ³n de Modelo", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled("UID: %s", s_InspectedModelMeta.guid.c_str());
                    ImGui::TextDisabled("Mallas: %d | Vertices: %d", s_InspectedModelDetails.meshCount, s_InspectedModelDetails.vertexCount);

                    // Escala de ImportaciÃ³n con resoluciÃ³n fina para permitir 0.0100 (centÃ­metros a metros)
                    ImGui::DragFloat("Escala de ImportaciÃ³n", &s_InspectedModelMeta.importScale, 0.005f, 0.0001f, 1000.0f, "%.4f");

                    // RotaciÃ³n de ImportaciÃ³n (Pitch, Yaw, Roll en grados)
                    ImGui::DragFloat3("RotaciÃ³n de ImportaciÃ³n (Â°)", &s_InspectedModelMeta.importRotation.x, 1.0f, -360.0f, 360.0f, "%.1f");

                    // Hornear transformaciones de nodos (corrige ejes Z-up a Y-up y pivots de Assimp)
                    ImGui::Checkbox("Hornear Transformaciones de Nodos (Bake)", &s_InspectedModelMeta.bakeTransforms);

                    // Presets rÃ¡pidos para UE5 y Blender
                    ImGui::TextDisabled("Presets RÃ¡pidos de Ejes y Escala:");
                    if (ImGui::Button("Preset UE5 (Escala 0.01, -90Â° X)")) {
                        s_InspectedModelMeta.importScale = 0.01f;
                        s_InspectedModelMeta.importRotation = glm::vec3(-90.0f, 0.0f, 0.0f);
                        s_InspectedModelMeta.bakeTransforms = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Parar Modelo (-90Â° X)")) {
                        s_InspectedModelMeta.importRotation.x = -90.0f;
                    }

                    if (ImGui::Button("Preset Blender/EstÃ¡ndar (1.0, 0Â°)")) {
                        s_InspectedModelMeta.importScale = 1.0f;
                        s_InspectedModelMeta.importRotation = glm::vec3(0.0f);
                        s_InspectedModelMeta.bakeTransforms = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Rotar +90Â° X")) {
                        s_InspectedModelMeta.importRotation.x = 90.0f;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Revertir")) {
                        SlipModelExtract::LoadMeta(m_SelectedAssetPath + ".meta", s_InspectedModelMeta);
                    }

                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.85f, 0.90f, 1.0f, 1.0f), "Esqueleto y Animaciones:");
                    ImGui::Checkbox("Importar Esqueleto / Huesos (Skinning)", &s_InspectedModelMeta.importBones);
                    ImGui::Checkbox("Importar Animaciones (Clips)", &s_InspectedModelMeta.importAnimations);

                    if (s_InspectedModelDetails.boneCount > 0) {
                        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "  -> Huesos detectados: %d", s_InspectedModelDetails.boneCount);
                    }
                    if (!s_InspectedModelDetails.animations.empty()) {
                        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "  -> Animaciones detectadas (%d):", (int)s_InspectedModelDetails.animations.size());
                        for (size_t a = 0; a < s_InspectedModelDetails.animations.size(); ++a) {
                            const auto& anim = s_InspectedModelDetails.animations[a];
                            ImGui::BulletText("\"%s\" (%.2fs a %.0f fps)", anim.name.c_str(), anim.durationSeconds, anim.ticksPerSecond);
                        }
                    } else {
                        ImGui::TextDisabled("  -> (No se detectaron animaciones en este archivo)");
                    }

                    ImGui::Separator();
                    ImGui::Checkbox("Generar Normales Suaves", &s_InspectedModelMeta.importNormals);
                    ImGui::Checkbox("Voltear Coordenadas UV (Flip UVs)", &s_InspectedModelMeta.flipUVs);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Si las texturas de Mixamo se ven al reves o de cabeza, desmarca o marca esta opcion y reimporta.");
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Invertir UVs y Re-bakear")) {
                        s_InspectedModelMeta.flipUVs = !s_InspectedModelMeta.flipUVs;
                        SlipModelExtract::SaveMeta(m_SelectedAssetPath + ".meta", s_InspectedModelMeta);
                        std::string modelToRebake = m_SelectedAssetPath;
                        SlipModelExtract::BakeModelToCacheAsync(modelToRebake, s_InspectedModelMeta, [this, modelToRebake](bool success, const std::string&) {
                            if (success && m_ContextLevel) {
                                for (auto* entity : m_ContextLevel->GetEntities()) {
                                    if (auto mc = entity->GetComponent<MeshComponent>()) {
                                        if (mc->modelPath == modelToRebake) mc->SetModel(modelToRebake);
                                    }
                                    if (auto smc = entity->GetComponent<SkinnedMeshComponent>()) {
                                        if (smc->modelPath == modelToRebake) smc->SetModel(modelToRebake);
                                    }
                                }
                            }
                        });
                    }

                    ImGui::Checkbox("Importar Materiales (Crear subcarpeta Materials/)", &s_InspectedModelMeta.importMaterials);
                    if (!s_InspectedModelMeta.importMaterials) {
                        ImGui::BeginDisabled();
                    }
                    ImGui::Checkbox("Importar Texturas (Crear subcarpeta Textures/)", &s_InspectedModelMeta.importTextures);
                    if (!s_InspectedModelMeta.importMaterials) {
                        ImGui::EndDisabled();
                    }
                    ImGui::Checkbox("Generar Colliders AutomÃ¡ticos", &s_InspectedModelMeta.generateColliders);

                    ImGui::Spacing();
                    bool isBakingThis = SlipModelExtract::IsProcessing() && 
                                        SlipModelExtract::GetActiveAssetName() == fileName;

                    if (isBakingThis) {
                        float prog = SlipModelExtract::GetProcessProgress();
                        char overlay[64];
                        snprintf(overlay, sizeof(overlay), "Bakeando cache en segundo plano (%.0f%%)...", prog * 100.0f);
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.7f, 1.0f, 1.0f));
                        ImGui::ProgressBar(prog, ImVec2(-1, 32), overlay);
                        ImGui::PopStyleColor();
                    } else {
                        if (ImGui::Button(" Guardar Ajustes (.meta) y Reimportar ", ImVec2(-1, 32))) {
                            SlipModelExtract::SaveMeta(m_SelectedAssetPath + ".meta", s_InspectedModelMeta);
                            std::string modelToRebake = m_SelectedAssetPath;
                            SlipModelExtract::BakeModelToCacheAsync(modelToRebake, s_InspectedModelMeta, [this, modelToRebake](bool success, const std::string&) {
                                if (success && m_ContextLevel) {
                                    for (auto* entity : m_ContextLevel->GetEntities()) {
                                        if (auto mc = entity->GetComponent<MeshComponent>()) {
                                            if (mc->modelPath == modelToRebake ||
                                                SlipFileSystem::GetFileName(mc->modelPath) == SlipFileSystem::GetFileName(modelToRebake)) {
                                                mc->SetModel(modelToRebake);
                                            }
                                        }
                                    }
                                }
                            });
                        }
                    }
                }
            }
            // --- B) Texturas (.png, .jpg, .tga) ---
            else if (assetType == "Texture") {
                static std::string s_CurrentInspectedTexturePath;
                static AssetMetadata s_InspectedTextureMeta;

                if (s_CurrentInspectedTexturePath != m_SelectedAssetPath) {
                    SlipModelExtract::EnsureMetaForFile(m_SelectedAssetPath, &s_InspectedTextureMeta);
                    s_CurrentInspectedTexturePath = m_SelectedAssetPath;
                }

                if (ImGui::CollapsingHeader("Ajustes de ImportaciÃ³n de Textura", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled("UID: %s", s_InspectedTextureMeta.guid.c_str());

                    const char* filterModes[] = { "Linear", "Nearest" };
                    int currentFilter = (s_InspectedTextureMeta.filterMode == "Nearest") ? 1 : 0;
                    if (ImGui::Combo("Modo de Filtro", &currentFilter, filterModes, IM_ARRAYSIZE(filterModes))) {
                        s_InspectedTextureMeta.filterMode = filterModes[currentFilter];
                    }

                    const char* wrapModes[] = { "Repeat", "ClampToEdge", "MirroredRepeat" };
                    int currentWrap = 0;
                    if (s_InspectedTextureMeta.wrapMode == "ClampToEdge") currentWrap = 1;
                    else if (s_InspectedTextureMeta.wrapMode == "MirroredRepeat") currentWrap = 2;
                    if (ImGui::Combo("Modo de Envoltura (Wrap)", &currentWrap, wrapModes, IM_ARRAYSIZE(wrapModes))) {
                        s_InspectedTextureMeta.wrapMode = wrapModes[currentWrap];
                    }

                    ImGui::Checkbox("Generar Mipmaps", &s_InspectedTextureMeta.generateMipmaps);

                    ImGui::Spacing();
                    if (ImGui::Button(" ðŸ’¾ Guardar Ajustes (.meta) ", ImVec2(-1, 32))) {
                        SlipModelExtract::SaveMeta(m_SelectedAssetPath + ".meta", s_InspectedTextureMeta);
                        SlipConsole::LogSuccess("Ajustes de textura guardados: " + m_SelectedAssetPath + ".meta");
                    }
                }
            }
            // --- C) Materiales (.mat) ---
            else if (ext == ".mat") {
                static std::string s_CurrentLoadedMatPath;
                static SlipMaterial s_InspectedMat;

                if (s_CurrentLoadedMatPath != m_SelectedAssetPath) {
                    s_InspectedMat = SlipMaterial(m_SelectedAssetPath);
                    s_CurrentLoadedMatPath = m_SelectedAssetPath;
                }

                if (ImGui::CollapsingHeader("Propiedades del Material (.mat)", ImGuiTreeNodeFlags_DefaultOpen)) {
                    std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
                    if (assetsDir.empty() && SlipProject::Get().HasActiveProject()) {
                        assetsDir = SlipProject::Get().GetCurrentProject().assetsPath;
                    }
                    if (assetsDir.empty()) assetsDir = "assets";

                    RenderSlipMaterialPropertiesEditor(s_InspectedMat, "InspectedMatAsset", assetsDir, m_ContextLevel);

                    ImGui::Spacing();
                    if (ImGui::Button(" Guardar Material (.mat) ", ImVec2(-1, 32))) {
                        s_InspectedMat.Save(m_SelectedAssetPath);
                        SlipThumbnailManager::Get().GenerateMaterialThumbnail(m_SelectedAssetPath, true);
                        SlipConsole::LogSuccess("Material guardado exitosamente: " + m_SelectedAssetPath);

                        // Actualizar automÃ¡ticamente todas las entidades de la escena que usen este material
                        if (m_ContextLevel) {
                            std::string assetFilename = SlipFileSystem::GetFileName(m_SelectedAssetPath);
                            for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
                                if (!ent) continue;
                                if (auto mc = ent->GetComponent<MeshComponent>()) {
                                    std::string entMatFilename = SlipFileSystem::GetFileName(mc->materialPath);
                                    if (mc->materialPath == m_SelectedAssetPath || (!assetFilename.empty() && entMatFilename == assetFilename)) {
                                        mc->SetMaterial(m_SelectedAssetPath);
                                    }
                                    for (size_t sIdx = 0; sIdx < mc->GetMaterialSlotCount(); ++sIdx) {
                                        std::string slotPath = mc->GetSubmeshMaterialPath(sIdx);
                                        std::string slotFilename = SlipFileSystem::GetFileName(slotPath);
                                        if (slotPath == m_SelectedAssetPath || (!assetFilename.empty() && slotFilename == assetFilename)) {
                                            mc->SetSubmeshMaterial(sIdx, m_SelectedAssetPath);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (m_SelectedEntity && m_SelectedEntity->GetComponent<MeshComponent>()) {
                        ImGui::Spacing();
                        if (ImGui::Button(" Asignar Material al Objeto Seleccionado ", ImVec2(-1, 28))) {
                            m_SelectedEntity->GetComponent<MeshComponent>()->SetMaterial(m_SelectedAssetPath);
                            SlipConsole::LogSuccess("Material asignado a: " + m_SelectedEntity->GetName());
                        }
                    }
                }
            }
            // --- C) Texturas (.png, .jpg, .jpeg, .tga, .bmp) ---
            else if (assetType == "Texture" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
                SlipTexture* tex = SlipTexture::load(m_SelectedAssetPath.c_str());
                if (tex && tex->getID() != 0) {
                    ImGui::Text("Dimensiones: %d x %d px", tex->width, tex->height);
                    ImGui::Text("ID Textura GPU: %u", tex->getID());

                    float availWidth = ImGui::GetContentRegionAvail().x;
                    float aspect = (tex->height > 0) ? ((float)tex->width / (float)tex->height) : 1.0f;
                    float previewWidth = availWidth;
                    float previewHeight = previewWidth / aspect;
                    if (previewHeight > 300.0f) {
                        previewHeight = 300.0f;
                        previewWidth = previewHeight * aspect;
                    }

                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Vista Previa:");

                    ImVec2 p0 = ImGui::GetCursorScreenPos();
                    ImVec2 p1 = ImVec2(p0.x + previewWidth, p0.y + previewHeight);
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(p0, p1, IM_COL32(18, 18, 22, 255));
                    drawList->AddRect(p0, p1, IM_COL32(60, 60, 75, 255));

                    if (SlipRHI::GetActiveAPI() == GraphicsAPI::OpenGL) {
                        ImGui::Image((ImTextureID)(uintptr_t)tex->getID(), ImVec2(previewWidth, previewHeight), ImVec2(0, 0), ImVec2(1, 1));
                    } else {
                        ImGui::Dummy(ImVec2(previewWidth, previewHeight));
                        drawList->AddText(ImVec2(p0.x + 8.0f, p0.y + 8.0f), IM_COL32(180, 195, 220, 255), "Preview Vulkan (Fase 4)");
                    }

                    if (m_SelectedEntity && m_SelectedEntity->GetComponent<MeshComponent>()) {
                        ImGui::Spacing();
                        if (ImGui::Button(" Asignar como Albedo a Entidad ", ImVec2(-1, 26))) {
                            auto mc = m_SelectedEntity->GetComponent<MeshComponent>();
                            if (mc->material) {
                                std::strncpy(mc->material->diffPath, m_SelectedAssetPath.c_str(), sizeof(mc->material->diffPath) - 1);
                                mc->material->diffuseTexture = tex;
                                mc->material->Save();
                                SlipConsole::LogSuccess("Textura Albedo asignada a material de " + m_SelectedEntity->GetName());
                            }
                        }
                        if (ImGui::Button(" Asignar como Normal Map a Entidad ", ImVec2(-1, 26))) {
                            auto mc = m_SelectedEntity->GetComponent<MeshComponent>();
                            if (mc->material) {
                                std::strncpy(mc->material->normalPath, m_SelectedAssetPath.c_str(), sizeof(mc->material->normalPath) - 1);
                                mc->material->normalTexture = tex;
                                mc->material->Save();
                                SlipConsole::LogSuccess("Normal Map asignado a material de " + m_SelectedEntity->GetName());
                            }
                        }
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Cargando o no se pudo decodificar la textura.");
                }
            }
            // --- D) Clips de AnimaciÃ³n (.anim) ---
            else if (ext == ".anim" || assetType == "Animation") {
                static std::string s_CurrentAnimAssetPath;
                static AnimationClipAsset s_InspectedAnimData;

                if (s_CurrentAnimAssetPath != m_SelectedAssetPath) {
                    SlipModelExtract::LoadAnimAsset(m_SelectedAssetPath, s_InspectedAnimData);
                    s_CurrentAnimAssetPath = m_SelectedAssetPath;
                }

                if (ImGui::CollapsingHeader("Propiedades del Clip de Animacion", ImGuiTreeNodeFlags_DefaultOpen)) {
                    char nameBuf[128];
                    std::strncpy(nameBuf, s_InspectedAnimData.name.c_str(), sizeof(nameBuf) - 1);
                    nameBuf[sizeof(nameBuf) - 1] = '\0';
                    if (ImGui::InputText("Nombre del Clip", nameBuf, sizeof(nameBuf))) {
                        s_InspectedAnimData.name = nameBuf;
                    }

                    ImGui::TextDisabled("Modelo Origen: %s", s_InspectedAnimData.sourceModel.c_str());
                    ImGui::TextDisabled("Indice de Pista: %d", s_InspectedAnimData.clipIndex);
                    ImGui::Spacing();

                    ImGui::DragFloat("Duracion (seg)", &s_InspectedAnimData.duration, 0.05f, 0.01f, 300.0f, "%.2f s");
                    ImGui::DragFloat("Velocidad de Reproduccion", &s_InspectedAnimData.playbackSpeed, 0.05f, 0.1f, 10.0f, "%.2fx");
                    ImGui::Checkbox("Repetir en Bucle (Loop)", &s_InspectedAnimData.loop);
                    ImGui::Spacing();

                    ImGui::DragFloatRange2("Recorte (Inicio / Fin)", &s_InspectedAnimData.startTime, &s_InspectedAnimData.endTime, 0.05f, 0.0f, s_InspectedAnimData.duration, "Inicio: %.2fs", "Fin: %.2fs");
                    ImGui::Spacing();

                    if (ImGui::Button(" Guardar Cambios (.anim) ", ImVec2(-1, 30))) {
                        if (SlipModelExtract::SaveAnimAsset(m_SelectedAssetPath, s_InspectedAnimData)) {
                            SlipConsole::LogSuccess("Clip de animacion guardado: " + s_InspectedAnimData.name);
                        }
                    }

                    ImGui::Spacing();
                    if (ImGui::Button(" Previsualizar en Animation Editor ", ImVec2(-1, 30))) {
                        if (auto aep = AnimationEditorPanel::GetInstance()) {
                            aep->SetOpen(true);
                            if (!s_InspectedAnimData.sourceModel.empty()) {
                                aep->LoadModelForPreview(s_InspectedAnimData.sourceModel);
                            }
                        }
                    }

                    if (m_SelectedEntity && m_SelectedEntity->GetComponent<AnimatorComponent>()) {
                        ImGui::Spacing();
                        if (ImGui::Button(" Asignar al Animator de Entidad Seleccionada ", ImVec2(-1, 30))) {
                            auto anim = m_SelectedEntity->GetComponent<AnimatorComponent>();
                            if (anim) {
                                anim->Play(s_InspectedAnimData.name, s_InspectedAnimData.playbackSpeed, s_InspectedAnimData.loop);
                                SlipConsole::LogSuccess("Clip asignado y reproducido en animator de: " + m_SelectedEntity->GetName());
                            }
                        }
                    }
                }
            }
        }
        // =========================================================================
        // 2. INSPECTOR DE ENTIDAD Y SUS COMPONENTES
        // =========================================================================
        else if (m_SelectedEntity) {
            char nameBuf[128];
            std::strncpy(nameBuf, m_SelectedEntity->GetName().c_str(), sizeof(nameBuf) - 1);
            nameBuf[sizeof(nameBuf) - 1] = '\0';
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 110.0f);
            if (ImGui::InputText("##NombreEntidad", nameBuf, sizeof(nameBuf))) {
                m_SelectedEntity->SetName(nameBuf);
            }
            // Validar unicidad del nombre al terminar la edición (sufijo automático _NNN)
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                std::string uniqueName = SlipPrefab::MakeUniqueName(m_SelectedEntity->GetName(), m_ContextLevel, m_SelectedEntity);
                if (uniqueName != m_SelectedEntity->GetName()) {
                    m_SelectedEntity->SetName(uniqueName);
                }
            }
            ImGui::SameLine();
            ImGui::Checkbox("EstÃ¡tico", &m_SelectedEntity->isStatic);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("EstÃ¡tico / DinÃ¡mico (estilo Unity3D/UE5):\nLos objetos estÃ¡ticos no se mueven en runtime,\nse optimizan mediante Frustum Culling y se hornean en el cubemap de reflejos.");
            }

            ImGui::Separator();

            std::shared_ptr<SlipComponent> compToRemove = nullptr;

            // Recorrer todos los componentes presentes en la entidad
            for (auto& comp : m_SelectedEntity->GetComponents()) {
                if (!comp) continue;

                // --- TRANSFORM COMPONENT ---
                if (auto tc = std::dynamic_pointer_cast<TransformComponent>(comp)) {
                    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::DragFloat3("PosiciÃ³n", &tc->position.x, 0.1f);
                        ImGui::DragFloat3("RotaciÃ³n", &tc->rotation.x, 0.5f);
                        ImGui::DragFloat3("Escala", &tc->scale.x, 0.1f);

                        if (m_SelectedEntity->GetParent() != nullptr) {
                            ImGui::Checkbox("Heredar Escala del Padre", &tc->inheritScale);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Si estÃ¡ desmarcado, el objeto rotarÃ¡ y se moverÃ¡ con el padre pero mantendrÃ¡ su escala propia sin deformarse (ideal para ruedas de vehÃ­culos).");
                            }
                        }

                        // Sincronizar bidireccionalmente con la entidad
                        m_SelectedEntity->position = tc->position;
                        m_SelectedEntity->rotation = tc->rotation;
                        m_SelectedEntity->scale = tc->scale;

                        if (m_SelectedEntity->HasComponent<SpriteRendererComponent>() || m_SelectedEntity->HasComponent<TileGridComponent>()) {
                            if (ImGui::Button("Snap 2D a Rejilla (Enteros)")) {
                                tc->position.x = std::round(tc->position.x);
                                tc->position.y = std::round(tc->position.y);
                                m_SelectedEntity->position = tc->position;
                            }
                        }
                        if (auto bc = m_SelectedEntity->GetComponent<BoneComponent>()) {
                            glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), glm::radians(tc->rotation.x), glm::vec3(1, 0, 0)) *
                                               glm::rotate(glm::mat4(1.0f), glm::radians(tc->rotation.y), glm::vec3(0, 1, 0)) *
                                               glm::rotate(glm::mat4(1.0f), glm::radians(tc->rotation.z), glm::vec3(0, 0, 1));
                            bc->orientation = glm::normalize(glm::quat_cast(rotMat));
                        }
                        if (auto col = m_SelectedEntity->GetComponent<ColliderComponent>()) {
                            col->SyncTransformFromEntity();
                        }
                        for (SlipEntity* p = m_SelectedEntity; p != nullptr; p = p->GetParent()) {
                            if (auto smc = p->GetComponent<SkinnedMeshComponent>()) {
                                smc->MarkBoneMatricesDirty();
                                smc->UpdateBoneMatrices();
                                break;
                            }
                        }
                    }
                }
                // --- MESH COMPONENT ---
                else if (auto mc = std::dynamic_pointer_cast<MeshComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Mesh Component", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
                        if (assetsDir.empty() && SlipProject::Get().HasActiveProject()) {
                            assetsDir = SlipProject::Get().GetCurrentProject().assetsPath;
                        }
                        if (assetsDir.empty() && SlipProject::Get().HasActiveProject()) {
                            assetsDir = SlipFileSystem::CombinePath(SlipProject::Get().GetCurrentProject().rootPath, "assets");
                        }
                        if (assetsDir.empty()) {
                            assetsDir = "assets";
                        }

                        // 1. Selector de Malla (Estilo Unity)
                        std::string currentModelDisplay = "[Ninguna]";
                        if (mc->modelPath == "primitive:cube") currentModelDisplay = "Cubo (Primitiva)";
                        else if (mc->modelPath == "primitive:sphere") currentModelDisplay = "Esfera (Primitiva)";
                        else if (mc->modelPath == "primitive:plane") currentModelDisplay = "Plano (Primitiva)";
                        else if (!mc->modelPath.empty()) currentModelDisplay = SlipFileSystem::GetFileName(mc->modelPath);

                        ImGui::Text("Malla 3D (Mesh):");
                        if (ImGui::BeginCombo("##MeshSelector", currentModelDisplay.c_str())) {
                            if (ImGui::Selectable("Cubo (Primitiva)", mc->modelPath == "primitive:cube")) {
                                mc->SetModel("primitive:cube");
                            }
                            if (ImGui::Selectable("Esfera (Primitiva)", mc->modelPath == "primitive:sphere")) {
                                mc->SetModel("primitive:sphere");
                            }
                            if (ImGui::Selectable("Plano (Primitiva)", mc->modelPath == "primitive:plane")) {
                                mc->SetModel("primitive:plane");
                            }

                            if (!assetsDir.empty()) {
                                auto modelFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".fbx,.obj,.dae,.gltf");
                                if (!modelFiles.empty()) {
                                    ImGui::Separator();
                                    ImGui::TextDisabled("Modelos del Proyecto:");
                                    for (const auto& mf : modelFiles) {
                                        bool isSel = (mc->modelPath == mf.fullPath);
                                        if (ImGui::Selectable(mf.name.c_str(), isSel)) {
                                            mc->SetModel(mf.fullPath);
                                        }
                                        if (isSel) ImGui::SetItemDefaultFocus();
                                    }
                                }
                            }
                            ImGui::EndCombo();
                        }

                        // Drag & Drop para Malla
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                std::string droppedPath = (const char*)payload->Data;
                                std::string ext = SlipFileSystem::GetFileExtension(droppedPath);
                                if (ext == ".fbx" || ext == ".obj" || ext == ".dae" || ext == ".gltf") {
                                    mc->SetModel(droppedPath);
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        ImGui::Separator();

                        // 2. Materiales del Modelo (Soporte Multi-Material por Slot)
                        size_t slotCount = mc->GetMaterialSlotCount();
                        std::vector<FileInfo> matFiles;
                        if (!assetsDir.empty()) {
                            matFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".mat");
                        }

                        if (slotCount <= 1) {
                            std::string currentMatDisplay = "[VacÃ­o / Default]";
                            if (!mc->materialPath.empty()) {
                                currentMatDisplay = SlipFileSystem::GetFileName(mc->materialPath);
                            }

                            std::string slotLabel = "Material";
                            if (slotCount == 1) {
                                slotLabel = "Material (" + mc->GetMaterialSlotName(0) + ")";
                            }
                            ImGui::Text("%s:", slotLabel.c_str());
                            float availWidth = ImGui::GetContentRegionAvail().x;
                            ImGui::SetNextItemWidth(availWidth - 75.0f);
                            if (ImGui::BeginCombo("##MaterialSelector", currentMatDisplay.c_str())) {
                                if (ImGui::Selectable("[VacÃ­o / Default]", mc->materialPath.empty())) {
                                    mc->SetMaterial("");
                                }

                                if (!matFiles.empty()) {
                                    ImGui::Separator();
                                    ImGui::TextDisabled("Materiales del Proyecto:");
                                    for (const auto& mf : matFiles) {
                                        bool isSel = (mc->materialPath == mf.fullPath);
                                        if (ImGui::Selectable(mf.name.c_str(), isSel)) {
                                            mc->SetMaterial(mf.fullPath);
                                        }
                                        if (isSel) ImGui::SetItemDefaultFocus();
                                    }
                                }
                                ImGui::EndCombo();
                            }

                            // Drag & Drop para Material
                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                    std::string droppedPath = (const char*)payload->Data;
                                    if (SlipFileSystem::GetFileExtension(droppedPath) == ".mat") {
                                        mc->SetMaterial(droppedPath);
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }

                            ImGui::SameLine();
                            if (ImGui::Button("[+] Nuevo##NewMat", ImVec2(65, 0))) {
                                std::string matDir = SlipFileSystem::CombinePath(assetsDir, "Materials");
                                if (!mc->modelPath.empty() && mc->modelPath.rfind("primitive:", 0) != 0) {
                                    std::string modelParent = SlipFileSystem::GetParentPath(mc->modelPath);
                                    if (!modelParent.empty() && SlipFileSystem::Exists(modelParent)) {
                                        matDir = SlipFileSystem::CombinePath(modelParent, "Materials");
                                    }
                                }
                                SlipFileSystem::CreateDirectoryPath(matDir);
                                std::string baseMatName = m_SelectedEntity ? (m_SelectedEntity->GetName() + "_Mat") : "NuevoMaterial";
                                std::string newMatPath = SlipFileSystem::CombinePath(matDir, baseMatName + ".mat");
                                int counter = 1;
                                while (SlipFileSystem::Exists(newMatPath)) {
                                    newMatPath = SlipFileSystem::CombinePath(matDir, baseMatName + "_" + std::to_string(counter++) + ".mat");
                                }
                                SlipMaterial newMat;
                                newMat.color = glm::vec3(0.8f, 0.8f, 0.8f);
                                newMat.roughness = 0.5f;
                                newMat.Save(newMatPath);
                                SlipModelExtract::EnsureMetaForFile(newMatPath);
                                SlipThumbnailManager::Get().GenerateMaterialThumbnail(newMatPath, true);
                                mc->SetMaterial(newMatPath);
                                SlipConsole::LogSuccess("Material creado: " + SlipFileSystem::GetFileName(newMatPath));
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Crear y vincular un nuevo material (.mat)");
                            }

                            // Propiedades del Material Asignado
                            if (mc->material) {
                                ImGui::Spacing();
                                ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "Propiedades: %s", currentMatDisplay.c_str());
                                RenderSlipMaterialPropertiesEditor(*mc->material, "SingleSlotMat", assetsDir, m_ContextLevel);

                                if (!mc->materialPath.empty()) {
                                    if (ImGui::Button(" Guardar Material (.mat) ", ImVec2(-1, 26))) {
                                        mc->material->Save(mc->materialPath);
                                        SlipThumbnailManager::Get().GenerateMaterialThumbnail(mc->materialPath, true);
                                        SlipConsole::LogSuccess("Material guardado: " + mc->materialPath);
                                    }
                                }
                            }
                        } else {
                            ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "Materiales del Modelo (%zu Slots):", slotCount);
                            ImGui::TextDisabled("Arrastra un archivo .mat o selecciona uno para cada sub-malla:");
                            ImGui::Spacing();

                            for (size_t sIdx = 0; sIdx < slotCount; ++sIdx) {
                                ImGui::PushID((int)sIdx);
                                std::string slotName = mc->GetMaterialSlotName(sIdx);
                                std::string slotPath = mc->GetSubmeshMaterialPath(sIdx);
                                std::string slotDisplay = slotPath.empty() ? "[VacÃ­o / Default]" : SlipFileSystem::GetFileName(slotPath);

                                ImGui::Text("Slot %zu (%s):", sIdx, slotName.c_str());
                                float availW = ImGui::GetContentRegionAvail().x;
                                ImGui::SetNextItemWidth(availW - 75.0f);
                                if (ImGui::BeginCombo("##SlotMatCombo", slotDisplay.c_str())) {
                                    if (ImGui::Selectable("[VacÃ­o / Default]", slotPath.empty())) {
                                        mc->SetSubmeshMaterial(sIdx, "");
                                    }
                                    if (!matFiles.empty()) {
                                        ImGui::Separator();
                                        ImGui::TextDisabled("Materiales del Proyecto:");
                                        for (const auto& mf : matFiles) {
                                            bool isSel = (slotPath == mf.fullPath);
                                            if (ImGui::Selectable(mf.name.c_str(), isSel)) {
                                                mc->SetSubmeshMaterial(sIdx, mf.fullPath);
                                            }
                                            if (isSel) ImGui::SetItemDefaultFocus();
                                        }
                                    }
                                    ImGui::EndCombo();
                                }

                                if (ImGui::BeginDragDropTarget()) {
                                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                        std::string droppedPath = (const char*)payload->Data;
                                        if (SlipFileSystem::GetFileExtension(droppedPath) == ".mat") {
                                            mc->SetSubmeshMaterial(sIdx, droppedPath);
                                        }
                                    }
                                    ImGui::EndDragDropTarget();
                                }

                                ImGui::SameLine();
                                if (ImGui::Button("[+] Nuevo", ImVec2(65, 0))) {
                                    std::string matDir = SlipFileSystem::CombinePath(assetsDir, "Materials");
                                    if (!mc->modelPath.empty() && mc->modelPath.rfind("primitive:", 0) != 0) {
                                        std::string modelParent = SlipFileSystem::GetParentPath(mc->modelPath);
                                        if (!modelParent.empty() && SlipFileSystem::Exists(modelParent)) {
                                            matDir = SlipFileSystem::CombinePath(modelParent, "Materials");
                                        }
                                    }
                                    SlipFileSystem::CreateDirectoryPath(matDir);
                                    std::string baseMatName = slotName.empty() ? ("Slot_" + std::to_string(sIdx)) : slotName;
                                    std::string newMatPath = SlipFileSystem::CombinePath(matDir, baseMatName + ".mat");
                                    int counter = 1;
                                    while (SlipFileSystem::Exists(newMatPath)) {
                                        newMatPath = SlipFileSystem::CombinePath(matDir, baseMatName + "_" + std::to_string(counter++) + ".mat");
                                    }
                                    SlipMaterial newMat;
                                    newMat.color = glm::vec3(0.8f, 0.8f, 0.8f);
                                    newMat.roughness = 0.5f;
                                    newMat.Save(newMatPath);
                                    SlipModelExtract::EnsureMetaForFile(newMatPath);
                                    SlipThumbnailManager::Get().GenerateMaterialThumbnail(newMatPath, true);
                                    mc->SetSubmeshMaterial(sIdx, newMatPath);
                                    SlipConsole::LogSuccess("Material creado y asignado al Slot " + std::to_string(sIdx) + ": " + SlipFileSystem::GetFileName(newMatPath));
                                }

                                if (auto slotMat = mc->GetSubmeshMaterial(sIdx)) {
                                    std::string treeLabel = "Editar " + slotDisplay + "##SlotEdit_" + std::to_string(sIdx);
                                    if (ImGui::TreeNode(treeLabel.c_str())) {
                                        RenderSlipMaterialPropertiesEditor(*slotMat, ("SlotMat_" + std::to_string(sIdx)).c_str(), assetsDir, m_ContextLevel);
                                        if (!slotPath.empty()) {
                                            if (ImGui::Button(" Guardar Material (.mat) ", ImVec2(-1, 26))) {
                                                slotMat->Save(slotPath);
                                                SlipThumbnailManager::Get().GenerateMaterialThumbnail(slotPath, true);
                                                SlipConsole::LogSuccess("Material guardado: " + slotPath);
                                            }
                                        }
                                        ImGui::TreePop();
                                    }
                                }

                                ImGui::Separator();
                                ImGui::PopID();
                            }
                        }
                    }
                    if (!open) {
                        compToRemove = comp;
                    }
                }
                // --- BOX COLLIDER ---
                else if (auto box = std::dynamic_pointer_cast<BoxColliderComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Box Collider", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (ImGui::DragFloat3("Dimensiones (Size)", &box->size.x, 0.1f, 0.01f, 1000.0f)) {
                            box->CreateOrUpdateShape();
                        }
                        ImGui::DragFloat3("Offset Central", &box->centerOffset.x, 0.1f);
                        ImGui::Checkbox("Es Trigger", &box->isTrigger);
                    }
                    if (!open) compToRemove = comp;
                }
                // --- SPHERE COLLIDER ---
                else if (auto sphere = std::dynamic_pointer_cast<SphereColliderComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Sphere Collider", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (ImGui::DragFloat("Radio", &sphere->radius, 0.05f, 0.01f, 500.0f)) {
                            sphere->CreateOrUpdateShape();
                        }
                        ImGui::DragFloat3("Offset Central", &sphere->centerOffset.x, 0.1f);
                        ImGui::Checkbox("Es Trigger", &sphere->isTrigger);
                    }
                    if (!open) compToRemove = comp;
                }
                // --- CAPSULE COLLIDER ---
                else if (auto capsule = std::dynamic_pointer_cast<CapsuleColliderComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Capsule Collider", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (ImGui::DragFloat("Radio", &capsule->radius, 0.05f, 0.01f, 500.0f) ||
                            ImGui::DragFloat("Altura", &capsule->height, 0.1f, 0.01f, 500.0f)) {
                            capsule->CreateOrUpdateShape();
                        }
                        ImGui::DragFloat3("Offset Central", &capsule->centerOffset.x, 0.1f);
                        ImGui::Checkbox("Es Trigger", &capsule->isTrigger);
                    }
                    if (!open) compToRemove = comp;
                }
                // --- MESH COLLIDER ---
                else if (auto meshCol = std::dynamic_pointer_cast<MeshColliderComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Mesh Collider", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (ImGui::Checkbox("Es Convexo", &meshCol->isConvex)) {
                            meshCol->CreateOrUpdateShape();
                            auto rb = m_SelectedEntity->GetComponent<RigidBodyComponent>();
                            float m = rb ? (rb->isKinematic ? 0.0f : rb->mass) : 0.0f;
                            bool k = rb ? rb->isKinematic : false;
                            meshCol->RecreateRigidBody(m, k);
                        }
                        if (ImGui::DragFloat3("Offset Central", &meshCol->centerOffset.x, 0.1f)) {
                            meshCol->SyncTransformFromEntity();
                        }
                        ImGui::Checkbox("Es Trigger", &meshCol->isTrigger);
                        if (ImGui::Button(" Reconstruir Malla de Colision ")) {
                            meshCol->CreateOrUpdateShape();
                            auto rb = m_SelectedEntity->GetComponent<RigidBodyComponent>();
                            float m = rb ? (rb->isKinematic ? 0.0f : rb->mass) : 0.0f;
                            bool k = rb ? rb->isKinematic : false;
                            meshCol->RecreateRigidBody(m, k);
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- RIGIDBODY (ESTILO UNITY) ---
                else if (auto rb = std::dynamic_pointer_cast<RigidBodyComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("RigidBody (Fisicas Dinamicas)", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        bool changed = false;

                        if (ImGui::DragFloat("Mass (kg)", &rb->mass, 0.1f, 0.001f, 10000.0f)) changed = true;
                        if (ImGui::DragFloat("Linear Drag", &rb->linearDrag, 0.01f, 0.0f, 100.0f)) changed = true;
                        if (ImGui::DragFloat("Angular Drag", &rb->angularDrag, 0.01f, 0.0f, 100.0f)) changed = true;
                        if (ImGui::SliderFloat("Friction", &rb->friction, 0.0f, 1.0f)) changed = true;
                        if (ImGui::SliderFloat("Rolling Friction", &rb->rollingFriction, 0.0f, 1.0f)) changed = true;
                        if (ImGui::SliderFloat("Restitution (Bounciness)", &rb->restitution, 0.0f, 1.0f)) changed = true;
                        if (ImGui::Checkbox("Use Gravity", &rb->useGravity)) changed = true;
                        if (ImGui::Checkbox("Is Kinematic", &rb->isKinematic)) changed = true;

                        ImGui::Spacing();
                        if (ImGui::TreeNodeEx("Constraints", ImGuiTreeNodeFlags_DefaultOpen)) {
                            // Freeze Position
                            ImGui::Text("Freeze Position");
                            ImGui::SameLine(160);
                            if (ImGui::Checkbox("X##FPosX", &rb->freezePositionX)) changed = true;
                            ImGui::SameLine();
                            if (ImGui::Checkbox("Y##FPosY", &rb->freezePositionY)) changed = true;
                            ImGui::SameLine();
                            if (ImGui::Checkbox("Z##FPosZ", &rb->freezePositionZ)) changed = true;

                            // Freeze Rotation
                            ImGui::Text("Freeze Rotation");
                            ImGui::SameLine(160);
                            if (ImGui::Checkbox("X##FRotX", &rb->freezeRotationX)) changed = true;
                            ImGui::SameLine();
                            if (ImGui::Checkbox("Y##FRotY", &rb->freezeRotationY)) changed = true;
                            ImGui::SameLine();
                            if (ImGui::Checkbox("Z##FRotZ", &rb->freezeRotationZ)) changed = true;

                            ImGui::TreePop();
                        }

                        if (changed) {
                            rb->ApplyToCollider();
                        }

                        if (SlipEditor::GetInstance() && SlipEditor::GetInstance()->IsInPlaySession()) {
                            ImGui::Spacing();
                            ImGui::Separator();
                            glm::vec3 linVel = rb->GetLinearVelocity();
                            glm::vec3 angVel = rb->GetAngularVelocity();
                            ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Velocidad Lineal: (%.2f, %.2f, %.2f)", linVel.x, linVel.y, linVel.z);
                            ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Velocidad Angular: (%.2f, %.2f, %.2f)", angVel.x, angVel.y, angVel.z);
                        }

                        ImGui::Spacing();
                        if (ImGui::Button(" Aplicar Fisicas al Collider ", ImVec2(-1, 28))) {
                            rb->ApplyToCollider();
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- VEHICLE (BULLET RAYCAST VEHICLE) ---
                else if (auto veh = std::dynamic_pointer_cast<VehicleComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Vehicle (Bullet Raycast)", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        bool changed = false;

                        float speed = veh->GetCurrentSpeedKmHour();
                        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "Velocidad actual: %.1f km/h", speed);

                        if (ImGui::Checkbox("Mostrar Gizmos de Debug (Ruedas/CoM)", &veh->showDebugGizmos)) changed = true;
                        if (ImGui::Checkbox("Estabilizador Antivuelco Activo", &veh->enableAntiRollBar)) changed = true;
                        if (veh->enableAntiRollBar) {
                            if (ImGui::DragFloat("Fuerza Antivuelco", &veh->antiRollStrength, 100.0f, 500.0f, 20000.0f)) changed = true;
                        }

                        if (ImGui::TreeNodeEx("Parametros de Suspension", ImGuiTreeNodeFlags_DefaultOpen)) {
                            if (ImGui::SliderFloat("Rigidez (Stiffness)", &veh->suspensionStiffness, 5.0f, 100.0f)) changed = true;
                            if (ImGui::SliderFloat("Amortiguacion (Damping)", &veh->suspensionDamping, 0.1f, 10.0f)) changed = true;
                            if (ImGui::SliderFloat("Compresion", &veh->suspensionCompression, 0.1f, 10.0f)) changed = true;
                            if (ImGui::SliderFloat("Recorrido Max (cm)", &veh->maxSuspensionTravelCm, 10.0f, 1000.0f)) changed = true;
                            if (ImGui::SliderFloat("Friccion / Agarre (Friction Slip)", &veh->frictionSlip, 1.0f, 50.0f)) changed = true;
                            if (ImGui::SliderFloat("Influencia de Balanceo", &veh->rollInfluence, 0.0f, 1.0f)) changed = true;
                            if (ImGui::DragFloat("Fuerza Max Suspension", &veh->maxSuspensionForce, 100.0f, 500.0f, 50000.0f)) changed = true;
                            ImGui::TreePop();
                        }

                        if (ImGui::TreeNodeEx("Dimensiones de Chasis", ImGuiTreeNodeFlags_DefaultOpen)) {
                            if (ImGui::DragFloat("Ancho Chasis", &veh->chassisWidth, 0.05f, 0.5f, 10.0f)) changed = true;
                            if (ImGui::DragFloat("Alto Chasis", &veh->chassisHeight, 0.05f, 0.1f, 5.0f)) changed = true;
                            if (ImGui::DragFloat("Largo Chasis", &veh->chassisLength, 0.05f, 1.0f, 20.0f)) changed = true;
                            if (ImGui::DragFloat("Radio de Rueda", &veh->defaultWheelRadius, 0.02f, 0.1f, 2.0f)) changed = true;

                            if (ImGui::Button("Reconfigurar 4 Ruedas por Defecto", ImVec2(-1, 26))) {
                                veh->SetupDefaultCarWheels();
                                veh->InitVehicle();
                            }

                            if (ImGui::Button("Generar 4 Ruedas Esfericas (Hijos)", ImVec2(-1, 26))) {
                                float halfW = veh->chassisWidth * 0.5f;
                                float halfL = veh->chassisLength * 0.5f;
                                float heightOff = -veh->chassisHeight * 0.5f;
                                float d = veh->defaultWheelRadius * 2.0f;

                                struct WheelDef { const char* name; glm::vec3 pos; };
                                WheelDef defs[4] = {
                                    { "Rueda_FL", glm::vec3( halfW, heightOff,  halfL) },
                                    { "Rueda_FR", glm::vec3(-halfW, heightOff,  halfL) },
                                    { "Rueda_RL", glm::vec3( halfW, heightOff, -halfL) },
                                    { "Rueda_RR", glm::vec3(-halfW, heightOff, -halfL) }
                                };

                                for (const auto& wdef : defs) {
                                    SlipEntity* existing = m_SelectedEntity->FindChild(wdef.name, false);
                                    if (!existing) {
                                        existing = new SlipEntity(wdef.name);
                                        existing->SetParent(m_SelectedEntity);
                                        SlipLevel::Get().AddEntity(existing);
                                    }
                                    existing->position = wdef.pos;
                                    existing->scale = glm::vec3(d);
                                    if (auto wt = existing->GetComponent<TransformComponent>()) {
                                        wt->position = wdef.pos;
                                        wt->scale = glm::vec3(d);
                                        wt->inheritScale = false;
                                    }
                                    if (!existing->GetComponent<MeshComponent>()) {
                                        existing->AddComponent<MeshComponent>("primitive:sphere");
                                    }
                                }
                            }
                            ImGui::TreePop();
                        }

                        if (SlipEditor::GetInstance() && SlipEditor::GetInstance()->IsInPlaySession()) {
                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Text("Controles en Tiempo Real:");
                            static float liveEngineForce = 0.0f;
                            static float liveBrake = 0.0f;
                            static float liveSteering = 0.0f;

                            if (ImGui::SliderFloat("Fuerza Motor", &liveEngineForce, -3000.0f, 3000.0f)) {
                                veh->SetEngineForce(liveEngineForce);
                            }
                            if (ImGui::SliderFloat("Direccion", &liveSteering, -0.6f, 0.6f)) {
                                veh->SetSteering(liveSteering);
                            }
                            if (ImGui::SliderFloat("Freno", &liveBrake, 0.0f, 500.0f)) {
                                veh->SetBrake(liveBrake);
                            }
                            if (ImGui::Button("Freno de Mano")) {
                                liveBrake = 200.0f;
                                veh->SetBrake(200.0f);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Soltar Freno")) {
                                liveBrake = 0.0f;
                                veh->SetBrake(0.0f);
                            }

                            ImGui::Text("Ruedas activas: %d", veh->GetNumWheels());
                            for (int w = 0; w < veh->GetNumWheels(); ++w) {
                                glm::vec3 wPos = veh->GetWheelPosition(w);
                                bool onGround = veh->IsWheelOnGround(w);
                                ImGui::Text("Rueda %d: (%.2f, %.2f, %.2f) [%s]", w, wPos.x, wPos.y, wPos.z, onGround ? "En Suelo" : "En Aire");
                            }
                        }

                        if (changed && veh->GetBulletVehicle()) {
                            veh->InitVehicle();
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- LIGHT COMPONENT ---
                else if (auto light = std::dynamic_pointer_cast<LightComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Light Component", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        const char* lightTypes[] = { "Direccional (Sol)", "Puntual (Point)", "Spot (Foco)" };
                        int currentType = static_cast<int>(light->type);
                        if (ImGui::Combo("Tipo de Luz", &currentType, lightTypes, IM_ARRAYSIZE(lightTypes))) {
                            light->type = static_cast<LightType>(currentType);
                        }

                        const char* mobilityModes[] = { "Estática (Baked Lightmap)", "Mixta (Indirect Baked + Direct RT)", "Dinámica (Clustered RT)" };
                        int currentMobility = static_cast<int>(light->mobility);
                        if (ImGui::Combo("Movilidad (Baking)", &currentMobility, mobilityModes, IM_ARRAYSIZE(mobilityModes))) {
                            light->mobility = static_cast<LightMobility>(currentMobility);
                        }

                        ImGui::ColorEdit3("Color", &light->color.r);
                        ImGui::DragFloat("Intensidad", &light->intensity, 0.1f, 0.0f, 100.0f);

                        if (light->type == LightType::Point || light->type == LightType::Spot) {
                            ImGui::DragFloat("Rango (Distancia)", &light->range, 0.5f, 0.1f, 500.0f);
                        }
                        if (light->type == LightType::Spot) {
                            ImGui::DragFloat("Ãngulo Cono (Spot)", &light->spotAngle, 0.5f, 1.0f, 179.0f);
                        }

                        ImGui::Spacing();
                        ImGui::Checkbox("Proyectar Sombras (Cast Shadows)", &light->castShadows);
                    }
                    if (!open) compToRemove = comp;
                }
                else if (auto volume = std::dynamic_pointer_cast<DDGIVolumeComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Volumen DDGI", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::DragFloat3("TamaÃ±o", &volume->size.x, 0.1f, 0.5f, 500.0f);
                        int equal = volume->probesX;
                        if (volume->probesX == volume->probesY && volume->probesY == volume->probesZ) equal = volume->probesX;
                        if (ImGui::SliderInt("Iguales por eje", &equal, 1, 16)) {
                            volume->probesX = volume->probesY = volume->probesZ = equal;
                        }
                        auto clampEdited = [](int& axis, int otherA, int otherB) {
                            otherA = (std::max)(otherA, 1);
                            otherB = (std::max)(otherB, 1);
                            int maxAxis = (std::max)(1, (std::min)(32, 2048 / (otherA * otherB)));
                            axis = std::clamp(axis, 1, maxAxis);
                        };
                        int prevX = volume->probesX;
                        int prevY = volume->probesY;
                        int prevZ = volume->probesZ;
                        ImGui::DragInt("Sondas X", &volume->probesX, 1.0f, 1, 32);
                        ImGui::DragInt("Sondas Y", &volume->probesY, 1.0f, 1, 32);
                        ImGui::DragInt("Sondas Z", &volume->probesZ, 1.0f, 1, 32);
                        if (volume->probesX != prevX) clampEdited(volume->probesX, volume->probesY, volume->probesZ);
                        else if (volume->probesY != prevY) clampEdited(volume->probesY, volume->probesX, volume->probesZ);
                        else if (volume->probesZ != prevZ) clampEdited(volume->probesZ, volume->probesX, volume->probesY);
                        int product = volume->probesX * volume->probesY * volume->probesZ;
                        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Total de Sondas: %d / 2048", product);

                        ImGui::TextDisabled("Densidades Predefinidas:");
                        if (ImGui::Button("Baja (8x4x8)"))   { volume->probesX = 8; volume->probesY = 4; volume->probesZ = 8; }
                        ImGui::SameLine();
                        if (ImGui::Button("Media (12x6x12)")) { volume->probesX = 12; volume->probesY = 6; volume->probesZ = 12; }
                        ImGui::SameLine();
                        if (ImGui::Button("Alta (16x8x16)"))  { volume->probesX = 16; volume->probesY = 8; volume->probesZ = 16; }

                        ImGui::Checkbox("Previsualizar sondas", &volume->previewProbes);
                    }
                    if (!open) compToRemove = comp;
                }
                else if (auto probe = std::dynamic_pointer_cast<ReflectionProbeComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Sonda de Reflexión (Volumen)", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::DragFloat3("Tamaño de Caja", &probe->size.x, 0.1f, 0.5f, 500.0f);
                        const char* resOptions[] = { "128x128", "256x256", "512x512" };
                        int currentResIdx = (probe->resolution <= 128) ? 0 : ((probe->resolution <= 256) ? 1 : 2);
                        if (ImGui::Combo("Resolución Cubemap", &currentResIdx, resOptions, 3)) {
                            if (currentResIdx == 0) probe->resolution = 128;
                            else if (currentResIdx == 1) probe->resolution = 256;
                            else if (currentResIdx == 2) probe->resolution = 512;
                        }
                        ImGui::SliderFloat("Intensidad de Reflejo", &probe->intensity, 0.0f, 3.0f, "%.2f");
                        ImGui::SliderFloat("Límite de Rugosidad (Cutoff)", &probe->roughnessCutoff, 0.05f, 1.0f, "%.2f");
                        ImGui::Checkbox("Corrección de Paralaje (Box Projection)", &probe->boxProjection);
                        ImGui::Checkbox("Incluir Objetos Dinámicos al Hornear", &probe->includeDynamic);

                        ImGui::Spacing();
                        if (probe->isBaked && probe->cubemap != 0) {
                            ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.45f, 1.0f), "[Cubemap Horneado con Exito]");
                        } else {
                            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "[Sin Hornear]");
                        }

                        if (ImGui::Button("Hornear Reflexión (Bake Cubemap)", ImVec2(240, 30))) {
                            SlipEditor* ed = SlipEditor::GetInstance();
                            if (ed && ed->GetViewportPanel() && m_SelectedEntity) {
                                glm::vec3 entPos = m_SelectedEntity->GetTransform() ? m_SelectedEntity->GetTransform()->position : m_SelectedEntity->position;
                                ed->GetViewportPanel()->BakeReflectionProbe(probe.get(), entPos);
                                SlipConsole::LogSuccess("Sonda de reflexion horneada para entidad: " + m_SelectedEntity->GetName());
                            }
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                else if (auto post = std::dynamic_pointer_cast<PostProcessVolumeComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Volumen de post-proceso", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Global (extension infinita)", &post->isGlobal);
                        ImGui::DragInt("Prioridad", &post->priority, 1.0f, -20, 20);
                        ImGui::SliderFloat("Peso", &post->weight, 0.0f, 1.0f);
                        if (!post->isGlobal) {
                            ImGui::DragFloat3("TamaÃ±o", &post->size.x, 0.1f, 0.1f, 500.0f);
                            ImGui::DragFloat("Distancia de mezcla", &post->blendDistance, 0.05f, 0.0f, 50.0f);
                        }

                        if (ImGui::TreeNode("Bloom")) {
                            ImGui::Checkbox("Override##Bloom", &post->bloomOverride);
                            ImGui::Checkbox("Activado##Bloom", &post->bloomEnabled);
                            ImGui::DragFloat("Intensidad##Bloom", &post->bloomIntensity, 0.01f, 0.0f, 8.0f);
                            ImGui::DragFloat("Umbral##Bloom", &post->bloomThreshold, 0.01f, 0.0f, 10.0f);
                            ImGui::SliderFloat("Soft knee##Bloom", &post->bloomSoftKnee, 0.0f, 1.0f);
                            ImGui::SliderFloat("Radio##Bloom", &post->bloomRadius, 0.0f, 1.5f);
                            ImGui::ColorEdit3("Tinte##Bloom", &post->bloomTint.r);
                            ImGui::TreePop();
                        }
                        if (ImGui::TreeNode("Bloom de lente")) {
                            ImGui::Checkbox("Override##Lens", &post->lensOverride);
                            ImGui::Checkbox("Activado##Lens", &post->lensEnabled);
                            ImGui::DragFloat("Intensidad##Lens", &post->lensIntensity, 0.01f, 0.0f, 8.0f);
                            ImGui::DragFloat("Umbral##Lens", &post->lensThreshold, 0.01f, 0.0f, 16.0f);
                            ImGui::SliderFloat("Rayas##Lens", &post->lensStreak, 0.0f, 1.0f);
                            ImGui::SliderFloat("Halo##Lens", &post->lensHalo, 0.0f, 1.0f);
                            ImGui::TreePop();
                        }
                        if (ImGui::TreeNode("Oclusion ambiental")) {
                            ImGui::Checkbox("Override##AO", &post->aoOverride);
                            ImGui::Checkbox("Activado##AO", &post->aoEnabled);
                            ImGui::DragFloat("Radio##AO", &post->aoRadius, 0.01f, 0.01f, 4.0f);
                            ImGui::DragFloat("Bias##AO", &post->aoBias, 0.001f, 0.0f, 0.2f);
                            ImGui::SliderFloat("Intensidad##AO", &post->aoIntensity, 0.0f, 2.0f);
                            ImGui::DragFloat("Potencia##AO", &post->aoPower, 0.05f, 0.1f, 4.0f);
                            ImGui::TreePop();
                        }
                        if (ImGui::TreeNode("ViÃ±eta")) {
                            ImGui::Checkbox("Override##Vignette", &post->vignetteOverride);
                            ImGui::Checkbox("Activado##Vignette", &post->vignetteEnabled);
                            ImGui::SliderFloat("Intensidad##Vignette", &post->vignetteIntensity, 0.0f, 1.0f);
                            ImGui::SliderFloat("Suavidad##Vignette", &post->vignetteSmoothness, 0.05f, 0.95f);
                            ImGui::TreePop();
                        }
                        if (ImGui::TreeNode("Auto exposicion")) {
                            ImGui::Checkbox("Override##Exposure", &post->exposureOverride);
                            ImGui::Checkbox("Activado##Exposure", &post->exposureEnabled);
                            ImGui::DragFloat("Luminancia min##Exposure", &post->exposureMin, 0.01f, 0.001f, 4.0f);
                            ImGui::DragFloat("Luminancia max##Exposure", &post->exposureMax, 0.05f, 0.01f, 32.0f);
                            ImGui::DragFloat("Compensacion EV##Exposure", &post->exposureCompensation, 0.05f, -8.0f, 8.0f);
                            ImGui::DragFloat("Velocidad##Exposure", &post->exposureSpeed, 0.05f, 0.0f, 10.0f);
                            ImGui::TreePop();
                        }
                        if (ImGui::TreeNode("Correccion de color")) {
                            ImGui::Checkbox("Override##Color", &post->colorOverride);
                            const char* tonemaps[] = { "Reinhard", "ACES" };
                            ImGui::Combo("Tonemap##Color", &post->tonemap, tonemaps, IM_ARRAYSIZE(tonemaps));
                            ImGui::DragFloat("Contraste##Color", &post->contrast, 0.01f, 0.0f, 3.0f);
                            ImGui::DragFloat("Saturacion##Color", &post->saturation, 0.01f, 0.0f, 3.0f);
                            ImGui::SliderFloat("Temperatura##Color", &post->temperature, -1.0f, 1.0f);
                            ImGui::DragFloat3("Lift##Color", &post->lift.r, 0.01f, -1.0f, 1.0f);
                            ImGui::DragFloat3("Gamma##Color", &post->gamma.r, 0.01f, 0.1f, 4.0f);
                            ImGui::DragFloat3("Gain##Color", &post->gain.r, 0.01f, 0.0f, 4.0f);
                            ImGui::ColorEdit3("Filtro##Color", &post->colorFilter.r);
                            ImGui::TreePop();
                        }
                        if (ImGui::TreeNode("Blur")) {
                            ImGui::Checkbox("Override##Blur", &post->blurOverride);
                            ImGui::DragFloat("Radio (pixeles)##Blur", &post->blurRadius, 0.1f, 0.0f, 24.0f);
                            ImGui::TreePop();
                        }
                        if (ImGui::TreeNode("Radial blur")) {
                            ImGui::Checkbox("Override##Radial", &post->radialOverride);
                            ImGui::SliderFloat("Fuerza##Radial", &post->radialStrength, 0.0f, 1.0f);
                            ImGui::SliderFloat2("Centro UV##Radial", &post->radialCenter.x, 0.0f, 1.0f);
                            ImGui::TreePop();
                        }
                        if (ImGui::TreeNode("Niebla Volumetrica (Height Fog)")) {
                            ImGui::Checkbox("Override##Fog", &post->fogOverride);
                            ImGui::Checkbox("Activado##Fog", &post->fogEnabled);
                            ImGui::DragFloat("Densidad##Fog", &post->fogDensity, 0.0002f, 0.0f, 0.05f, "%.5f");
                            ImGui::DragFloat("Caida por Altura##Fog", &post->fogHeightFalloff, 0.002f, 0.0001f, 0.5f, "%.4f");
                            ImGui::DragFloat("Altura Base (m)##Fog", &post->fogHeight, 0.5f, -500.0f, 500.0f, "%.1f m");
                            ImGui::DragFloat("Distancia Inicial##Fog", &post->fogStartDistance, 1.0f, 0.0f, 1000.0f, "%.1f m");
                            ImGui::DragFloat("Distancia Maxima##Fog", &post->fogMaxDistance, 10.0f, 50.0f, 30000.0f, "%.0f m");
                            ImGui::ColorEdit3("Color de Niebla##Fog", &post->fogColor.r);
                            ImGui::SliderFloat("Scattering Solar##Fog", &post->fogSunScattering, 0.0f, 2.0f, "%.2f");
                            ImGui::SliderFloat("Afectar Nubes/Cielo##Fog", &post->fogSkyBlend, 0.0f, 1.0f, "%.2f");
                            ImGui::TreePop();
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- SLIP SKY COMPONENT (NUBES VOLUMÃ‰TRICAS) ---
                else if (auto sky = std::dynamic_pointer_cast<SlipSkyComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Cielo y Nubes VolumÃ©tricas (SlipSkyComponent)", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "Cielo Dinamico y Nubes Volumetricas Hibridas");
                        ImGui::Separator();

                        bool enabled = sky->IsEnabled();
                        if (ImGui::Checkbox("Habilitado##SkyEnabled", &enabled)) {
                            sky->SetEnabled(enabled);
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled(enabled ? "(Renderizando)" : "(Desactivado)");

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "Cielo Dinamico y Atmosfera Solar:");
                        ImGui::Checkbox("Vincular a Luz Direccional (Sol)", &sky->params.followDirectionalLight);
                        if (sky->params.followDirectionalLight) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("(Rotar luz direccional en escena)");
                        }
                        ImGui::Checkbox("Atmosfera Dinamica (Atardecer / Noche)", &sky->params.dynamicAtmosphere);

                        // Indicador de Hora del DÃ­a segÃºn la altura solar
                        float sunY = sky->params.sunDirection.y;
                        if (sky->params.dynamicAtmosphere) {
                            if (sky->currentAtmosphere.sunsetFactor > 0.05f) {
                                ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.15f, 1.0f), "Estado: Atardecer / Golden Hour (%.0f%%)", sky->currentAtmosphere.sunsetFactor * 100.0f);
                            } else if (sunY < -0.02f) {
                                ImGui::TextColored(ImVec4(0.45f, 0.65f, 1.0f, 1.0f), "Estado: Noche Estrellada / Crepusculo (Luna activa)");
                            } else {
                                ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.5f, 1.0f), "Estado: Dia Pleno (Sol en cenit)");
                            }
                        }

                        if (!sky->params.followDirectionalLight) {
                            ImGui::DragFloat3("Direccion Manual del Sol", &sky->params.sunDirection.x, 0.01f, -1.0f, 1.0f);
                            if (glm::length(sky->params.sunDirection) > 0.0001f) {
                                sky->params.sunDirection = glm::normalize(sky->params.sunDirection);
                            }
                        }

                        if (!sky->params.dynamicAtmosphere) {
                            ImGui::ColorEdit3("Color del Sol", &sky->params.sunColor.r);
                            ImGui::ColorEdit3("Color Ambiente Nubes", &sky->params.cloudAmbientColor.r);
                        } else {
                            ImGui::TextDisabled("Colores de sol y cielo calculados automaticamente");
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Text("Morfologia de la Capa de Nubes:");
                        ImGui::SliderFloat("Altura Base (m)", &sky->params.cloudBaseHeight, 10.0f, 3000.0f, "%.1f");
                        ImGui::SliderFloat("Grosor de Capa (m)", &sky->params.cloudLayerThickness, 10.0f, 1500.0f, "%.1f");
                        ImGui::SliderFloat("Cobertura Global", &sky->params.cloudCoverage, 0.0f, 1.0f, "%.2f");
                        ImGui::SliderFloat("Densidad Optica", &sky->params.cloudDensity, 0.001f, 0.3f, "%.4f");
                        ImGui::SliderFloat("Escala UV XZ", &sky->params.cloudScale, 0.0001f, 0.01f, "%.5f");
                        ImGui::SliderFloat("Desperfectos en Bordes", &sky->params.cloudEdgeErosion, 0.0f, 1.0f, "%.2f");
                        ImGui::SliderFloat("Escala Desperfectos", &sky->params.cloudDetailScale, 1.0f, 12.0f, "%.1f");

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Text("Dinamica de Viento:");
                        ImGui::SliderFloat("Velocidad Viento", &sky->params.windSpeed, -60.0f, 60.0f, "%.1f u/s");
                        ImGui::DragFloat2("Direccion Viento", &sky->params.windDirection.x, 0.02f, -1.0f, 1.0f);

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Text("Dispersion Optica (Scattering):");
                        ImGui::SliderFloat("Scattering Frontal (Henyey-G)", &sky->params.henyeyG, 0.0f, 0.95f, "%.2f");
                        ImGui::SliderFloat("Factor de Absorcion", &sky->params.absorption, 0.1f, 4.0f, "%.2f");
                        ImGui::SliderFloat("Efecto Azucar / Powder", &sky->params.powderFactor, 0.1f, 3.0f, "%.2f");

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(0.85f, 0.90f, 1.0f, 1.0f), "Sombras Dinamicas en Terreno:");
                        ImGui::Checkbox("Proyectar Sombras en Terreno", &sky->params.cloudShadowsEnabled);
                        if (sky->params.cloudShadowsEnabled) {
                            ImGui::SliderFloat("Intensidad Sombra Suelo", &sky->params.cloudShadowIntensity, 0.0f, 1.0f, "%.2f");
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Text("Calidad y Optimizacion Multiplataforma:");
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "Perfil: Android (OpenGL ES 3.0 TBDR)");
                        ImGui::SliderInt("Slices Instanciados", &sky->params.totalSlices, 4, 32);
                        ImGui::SliderFloat("Umbral Discard TBDR", &sky->params.discardThreshold, 0.001f, 0.2f, "%.3f");
#else
                        ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Perfil: Desktop PC (Raymarching + Bilateral)");
                        const char* qualityModes[] = { "Nativa 1:1 (Ultra Nitidez)", "Media 1/2 (Alta Fidelidad - Recomendada)", "Cuarto 1/4 (Rendimiento Ligero)" };
                        int currentQuality = (sky->params.downscaleFactor == 1) ? 0 : (sky->params.downscaleFactor == 4 ? 2 : 1);
                        if (ImGui::Combo("Resolucion de Nubes", &currentQuality, qualityModes, IM_ARRAYSIZE(qualityModes))) {
                            if (currentQuality == 0) sky->params.downscaleFactor = 1;
                            else if (currentQuality == 1) sky->params.downscaleFactor = 2;
                            else sky->params.downscaleFactor = 4;
                        }
                        ImGui::SliderInt("Pasos Raymarching", &sky->params.raymarchSteps, 8, 64);
                        ImGui::SliderFloat("Sensibilidad Bilateral", &sky->params.depthSensitivity, 0.01f, 1.0f, "%.3f");
                        ImGui::SliderInt("Slices (Perfil Android)", &sky->params.totalSlices, 4, 32);
                        ImGui::SliderFloat("Discard TBDR (Android)", &sky->params.discardThreshold, 0.001f, 0.2f, "%.3f");
#endif

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Text("Texturas de Nubes:");
                        std::string noiseLabel = sky->cloudNoisePath.empty() ? "(Procedural Generada en GPU)" : sky->cloudNoisePath;
                        ImGui::Text("Ruido 2D: %s", noiseLabel.c_str());

                        std::string blueLabel = sky->blueNoisePath.empty() ? "(Procedural Generada en GPU)" : sky->blueNoisePath;
                        ImGui::Text("Blue Noise: %s", blueLabel.c_str());

                        if (ImGui::Button("Restablecer a Texturas Procedurales")) {
                            sky->cloudNoiseTexture = nullptr;
                            sky->blueNoiseTexture = nullptr;
                            sky->cloudNoisePath.clear();
                            sky->blueNoisePath.clear();
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- SLIP TERRAIN COMPONENT (V-CLUST & BULLET PHYSICS) ---
                else if (auto terrain = std::dynamic_pointer_cast<SlipTerrainComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Terreno V-Clust (SlipTerrainComponent)", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "Malla Procedural y Clusters V-Clust");
                        ImGui::Separator();

                        bool enabled = terrain->IsEnabled();
                        if (ImGui::Checkbox("Habilitado##TerrainEnabled", &enabled)) {
                            terrain->SetEnabled(enabled);
                        }

                        bool needsRebuild = false;

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "Dimensiones del Terreno:");
                        if (ImGui::DragFloat("Ancho (X)##TerrainW", &terrain->width, 1.0f, 1.0f, 2048.0f, "%.1f m")) {
                            needsRebuild = true;
                        }
                        if (ImGui::DragFloat("Largo (Z)##TerrainL", &terrain->length, 1.0f, 1.0f, 2048.0f, "%.1f m")) {
                            needsRebuild = true;
                        }

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "Resolucion de Cuadricula:");
                        int resX = static_cast<int>(terrain->resolutionX);
                        int resZ = static_cast<int>(terrain->resolutionZ);
                        if (ImGui::DragInt("Subdivisiones X##TerrainResX", &resX, 1, 2, 512)) {
                            terrain->resolutionX = static_cast<uint32_t>((std::max)(2, resX));
                            needsRebuild = true;
                        }
                        if (ImGui::DragInt("Subdivisiones Z##TerrainResZ", &resZ, 1, 2, 512)) {
                            terrain->resolutionZ = static_cast<uint32_t>((std::max)(2, resZ));
                            needsRebuild = true;
                        }

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "Desplazamiento y Altura:");
                        if (ImGui::DragFloat("Escala de Altura##TerrainHScale", &terrain->heightScale, 0.2f, 0.0f, 500.0f, "%.2f m")) {
                            needsRebuild = true;
                        }
                        if (ImGui::DragFloat2("Tiling UV##TerrainUV", &terrain->uvScale.x, 0.1f, 0.1f, 100.0f, "%.2f")) {
                            needsRebuild = true;
                        }

                        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
                        if (assetsDir.empty() && SlipProject::Get().HasActiveProject()) {
                            assetsDir = SlipProject::Get().GetCurrentProject().assetsPath;
                        }
                        if (assetsDir.empty()) assetsDir = "assets";

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "Mapa de Alturas (Heightmap):");
                        std::string currentHmDisplay = terrain->heightmapPath.empty() ? "[Plano / Sin Heightmap]" : SlipFileSystem::GetFileName(terrain->heightmapPath);
                        if (ImGui::BeginCombo("##TerrainHMCombo", currentHmDisplay.c_str())) {
                            if (ImGui::Selectable("[Plano / Sin Heightmap]", terrain->heightmapPath.empty())) {
                                terrain->heightmapPath.clear();
                                needsRebuild = true;
                            }
                            auto hmFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".png");
                            auto jpgFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".jpg");
                            hmFiles.insert(hmFiles.end(), jpgFiles.begin(), jpgFiles.end());
                            if (!hmFiles.empty()) {
                                ImGui::Separator();
                                for (const auto& hf : hmFiles) {
                                    bool isSel = (terrain->heightmapPath == hf.fullPath);
                                    if (ImGui::Selectable(hf.name.c_str(), isSel)) {
                                        terrain->heightmapPath = hf.fullPath;
                                        needsRebuild = true;
                                    }
                                    if (isSel) ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }

                        // Drag & Drop para Heightmap desde ContentBrowser
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                const char* droppedPath = (const char*)payload->Data;
                                if (droppedPath) {
                                    std::string pStr(droppedPath);
                                    std::string ext = SlipFileSystem::GetFileExtension(pStr);
                                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
                                        terrain->heightmapPath = pStr;
                                        needsRebuild = true;
                                    }
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "Material:");
                        std::string currentMatDisplay = terrain->materialPath.empty() ? "[Material Predeterminado]" : SlipFileSystem::GetFileName(terrain->materialPath);
                        if (ImGui::BeginCombo("##TerrainMatCombo", currentMatDisplay.c_str())) {
                            if (ImGui::Selectable("[Material Predeterminado]", terrain->materialPath.empty())) {
                                terrain->SetMaterial("");
                            }
                            auto matFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".mat");
                            if (!matFiles.empty()) {
                                ImGui::Separator();
                                for (const auto& mf : matFiles) {
                                    bool isSel = (terrain->materialPath == mf.fullPath);
                                    if (ImGui::Selectable(mf.name.c_str(), isSel)) {
                                        terrain->SetMaterial(mf.fullPath);
                                    }
                                    if (isSel) ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }

                        // Drag & Drop para Material
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                const char* droppedPath = (const char*)payload->Data;
                                if (droppedPath) {
                                    std::string pStr(droppedPath);
                                    if (SlipFileSystem::GetFileExtension(pStr) == ".mat") {
                                        terrain->SetMaterial(pStr);
                                    }
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "Fisica y Colisiones Bullet:");
                        if (ImGui::Checkbox("Colision Habilitada (btBvhTriangleMeshShape)", &terrain->enableCollision)) {
                            needsRebuild = true;
                        }
                        ImGui::Checkbox("Wireframe", &terrain->wireframe);
                        ImGui::Checkbox("Proyectar Sombras de Terreno", &terrain->castShadows);

                        ImGui::Spacing();
                        if (ImGui::Button("Regenerar Terreno / V-Clust", ImVec2(-1, 32)) || needsRebuild) {
                            terrain->RebuildTerrain();
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::TextDisabled("Estadisticas V-Clust:");
                        ImGui::Text("Vertices: %u | Triangulos: %u", terrain->GetVertexCount(), terrain->GetTriangleCount());
                        ImGui::Text("Clusters V-Clust: %u", terrain->GetClusterCount());
                        ImGui::Text("Collider Bullet: %s", (terrain->GetRigidBody() ? "Activo (btBvhTriangleMeshShape)" : "Inactivo"));
                    }
                    if (!open) compToRemove = comp;
                }
                // --- SCRIPT COMPONENT (LUA) ---
                else if (auto script = std::dynamic_pointer_cast<ScriptComponent>(comp)) {
                    bool open = true;
                    std::string scriptTitle = script->GetScriptFileName();
                    if (script->scriptPath.empty()) scriptTitle = "Script (Ninguno)";
                    else scriptTitle += " (Script)";
                    std::string headerLabel = scriptTitle + "###ScriptHeader_" + std::to_string((uintptr_t)script.get());

                    if (ImGui::CollapsingHeader(headerLabel.c_str(), &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
                        if (assetsDir.empty() && SlipProject::Get().HasActiveProject()) {
                            assetsDir = SlipProject::Get().GetCurrentProject().assetsPath;
                        }
                        if (assetsDir.empty() && SlipProject::Get().HasActiveProject()) {
                            assetsDir = SlipFileSystem::CombinePath(SlipProject::Get().GetCurrentProject().rootPath, "assets");
                        }
                        if (assetsDir.empty()) {
                            assetsDir = "assets";
                        }

                        std::string currentScriptDisplay = "[Ninguno / Seleccionar Script]";
                        if (!script->scriptPath.empty()) {
                            currentScriptDisplay = SlipFileSystem::GetFileName(script->scriptPath);
                        }

                        ImGui::Text("Script Lua:");
                        std::string comboId = "##ScriptSelector_" + std::to_string((uintptr_t)script.get());
                        if (ImGui::BeginCombo(comboId.c_str(), currentScriptDisplay.c_str())) {
                            if (ImGui::Selectable("[Ninguno]", script->scriptPath.empty())) {
                                script->SetScriptPath("");
                            }

                            if (!assetsDir.empty()) {
                                auto scriptFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".lua");
                                if (!scriptFiles.empty()) {
                                    ImGui::Separator();
                                    ImGui::TextDisabled("Scripts del Proyecto:");
                                    for (const auto& sf : scriptFiles) {
                                        bool isSel = (script->scriptPath == sf.fullPath);
                                        if (ImGui::Selectable(sf.name.c_str(), isSel)) {
                                            script->SetScriptPath(sf.fullPath);
                                        }
                                        if (isSel) ImGui::SetItemDefaultFocus();
                                    }
                                }
                            }
                            ImGui::EndCombo();
                        }

                        // Drag & Drop para Script
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                const char* droppedPath = (const char*)payload->Data;
                                if (droppedPath) {
                                    std::string pStr(droppedPath);
                                    if (SlipFileSystem::GetFileExtension(pStr) == ".lua") {
                                        script->SetScriptPath(pStr);
                                    }
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        bool enabled = script->IsEnabled();
                        std::string chkId = "Habilitado##ScriptEn_" + std::to_string((uintptr_t)script.get());
                        if (ImGui::Checkbox(chkId.c_str(), &enabled)) {
                            script->SetEnabled(enabled);
                        }

                        ImGui::SameLine();
                        std::string reloadBtnId = " Recargar Script ##Reload_" + std::to_string((uintptr_t)script.get());
                        if (ImGui::Button(reloadBtnId.c_str())) {
                            script->ReloadScript();
                        }

                        std::string openEditBtnId = " Abrir en Editor de Codigo ##OpenEdit_" + std::to_string((uintptr_t)script.get());
                        if (ImGui::Button(openEditBtnId.c_str(), ImVec2(-1, 28))) {
                            if (SlipEditor::GetInstance() && !script->scriptPath.empty()) {
                                SlipEditor::GetInstance()->OpenScript(script->scriptPath);
                            }
                        }

                        if (!script->lastError.empty()) {
                            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", script->lastError.c_str());
                        } else if (script->isCompiled) {
                            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Estado: Compilado OK");
                        }

                        // --- PROPIEDADES PÃšBLICAS EXPUESTAS ---
                        if (script->isCompiled && !script->GetProperties().empty()) {
                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::TextDisabled("Propiedades Publicas:");
                            for (auto& prop : script->GetProperties()) {
                                ImGui::PushID(prop.name.c_str());
                                switch (prop.type) {
                                    case ScriptPropertyType::Float: {
                                        float fVal = prop.floatValue;
                                        if (ImGui::DragFloat(prop.name.c_str(), &fVal, 0.1f)) {
                                            script->SetPropertyValue(prop.name, fVal);
                                        }
                                        break;
                                    }
                                    case ScriptPropertyType::Int: {
                                        int iVal = prop.intValue;
                                        if (ImGui::DragInt(prop.name.c_str(), &iVal, 1)) {
                                            script->SetPropertyValue(prop.name, iVal);
                                        }
                                        break;
                                    }
                                    case ScriptPropertyType::Bool: {
                                        bool bVal = prop.boolValue;
                                        if (ImGui::Checkbox(prop.name.c_str(), &bVal)) {
                                            script->SetPropertyValue(prop.name, bVal);
                                        }
                                        break;
                                    }
                                    case ScriptPropertyType::String: {
                                        char strBuf[256];
                                        std::strncpy(strBuf, prop.stringValue.c_str(), sizeof(strBuf) - 1);
                                        strBuf[sizeof(strBuf) - 1] = '\0';
                                        if (ImGui::InputText(prop.name.c_str(), strBuf, sizeof(strBuf))) {
                                            script->SetPropertyValue(prop.name, std::string(strBuf));
                                        }
                                        break;
                                    }
                                    case ScriptPropertyType::Vec3: {
                                        glm::vec3 vVal = prop.vec3Value;
                                        if (ImGui::DragFloat3(prop.name.c_str(), &vVal.x, 0.1f)) {
                                            script->SetPropertyValue(prop.name, vVal);
                                        }
                                        break;
                                    }
                                }
                                ImGui::PopID();
                            }
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- CAMERA COMPONENT ---
                else if (auto cam = std::dynamic_pointer_cast<CameraComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Camera Component", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        const char* projOptions[] = { "Perspectiva", "Ortografica" };
                        int currentProj = (cam->projectionType == CameraProjection::Orthographic) ? 1 : 0;
                        if (ImGui::Combo("Proyeccion", &currentProj, projOptions, IM_ARRAYSIZE(projOptions))) {
                            cam->projectionType = (currentProj == 1) ? CameraProjection::Orthographic : CameraProjection::Perspective;
                        }

                        if (cam->projectionType == CameraProjection::Perspective) {
                            ImGui::SliderFloat("Campo de Vision (FOV)", &cam->fov, 10.0f, 130.0f, "%.1f deg");
                        } else {
                            ImGui::DragFloat("Tamano Ortografico (Size)", &cam->orthoSize, 0.1f, 0.1f, 500.0f);
                        }

                        ImGui::DragFloat("Plano Cercano (Near)", &cam->nearClip, 0.05f, 0.01f, 50.0f);
                        ImGui::DragFloat("Plano Lejano (Far)", &cam->farClip, 5.0f, 1.0f, 50000.0f);
                        if (cam->nearClip >= cam->farClip) cam->nearClip = cam->farClip - 0.1f;

                        ImGui::Checkbox("Camara Principal (Main Camera)", &cam->isPrimary);
                        ImGui::ColorEdit4("Color de Fondo (Clear Color)", &cam->clearColor.r);

                        ImGui::Spacing();
                        ImGui::Text("Efectos Cinematograficos:");
                        ImGui::SliderFloat("Letterbox (Bordes Horizontales)", &cam->letterbox, 0.0f, 0.45f, "%.3f");
                        ImGui::SliderFloat("Pillarbox (Bordes Verticales)", &cam->pillarbox, 0.0f, 0.45f, "%.3f");

                        ImGui::Spacing();
                        ImGui::Text("Camera Shake (Sacudida):");
                        if (ImGui::Button("Probar Shake Suave", ImVec2(140, 24))) {
                            cam->Shake(0.35f, 0.8f, 25.0f);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Probar Shake Fuerte", ImVec2(140, 24))) {
                            cam->Shake(0.85f, 1.2f, 30.0f);
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- CINEMATIC CAMERA COMPONENT (NFS / Forza Horizon) ---
                else if (auto ccam = std::dynamic_pointer_cast<CinematicCameraComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Cinematic Camera (Spring Arm NFS/Forza)", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        char targetBuf[128];
                        strncpy_s(targetBuf, ccam->targetEntityName.c_str(), sizeof(targetBuf));
                        if (ImGui::InputText("Entidad Objetivo (Target)", targetBuf, sizeof(targetBuf))) {
                            ccam->SetTargetName(targetBuf);
                        }
                        if (ImGui::Button("Reset / Centrar Inmediato")) {
                            ccam->ResetImmediate();
                        }

                        ImGui::Separator();
                        ImGui::TextDisabled("Spring Arm & Distancias:");
                        ImGui::DragFloat("Distancia Trasera", &ccam->targetDistance, 0.1f, 1.0f, 40.0f, "%.1f m");
                        ImGui::DragFloat("Altura Pivote", &ccam->targetHeight, 0.05f, 0.0f, 15.0f, "%.2f m");
                        ImGui::DragFloat("Inclinacion (Pitch)", &ccam->targetPitch, 0.2f, -30.0f, 60.0f, "%.1f deg");
                        ImGui::DragFloat("Offset Y Objetivo", &ccam->pivotYOffset, 0.05f, -5.0f, 5.0f, "%.2f m");

                        ImGui::Separator();
                        ImGui::TextDisabled("Amortiguacion & Inercia (Damped Spring):");
                        ImGui::DragFloat("Lag de Posicion", &ccam->positionLag, 0.1f, 0.5f, 30.0f, "%.1f");
                        ImGui::DragFloat("Lag de Rotacion (Yaw)", &ccam->rotationLag, 0.1f, 0.5f, 30.0f, "%.1f");
                        ImGui::DragFloat("Lag de Cabeceo (Pitch)", &ccam->pitchLag, 0.1f, 0.5f, 30.0f, "%.1f");

                        ImGui::Separator();
                        ImGui::TextDisabled("Dinámica de Derrape estilo NFS:");
                        ImGui::SliderFloat("Compensacion de Derrape", &ccam->driftCompensation, 0.0f, 1.0f, "%.2f");
                        ImGui::SliderFloat("Offset Maximo Derrape", &ccam->maxDriftYawOffset, 5.0f, 60.0f, "%.1f deg");

                        ImGui::Separator();
                        ImGui::TextDisabled("FOV Dinámico & Anticolisión:");
                        ImGui::Checkbox("Activar FOV Dinámico", &ccam->enableDynamicFOV);
                        if (ccam->enableDynamicFOV) {
                            ImGui::DragFloat("FOV Base", &ccam->baseFOV, 0.5f, 30.0f, 100.0f, "%.1f deg");
                            ImGui::DragFloat("FOV Maximo / Boost", &ccam->boostFOV, 0.5f, 40.0f, 120.0f, "%.1f deg");
                            ImGui::DragFloat("Velocidad para FOV Max (km/h)", &ccam->maxSpeedForFOV, 5.0f, 50.0f, 400.0f, "%.0f km/h");
                        }
                        ImGui::Checkbox("Anticolisión con Entorno (Raycast Probe)", &ccam->enableCollisionAvoidance);
                        ImGui::Checkbox("Permitir Orbita con Mouse", &ccam->enableMouseOrbit);
                    }
                    if (!open) compToRemove = comp;
                }
                // --- SPRITE RENDERER COMPONENT (2D) ---
                // --- SPRITE RENDERER COMPONENT (2D) ---
                else if (auto sr = std::dynamic_pointer_cast<SpriteRendererComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Sprite Renderer (2D)", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
                        if (assetsDir.empty()) assetsDir = "assets";

                        std::string currentTexDisplay = sr->texturePath.empty() ? "[Blanco / Sin Textura]" : SlipFileSystem::GetFileName(sr->texturePath);
                        ImGui::Text("Sprite (Textura):");
                        if (ImGui::BeginCombo("##SpriteTexCombo", currentTexDisplay.c_str())) {
                            if (ImGui::Selectable("[Blanco / Sin Textura]", sr->texturePath.empty())) {
                                sr->SetTexture("");
                            }
                            auto texFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".png,.jpg,.jpeg,.bmp,.tga");
                            for (const auto& tf : texFiles) {
                                bool isSel = (sr->texturePath == tf.fullPath);
                                if (ImGui::Selectable(tf.name.c_str(), isSel)) {
                                    sr->SetTexture(tf.fullPath);
                                }
                                if (isSel) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                std::string droppedPath = (const char*)payload->Data;
                                std::string ext = SlipFileSystem::GetFileExtension(droppedPath);
                                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
                                    sr->SetTexture(droppedPath);
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        ImGui::ColorEdit4("Color / Tint (RGBA)", &sr->color.r);
                        ImGui::Checkbox("Voltear X (Flip X)", &sr->flipX);
                        ImGui::SameLine();
                        ImGui::Checkbox("Voltear Y (Flip Y)", &sr->flipY);

                        // Sistema de Sorting Layers estilo Unity3D
                        const auto& layers = SlipSortingLayers::Get().GetLayers();
                        if (ImGui::BeginCombo("Capa de Dibujo (Sorting Layer)", sr->sortingLayerName.c_str())) {
                            for (const auto& layerName : layers) {
                                bool isSel = (sr->sortingLayerName == layerName);
                                if (ImGui::Selectable(layerName.c_str(), isSel)) {
                                    sr->sortingLayerName = layerName;
                                }
                                if (isSel) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        if (ImGui::DragInt("Orden en Capa (Order in Layer)", &sr->sortingOrder, 1.0f, -9999, 9999)) {
                            sr->orderInLayer = sr->sortingOrder;
                        }
                        ImGui::DragFloat("PÃ­xeles Por Unidad (PPU)", &sr->pixelsPerUnit, 1.0f, 1.0f, 10000.0f);
                        if (ImGui::Checkbox("NÃ­tido / Pixel Art (Nearest Filter)", &sr->isPixelArt)) {
                            if (sr->texture) sr->texture->SetFilterMode(sr->isPixelArt);
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mantiene los pÃ­xeles nÃ­tidos sin difuminado ni suavizado bilineal");

                        ImGui::Separator();
                        ImGui::Checkbox("IluminaciÃ³n 2D DinÃ¡mica (Lit 2D)", &sr->isLit);
                        if (sr->isLit) {
                            std::string normalDisplay = sr->normalMapPath.empty() ? "[Ninguno]" : SlipFileSystem::GetFileName(sr->normalMapPath);
                            ImGui::Text("Normal Map (Relieve 2D):");
                            if (ImGui::BeginCombo("##NormalMapCombo", normalDisplay.c_str())) {
                                if (ImGui::Selectable("[Ninguno]", sr->normalMapPath.empty())) {
                                    sr->SetNormalMap("");
                                }
                                auto texFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".png,.jpg,.jpeg,.bmp,.tga");
                                for (const auto& tf : texFiles) {
                                    bool isSel = (sr->normalMapPath == tf.fullPath);
                                    if (ImGui::Selectable(tf.name.c_str(), isSel)) {
                                        sr->SetNormalMap(tf.fullPath);
                                    }
                                    if (isSel) ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                        }

                        if (ImGui::TreeNodeEx("AnimaciÃ³n / Sprite Sheet (Frames)", ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::DragInt("Columnas (H Frames)", &sr->hFrames, 0.2f, 1, 64);
                            ImGui::DragInt("Filas (V Frames)", &sr->vFrames, 0.2f, 1, 64);
                            int totalFrames = (std::max)(1, sr->hFrames * sr->vFrames);
                            ImGui::SliderInt("Frame Actual", &sr->currentFrame, 0, totalFrames - 1);
                            ImGui::TreePop();
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- SPRITE ANIMATOR COMPONENT (2D) ---
                else if (auto sa = std::dynamic_pointer_cast<SpriteAnimatorComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Sprite Animator (2D)", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Reproducir al Iniciar (Play on Start)", &sa->playOnStart);

                        // Estado de reproducciÃ³n actual
                        std::string clipState = sa->GetCurrentClipName();
                        if (clipState.empty()) clipState = "[Ninguno]";
                        ImGui::Text("Clip Activo: %s", clipState.c_str());
                        ImGui::Text("Estado: %s", sa->isPlaying ? "Reproduciendo" : "Pausado / Detenido");
                        ImGui::Text("Frame Actual: %d", sa->currentFrame);

                        if (ImGui::Button("Play")) {
                            if (!sa->clips.empty()) {
                                if (sa->activeClipIndex >= 0) sa->Resume();
                                else sa->PlayClipByIndex(0);
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Pause")) sa->Pause();
                        ImGui::SameLine();
                        if (ImGui::Button("Stop")) sa->Stop();

                        ImGui::Separator();
                        ImGui::Text("Clips de AnimaciÃ³n (%d):", static_cast<int>(sa->clips.size()));
                        int clipToDelete = -1;
                        for (size_t i = 0; i < sa->clips.size(); ++i) {
                            ImGui::PushID(static_cast<int>(i));
                            auto& clip = sa->clips[i];
                            bool isCurrent = (sa->activeClipIndex == static_cast<int>(i));
                            std::string headerName = clip.name.empty() ? ("Clip " + std::to_string(i)) : clip.name;
                            if (isCurrent) headerName += " [Activo]";

                            if (ImGui::TreeNode(headerName.c_str())) {
                                char nameBuf[64];
                                std::strncpy(nameBuf, clip.name.c_str(), sizeof(nameBuf) - 1);
                                nameBuf[sizeof(nameBuf) - 1] = '\0';
                                if (ImGui::InputText("Nombre", nameBuf, sizeof(nameBuf))) {
                                    clip.name = nameBuf;
                                }

                                ImGui::DragInt("Frame Inicial", &clip.startFrame, 0.2f, 0, 999);
                                ImGui::DragInt("Frame Final", &clip.endFrame, 0.2f, 0, 999);
                                ImGui::DragFloat("FPS (Velocidad)", &clip.frameRate, 0.5f, 1.0f, 60.0f);
                                ImGui::Checkbox("Bucle (Loop)", &clip.loop);

                                if (ImGui::Button("Reproducir Este Clip")) {
                                    sa->PlayClipByIndex(static_cast<int>(i));
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Eliminar Clip")) {
                                    clipToDelete = static_cast<int>(i);
                                }

                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        if (clipToDelete >= 0) {
                            sa->RemoveClip(clipToDelete);
                        }

                        if (ImGui::Button("+ AÃ±adir Nuevo Clip")) {
                            sa->AddClip("Nuevo Clip", 0, 3, 12.0f, true);
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- BOX COLLIDER 2D ---
                else if (auto box2d = std::dynamic_pointer_cast<BoxCollider2DComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Box Collider 2D", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (ImGui::DragFloat2("TamaÃ±o (Size)", &box2d->size.x, 0.05f, 0.01f, 1000.0f)) {
                            if (auto rb = m_SelectedEntity->GetComponent<RigidBody2DComponent>()) rb->RebuildPhysicsBody();
                            else box2d->RecreateStaticBody();
                        }
                        if (ImGui::DragFloat2("Desplazamiento (Offset)", &box2d->offset.x, 0.05f, -1000.0f, 1000.0f)) {
                            if (auto rb = m_SelectedEntity->GetComponent<RigidBody2DComponent>()) rb->RebuildPhysicsBody();
                            else box2d->RecreateStaticBody();
                        }
                        if (ImGui::Checkbox("Es Trigger (Disparador)", &box2d->isTrigger)) {
                            if (auto rb = m_SelectedEntity->GetComponent<RigidBody2DComponent>()) rb->RebuildPhysicsBody();
                            else box2d->RecreateStaticBody();
                        }
                        ImGui::DragFloat("Densidad", &box2d->density, 0.05f, 0.01f, 100.0f);
                        if (ImGui::SliderFloat("FricciÃ³n", &box2d->friction, 0.0f, 1.0f)) {
                            if (auto rb = m_SelectedEntity->GetComponent<RigidBody2DComponent>()) rb->RebuildPhysicsBody();
                            else box2d->RecreateStaticBody();
                        }
                        if (ImGui::SliderFloat("RestituciÃ³n (Rebote)", &box2d->restitution, 0.0f, 1.0f)) {
                            if (auto rb = m_SelectedEntity->GetComponent<RigidBody2DComponent>()) rb->RebuildPhysicsBody();
                            else box2d->RecreateStaticBody();
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- CIRCLE COLLIDER 2D ---
                else if (auto circ2d = std::dynamic_pointer_cast<CircleCollider2DComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Circle Collider 2D", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (ImGui::DragFloat("Radio (Radius)", &circ2d->radius, 0.05f, 0.01f, 1000.0f)) {
                            if (auto rb = m_SelectedEntity->GetComponent<RigidBody2DComponent>()) rb->RebuildPhysicsBody();
                            else circ2d->RecreateStaticBody();
                        }
                        if (ImGui::DragFloat2("Desplazamiento (Offset)", &circ2d->offset.x, 0.05f, -1000.0f, 1000.0f)) {
                            if (auto rb = m_SelectedEntity->GetComponent<RigidBody2DComponent>()) rb->RebuildPhysicsBody();
                            else circ2d->RecreateStaticBody();
                        }
                        if (ImGui::Checkbox("Es Trigger (Disparador)", &circ2d->isTrigger)) {
                            if (auto rb = m_SelectedEntity->GetComponent<RigidBody2DComponent>()) rb->RebuildPhysicsBody();
                            else circ2d->RecreateStaticBody();
                        }
                        ImGui::DragFloat("Densidad", &circ2d->density, 0.05f, 0.01f, 100.0f);
                        if (ImGui::SliderFloat("FricciÃ³n", &circ2d->friction, 0.0f, 1.0f)) {
                            if (auto rb = m_SelectedEntity->GetComponent<RigidBody2DComponent>()) rb->RebuildPhysicsBody();
                            else circ2d->RecreateStaticBody();
                        }
                        if (ImGui::SliderFloat("RestituciÃ³n (Rebote)", &circ2d->restitution, 0.0f, 1.0f)) {
                            if (auto rb = m_SelectedEntity->GetComponent<RigidBody2DComponent>()) rb->RebuildPhysicsBody();
                            else circ2d->RecreateStaticBody();
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- RIGIDBODY 2D ---
                else if (auto rb2d = std::dynamic_pointer_cast<RigidBody2DComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("RigidBody 2D", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        const char* bodyTypes[] = { "Dynamic", "Static", "Kinematic" };
                        int currentType = static_cast<int>(rb2d->bodyType);
                        if (ImGui::Combo("Tipo de Cuerpo", &currentType, bodyTypes, IM_ARRAYSIZE(bodyTypes))) {
                            rb2d->bodyType = static_cast<BodyType2D>(currentType);
                            rb2d->RebuildPhysicsBody();
                        }

                        if (rb2d->bodyType == BodyType2D::Dynamic) {
                            if (ImGui::DragFloat("Masa (Mass)", &rb2d->mass, 0.1f, 0.01f, 10000.0f)) {
                                rb2d->RebuildPhysicsBody();
                            }
                            if (ImGui::DragFloat("Escala de Gravedad", &rb2d->gravityScale, 0.05f, -10.0f, 10.0f)) {
                                rb2d->SetGravityScale(rb2d->gravityScale);
                            }
                        }

                        if (ImGui::Checkbox("Bloquear RotaciÃ³n (Freeze Rotation)", &rb2d->freezeRotation)) {
                            rb2d->RebuildPhysicsBody();
                        }

                        ImGui::DragFloat("Amortiguamiento Lineal", &rb2d->linearDamping, 0.01f, 0.0f, 10.0f);
                        ImGui::DragFloat("Amortiguamiento Angular", &rb2d->angularDamping, 0.01f, 0.0f, 10.0f);

                        // Indicadores en tiempo real
                        glm::vec2 vel = rb2d->GetLinearVelocity();
                        ImGui::Text("Velocidad Lineal: (%.2f, %.2f)", vel.x, vel.y);
                        ImGui::Text("Velocidad Angular: %.2f rad/s", rb2d->GetAngularVelocity());
                    }
                    if (!open) compToRemove = comp;
                }
                // --- LIGHT 2D ---
                else if (auto l2d = std::dynamic_pointer_cast<Light2DComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Luz 2D (Light 2D)", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        const char* lightTypes[] = { "Point (Puntual)", "Spot (Focal)", "Directional" };
                        int currentType = static_cast<int>(l2d->lightType);
                        if (ImGui::Combo("Tipo de Luz", &currentType, lightTypes, IM_ARRAYSIZE(lightTypes))) {
                            l2d->lightType = static_cast<Light2DType>(currentType);
                        }

                        ImGui::ColorEdit3("Color", &l2d->color.r);
                        ImGui::DragFloat("Intensidad", &l2d->intensity, 0.05f, 0.0f, 50.0f);
                        ImGui::DragFloat("Radio Exterior", &l2d->radius, 0.1f, 0.1f, 500.0f);
                        ImGui::DragFloat("Radio Interior", &l2d->innerRadius, 0.1f, 0.0f, l2d->radius);
                        if (l2d->lightType == Light2DType::Spot) {
                            ImGui::SliderFloat("Ãngulo de Cono (Spot)", &l2d->spotAngle, 5.0f, 180.0f);
                        }

                        ImGui::Separator();
                        ImGui::Checkbox("Proyectar Sombras 2D (Cast Shadows)", &l2d->castShadows);
                        if (l2d->castShadows) {
                            ImGui::SliderFloat("Intensidad de Sombra", &l2d->shadowIntensity, 0.0f, 1.0f);
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- SHADOW CASTER 2D ---
                else if (auto sc2d = std::dynamic_pointer_cast<ShadowCaster2DComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Sombra 2D (Shadow Caster 2D)", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        const char* shapes[] = { "Box (Caja)", "Circle (CÃ­rculo)" };
                        int currentShape = static_cast<int>(sc2d->shape);
                        if (ImGui::Combo("Forma de Sombra", &currentShape, shapes, IM_ARRAYSIZE(shapes))) {
                            sc2d->shape = static_cast<ShadowCasterShape2D>(currentShape);
                        }

                        if (sc2d->shape == ShadowCasterShape2D::Box) {
                            ImGui::DragFloat2("Dimensiones (Size)", &sc2d->size.x, 0.05f, 0.01f, 1000.0f);
                        } else {
                            ImGui::DragFloat("Radio", &sc2d->radius, 0.05f, 0.01f, 1000.0f);
                        }
                        ImGui::DragFloat2("Desplazamiento (Offset)", &sc2d->offset.x, 0.05f, -1000.0f, 1000.0f);
                        ImGui::Checkbox("Activo", &sc2d->castShadows);
                    }
                    if (!open) compToRemove = comp;
                }
                // --- TILEGRID COMPONENT ---
                else if (auto tg = std::dynamic_pointer_cast<TileGridComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("TileGrid (Tilemap 2D con Anclaje)", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
                        if (assetsDir.empty()) assetsDir = "assets";

                        std::string currentSheetDisplay = tg->tileSheetPath.empty() ? "[Ninguno]" : SlipFileSystem::GetFileName(tg->tileSheetPath);
                        ImGui::Text("Tilesheet (Atlas):");
                        if (ImGui::BeginCombo("##TileSheetCombo", currentSheetDisplay.c_str())) {
                            if (ImGui::Selectable("[Ninguno]", tg->tileSheetPath.empty())) {
                                tg->SetTileSheet("");
                            }
                            auto texFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".png,.jpg,.jpeg,.bmp,.tga");
                            for (const auto& tf : texFiles) {
                                bool isSel = (tg->tileSheetPath == tf.fullPath);
                                if (ImGui::Selectable(tf.name.c_str(), isSel)) {
                                    tg->SetTileSheet(tf.fullPath);
                                }
                                if (isSel) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                std::string droppedPath = (const char*)payload->Data;
                                std::string ext = SlipFileSystem::GetFileExtension(droppedPath);
                                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
                                    tg->SetTileSheet(droppedPath);
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        ImGui::DragFloat2("TamaÃ±o de Celda (Cell Size)", &tg->cellSize.x, 0.05f, 0.1f, 100.0f);
                        const char* anchors[] = { "Center", "Bottom-Left", "Top-Left", "Bottom-Center" };
                        int currentAnchor = static_cast<int>(tg->anchor);
                        if (ImGui::Combo("Anclaje (Anchor)", &currentAnchor, anchors, IM_ARRAYSIZE(anchors))) {
                            tg->anchor = static_cast<TileGridAnchor>(currentAnchor);
                        }

                        // Capa de dibujo
                        const auto& layers = SlipSortingLayers::Get().GetLayers();
                        if (ImGui::BeginCombo("Capa de Dibujo (Sorting Layer)##TG", tg->sortingLayerName.c_str())) {
                            for (const auto& layerName : layers) {
                                bool isSel = (tg->sortingLayerName == layerName);
                                if (ImGui::Selectable(layerName.c_str(), isSel)) {
                                    tg->sortingLayerName = layerName;
                                }
                                if (isSel) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::DragInt("Orden en Capa##TG", &tg->orderInLayer, 1.0f, -9999, 9999);
                        if (ImGui::Checkbox("NÃ­tido / Pixel Art (Nearest)##TG", &tg->isPixelArt)) {
                            if (tg->tileSheet) tg->tileSheet->SetFilterMode(tg->isPixelArt);
                        }

                        ImGui::DragInt("Columnas del Atlas (H)", &tg->sheetHFrames, 0.2f, 1, 64);
                        ImGui::DragInt("Filas del Atlas (V)", &tg->sheetVFrames, 0.2f, 1, 64);

                        ImGui::Separator();
                        ImGui::Text("Total de Tiles Colocados: %d", static_cast<int>(tg->tiles.size()));
                        if (ImGui::Button("Limpiar Todo el Tilemap")) {
                            tg->Clear();
                        }

                        // Herramienta rÃ¡pida de prueba para colocar/borrar tiles desde inspector
                        static int testTileX = 0, testTileY = 0, testTileIdx = 0;
                        int maxTiles = tg->sheetHFrames * tg->sheetVFrames;
                        ImGui::DragInt2("Coords Celda (X, Y)", &testTileX);
                        ImGui::SliderInt("Ãndice de Tile", &testTileIdx, 0, (std::max)(0, maxTiles - 1));
                        if (ImGui::Button("Colocar Tile")) {
                            tg->SetTile(testTileX, testTileY, testTileIdx);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Borrar Tile")) {
                            tg->RemoveTile(testTileX, testTileY);
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- CANVAS COMPONENT (UI) ---
                else if (auto cv = std::dynamic_pointer_cast<CanvasComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Canvas (UI)", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        const char* renderModes[] = { "Screen Space - Overlay", "World Space" };
                        int currentMode = static_cast<int>(cv->renderMode);
                        if (ImGui::Combo("Modo de Renderizado", &currentMode, renderModes, IM_ARRAYSIZE(renderModes))) {
                            cv->renderMode = static_cast<CanvasRenderMode>(currentMode);
                        }

                        ImGui::DragFloat2("ResoluciÃ³n de Referencia", &cv->referenceResolution.x, 1.0f, 100.0f, 7680.0f);
                        ImGui::Checkbox("Escalar con Pantalla", &cv->scaleWithScreenSize);
                        ImGui::DragInt("Orden de Dibujo (Sorting Order)", &cv->sortingOrder, 1.0f, -9999, 9999);
                    }
                    if (!open) compToRemove = comp;
                }
                // --- RECT TRANSFORM COMPONENT (UI) ---
                else if (auto rt = std::dynamic_pointer_cast<RectTransformComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Rect Transform (UI)", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        const char* presets[] = {
                            "Centro (Center)",
                            "Arriba Izquierda (Top-Left)",
                            "Arriba Centro (Top-Center)",
                            "Arriba Derecha (Top-Right)",
                            "Medio Izquierda (Middle-Left)",
                            "Medio Derecha (Middle-Right)",
                            "Abajo Izquierda (Bottom-Left)",
                            "Abajo Centro (Bottom-Center)",
                            "Abajo Derecha (Bottom-Right)",
                            "Estirar Todo (Stretch All)"
                        };
                        int currentPreset = static_cast<int>(rt->anchorPreset);
                        if (ImGui::Combo("Preset de Anclajes (Anchors)", &currentPreset, presets, IM_ARRAYSIZE(presets))) {
                            rt->ApplyAnchorPreset(static_cast<UIAnchorPreset>(currentPreset));
                        }

                        ImGui::DragFloat2("PosiciÃ³n Anclada (Pos X, Pos Y)", &rt->anchoredPosition.x, 1.0f);
                        ImGui::DragFloat2("Dimensiones (Ancho, Alto)", &rt->size.x, 1.0f, 1.0f, 10000.0f);
                        ImGui::SliderFloat2("Pivote (Pivot)", &rt->pivot.x, 0.0f, 1.0f);

                        if (ImGui::TreeNodeEx("Anclajes Manuales (Min / Max)", ImGuiTreeNodeFlags_None)) {
                            ImGui::DragFloat2("Ancla MÃ­nima (Min X, Min Y)", &rt->anchorMin.x, 0.01f, 0.0f, 1.0f);
                            ImGui::DragFloat2("Ancla MÃ¡xima (Max X, Max Y)", &rt->anchorMax.x, 0.01f, 0.0f, 1.0f);
                            ImGui::TreePop();
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- UI IMAGE COMPONENT ---
                else if (auto uiImg = std::dynamic_pointer_cast<UIImageComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("UI Image", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
                        if (assetsDir.empty()) assetsDir = "assets";

                        std::string currentTexDisplay = uiImg->texturePath.empty() ? "[Color SÃ³lido / Sin Textura]" : SlipFileSystem::GetFileName(uiImg->texturePath);
                        ImGui::Text("Imagen (Sprite):");
                        if (ImGui::BeginCombo("##UIImgTexCombo", currentTexDisplay.c_str())) {
                            if (ImGui::Selectable("[Color SÃ³lido / Sin Textura]", uiImg->texturePath.empty())) {
                                uiImg->SetTexture("");
                            }
                            auto texFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".png,.jpg,.jpeg,.bmp,.tga");
                            for (const auto& tf : texFiles) {
                                bool isSel = (uiImg->texturePath == tf.fullPath);
                                if (ImGui::Selectable(tf.name.c_str(), isSel)) {
                                    uiImg->SetTexture(tf.fullPath);
                                }
                                if (isSel) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                std::string droppedPath = (const char*)payload->Data;
                                std::string ext = SlipFileSystem::GetFileExtension(droppedPath);
                                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
                                    uiImg->SetTexture(droppedPath);
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        ImGui::ColorEdit4("Color de Imagen (RGBA)", &uiImg->color.r);
                        ImGui::Checkbox("Preservar Proporciones (Aspect)", &uiImg->preserveAspect);
                    }
                    if (!open) compToRemove = comp;
                }
                // --- UI TEXT COMPONENT ---
                else if (auto uiTxt = std::dynamic_pointer_cast<UITextComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("UI Text", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        char textBuf[1024];
                        std::strncpy(textBuf, uiTxt->text.c_str(), sizeof(textBuf) - 1);
                        textBuf[sizeof(textBuf) - 1] = '\0';
                        if (ImGui::InputTextMultiline("Texto (Mensaje)", textBuf, sizeof(textBuf), ImVec2(-1, 60))) {
                            uiTxt->text = textBuf;
                        }

                        ImGui::DragFloat("TamaÃ±o de Fuente (Font Size)", &uiTxt->fontSize, 1.0f, 6.0f, 256.0f);
                        ImGui::ColorEdit4("Color del Texto (RGBA)", &uiTxt->color.r);

                        const char* hAligns[] = { "Izquierda (Left)", "Centro (Center)", "Derecha (Right)" };
                        int currentH = static_cast<int>(uiTxt->alignH);
                        if (ImGui::Combo("AlineaciÃ³n Horizontal", &currentH, hAligns, IM_ARRAYSIZE(hAligns))) {
                            uiTxt->alignH = static_cast<UITextAlignH>(currentH);
                        }

                        const char* vAligns[] = { "Arriba (Top)", "Medio (Middle)", "Abajo (Bottom)" };
                        int currentV = static_cast<int>(uiTxt->alignV);
                        if (ImGui::Combo("AlineaciÃ³n Vertical", &currentV, vAligns, IM_ARRAYSIZE(vAligns))) {
                            uiTxt->alignV = static_cast<UITextAlignV>(currentV);
                        }

                        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
                        if (assetsDir.empty()) assetsDir = "assets";

                        std::string currentFontDisplay = uiTxt->fontPath.empty() ? "[Fuente del Sistema (Default)]" : SlipFileSystem::GetFileName(uiTxt->fontPath);
                        ImGui::Text("Fuente TrueType (.ttf):");
                        if (ImGui::BeginCombo("##UITextFontCombo", currentFontDisplay.c_str())) {
                            if (ImGui::Selectable("[Fuente del Sistema (Default)]", uiTxt->fontPath.empty())) {
                                uiTxt->fontPath = "";
                            }
                            auto fontFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".ttf,.otf");
                            for (const auto& ff : fontFiles) {
                                bool isSel = (uiTxt->fontPath == ff.fullPath);
                                if (ImGui::Selectable(ff.name.c_str(), isSel)) {
                                    uiTxt->fontPath = ff.fullPath;
                                }
                                if (isSel) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                std::string droppedPath = (const char*)payload->Data;
                                std::string ext = SlipFileSystem::GetFileExtension(droppedPath);
                                if (ext == ".ttf" || ext == ".otf") {
                                    uiTxt->fontPath = droppedPath;
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- UI BUTTON COMPONENT ---
                else if (auto uiBtn = std::dynamic_pointer_cast<UIButtonComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("UI Button", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Interactuable", &uiBtn->interactable);

                        if (ImGui::TreeNodeEx("Colores por Estado", ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::ColorEdit4("Normal", &uiBtn->normalColor.r);
                            ImGui::ColorEdit4("Hover (Encima)", &uiBtn->hoverColor.r);
                            ImGui::ColorEdit4("Presionado (Pressed)", &uiBtn->pressedColor.r);
                            ImGui::ColorEdit4("Deshabilitado", &uiBtn->disabledColor.r);
                            ImGui::TreePop();
                        }

                        if (ImGui::TreeNodeEx("Evento OnClick (Inspector)", ImGuiTreeNodeFlags_DefaultOpen)) {
                            char entBuf[128];
                            std::strncpy(entBuf, uiBtn->targetEntityName.c_str(), sizeof(entBuf) - 1);
                            entBuf[sizeof(entBuf) - 1] = '\0';
                            if (ImGui::InputText("Entidad Objetivo (VacÃ­o = este)", entBuf, sizeof(entBuf))) {
                                uiBtn->targetEntityName = entBuf;
                            }

                            char fnBuf[128];
                            std::strncpy(fnBuf, uiBtn->onClickFunction.c_str(), sizeof(fnBuf) - 1);
                            fnBuf[sizeof(fnBuf) - 1] = '\0';
                            if (ImGui::InputText("FunciÃ³n Lua Objetivo", fnBuf, sizeof(fnBuf))) {
                                uiBtn->onClickFunction = fnBuf;
                            }
                            ImGui::TreePop();
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- UI SLIDER COMPONENT ---
                else if (auto uiSld = std::dynamic_pointer_cast<UISliderComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("UI Slider", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Interactuable", &uiSld->interactable);
                        ImGui::Checkbox("Solo NÃºmeros Enteros (Whole Numbers)", &uiSld->wholeNumbers);

                        ImGui::DragFloat("Valor MÃ­nimo", &uiSld->minValue, 0.1f);
                        ImGui::DragFloat("Valor MÃ¡ximo", &uiSld->maxValue, 0.1f);
                        if (ImGui::SliderFloat("Valor Actual", &uiSld->value, uiSld->minValue, uiSld->maxValue)) {
                            uiSld->SetValue(uiSld->value, true);
                        }

                        if (ImGui::TreeNodeEx("Colores del Slider", ImGuiTreeNodeFlags_None)) {
                            ImGui::ColorEdit4("Fondo (Track)", &uiSld->backgroundColor.r);
                            ImGui::ColorEdit4("Relleno (Fill)", &uiSld->fillColor.r);
                            ImGui::ColorEdit4("Manija (Handle)", &uiSld->handleColor.r);
                            ImGui::TreePop();
                        }

                        if (ImGui::TreeNodeEx("Evento OnValueChanged (Inspector)", ImGuiTreeNodeFlags_None)) {
                            char entBuf[128];
                            std::strncpy(entBuf, uiSld->targetEntityName.c_str(), sizeof(entBuf) - 1);
                            entBuf[sizeof(entBuf) - 1] = '\0';
                            if (ImGui::InputText("Entidad Objetivo (VacÃ­o = este)", entBuf, sizeof(entBuf))) {
                                uiSld->targetEntityName = entBuf;
                            }

                            char fnBuf[128];
                            std::strncpy(fnBuf, uiSld->onValueChangedFunction.c_str(), sizeof(fnBuf) - 1);
                            fnBuf[sizeof(fnBuf) - 1] = '\0';
                            if (ImGui::InputText("FunciÃ³n Lua Objetivo", fnBuf, sizeof(fnBuf))) {
                                uiSld->onValueChangedFunction = fnBuf;
                            }
                            ImGui::TreePop();
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                // --- UI PROGRESS BAR COMPONENT ---
                else if (auto uiPb = std::dynamic_pointer_cast<UIProgressBarComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("UI Progress Bar", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::DragFloat("Valor MÃ­nimo", &uiPb->minValue, 1.0f);
                        ImGui::DragFloat("Valor MÃ¡ximo", &uiPb->maxValue, 1.0f);
                        if (ImGui::SliderFloat("Progreso", &uiPb->value, uiPb->minValue, uiPb->maxValue)) {
                            uiPb->SetValue(uiPb->value);
                        }

                        ImGui::Checkbox("Mostrar Texto de Porcentaje", &uiPb->showPercentageText);

                        if (ImGui::TreeNodeEx("Colores de Barra", ImGuiTreeNodeFlags_None)) {
                            ImGui::ColorEdit4("Color de Fondo", &uiPb->backgroundColor.r);
                            ImGui::ColorEdit4("Color de Relleno", &uiPb->fillColor.r);
                            ImGui::TreePop();
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                else if (auto joy = std::dynamic_pointer_cast<UIJoystickComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("UI Joystick", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextDisabled("Solo responde al jugar en Android. En ediciÃ³n se muestra para colocarlo.");
                        const auto& sticks = SlipInputMap::Get().GetTouchSticks();
                        const char* preview = joy->stickName.empty() ? "(Sin stick)" : joy->stickName.c_str();
                        if (ImGui::BeginCombo("Stick", preview)) {
                            for (const InputTouchStick& stick : sticks) {
                                if (ImGui::Selectable(stick.name.c_str(), stick.name == joy->stickName)) {
                                    joy->stickName = stick.name;
                                }
                            }
                            if (sticks.empty()) ImGui::TextDisabled("Define un stick en Ajustes de Entrada.");
                            ImGui::EndCombo();
                        }
                        const char* modes[] = { "Fijo", "Flotante", "Invisible" };
                        int mode = static_cast<int>(joy->mode);
                        if (ImGui::Combo("Modo", &mode, modes, IM_ARRAYSIZE(modes))) {
                            joy->mode = static_cast<UIJoystickMode>(mode);
                        }
                        ImGui::DragFloat("Radio de recorrido", &joy->travelRadius, 1.0f, 8.0f, 800.0f);
                        ImGui::DragFloat("Radio de activaciÃ³n", &joy->activationRadius, 1.0f, 8.0f, 1200.0f);
                        if (joy->activationRadius < joy->travelRadius) joy->activationRadius = joy->travelRadius;
                        ImGui::Checkbox("Interactuable", &joy->interactable);
                        ImGui::ColorEdit4("Color de base", &joy->backgroundColor.r);
                        ImGui::ColorEdit4("Color de perilla", &joy->knobColor.r);
                    }
                    if (!open) compToRemove = comp;
                }
                else if (auto actionBtn = std::dynamic_pointer_cast<UIActionButtonComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("UI BotÃ³n de acciÃ³n", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextDisabled("Solo se dibuja y pulsa la acciÃ³n al jugar en Android.");
                        const auto& actions = SlipInputMap::Get().GetActions();
                        const char* preview = actionBtn->actionName.empty() ? "(Sin acciÃ³n)" : actionBtn->actionName.c_str();
                        if (ImGui::BeginCombo("AcciÃ³n", preview)) {
                            for (const InputAction& action : actions) {
                                if (ImGui::Selectable(action.name.c_str(), action.name == actionBtn->actionName)) {
                                    actionBtn->actionName = action.name;
                                }
                            }
                            if (actions.empty()) ImGui::TextDisabled("Define una acciÃ³n en Ajustes de Entrada.");
                            ImGui::EndCombo();
                        }
                        ImGui::Checkbox("Interactuable", &actionBtn->interactable);
                        ImGui::ColorEdit4("Normal", &actionBtn->normalColor.r);
                        ImGui::ColorEdit4("Presionado", &actionBtn->pressedColor.r);
                    }
                    if (!open) compToRemove = comp;
                }
                // --- PARTICLE SYSTEM COMPONENT (UNITY BILLBOARDS / UNREAL NIAGARA GL_POINTS) ---
                else if (auto ps = std::dynamic_pointer_cast<ParticleSystemComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Particle System (Unity / Niagara)", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        // Controles de SimulaciÃ³n en Tiempo Real
                        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "PartÃ­culas Activas: %d / %d (Tiempo: %.1fs)",
                                           ps->GetActiveParticleCount(), ps->maxParticles, ps->GetSystemTime());

                        if (ImGui::Button(" Play ")) ps->Play();
                        ImGui::SameLine();
                        if (ImGui::Button(" Pause ")) ps->Pause();
                        ImGui::SameLine();
                        if (ImGui::Button(" Stop ")) ps->Stop();
                        ImGui::SameLine();
                        if (ImGui::Button(" Restart ")) ps->Restart();
                        ImGui::SameLine();
                        if (ImGui::Button(" Burst +50 ")) ps->Emit(50);

                        ImGui::Spacing();
                        ImGui::TextDisabled("Presets RÃ¡pidos:");
                        if (ImGui::Button("Chispas Niagara")) ps->ApplyPreset("NiagaraSparks");
                        ImGui::SameLine();
                        if (ImGui::Button("Fuego")) ps->ApplyPreset("Fire");
                        ImGui::SameLine();
                        if (ImGui::Button("Humo (Lit)")) ps->ApplyPreset("Smoke");
                        if (ImGui::Button("Fuente con Rebote")) ps->ApplyPreset("BouncingFountain");
                        ImGui::SameLine();
                        if (ImGui::Button("ExplosiÃ³n")) ps->ApplyPreset("Explosion");

                        ImGui::Separator();

                        const char* renderModes[] = {
                            "Billboard Sprite (Estilo Unity)",
                            "Heavy GL_POINTS (Estilo Unreal Niagara)"
                        };
                        int currentRenderMode = static_cast<int>(ps->renderMode);
                        if (ImGui::Combo("Modo de Sistema", &currentRenderMode, renderModes, IM_ARRAYSIZE(renderModes))) {
                            ps->renderMode = static_cast<ParticleRenderMode>(currentRenderMode);
                        }

                        ImGui::DragInt("MÃ¡x. PartÃ­culas", &ps->maxParticles, 10.0f, 1, 100000);
                        float prevDuration = ps->duration;
                        if (ImGui::DragFloat("DuraciÃ³n / Ciclo de Vida (s)", &ps->duration, 0.1f, 0.05f, 3600.0f)) {
                            ps->duration = (std::max)(0.05f, ps->duration);
                            float ratio = (prevDuration > 0.001f) ? (ps->duration / prevDuration) : 1.0f;
                            ps->lifetimeMin = (std::max)(0.05f, ps->lifetimeMin * ratio);
                            ps->lifetimeMax = (std::max)(ps->lifetimeMin, ps->lifetimeMax * ratio);
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Define la duraciÃ³n del ciclo y escala automÃ¡ticamente la vida de cada partÃ­cula.");
                        }
                        ImGui::Checkbox("Bucle (Looping)", &ps->looping);
                        ImGui::SameLine();
                        ImGui::Checkbox("Play On Awake", &ps->playOnAwake);
                        ImGui::Checkbox("Espacio Local (Sigue al Emisor)", &ps->simulationSpaceLocal);
                        ImGui::SliderFloat("Velocidad de SimulaciÃ³n", &ps->simulationSpeed, 0.0f, 4.0f);

                        // 1. EMISIÃ“N
                        if (ImGui::TreeNodeEx("EmisiÃ³n y RÃ¡fagas (Bursts)", ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::Checkbox("Habilitar EmisiÃ³n", &ps->emissionEnabled);
                            ImGui::DragFloat("Tasa de EmisiÃ³n (part/seg)", &ps->emissionRate, 1.0f, 0.0f, 20000.0f);

                            ImGui::TextDisabled("RÃ¡fagas (Bursts):");
                            int burstToRemove = -1;
                            for (size_t bIdx = 0; bIdx < ps->bursts.size(); ++bIdx) {
                                ImGui::PushID(static_cast<int>(bIdx));
                                ImGui::SetNextItemWidth(90.0f);
                                ImGui::DragFloat("Tiempo (s)", &ps->bursts[bIdx].time, 0.05f, 0.0f, ps->duration);
                                ImGui::SameLine();
                                ImGui::SetNextItemWidth(90.0f);
                                ImGui::DragInt("Cantidad", &ps->bursts[bIdx].count, 1.0f, 1, 10000);
                                ImGui::SameLine();
                                if (ImGui::SmallButton("X")) {
                                    burstToRemove = static_cast<int>(bIdx);
                                }
                                ImGui::PopID();
                            }
                            if (burstToRemove >= 0) {
                                ps->bursts.erase(ps->bursts.begin() + burstToRemove);
                            }
                            if (ImGui::SmallButton("+ AÃ±adir RÃ¡faga (Burst)")) {
                                ps->bursts.push_back({ 0.0f, 50, false });
                            }
                            ImGui::TreePop();
                        }

                        // 2. FORMA Y FUERZA INICIAL
                        if (ImGui::TreeNodeEx("Forma del Emisor y Fuerza Inicial (Lanzamiento)", ImGuiTreeNodeFlags_DefaultOpen)) {
                            const char* shapes[] = { "Punto (Point)", "Esfera (Sphere)", "Cono / Chorro (Cone)", "Caja (Box)", "CÃ­rculo (Circle)" };
                            int currentShape = static_cast<int>(ps->emitterShape);
                            if (ImGui::Combo("Forma del Emisor", &currentShape, shapes, IM_ARRAYSIZE(shapes))) {
                                ps->emitterShape = static_cast<ParticleEmitterShape>(currentShape);
                            }

                            if (ps->emitterShape == ParticleEmitterShape::Sphere ||
                                ps->emitterShape == ParticleEmitterShape::Cone ||
                                ps->emitterShape == ParticleEmitterShape::Circle) {
                                ImGui::DragFloat("Radio del Emisor", &ps->shapeRadius, 0.05f, 0.01f, 500.0f);
                                ImGui::Checkbox("Emitir desde Borde (Shell)", &ps->emitFromShell);
                            }
                            if (ps->emitterShape == ParticleEmitterShape::Cone) {
                                ImGui::SliderFloat("Ãngulo del Cono (Â°)", &ps->coneAngle, 0.0f, 89.0f);
                            }
                            if (ps->emitterShape == ParticleEmitterShape::Box) {
                                ImGui::DragFloat3("TamaÃ±o de Caja", &ps->boxSize.x, 0.05f, 0.01f, 500.0f);
                            }

                            ImGui::Separator();
                            ImGui::TextDisabled("Fuerza / Velocidad de Lanzamiento:");
                            ImGui::DragFloat("Velocidad Inicial MÃ­n.", &ps->initialSpeedMin, 0.1f, 0.0f, 500.0f);
                            ImGui::DragFloat("Velocidad Inicial MÃ¡x.", &ps->initialSpeedMax, 0.1f, 0.0f, 500.0f);
                            ImGui::DragFloat3("Impulso / Fuerza Inicial (XYZ)", &ps->initialVelocityOffset.x, 0.1f, -500.0f, 500.0f);
                            ImGui::TreePop();
                        }

                        // 3. CICLO DE VIDA Y TAMAÃ‘O
                        if (ImGui::TreeNodeEx("Ciclo de Vida, TamaÃ±o y RotaciÃ³n", ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::DragFloat("Vida MÃ­nima (s)", &ps->lifetimeMin, 0.05f, 0.05f, 120.0f);
                            ImGui::DragFloat("Vida MÃ¡xima (s)", &ps->lifetimeMax, 0.05f, 0.05f, 120.0f);
                            ImGui::DragFloat("TamaÃ±o Inicial MÃ­n.", &ps->startSizeMin, 0.01f, 0.005f, 50.0f);
                            ImGui::DragFloat("TamaÃ±o Inicial MÃ¡x.", &ps->startSizeMax, 0.01f, 0.005f, 50.0f);
                            ImGui::DragFloat("Multiplicador TamaÃ±o Final", &ps->endSizeMultiplier, 0.02f, 0.0f, 20.0f);

                            if (ps->renderMode == ParticleRenderMode::Billboard) {
                                ImGui::DragFloat("RotaciÃ³n Inicial MÃ­n. (Â°)", &ps->startRotationMin, 1.0f, -360.0f, 360.0f);
                                ImGui::DragFloat("RotaciÃ³n Inicial MÃ¡x. (Â°)", &ps->startRotationMax, 1.0f, -360.0f, 360.0f);
                                ImGui::DragFloat("Vel. Angular MÃ­n. (Â°/s)", &ps->angularVelocityMin, 1.0f, -1080.0f, 1080.0f);
                                ImGui::DragFloat("Vel. Angular MÃ¡x. (Â°/s)", &ps->angularVelocityMax, 1.0f, -1080.0f, 1080.0f);
                            }
                            ImGui::TreePop();
                        }

                        // 4. COLOR Y GRADIENTE SOBRE EL CICLO DE VIDA
                        if (ImGui::TreeNodeEx("Color y Gradiente sobre Ciclo de Vida", ImGuiTreeNodeFlags_DefaultOpen)) {
                            const char* colorModes[] = {
                                "Color Fijo (Fixed)",
                                "Aleatorio entre 2 Colores",
                                "Gradiente sobre Ciclo de Vida"
                            };
                            int cMode = static_cast<int>(ps->colorMode);
                            if (ImGui::Combo("Modo de Color", &cMode, colorModes, IM_ARRAYSIZE(colorModes))) {
                                ps->colorMode = static_cast<ParticleColorMode>(cMode);
                            }

                            if (ps->colorMode == ParticleColorMode::Fixed) {
                                ImGui::ColorEdit4("Color de PartÃ­cula (RGBA)", &ps->startColor.r);
                            } else if (ps->colorMode == ParticleColorMode::RandomBetweenTwo) {
                                ImGui::ColorEdit4("Color A (RGBA)", &ps->startColor.r);
                                ImGui::ColorEdit4("Color B (RGBA)", &ps->startColor2.r);
                            } else {
                                // Barra visual de previsualizaciÃ³n del gradiente
                                ImVec2 barPos = ImGui::GetCursorScreenPos();
                                float barWidth = (std::max)(120.0f, ImGui::GetContentRegionAvail().x);
                                float barHeight = 22.0f;
                                ImDrawList* dl = ImGui::GetWindowDrawList();
                                const int steps = 32;
                                for (int s = 0; s < steps; ++s) {
                                    float t0 = static_cast<float>(s) / steps;
                                    float t1 = static_cast<float>(s + 1) / steps;
                                    glm::vec4 c0 = ps->EvaluateGradient(t0);
                                    glm::vec4 c1 = ps->EvaluateGradient(t1);
                                    ImU32 u0 = ImGui::ColorConvertFloat4ToU32(ImVec4(c0.r, c0.g, c0.b, c0.a));
                                    ImU32 u1 = ImGui::ColorConvertFloat4ToU32(ImVec4(c1.r, c1.g, c1.b, c1.a));
                                    float x0 = barPos.x + t0 * barWidth;
                                    float x1 = barPos.x + t1 * barWidth;
                                    dl->AddRectFilledMultiColor(ImVec2(x0, barPos.y), ImVec2(x1, barPos.y + barHeight), u0, u1, u1, u0);
                                }
                                dl->AddRect(barPos, ImVec2(barPos.x + barWidth, barPos.y + barHeight), IM_COL32(220, 220, 220, 180));
                                ImGui::Dummy(ImVec2(barWidth, barHeight + 4.0f));

                                int stopToRemove = -1;
                                for (size_t sIdx = 0; sIdx < ps->gradientStops.size(); ++sIdx) {
                                    ImGui::PushID(static_cast<int>(sIdx));
                                    ImGui::SetNextItemWidth(75.0f);
                                    if (ImGui::SliderFloat("##StopT", &ps->gradientStops[sIdx].t, 0.0f, 1.0f, "t=%.2f")) {
                                        std::stable_sort(ps->gradientStops.begin(), ps->gradientStops.end(),
                                                         [](const ParticleGradientStop& a, const ParticleGradientStop& b) { return a.t < b.t; });
                                    }
                                    ImGui::SameLine();
                                    ImGui::ColorEdit4("##StopCol", &ps->gradientStops[sIdx].color.r, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                                    ImGui::SameLine();
                                    ImGui::Text("Parada %d", static_cast<int>(sIdx + 1));
                                    if (ps->gradientStops.size() > 2) {
                                        ImGui::SameLine();
                                        if (ImGui::SmallButton("X")) {
                                            stopToRemove = static_cast<int>(sIdx);
                                        }
                                    }
                                    ImGui::PopID();
                                }
                                if (stopToRemove >= 0) {
                                    ps->gradientStops.erase(ps->gradientStops.begin() + stopToRemove);
                                }
                                if (ps->gradientStops.size() < 8) {
                                    if (ImGui::SmallButton("+ AÃ±adir Parada de Color")) {
                                        ps->gradientStops.push_back({ 1.0f, glm::vec4(1.0f) });
                                    }
                                }
                            }
                            ImGui::TreePop();
                        }

                        // 5. FÃSICA, TURBULENCIA Y COLISIONES (ESTILO RIGIDBODY)
                        if (ImGui::TreeNodeEx("SimulaciÃ³n FÃ­sica y Colisiones (RigidBody / Rebote)", ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::Checkbox("Habilitar SimulaciÃ³n FÃ­sica", &ps->physicsEnabled);
                            if (ps->physicsEnabled) {
                                ImGui::Checkbox("Usar Gravedad", &ps->useGravity);
                                if (ps->useGravity) {
                                    ImGui::DragFloat("Multiplicador de Gravedad", &ps->gravityMultiplier, 0.05f, -10.0f, 10.0f);
                                }
                                ImGui::DragFloat("Masa por PartÃ­cula (kg)", &ps->mass, 0.01f, 0.001f, 100.0f);
                                ImGui::DragFloat("Resistencia al Aire (Drag)", &ps->linearDrag, 0.01f, 0.0f, 20.0f);
                                ImGui::DragFloat3("Fuerza Constante / Viento", &ps->constantForce.x, 0.1f, -200.0f, 200.0f);

                                ImGui::Separator();
                                ImGui::Checkbox("Turbulencia y VÃ³rtice (Estilo Niagara)", &ps->turbulenceEnabled);
                                if (ps->turbulenceEnabled) {
                                    ImGui::DragFloat("Fuerza de Turbulencia", &ps->turbulenceStrength, 0.1f, 0.0f, 50.0f);
                                    ImGui::DragFloat("Frecuencia de Turbulencia", &ps->turbulenceFrequency, 0.05f, 0.05f, 20.0f);
                                }
                                ImGui::DragFloat("Fuerza de VÃ³rtice (Giro)", &ps->vortexStrength, 0.1f, -50.0f, 50.0f);

                                ImGui::Separator();
                                ImGui::Checkbox("Colisiones con el Mundo (Bullet Physics)", &ps->collisionEnabled);
                                if (ps->collisionEnabled) {
                                    ImGui::DragFloat("Radio de ColisiÃ³n", &ps->particleRadius, 0.005f, 0.005f, 2.0f);
                                    ImGui::SliderFloat("Rebote / RestituciÃ³n (Bounciness)", &ps->bounciness, 0.0f, 1.0f);
                                    ImGui::SliderFloat("FricciÃ³n al Colisionar", &ps->collisionFriction, 0.0f, 1.0f);
                                    ImGui::SliderFloat("PÃ©rdida de Vida por Impacto", &ps->lifetimeLossOnCollision, 0.0f, 1.0f);
                                    ImGui::Checkbox("Empujar RigidBodies DinÃ¡micos al Chocar", &ps->pushRigidBodies);
                                }
                            }
                            ImGui::TreePop();
                        }

                        // 6. MATERIAL, ILUMINACIÃ“N Y SOMBRAS
                        if (ImGui::TreeNodeEx("Material, IluminaciÃ³n y Sombras", ImGuiTreeNodeFlags_DefaultOpen)) {
                            const char* matModes[] = {
                                "Unlit (Color Plano / Sin Luz)",
                                "Lit (Normal PBR - Percibe Luces y Sombras)",
                                "Emisivo (HDR Glow / Bloom)"
                            };
                            int mMode = static_cast<int>(ps->materialMode);
                            if (ImGui::Combo("Tipo de Material", &mMode, matModes, IM_ARRAYSIZE(matModes))) {
                                ps->materialMode = static_cast<ParticleMaterialMode>(mMode);
                            }

                            const char* blendModes[] = {
                                "Alpha Blend (Transparencia Normal)",
                                "Additive (Suma de Luz / Fuego / Chispas)"
                            };
                            int bMode = static_cast<int>(ps->blendMode);
                            if (ImGui::Combo("Modo de Mezcla (Blend)", &bMode, blendModes, IM_ARRAYSIZE(blendModes))) {
                                ps->blendMode = static_cast<ParticleBlendMode>(bMode);
                            }

                            if (ps->renderMode == ParticleRenderMode::PointNiagara) {
                                const char* ptShapes[] = {
                                    "CÃ­rculo Suave (Soft Circle)",
                                    "Esfera 3D con Normal SintÃ©tica (Lit Sphere)",
                                    "Punto Cuadrado (Pixel Square)"
                                };
                                int pShape = static_cast<int>(ps->pointShape);
                                if (ImGui::Combo("Forma de GL_POINT", &pShape, ptShapes, IM_ARRAYSIZE(ptShapes))) {
                                    ps->pointShape = static_cast<ParticlePointShape>(pShape);
                                }
                                ImGui::DragFloat("Escala Base de Punto", &ps->pointBaseScale, 0.05f, 0.1f, 20.0f);
                            } else {
                                std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
                                if (assetsDir.empty()) assetsDir = "assets";

                                std::string currentTexDisplay = ps->texturePath.empty() ? "[CÃ­rculo Procedural Suave (Sin Textura)]" : SlipFileSystem::GetFileName(ps->texturePath);
                                ImGui::Text("Sprite / Textura (Con Alfa):");
                                if (ImGui::BeginCombo("##ParticleTexCombo", currentTexDisplay.c_str())) {
                                    if (ImGui::Selectable("[CÃ­rculo Procedural Suave (Sin Textura)]", ps->texturePath.empty())) {
                                        ps->SetTexture("");
                                    }
                                    auto texFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".png,.jpg,.jpeg,.bmp,.tga");
                                    for (const auto& tf : texFiles) {
                                        bool isSel = (ps->texturePath == tf.fullPath);
                                        if (ImGui::Selectable(tf.name.c_str(), isSel)) {
                                            ps->SetTexture(tf.fullPath);
                                        }
                                        if (isSel) ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }
                                if (ImGui::BeginDragDropTarget()) {
                                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                        std::string droppedPath = (const char*)payload->Data;
                                        std::string ext = SlipFileSystem::GetFileExtension(droppedPath);
                                        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
                                            ps->SetTexture(droppedPath);
                                        }
                                    }
                                    ImGui::EndDragDropTarget();
                                }

                                ImGui::DragInt("Columnas Sprite Sheet (H)", &ps->hFrames, 0.2f, 1, 32);
                                ImGui::DragInt("Filas Sprite Sheet (V)", &ps->vFrames, 0.2f, 1, 32);
                                if (ps->hFrames > 1 || ps->vFrames > 1) {
                                    ImGui::Checkbox("Animar Frames segÃºn Ciclo de Vida", &ps->animateSpriteSheet);
                                }
                            }

                            if (ps->materialMode == ParticleMaterialMode::Emissive || ps->materialMode == ParticleMaterialMode::Lit) {
                                ImGui::Separator();
                                ImGui::ColorEdit3("Color Emisivo", &ps->emissiveColor.r);
                                ImGui::DragFloat("Intensidad Emisiva (HDR)", &ps->emissiveIntensity, 0.1f, 0.0f, 50.0f);
                            }

                            if (ps->materialMode == ParticleMaterialMode::Lit) {
                                ImGui::Separator();
                                ImGui::SliderFloat("Rugosidad (Roughness)", &ps->roughness, 0.0f, 1.0f);
                                ImGui::SliderFloat("MetÃ¡lico (Metallic)", &ps->metallic, 0.0f, 1.0f);
                                ImGui::Checkbox("Percibir Luces de la Escena", &ps->receiveLights);
                                ImGui::Checkbox("Recibir Sombras (CSM)", &ps->receiveShadows);
                            }

                            ImGui::Separator();
                            ImGui::Checkbox("Causar / Proyectar Sombras (Cast Shadows)", &ps->castShadows);
                            if (ps->castShadows) {
                                ImGui::SliderFloat("Corte de Alfa en Sombra", &ps->shadowAlphaCutoff, 0.01f, 0.95f);
                            }

                            ImGui::Separator();
                            ImGui::Checkbox("Emitir Luz Real hacia la Escena (Light Emission)", &ps->lightEmissionEnabled);
                            if (ps->lightEmissionEnabled) {
                                ImGui::Checkbox("Usar Color de PartÃ­cula", &ps->useParticleColorForLight);
                                if (!ps->useParticleColorForLight) {
                                    ImGui::ColorEdit3("Color de Luz Emitida", &ps->lightColor.r);
                                }
                                ImGui::DragFloat("Intensidad de Luz", &ps->lightIntensity, 0.1f, 0.0f, 50.0f);
                                ImGui::DragFloat("Radio de Luz (Range)", &ps->lightRange, 0.2f, 0.5f, 50.0f);
                                ImGui::SliderInt("MÃ¡x. Luces DinÃ¡micas", &ps->maxLights, 1, 4);
                            }

                            ImGui::TreePop();
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                else if (auto sm = std::dynamic_pointer_cast<SkinnedMeshComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Skinned Mesh Component", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
                        if (assetsDir.empty()) assetsDir = "assets";

                        // 1. Selector de Malla con huesos
                        std::string currentModelDisplay = sm->modelPath.empty() ? "[Ninguno / Arrastra un modelo .fbx/.gltf]" : SlipFileSystem::GetFileName(sm->modelPath);
                        ImGui::Text("Malla con Huesos (FBX / GLTF):");
                        if (ImGui::BeginCombo("##SkinnedMeshSelector", currentModelDisplay.c_str())) {
                            if (ImGui::Selectable("[Ninguno]", sm->modelPath.empty())) {
                                sm->SetModel("");
                            }
                            auto modelFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".fbx,.gltf,.glb,.dae");
                            for (const auto& mf : modelFiles) {
                                bool isSel = (sm->modelPath == mf.fullPath);
                                if (ImGui::Selectable(mf.name.c_str(), isSel)) {
                                    sm->SetModel(mf.fullPath);
                                }
                                if (isSel) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        // Drag & drop para el modelo
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                std::string droppedPath = (const char*)payload->Data;
                                std::string ext = SlipFileSystem::GetFileExtension(droppedPath);
                                if (ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae") {
                                    sm->SetModel(droppedPath);
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        // 2. InformaciÃ³n del Esqueleto
                        int boneCount = sm->skeleton ? (int)sm->skeleton->GetBoneCount() : 0;
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Huesos en Esqueleto: %d", boneCount);
                        if (boneCount > 0) {
                            if (ImGui::Button("Regenerar Jerarquia de Huesos en Escena", ImVec2(-1, 26))) {
                                sm->BuildBoneHierarchy(m_ContextLevel);
                            }
                        }

                        // 3. Materiales del Modelo (Slots)
                        ImGui::Separator();
                        size_t slotCount = sm->GetMaterialSlotCount();
                        auto matFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".mat");

                        if (slotCount <= 1) {
                            std::string currentMatDisplay = sm->materialPath.empty() ? "[Vacio / Default]" : SlipFileSystem::GetFileName(sm->materialPath);
                            std::string slotLabel = (slotCount == 1) ? ("Material (" + sm->GetMaterialSlotName(0) + "):") : "Material:";
                            ImGui::Text("%s", slotLabel.c_str());

                            if (ImGui::BeginCombo("##SkinnedMatCombo", currentMatDisplay.c_str())) {
                                if (ImGui::Selectable("[Vacio / Default]", sm->materialPath.empty())) {
                                    sm->SetMaterial("");
                                }
                                for (const auto& mf : matFiles) {
                                    bool isSel = (sm->materialPath == mf.fullPath);
                                    if (ImGui::Selectable(mf.name.c_str(), isSel)) {
                                        sm->SetMaterial(mf.fullPath);
                                    }
                                    if (isSel) ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                    std::string droppedPath = (const char*)payload->Data;
                                    if (SlipFileSystem::GetFileExtension(droppedPath) == ".mat") {
                                        sm->SetMaterial(droppedPath);
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }
                        } else {
                            ImGui::Text("Slots de Material (%d):", (int)slotCount);
                            for (size_t s = 0; s < slotCount; ++s) {
                                ImGui::PushID(static_cast<int>(s));
                                std::string slotName = sm->GetMaterialSlotName(s);
                                std::string curMat = sm->GetSubmeshMaterialPath(s);
                                std::string curDisplay = curMat.empty() ? "[Vacio / Default]" : SlipFileSystem::GetFileName(curMat);

                                ImGui::Text("  Slot %d (%s):", (int)s, slotName.c_str());
                                if (ImGui::BeginCombo("##SlotCombo", curDisplay.c_str())) {
                                    if (ImGui::Selectable("[Vacio / Default]", curMat.empty())) {
                                        sm->SetSubmeshMaterial(s, "");
                                    }
                                    for (const auto& mf : matFiles) {
                                        bool isSel = (curMat == mf.fullPath);
                                        if (ImGui::Selectable(mf.name.c_str(), isSel)) {
                                            sm->SetSubmeshMaterial(s, mf.fullPath);
                                        }
                                        if (isSel) ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }
                                if (ImGui::BeginDragDropTarget()) {
                                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                        std::string droppedPath = (const char*)payload->Data;
                                        if (SlipFileSystem::GetFileExtension(droppedPath) == ".mat") {
                                            sm->SetSubmeshMaterial(s, droppedPath);
                                        }
                                    }
                                    ImGui::EndDragDropTarget();
                                }
                                ImGui::PopID();
                            }
                        }

                        ImGui::Separator();
                        ImGui::Checkbox("Proyectar Sombras (Cast Shadows)", &sm->castShadows);

                        if (m_SelectedEntity && !m_SelectedEntity->GetComponent<AnimatorComponent>()) {
                            ImGui::Spacing();
                            if (ImGui::Button("[+] Anadir Animator Component", ImVec2(-1, 28))) {
                                m_SelectedEntity->AddComponent<AnimatorComponent>();
                            }
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                else if (auto anim = std::dynamic_pointer_cast<AnimatorComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Animator Component", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Clips de Animacion Cargados: %d", (int)anim->clips.size());

                        // Animator Controller (Locomotion & State Machine)
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "Animator Controller (Locomotion):");
                        char ctrlBuf[256] = { 0 };
                        std::strncpy(ctrlBuf, anim->controllerPath.c_str(), sizeof(ctrlBuf) - 1);
                        if (ImGui::InputText("Controller", ctrlBuf, sizeof(ctrlBuf))) {
                            anim->SetController(ctrlBuf);
                        }
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                std::string droppedPath = (const char*)payload->Data;
                                if (SlipFileSystem::GetFileExtension(droppedPath) == ".controller") {
                                    anim->SetController(droppedPath);
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        if (ImGui::Button("[Grafo] Abrir en Editor de Nodos", ImVec2(-1, 24))) {
                            if (SlipEditor::GetInstance()) {
                                std::string cPath = anim->controllerPath;
                                if (cPath.empty()) {
                                    cPath = "assets/Animations/" + m_SelectedEntity->GetName() + ".controller";
                                    anim->SetController(cPath);
                                }
                                SlipEditor::GetInstance()->OpenAnimatorController(cPath);
                            }
                        }

                        if (anim->controller) {
                            auto& ctrl = *anim->controller;
                            std::string stateName = (ctrl.currentStateIndex >= 0 && ctrl.currentStateIndex < (int)ctrl.states.size())
                                ? ctrl.states[ctrl.currentStateIndex].name : "(Ninguno)";
                            ImGui::Text("Estado Activo: %s", stateName.c_str());
                            if (ctrl.isTransitioning) {
                                std::string nextName = (ctrl.targetStateIndex >= 0 && ctrl.targetStateIndex < (int)ctrl.states.size())
                                    ? ctrl.states[ctrl.targetStateIndex].name : "?";
                                ImGui::ProgressBar(ctrl.transitionProgress, ImVec2(-1, 14), ("-> " + nextName).c_str());
                            }

                            // ParÃ¡metros en tiempo real
                            if (!ctrl.parameters.empty()) {
                                ImGui::Spacing();
                                ImGui::TextDisabled("Parametros (Prueba en vivo / Locomocion):");
                                for (auto& pair : ctrl.parameters) {
                                    auto& p = pair.second;
                                    ImGui::PushID(p.name.c_str());
                                    if (p.type == AnimatorParamType::Float) {
                                        ImGui::SliderFloat(p.name.c_str(), &p.floatValue, -5.0f, 10.0f, "%.2f");
                                    } else if (p.type == AnimatorParamType::Int) {
                                        ImGui::DragInt(p.name.c_str(), &p.intValue);
                                    } else if (p.type == AnimatorParamType::Bool) {
                                        ImGui::Checkbox(p.name.c_str(), &p.boolValue);
                                    } else if (p.type == AnimatorParamType::Trigger) {
                                        if (ImGui::Button(("Disparar " + p.name).c_str())) {
                                            p.boolValue = true;
                                        }
                                    }
                                    ImGui::PopID();
                                }
                            }
                        } else {
                            if (ImGui::Button("Crear Nuevo Controller", ImVec2(-1, 24))) {
                                std::string animDir = SlipFileSystem::CombinePath(SlipFileSystem::GetProjectAssetsPath(), "Animations");
                                SlipFileSystem::CreateDirectoryPath(animDir);
                                std::string newPath = SlipFileSystem::CombinePath(animDir, m_SelectedEntity->GetName() + "_Controller.controller");
                                SlipAnimatorController newCtrl;
                                newCtrl.name = m_SelectedEntity->GetName() + "_Controller";
                                newCtrl.AddParameter("Speed", AnimatorParamType::Float);
                                newCtrl.AddParameter("IsGrounded", AnimatorParamType::Bool);
                                newCtrl.AddParameter("Jump", AnimatorParamType::Trigger);
                                std::string firstClipName = anim->clips.empty() ? "" : anim->clips[0]->name;
                                newCtrl.AddState("Idle", firstClipName);
                                newCtrl.SaveToFile(newPath);
                                anim->SetController(newPath);
                            }
                        }
                        ImGui::Separator();

                        if (anim->clips.empty()) {
                            ImGui::TextDisabled("No hay clips cargados en este componente.");
                            ImGui::TextDisabled("Al asignar un modelo con animaciones en SkinnedMeshComponent,\nse vincularan automaticamente.");
                        } else {
                            // Selector de clip activo
                            std::string activeClipLabel = "(Ninguno)";
                            AnimationClip* curClip = anim->GetCurrentClip();
                            if (curClip) {
                                char buf[128];
                                snprintf(buf, sizeof(buf), "%s (%.2fs)", curClip->name.c_str(), curClip->duration);
                                activeClipLabel = buf;
                            }

                            if (ImGui::BeginCombo("Clip Activo", activeClipLabel.c_str())) {
                                for (int i = 0; i < (int)anim->clips.size(); ++i) {
                                    if (!anim->clips[i]) continue;
                                    bool isSelected = (i == anim->currentClipIndex);
                                    char buf[128];
                                    snprintf(buf, sizeof(buf), "%s (%.2fs)", anim->clips[i]->name.c_str(), anim->clips[i]->duration);
                                    ImGui::PushID(i);
                                    if (ImGui::Selectable(buf, isSelected)) {
                                        anim->currentClipIndex = i;
                                        anim->currentTime = 0.0f;
                                    }
                                    if (isSelected) ImGui::SetItemDefaultFocus();
                                    ImGui::PopID();
                                }
                                ImGui::EndCombo();
                            }

                            ImGui::SliderFloat("Velocidad", &anim->playbackSpeed, -3.0f, 3.0f, "%.2fx");
                            ImGui::Checkbox("Repetir en Bucle (Looping)", &anim->isLooping);

                            float dur = anim->GetCurrentDuration();
                            if (dur > 0.0f) {
                                if (ImGui::SliderFloat("Tiempo", &anim->currentTime, 0.0f, dur, "%.2fs")) {
                                    anim->SetTime(anim->currentTime);
                                }
                            }

                            ImGui::Spacing();
                            if (anim->isPlaying) {
                                if (ImGui::Button(" [||] Pausar ", ImVec2(100, 26))) anim->Pause();
                            } else {
                                if (ImGui::Button(" [>] Reproducir ", ImVec2(100, 26))) anim->Resume();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(" [X] Detener ", ImVec2(90, 26))) anim->Stop();

                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::TextDisabled("Clips disponibles:");
                            for (size_t c = 0; c < anim->clips.size(); ++c) {
                                if (!anim->clips[c]) continue;
                                ImGui::BulletText("\"%s\" (%.2fs - %d pistas de huesos)", 
                                    anim->clips[c]->name.c_str(), anim->clips[c]->duration, (int)anim->clips[c]->tracks.size());
                            }

                            ImGui::Spacing();
                            if (ImGui::Button("[Anim] Abrir en Panel de Animaciones", ImVec2(-1, 28))) {
                                if (SlipEditor::GetInstance()) {
                                    if (auto aep = SlipEditor::GetInstance()->GetPanel<AnimationEditorPanel>()) {
                                        aep->SetOpen(true);
                                        aep->SetTargetEntity(m_SelectedEntity);
                                    }
                                }
                            }
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                else if (auto kfa = std::dynamic_pointer_cast<KeyframeAnimationComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Keyframe Animation (Cinematica)", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Clip: %s", kfa->clip ? kfa->clip->name.c_str() : "(Ninguno)");
                        char pathBuf[256] = { 0 };
                        std::strncpy(pathBuf, kfa->clipPath.c_str(), sizeof(pathBuf) - 1);
                        if (ImGui::InputText("Ruta Clip (.clip)", pathBuf, sizeof(pathBuf))) {
                            kfa->clipPath = pathBuf;
                            kfa->LoadClip(pathBuf);
                        }
                        ImGui::Checkbox("Reproducir al Iniciar (Play On Start)", &kfa->playOnStart);
                        ImGui::Checkbox("Repetir en Bucle (Loop)", &kfa->loop);
                        ImGui::SliderFloat("Velocidad", &kfa->playbackSpeed, 0.1f, 3.0f, "%.2fx");

                        if (kfa->clip && kfa->clip->duration > 0.0f) {
                            float dur = kfa->clip->duration;
                            if (ImGui::SliderFloat("Tiempo", &kfa->currentTime, 0.0f, dur, "%.2f s")) {
                                kfa->SetTime(kfa->currentTime);
                            }
                            if (ImGui::Button(kfa->isPlaying ? "Pausar" : "Reproducir", ImVec2(100, 24))) {
                                if (kfa->isPlaying) kfa->Pause(); else kfa->Play();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Detener", ImVec2(90, 24))) {
                                kfa->Stop();
                            }
                        }

                        ImGui::Spacing();
                        if (ImGui::Button("[Anim] Abrir en Editor de Animacion", ImVec2(-1, 28))) {
                            if (SlipEditor::GetInstance()) {
                                if (auto aep = SlipEditor::GetInstance()->GetPanel<AnimationEditorPanel>()) {
                                    aep->SetOpen(true);
                                    aep->SetTargetEntity(m_SelectedEntity);
                                }
                            }
                        }
                    }
                    if (!open) compToRemove = comp;
                }
                else if (auto bone = std::dynamic_pointer_cast<BoneComponent>(comp)) {
                    bool open = true;
                    if (ImGui::CollapsingHeader("Bone Component", &open, ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Articulacion / Hueso de Esqueleto");
                        ImGui::Text("Nombre: %s", bone->boneName.c_str());
                        ImGui::Text("Indice en Jerarquia: %d", bone->boneIndex);
                        if (m_SelectedEntity && m_SelectedEntity->GetParent()) {
                            ImGui::Spacing();
                            if (ImGui::Button("Seleccionar Hueso Padre")) {
                                m_SelectedEntity = m_SelectedEntity->GetParent();
                            }
                        }
                    }
                    if (!open) compToRemove = comp;
                }
            }

            if (compToRemove) {
                auto& comps = m_SelectedEntity->GetComponents();
                comps.erase(std::remove(comps.begin(), comps.end(), compToRemove), comps.end());
            }

            // Propiedades de Primitivo si aplica
            SlipPrimitiveEntity* prim = dynamic_cast<SlipPrimitiveEntity*>(m_SelectedEntity);
            if (prim) {
                if (ImGui::CollapsingHeader("Propiedades del Primitivo", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const char* typeName = "Desconocido";
                    switch (prim->primitiveType) {
                        case PrimitiveType::Cube:   typeName = "Cubo (3D Box)"; break;
                        case PrimitiveType::Sphere: typeName = "Esfera (UV Sphere)"; break;
                        case PrimitiveType::Plane:  typeName = "Plano (Plane)"; break;
                    }
                    ImGui::Text("Tipo: %s", typeName);
                    ImGui::ColorEdit4("Color", &prim->color.r);
#if !defined(SLIP_PLATFORM_ANDROID)
                    ImGui::Checkbox("Modo Wireframe", &prim->wireframe);
#endif
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // =====================================================================
            // BOTÃ“N PARA AÃ‘ADIR COMPONENTES (Estilo Unity)
            // =====================================================================
            if (ImGui::Button(" [+] AÃ±adir Componente ", ImVec2(-1, 32))) {
                ImGui::OpenPopup("AddComponentMenu");
            }

            if (ImGui::BeginPopup("AddComponentMenu")) {
                if (!m_SelectedEntity->HasComponent<MeshComponent>()) {
                    if (ImGui::MenuItem("Mesh Component")) {
                        m_SelectedEntity->AddComponent<MeshComponent>();
                    }
                }
                if (!m_SelectedEntity->HasComponent<SkinnedMeshComponent>()) {
                    if (ImGui::MenuItem("Skinned Mesh Component")) {
                        m_SelectedEntity->AddComponent<SkinnedMeshComponent>();
                    }
                }
                if (!m_SelectedEntity->HasComponent<AnimatorComponent>()) {
                    if (ImGui::MenuItem("Animator Component")) {
                        m_SelectedEntity->AddComponent<AnimatorComponent>();
                    }
                }
                if (!m_SelectedEntity->HasComponent<KeyframeAnimationComponent>()) {
                    if (ImGui::MenuItem("Keyframe Animation (Cinematica / Secuenciador)")) {
                        m_SelectedEntity->AddComponent<KeyframeAnimationComponent>();
                    }
                }
                ImGui::Separator();
                if (!m_SelectedEntity->HasComponent<BoxColliderComponent>()) {
                    if (ImGui::MenuItem("Box Collider")) {
                        m_SelectedEntity->AddComponent<BoxColliderComponent>();
                    }
                }
                if (!m_SelectedEntity->HasComponent<SphereColliderComponent>()) {
                    if (ImGui::MenuItem("Sphere Collider")) {
                        m_SelectedEntity->AddComponent<SphereColliderComponent>();
                    }
                }
                if (!m_SelectedEntity->HasComponent<CapsuleColliderComponent>()) {
                    if (ImGui::MenuItem("Capsule Collider")) {
                        m_SelectedEntity->AddComponent<CapsuleColliderComponent>();
                    }
                }
                if (!m_SelectedEntity->HasComponent<MeshColliderComponent>()) {
                    if (ImGui::MenuItem("Mesh Collider")) {
                        m_SelectedEntity->AddComponent<MeshColliderComponent>();
                    }
                }
                ImGui::Separator();
                if (!m_SelectedEntity->HasComponent<RigidBodyComponent>()) {
                    if (ImGui::MenuItem("RigidBody (Fisicas Dinamicas)")) {
                        auto rb = m_SelectedEntity->AddComponent<RigidBodyComponent>();
                        rb->ApplyToCollider();
                    }
                }
                if (!m_SelectedEntity->HasComponent<VehicleComponent>()) {
                    if (ImGui::MenuItem("Vehicle Component (Bullet Raycast)")) {
                        if (!m_SelectedEntity->HasComponent<RigidBodyComponent>()) {
                            auto rb = m_SelectedEntity->AddComponent<RigidBodyComponent>();
                            rb->mass = 1200.0f;
                            rb->ApplyToCollider();
                        }
                        if (!m_SelectedEntity->HasComponent<ColliderComponent>()) {
                            m_SelectedEntity->AddComponent<BoxColliderComponent>(glm::vec3(1.8f, 0.8f, 3.8f));
                        }
                        m_SelectedEntity->AddComponent<VehicleComponent>();
                    }
                }
                ImGui::Separator();
                if (!m_SelectedEntity->HasComponent<LightComponent>()) {
                    if (ImGui::MenuItem("Light Component")) {
                        m_SelectedEntity->AddComponent<LightComponent>();
                    }
                }
                if (!m_SelectedEntity->HasComponent<DDGIVolumeComponent>()) {
                    if (ImGui::MenuItem("Volumen DDGI")) {
                        m_SelectedEntity->AddComponent<DDGIVolumeComponent>();
                    }
                }
                if (!m_SelectedEntity->HasComponent<ReflectionProbeComponent>()) {
                    if (ImGui::MenuItem("Sonda de Reflexión (Volumen)")) {
                        m_SelectedEntity->AddComponent<ReflectionProbeComponent>();
                    }
                }
                if (!m_SelectedEntity->HasComponent<PostProcessVolumeComponent>()) {
                    if (ImGui::MenuItem("Volumen de post-proceso")) {
                        m_SelectedEntity->AddComponent<PostProcessVolumeComponent>();
                    }
                }
                if (!m_SelectedEntity->HasComponent<SlipSkyComponent>()) {
                    if (ImGui::MenuItem("Cielo y Nubes VolumÃ©tricas (SlipSkyComponent)")) {
                        m_SelectedEntity->AddComponent<SlipSkyComponent>();
                    }
                }
                if (!m_SelectedEntity->HasComponent<SlipTerrainComponent>()) {
                    if (ImGui::MenuItem("Terreno V-Clust (SlipTerrainComponent)")) {
                        auto tc = m_SelectedEntity->AddComponent<SlipTerrainComponent>();
                        tc->RebuildTerrain();
                    }
                }
                ImGui::Separator();
                if (!m_SelectedEntity->HasComponent<CameraComponent>()) {
                    if (ImGui::MenuItem("Camera Component")) {
                        m_SelectedEntity->AddComponent<CameraComponent>();
                    }
                }
                if (!m_SelectedEntity->HasComponent<CinematicCameraComponent>()) {
                    if (ImGui::MenuItem("Cinematic Camera (Spring Arm NFS)")) {
                        m_SelectedEntity->AddComponent<CinematicCameraComponent>();
                    }
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("2D")) {
                    if (!m_SelectedEntity->HasComponent<SpriteRendererComponent>()) {
                        if (ImGui::MenuItem("Sprite Renderer (2D)")) {
                            m_SelectedEntity->AddComponent<SpriteRendererComponent>();
                        }
                    }
                    if (!m_SelectedEntity->HasComponent<SpriteAnimatorComponent>()) {
                        if (ImGui::MenuItem("Sprite Animator (2D)")) {
                            m_SelectedEntity->AddComponent<SpriteAnimatorComponent>();
                        }
                    }
                    if (!m_SelectedEntity->HasComponent<TileGridComponent>()) {
                        if (ImGui::MenuItem("TileGrid 2D (Tilemap con Anclaje)")) {
                            m_SelectedEntity->AddComponent<TileGridComponent>();
                        }
                    }
                    ImGui::Separator();
                    if (!m_SelectedEntity->HasComponent<BoxCollider2DComponent>()) {
                        if (ImGui::MenuItem("Box Collider 2D")) {
                            m_SelectedEntity->AddComponent<BoxCollider2DComponent>();
                            if (auto rb = m_SelectedEntity->GetComponent<RigidBody2DComponent>()) rb->RebuildPhysicsBody();
                        }
                    }
                    if (!m_SelectedEntity->HasComponent<CircleCollider2DComponent>()) {
                        if (ImGui::MenuItem("Circle Collider 2D")) {
                            m_SelectedEntity->AddComponent<CircleCollider2DComponent>();
                            if (auto rb = m_SelectedEntity->GetComponent<RigidBody2DComponent>()) rb->RebuildPhysicsBody();
                        }
                    }
                    if (!m_SelectedEntity->HasComponent<RigidBody2DComponent>()) {
                        if (ImGui::MenuItem("RigidBody 2D")) {
                            auto rb2d = m_SelectedEntity->AddComponent<RigidBody2DComponent>();
                            rb2d->RebuildPhysicsBody();
                        }
                    }
                    ImGui::Separator();
                    if (!m_SelectedEntity->HasComponent<Light2DComponent>()) {
                        if (ImGui::MenuItem("Luz 2D (Point / Spot Light)")) {
                            m_SelectedEntity->AddComponent<Light2DComponent>();
                        }
                    }
                    if (!m_SelectedEntity->HasComponent<ShadowCaster2DComponent>()) {
                        if (ImGui::MenuItem("Sombra 2D (Shadow Caster)")) {
                            m_SelectedEntity->AddComponent<ShadowCaster2DComponent>();
                        }
                    }
                    ImGui::EndMenu();
                }
                if (!m_SelectedEntity->HasComponent<ParticleSystemComponent>()) {
                    if (ImGui::MenuItem("Particle System (Billboard - Estilo Unity)")) {
                        m_SelectedEntity->AddComponent<ParticleSystemComponent>(ParticleRenderMode::Billboard);
                    }
                    if (ImGui::MenuItem("Particle System (Niagara GL_POINTS - Estilo Unreal)")) {
                        m_SelectedEntity->AddComponent<ParticleSystemComponent>(ParticleRenderMode::PointNiagara);
                    }
                }
                if (ImGui::BeginMenu("UI (Interfaz Estilo Unity)")) {
                    if (!m_SelectedEntity->HasComponent<CanvasComponent>()) {
                        if (ImGui::MenuItem("Canvas")) {
                            m_SelectedEntity->AddComponent<CanvasComponent>();
                        }
                    }
                    if (!m_SelectedEntity->HasComponent<RectTransformComponent>()) {
                        if (ImGui::MenuItem("Rect Transform")) {
                            m_SelectedEntity->AddComponent<RectTransformComponent>();
                        }
                    }
                    if (!m_SelectedEntity->HasComponent<UIImageComponent>()) {
                        if (ImGui::MenuItem("UI Image")) {
                            if (!m_SelectedEntity->HasComponent<RectTransformComponent>())
                                m_SelectedEntity->AddComponent<RectTransformComponent>();
                            m_SelectedEntity->AddComponent<UIImageComponent>();
                        }
                    }
                    if (!m_SelectedEntity->HasComponent<UITextComponent>()) {
                        if (ImGui::MenuItem("UI Text")) {
                            if (!m_SelectedEntity->HasComponent<RectTransformComponent>())
                                m_SelectedEntity->AddComponent<RectTransformComponent>();
                            m_SelectedEntity->AddComponent<UITextComponent>();
                        }
                    }
                    if (!m_SelectedEntity->HasComponent<UIButtonComponent>()) {
                        if (ImGui::MenuItem("UI Button")) {
                            if (!m_SelectedEntity->HasComponent<RectTransformComponent>())
                                m_SelectedEntity->AddComponent<RectTransformComponent>();
                            m_SelectedEntity->AddComponent<UIButtonComponent>();
                        }
                    }
                    if (!m_SelectedEntity->HasComponent<UISliderComponent>()) {
                        if (ImGui::MenuItem("UI Slider")) {
                            if (!m_SelectedEntity->HasComponent<RectTransformComponent>())
                                m_SelectedEntity->AddComponent<RectTransformComponent>();
                            m_SelectedEntity->AddComponent<UISliderComponent>();
                        }
                    }
                    if (!m_SelectedEntity->HasComponent<UIProgressBarComponent>()) {
                        if (ImGui::MenuItem("UI Progress Bar")) {
                            if (!m_SelectedEntity->HasComponent<RectTransformComponent>())
                                m_SelectedEntity->AddComponent<RectTransformComponent>();
                            m_SelectedEntity->AddComponent<UIProgressBarComponent>();
                        }
                    }
                    if (!m_SelectedEntity->HasComponent<UIJoystickComponent>()) {
                        if (ImGui::MenuItem("UI Joystick")) {
                            if (!m_SelectedEntity->HasComponent<RectTransformComponent>())
                                m_SelectedEntity->AddComponent<RectTransformComponent>();
                            m_SelectedEntity->AddComponent<UIJoystickComponent>();
                        }
                    }
                    if (!m_SelectedEntity->HasComponent<UIActionButtonComponent>()) {
                        if (ImGui::MenuItem("UI BotÃ³n de acciÃ³n")) {
                            if (!m_SelectedEntity->HasComponent<RectTransformComponent>())
                                m_SelectedEntity->AddComponent<RectTransformComponent>();
                            m_SelectedEntity->AddComponent<UIActionButtonComponent>();
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Script Component (Lua)")) {
                    m_SelectedEntity->AddComponent<ScriptComponent>();
                }
                ImGui::EndPopup();
            }

        } else {
            ImGui::TextDisabled("Ninguna entidad ni asset seleccionado.");
            ImGui::TextDisabled("Selecciona un objeto en la jerarquÃ­a o un archivo en el explorador.");
        }
    }
    ImGui::End();
}
