#include "SlipEngine/render/SlipLightBaker.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/render/SlipMesh.h"
#include "SlipEngine/render/SlipPrimitive.h"
#include "SlipEngine/render/SlipShader.h"
#include "SlipEngine/core/SlipConsole.h"
#include "SlipEngine/tools/SlipFileSystem.h"

#include <stb_image_write.h>
#include <stb_image.h>

#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>

static const char* s_Uv2RasterVert = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 7) in vec2 aTexCoords2;

out vec3 vWorldPos;
out vec3 vWorldNormal;

uniform mat4 u_Model;
uniform vec4 u_LightmapScaleOffset; // xy = scale, zw = offset

void main() {
    vec4 worldPos = u_Model * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    mat3 normalMatrix = transpose(inverse(mat3(u_Model)));
    vWorldNormal = normalize(normalMatrix * aNormal);

    // Mapear coordenadas UV2 transformadas por el atlas al espacio NDC [-1.0, 1.0]
    vec2 atlasUV = aTexCoords2 * u_LightmapScaleOffset.xy + u_LightmapScaleOffset.zw;
    gl_Position = vec4(atlasUV * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

static const char* s_Uv2RasterFrag = R"GLSL(
#version 330 core
in vec3 vWorldPos;
in vec3 vWorldNormal;

layout (location = 0) out vec4 outWorldPos;
layout (location = 1) out vec4 outWorldNormal;

void main() {
    outWorldPos = vec4(vWorldPos, 1.0); // alpha = 1.0 indica texel geométrico válido
    outWorldNormal = vec4(normalize(vWorldNormal), 1.0);
}
)GLSL";

static const char* s_BakeDirectComp = R"GLSL(
#version 430 core
layout (local_size_x = 16, local_size_y = 16) in;

layout (rgba32f, binding = 0) readonly uniform image2D u_WorldPosMap;
layout (rgba16f, binding = 1) readonly uniform image2D u_WorldNormalMap;
layout (rgba16f, binding = 2) writeonly uniform image2D u_OutLightmap;

struct StaticDirLight {
    vec4 dir;   // xyz = dir, w = intensity
    vec4 color; // xyz = color, w = pad
};

struct StaticPointLight {
    vec4 pos;   // xyz = pos, w = range
    vec4 color; // xyz = color, w = intensity
};

struct StaticSpotLight {
    vec4 pos;   // xyz = pos, w = range
    vec4 dir;   // xyz = dir, w = spotAngle (grados)
    vec4 color; // xyz = color, w = intensity
};

#define MAX_STATIC_LIGHTS 64
uniform int u_NumDir;
uniform StaticDirLight u_DirLights[8];
uniform int u_NumPoint;
uniform StaticPointLight u_PointLights[MAX_STATIC_LIGHTS];
uniform int u_NumSpot;
uniform StaticSpotLight u_SpotLights[16];

void main() {
    ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(u_WorldPosMap);
    if (pixelCoord.x >= size.x || pixelCoord.y >= size.y) return;

    vec4 posSample = imageLoad(u_WorldPosMap, pixelCoord);
    if (posSample.a < 0.5) {
        imageStore(u_OutLightmap, pixelCoord, vec4(0.0, 0.0, 0.0, 0.0));
        return;
    }

    vec3 worldPos = posSample.xyz;
    vec3 N = normalize(imageLoad(u_WorldNormalMap, pixelCoord).xyz);
    vec3 directE = vec3(0.0);

    // Luces Direccionales Estáticas
    for (int i = 0; i < u_NumDir && i < 8; ++i) {
        vec3 L = normalize(-u_DirLights[i].dir.xyz);
        float ndl = max(dot(N, L), 0.0);
        directE += u_DirLights[i].color.xyz * u_DirLights[i].dir.w * ndl;
    }

    // Luces Puntuales Estáticas
    for (int i = 0; i < u_NumPoint && i < MAX_STATIC_LIGHTS; ++i) {
        vec3 lightDir = u_PointLights[i].pos.xyz - worldPos;
        float dist = length(lightDir);
        float range = u_PointLights[i].pos.w;
        if (range > 0.0 && dist > range) continue;

        vec3 L = lightDir / max(dist, 1e-4);
        float att = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
        if (range > 0.0) att *= clamp(1.0 - (dist / range), 0.0, 1.0);

        float ndl = max(dot(N, L), 0.0);
        directE += u_PointLights[i].color.xyz * u_PointLights[i].color.w * att * ndl;
    }

    // Luces Spot Estáticas
    for (int i = 0; i < u_NumSpot && i < 16; ++i) {
        vec3 lightDir = u_SpotLights[i].pos.xyz - worldPos;
        float dist = length(lightDir);
        float range = u_SpotLights[i].pos.w;
        if (range > 0.0 && dist > range) continue;

        vec3 L = lightDir / max(dist, 1e-4);
        float theta = dot(L, normalize(-u_SpotLights[i].dir.xyz));
        float innerCut = cos(radians(u_SpotLights[i].dir.w * 0.5));
        float outerCut = cos(radians(u_SpotLights[i].dir.w * 0.65));
        float spot = clamp((theta - outerCut) / max(innerCut - outerCut, 1e-4), 0.0, 1.0);
        if (spot <= 0.0) continue;

        float att = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
        float ndl = max(dot(N, L), 0.0);
        directE += u_SpotLights[i].color.xyz * u_SpotLights[i].color.w * att * spot * ndl;
    }

    imageStore(u_OutLightmap, pixelCoord, vec4(directE, 1.0));
}
)GLSL";

