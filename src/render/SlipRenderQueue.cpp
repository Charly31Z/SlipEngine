#include "SlipRenderQueue.h"
#include "SlipMesh.h"
#include "SlipMaterial.h"
#include "SlipShader.h"
#include "SlipShaderLibrary.h"
#include "SlipEntity.h"
#include "SlipPrimitive.h"
#include "SlipGL.h"
#include "SlipGlobals.h"
#include <algorithm>

uint64_t RenderCommand::MakeSortKey(RenderPassType pass, uint32_t shaderId, uint32_t matId, uint32_t vaoId, float depth, bool frontToBack)
{
    uint64_t p = static_cast<uint64_t>(pass) & 0xFF;
    uint64_t s = static_cast<uint64_t>(shaderId) & 0xFFFF;
    uint64_t m = static_cast<uint64_t>(matId) & 0xFFFF;
    uint64_t v = static_cast<uint64_t>(vaoId) & 0xFF;

    float clampedDepth = glm::clamp(depth, 0.0f, 10000.0f);
    uint16_t d = static_cast<uint16_t>((clampedDepth / 10000.0f) * 65535.0f);
    if (!frontToBack) {
        d = 65535 - d;
    }

    return (p << 56) | (s << 40) | (m << 24) | (v << 16) | d;
}

void SlipRenderQueue::Clear()
{
    m_Commands.clear();
}

void SlipRenderQueue::Submit(const RenderCommand& cmd)
{
    m_Commands.push_back(cmd);
}

void SlipRenderQueue::Sort()
{
    std::sort(m_Commands.begin(), m_Commands.end(), [](const RenderCommand& a, const RenderCommand& b) {
        return a.sortKey < b.sortKey;
    });
}