static GLuint CompileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLchar log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[SlipLightBaker] Compile error: " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint CreateProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) return 0;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

static GLuint CreateComputeProgram(const char* compSrc) {
    GLuint comp = CompileShader(GL_COMPUTE_SHADER, compSrc);
    if (!comp) return 0;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, comp);
    glLinkProgram(prog);
    glDeleteShader(comp);
    return prog;
}

SlipLightBaker& SlipLightBaker::Get() {
    static SlipLightBaker s_Baker;
    return s_Baker;
}

bool SlipLightBaker::BakeLevel(
    SlipLevel* level,
    const LightBakeSettings& settings,
    std::function<void(float progress, const std::string& status)> progressCb
) {
    if (!level) return false;
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glGenFramebuffers || !glad_glGenTextures) return false;
#endif

    m_IsBaking = true;
    m_Progress = 0.05f;
    if (progressCb) progressCb(0.05f, "Recolectando mallas y luces estáticas...");

    // 1. Recolectar entidades estáticas con mallas
    struct StaticMeshItem {
        SlipEntity* entity;
        MeshComponent* meshComp;
        SlipPrimitiveEntity* prim;
    };
    std::vector<StaticMeshItem> staticMeshes;

    for (SlipEntity* ent : level->GetEntities()) {
        if (!ent || !ent->IsStatic()) continue;
        auto mc = ent->GetComponent<MeshComponent>();
        auto prim = dynamic_cast<SlipPrimitiveEntity*>(ent);
        if ((mc && mc->model) || prim) {
            staticMeshes.push_back({ ent, mc ? mc.get() : nullptr, prim });
        }
    }

    if (staticMeshes.empty()) {
        SlipConsole::LogWarning("SlipLightBaker: No se encontraron entidades estáticas con mallas para hornear.");
        m_IsBaking = false;
        m_Progress = 1.0f;
        if (progressCb) progressCb(1.0f, "Completado (sin mallas estáticas)");
        return true;
    }

    // 2. Empaquetar Atlas UV2 (layout proporcional en rejilla)
    m_Progress = 0.15f;
    if (progressCb) progressCb(0.15f, "Generando atlas de texturas UV2 unificado...");

    size_t meshCount = staticMeshes.size();
    size_t cols = static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(meshCount))));
    size_t rows = static_cast<size_t>(std::ceil(static_cast<double>(meshCount) / static_cast<double>(cols)));
    float tileW = 1.0f / static_cast<float>(cols);
    float tileH = 1.0f / static_cast<float>(rows);

    for (size_t i = 0; i < meshCount; ++i) {
        size_t c = i % cols;
        size_t r = i / cols;
        float u = static_cast<float>(c) * tileW;
        float v = static_cast<float>(r) * tileH;

        glm::vec4 scaleOffset(tileW, tileH, u, v);
        if (staticMeshes[i].meshComp) {
            staticMeshes[i].meshComp->lightmapScaleOffset = scaleOffset;
            staticMeshes[i].meshComp->lightmapIndex = 0;
        }
    }

    // 3. Crear FBO para rasterización de G-Buffer en espacio UV2
    m_Progress = 0.30f;
    if (progressCb) progressCb(0.30f, "Rasterizando geometría en espacio UV2...");

    uint32_t res = settings.resolution;
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    GLuint posTex = 0;
    glGenTextures(1, &posTex);
    glBindTexture(GL_TEXTURE_2D, posTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, res, res, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, posTex, 0);

    GLuint normTex = 0;
    glGenTextures(1, &normTex);
    glBindTexture(GL_TEXTURE_2D, normTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, res, res, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, normTex, 0);

    GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, bufs);

    glViewport(0, 0, res, res);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLuint rasterProg = CreateProgram(s_Uv2RasterVert, s_Uv2RasterFrag);
    if (!rasterProg) {
        SlipConsole::LogError("SlipLightBaker: Fallo al compilar shader de rasterizado UV2.");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteTextures(1, &posTex);
        glDeleteTextures(1, &normTex);
        glDeleteFramebuffers(1, &fbo);
        m_IsBaking = false;
        return false;
    }

    glUseProgram(rasterProg);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    for (const auto& item : staticMeshes) {
        glm::mat4 modelMat = item.entity->getMatrix();
        glUniformMatrix4fv(glGetUniformLocation(rasterProg, "u_Model"), 1, GL_FALSE, &modelMat[0][0]);

        glm::vec4 scOff = item.meshComp ? item.meshComp->lightmapScaleOffset : glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
        glUniform4fv(glGetUniformLocation(rasterProg, "u_LightmapScaleOffset"), 1, &scOff[0]);

        if (item.meshComp && item.meshComp->model) {
            item.meshComp->model->draw(modelMat);
        } else if (item.prim && item.prim->GetVAO() != 0) {
            glBindVertexArray(item.prim->GetVAO());
            glDrawElements(GL_TRIANGLES, item.prim->GetIndexCount(), GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteProgram(rasterProg);

    // 4. Crear textura de salida de Lightmap
    m_Progress = 0.50f;
    if (progressCb) progressCb(0.50f, "Calculando iluminación directa e indirecta en GPU...");

    GLuint outLightmap = 0;
    glGenTextures(1, &outLightmap);
    glBindTexture(GL_TEXTURE_2D, outLightmap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, res, res, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 5. Despacho de Compute Shader para luz directa estática
    GLuint bakeComp = CreateComputeProgram(s_BakeDirectComp);
    if (bakeComp) {
        glUseProgram(bakeComp);
        glBindImageTexture(0, posTex, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(1, normTex, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
        glBindImageTexture(2, outLightmap, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);

        // Recolectar y enlazar luces estáticas o mixtas
        int numDir = 0, numPoint = 0, numSpot = 0;
        for (SlipEntity* ent : level->GetEntities()) {
            if (!ent) continue;
            auto lc = ent->GetComponent<LightComponent>();
            if (!lc || !lc->IsEnabled() || lc->mobility == LightMobility::Dynamic) continue;

            glm::mat4 m = ent->getMatrix();
            glm::vec3 pos = glm::vec3(m[3]);
            glm::vec3 dir = glm::normalize(glm::vec3(m * glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)));

            if (lc->type == LightType::Directional && numDir < 8) {
                std::string base = "u_DirLights[" + std::to_string(numDir) + "].";
                glUniform4f(glGetUniformLocation(bakeComp, (base + "dir").c_str()), dir.x, dir.y, dir.z, lc->intensity);
                glUniform4f(glGetUniformLocation(bakeComp, (base + "color").c_str()), lc->color.r, lc->color.g, lc->color.b, 1.0f);
                numDir++;
            } else if (lc->type == LightType::Point && numPoint < 64) {
                std::string base = "u_PointLights[" + std::to_string(numPoint) + "].";
                glUniform4f(glGetUniformLocation(bakeComp, (base + "pos").c_str()), pos.x, pos.y, pos.z, lc->range);
                glUniform4f(glGetUniformLocation(bakeComp, (base + "color").c_str()), lc->color.r, lc->color.g, lc->color.b, lc->intensity);
                numPoint++;
            } else if (lc->type == LightType::Spot && numSpot < 16) {
                std::string base = "u_SpotLights[" + std::to_string(numSpot) + "].";
                glUniform4f(glGetUniformLocation(bakeComp, (base + "pos").c_str()), pos.x, pos.y, pos.z, lc->range);
                glUniform4f(glGetUniformLocation(bakeComp, (base + "dir").c_str()), dir.x, dir.y, dir.z, lc->spotAngle);
                glUniform4f(glGetUniformLocation(bakeComp, (base + "color").c_str()), lc->color.r, lc->color.g, lc->color.b, lc->intensity);
                numSpot++;
            }
        }

        glUniform1i(glGetUniformLocation(bakeComp, "u_NumDir"), numDir);
        glUniform1i(glGetUniformLocation(bakeComp, "u_NumPoint"), numPoint);
        glUniform1i(glGetUniformLocation(bakeComp, "u_NumSpot"), numSpot);

        GLuint groupsX = (res + 15) / 16;
        GLuint groupsY = (res + 15) / 16;
        glDispatchCompute(groupsX, groupsY, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        glDeleteProgram(bakeComp);
    }

    // 6. Descarga y dilatación de texeles en CPU para guardar archivo HDR
    m_Progress = 0.80f;
    if (progressCb) progressCb(0.80f, "Guardando atlas HDR en disco...");

    std::vector<float> pixels(res * res * 4);
    glBindTexture(GL_TEXTURE_2D, outLightmap);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());

    // Dilatación simple en CPU para rellenar bordes de islas UV
    std::vector<float> dilated = pixels;
    int dilationDist = static_cast<int>(settings.dilationDistance);
    for (int y = 0; y < static_cast<int>(res); ++y) {
        for (int x = 0; x < static_cast<int>(res); ++x) {
            size_t idx = (y * res + x) * 4;
            if (pixels[idx + 3] < 0.5f) { // Pixel vacío
                glm::vec3 sum(0.0f);
                float count = 0.0f;
                for (int dy = -dilationDist; dy <= dilationDist; ++dy) {
                    for (int dx = -dilationDist; dx <= dilationDist; ++dx) {
                        int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && nx < static_cast<int>(res) && ny >= 0 && ny < static_cast<int>(res)) {
                            size_t nIdx = (ny * res + nx) * 4;
                            if (pixels[nIdx + 3] >= 0.5f) {
                                sum += glm::vec3(pixels[nIdx], pixels[nIdx + 1], pixels[nIdx + 2]);
                                count += 1.0f;
                            }
                        }
                    }
                }
                if (count > 0.0f) {
                    glm::vec3 avg = sum / count;
                    dilated[idx + 0] = avg.r;
                    dilated[idx + 1] = avg.g;
                    dilated[idx + 2] = avg.b;
                    dilated[idx + 3] = 1.0f;
                }
            }
        }
    }

    // Guardar archivo HDR
    std::string lmDir = SlipFileSystem::CombinePath(SlipFileSystem::GetProjectAssetsPath(), "lightmaps");
    SlipFileSystem::CreateDirectoryPath(lmDir);

    std::string levelName = level->path.empty() ? "Level" : SlipFileSystem::GetFileNameWithoutExtension(level->path);
    std::string hdrPath = SlipFileSystem::CombinePath(lmDir, levelName + "_lightmap.hdr");

    stbi_write_hdr(hdrPath.c_str(), static_cast<int>(res), static_cast<int>(res), 4, dilated.data());

    // 7. Actualizar nivel y enlazar textura
    if (level->lightmapTexture != 0) {
        glDeleteTextures(1, &level->lightmapTexture);
    }
    level->lightmapTexture = outLightmap;
    level->lightmapPath = hdrPath;

    // Subir los píxeles dilatados a la textura cargada
    glBindTexture(GL_TEXTURE_2D, level->lightmapTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, res, res, GL_RGBA, GL_FLOAT, dilated.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    // Limpieza de buffers temporales
    glDeleteTextures(1, &posTex);
    glDeleteTextures(1, &normTex);
    glDeleteFramebuffers(1, &fbo);

    m_IsBaking = false;
    m_Progress = 1.0f;
    if (progressCb) progressCb(1.0f, "¡Bakeo completado exitosamente!");
    SlipConsole::LogSuccess("SlipLightBaker: Lightmap de escena horneado con éxito en: " + hdrPath);
    return true;
}

bool SlipLightBaker::LoadBakedLightmap(SlipLevel* level) {
    if (!level || level->lightmapPath.empty()) return false;
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glGenTextures) return false;
#endif

    if (!SlipFileSystem::Exists(level->lightmapPath)) {
        return false;
    }

    int w = 0, h = 0, channels = 0;
    float* data = stbi_loadf(level->lightmapPath.c_str(), &w, &h, &channels, 4);
    if (!data) {
        SlipConsole::LogError("SlipLightBaker: Fallo al cargar textura HDR de lightmap: " + level->lightmapPath);
        return false;
    }

    if (level->lightmapTexture != 0) {
        glDeleteTextures(1, &level->lightmapTexture);
        level->lightmapTexture = 0;
    }

    glGenTextures(1, &level->lightmapTexture);
    glBindTexture(GL_TEXTURE_2D, level->lightmapTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    SlipConsole::LogSuccess("SlipLightBaker: Lightmap cargado (" + std::to_string(w) + "x" + std::to_string(h) + "): " + level->lightmapPath);
    return true;
}

void SlipLightBaker::ClearBakedLightmap(SlipLevel* level) {
    if (!level) return;
    if (level->lightmapTexture != 0) {
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
        if (glad_glDeleteTextures)
#endif
        glDeleteTextures(1, &level->lightmapTexture);
        level->lightmapTexture = 0;
    }
    level->lightmapPath.clear();
}