void SlipRenderQueue::Flush(const std::function<void(SlipShader*)>& onBindShaderGlobals)
{
    m_ActualDrawCalls = 0;
    if (m_Commands.empty()) return;

    Sort();

    SlipShader* currentShader = nullptr;
    SlipMaterial* currentMaterial = nullptr;
    GLuint currentVAO = 0;
    bool currentDoubleSided = false;
    bool currentWireframe = false;

    for (size_t cmdIdx = 0; cmdIdx < m_Commands.size(); ++cmdIdx) {
        const auto& cmd = m_Commands[cmdIdx];
        std::string sType = (cmd.material && !std::string(cmd.material->shaderType).empty()) ? cmd.material->shaderType : "Lit";
        if (sType == "default") sType = "Lit";

        SlipShader* shader = (cmd.material && cmd.material->shader) ? cmd.material->shader : nullptr;
        if (!shader) {
            shader = SlipShaderLibrary::Get(sType);
            if (!shader) shader = SlipShaderLibrary::Get("Lit");
        }
        if (!shader) continue;

        if (shader != currentShader) {
            shader->use();
            shader->setFloat("u_Time", SlipGlobals::Get().GetTime());
            shader->setFloat("u_DeltaTime", SlipGlobals::Get().GetDeltaTime());
            if (onBindShaderGlobals) {
                onBindShaderGlobals(shader);
            }
            currentShader = shader;
            currentMaterial = nullptr; // Forzar re-bind de material al cambiar shader
        }

        currentShader->setMat4("u_Model", cmd.transform);
        currentShader->setMat4("model", cmd.transform);
        currentShader->setVec4("u_LightmapScaleOffset", cmd.lightmapScaleOffset);

        if (cmd.hasBones && cmd.boneTransforms && cmd.boneTransformCount > 0) {
            currentShader->setBool("u_HasBones", true);
            currentShader->setInt("u_SkinningQuality", cmd.skinningQuality);
            currentShader->setMat4Array("u_BoneTransforms", cmd.boneTransforms, static_cast<int>(cmd.boneTransformCount));
        } else {
            currentShader->setBool("u_HasBones", false);
        }

        glFrontFace((glm::determinant(cmd.transform) < 0.0f) ? GL_CW : GL_CCW);
        if (cmd.isStencilSelected) {
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glStencilMask(0xFF);
        } else {
            glStencilFunc(GL_ALWAYS, 0, 0xFF);
            glStencilMask(0x00);
        }

        if (cmd.material != currentMaterial) {
            if (cmd.material) {
                if (sType == "Unlit") {
                    currentShader->setVec4("u_Color", cmd.material->color.x, cmd.material->color.y, cmd.material->color.z, 1.0f);
                    bool hasTex = (cmd.material->diffuseTexture != nullptr && std::strlen(cmd.material->diffPath) > 0);
                    currentShader->setBool("u_HasTexture", hasTex);
                    currentShader->setBool("u_ToLinear", true);
                    if (hasTex) {
                        currentShader->setInt("u_MainTexture", 0);
                        cmd.material->diffuseTexture->bind(0);
                    }
                } else {
                    cmd.material->bindMaterialProperties(currentShader);
                }
            } else {
                currentShader->setVec3("u_Albedo", 0.8f, 0.8f, 0.8f);
                currentShader->setFloat("u_Roughness", 0.5f);
                currentShader->setFloat("u_Metallic", 0.0f);
                currentShader->setFloat("u_AO", 1.0f);
                currentShader->setVec3("u_EmissiveColor", 0.0f, 0.0f, 0.0f);
                currentShader->setFloat("u_EmissiveIntensity", 0.0f);
                currentShader->setBool("u_HasAlbedoMap", false);
                currentShader->setBool("u_HasNormalMap", false);
                currentShader->setBool("u_HasRoughnessMap", false);
                currentShader->setBool("u_HasMetallicMap", false);
                currentShader->setBool("u_HasAOMap", false);
                currentShader->setBool("u_HasEmissiveMap", false);
                currentShader->setBool("u_HasOpacityMap", false);
                currentShader->setBool("u_PackedORM", false);
                currentShader->setBool("u_AlphaClip", false);
                currentShader->setFloat("u_AlphaCutoff", 0.5f);
                currentShader->setBool("u_InvertRoughness", false);

                // Compatibilidad previa y desvinculación limpia de slots de textura
                currentShader->setVec3("material.color", 0.8f, 0.8f, 0.8f);
                currentShader->setVec3("material.ambient", 0.125f, 0.125f, 0.125f);
                currentShader->setVec3("material.specular", 0.16f, 0.16f, 0.16f);
                currentShader->setFloat("material.shininess", 3.0f);
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, 0);
            }
            currentMaterial = cmd.material;
        }

        if (cmd.useDebugTint) {
            if (sType == "Unlit") {
                currentShader->setVec4("u_Color", cmd.debugTintColor.r, cmd.debugTintColor.g, cmd.debugTintColor.b, 1.0f);
                currentShader->setBool("u_HasTexture", false);
            } else {
                currentShader->setVec3("u_Albedo", cmd.debugTintColor.r, cmd.debugTintColor.g, cmd.debugTintColor.b);
                currentShader->setBool("u_HasAlbedoMap", false);
                currentShader->setFloat("u_Roughness", 0.55f);
                currentShader->setFloat("u_Metallic", 0.05f);
                currentShader->setInt("u_VClustDebugMode", cmd.debugClusterVisMode);
                currentShader->setInt("u_VClustPrimOffset", static_cast<int>(cmd.customFirstIndex / 3));
            }
            // Forzar restauración del material real si el siguiente comando no usa tinte de depuración
            currentMaterial = nullptr;
        } else if (sType != "Unlit") {
            currentShader->setInt("u_VClustDebugMode", 0);
        }

        if (cmd.doubleSided != currentDoubleSided) {
            if (cmd.doubleSided) glDisable(GL_CULL_FACE);
            else glEnable(GL_CULL_FACE);
            currentDoubleSided = cmd.doubleSided;
        }

        if (cmd.wireframe != currentWireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, cmd.wireframe ? GL_LINE : GL_FILL);
            currentWireframe = cmd.wireframe;
        }

        if (cmd.useCustomIndexRange && cmd.customVAO != 0 && cmd.customIndexCount > 0) {
            if (cmd.customVAO != currentVAO) {
                glBindVertexArray(cmd.customVAO);
                currentVAO = cmd.customVAO;
            }
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
            if (glad_glMultiDrawElements != nullptr) {
                static std::vector<GLsizei> s_BatchCounts;
                static std::vector<const void*> s_BatchIndices;
                s_BatchCounts.clear();
                s_BatchIndices.clear();

                size_t bEnd = cmdIdx;
                while (bEnd < m_Commands.size()) {
                    const auto& next = m_Commands[bEnd];
                    if (!next.useCustomIndexRange || next.customVAO != cmd.customVAO ||
                        next.material != cmd.material || next.entity != cmd.entity ||
                        next.transform != cmd.transform || next.useDebugTint != cmd.useDebugTint ||
                        next.doubleSided != cmd.doubleSided || next.wireframe != cmd.wireframe ||
                        next.isStencilSelected != cmd.isStencilSelected) {
                        break;
                    }
                    s_BatchCounts.push_back(static_cast<GLsizei>(next.customIndexCount));
                    s_BatchIndices.push_back(reinterpret_cast<const void*>(static_cast<uintptr_t>(next.customFirstIndex * sizeof(unsigned int))));
                    ++bEnd;
                }

                if (s_BatchCounts.size() > 1) {
                    glMultiDrawElements(GL_TRIANGLES, s_BatchCounts.data(), GL_UNSIGNED_INT, s_BatchIndices.data(), static_cast<GLsizei>(s_BatchCounts.size()));
                    m_ActualDrawCalls++;
                    cmdIdx = bEnd - 1;
                } else {
                    void* offset = (void*)(uintptr_t)(cmd.customFirstIndex * sizeof(unsigned int));
                    glDrawElements(GL_TRIANGLES, cmd.customIndexCount, GL_UNSIGNED_INT, offset);
                    m_ActualDrawCalls++;
                }
            } else {
                void* offset = (void*)(uintptr_t)(cmd.customFirstIndex * sizeof(unsigned int));
                glDrawElements(GL_TRIANGLES, cmd.customIndexCount, GL_UNSIGNED_INT, offset);
                m_ActualDrawCalls++;
            }
#else
            void* offset = (void*)(uintptr_t)(cmd.customFirstIndex * sizeof(unsigned int));
            glDrawElements(GL_TRIANGLES, cmd.customIndexCount, GL_UNSIGNED_INT, offset);
            m_ActualDrawCalls++;
#endif
        } else if (cmd.mesh && cmd.submeshIndex < cmd.mesh->meshes.size()) {
            const auto& sm = cmd.mesh->meshes[cmd.submeshIndex];
            if (sm.VAO != 0) {
                if (sm.VAO != currentVAO) {
                    glBindVertexArray(sm.VAO);
                    currentVAO = sm.VAO;
                }
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
                if (cmd.mesh->isUnifiedBuffer && glad_glMultiDrawElements != nullptr) {
                    static std::vector<GLsizei> s_BatchCounts;
                    static std::vector<const void*> s_BatchIndices;
                    s_BatchCounts.clear();
                    s_BatchIndices.clear();

                    size_t bEnd = cmdIdx;
                    while (bEnd < m_Commands.size()) {
                        const auto& next = m_Commands[bEnd];
                        if (next.useCustomIndexRange || !next.mesh || !next.mesh->isUnifiedBuffer ||
                            next.mesh->meshes.empty() || next.submeshIndex >= next.mesh->meshes.size() ||
                            next.mesh->meshes[next.submeshIndex].VAO != sm.VAO ||
                            next.material != cmd.material || next.entity != cmd.entity ||
                            next.transform != cmd.transform || next.useDebugTint != cmd.useDebugTint ||
                            next.doubleSided != cmd.doubleSided || next.wireframe != cmd.wireframe ||
                            next.isStencilSelected != cmd.isStencilSelected) {
                            break;
                        }
                        const auto& nsm = next.mesh->meshes[next.submeshIndex];
                        if (nsm.indexCount > 0) {
                            s_BatchCounts.push_back(static_cast<GLsizei>(nsm.indexCount));
                            s_BatchIndices.push_back(reinterpret_cast<const void*>(static_cast<uintptr_t>(nsm.firstIndex * sizeof(unsigned int))));
                        }
                        ++bEnd;
                    }

                    if (s_BatchCounts.size() > 1) {
                        glMultiDrawElements(GL_TRIANGLES, s_BatchCounts.data(), GL_UNSIGNED_INT, s_BatchIndices.data(), static_cast<GLsizei>(s_BatchCounts.size()));
                        m_ActualDrawCalls++;
                        cmdIdx = bEnd - 1;
                    } else {
                        uint32_t count = sm.indexCount;
                        if (count > 0) {
                            void* offset = (void*)(uintptr_t)(sm.firstIndex * sizeof(unsigned int));
                            glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, offset);
                            m_ActualDrawCalls++;
                        }
                    }
                } else {
                    uint32_t count = cmd.mesh->isUnifiedBuffer ? sm.indexCount : static_cast<uint32_t>(sm.indices.size());
                    if (count > 0) {
                        void* offset = cmd.mesh->isUnifiedBuffer ? (void*)(uintptr_t)(sm.firstIndex * sizeof(unsigned int)) : 0;
                        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, offset);
                        m_ActualDrawCalls++;
                    }
                }
#else
                uint32_t count = cmd.mesh->isUnifiedBuffer ? sm.indexCount : static_cast<uint32_t>(sm.indices.size());
                if (count > 0) {
                    void* offset = cmd.mesh->isUnifiedBuffer ? (void*)(uintptr_t)(sm.firstIndex * sizeof(unsigned int)) : 0;
                    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, offset);
                    m_ActualDrawCalls++;
                }
#endif
            }
        } else if (cmd.isPrimitive && cmd.primEntity && cmd.primEntity->GetVAO() != 0) {
            if (cmd.primEntity->GetVAO() != currentVAO) {
                glBindVertexArray(cmd.primEntity->GetVAO());
                currentVAO = cmd.primEntity->GetVAO();
            }
            glDrawElements(GL_TRIANGLES, cmd.primEntity->GetIndexCount(), GL_UNSIGNED_INT, 0);
            m_ActualDrawCalls++;
        }
    }

    if (currentVAO != 0) {
        glBindVertexArray(0);
    }
    if (currentDoubleSided) {
        glEnable(GL_CULL_FACE);
    }
    if (currentWireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
}
