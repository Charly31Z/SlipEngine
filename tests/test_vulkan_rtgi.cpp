#include "SlipEngine/render/rhi/SlipRHI.h"
#include "SlipEngine/render/SlipGraphicsSettings.h"
#include "SlipEngine/render/vulkan/VulkanRHI.h"
#include "SlipEngine/render/vulkan/VulkanSceneRenderer.h"
#include "SlipEngine/render/vulkan/VulkanStreamline.h"
#include "SlipEngine/render/vulkan/VulkanBuffer.h"
#include "SlipEngine/render/vulkan/VulkanTexture.h"
#include "SlipEngine/render/SlipMesh.h"
#include "SlipEngine/render/SlipMaterial.h"
#include "SlipEngine/render/SlipPrimitive.h"
#include "SlipEngine/render/shadergraph/ShaderGraphCompiler.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/scene/SlipSkyComponent.h"

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <volk.h>

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

SlipEntity* CreateBoxEntity(
    const std::string& name,
    const glm::vec3& pos,
    const glm::vec3& scale,
    const glm::vec3& color,
    float roughness = 0.7f,
    float metallic = 0.0f)
{
    SlipEntity* ent = new SlipEntity(name);
    ent->position = pos;
    ent->scale = scale;
    if (auto tr = ent->GetTransform()) {
        tr->position = pos;
        tr->scale = scale;
    }
    auto mc = ent->AddComponent<MeshComponent>();
    std::vector<Vertex> verts;
    std::vector<unsigned int> indices;
    SlipPrimitive::GenerateCube(1.0f, verts, indices);
    mc->model = new SlipMesh(verts, indices);
    mc->material = new SlipMaterial();
    mc->material->color = color;
    mc->material->roughness = roughness;
    mc->material->metallic = metallic;
    return ent;
}

void SetEntityTransform(SlipEntity* ent, const glm::vec3& pos, const glm::vec3& rot = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f)) {
    ent->position = pos;
    ent->rotation = rot;
    ent->scale = scale;
    if (auto tr = ent->GetTransform()) {
        tr->position = pos;
        tr->rotation = rot;
        tr->scale = scale;
    }
}

bool RenderAndReadback(
    VulkanContext& ctx,
    VulkanSceneRenderer* renderer,
    SlipLevel* level,
    const glm::mat4& view,
    const glm::mat4& proj,
    const glm::vec3& eye,
    uint32_t width,
    uint32_t height,
    uint32_t frames,
    VulkanTexture& outTex,
    VulkanBuffer& staging,
    std::vector<uint8_t>& outPixels)
{
    for (uint32_t f = 0; f < frames; ++f) {
        VkCommandBuffer cmd = ctx.BeginSingleTimeCommands();
        VkImageLayout initLayout = (f == 0) ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VulkanTexture::TransitionImageLayout(cmd, outTex.GetImage(), initLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

        bool ok = renderer->RenderScene(
            cmd, f, level, view, proj, eye,
            outTex.GetImage(), outTex.GetImageView(), width, height,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            1
        );
        if (!ok) {
            ctx.EndSingleTimeCommands(cmd);
            return false;
        }

        if (f + 1 == frames) {
            VulkanTexture::TransitionImageLayout(cmd, outTex.GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VkBufferImageCopy region{};
            region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            region.imageExtent = { width, height, 1 };
            vkCmdCopyImageToBuffer(cmd, outTex.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.GetBuffer(), 1, &region);
        }
        ctx.EndSingleTimeCommands(cmd);
    }

    const uint8_t* mapped = static_cast<const uint8_t*>(staging.GetMapped());
    if (!mapped) return false;
    outPixels.assign(mapped, mapped + static_cast<size_t>(width) * height * 4);
    return true;
}

glm::vec3 SampleRegionAvg(const std::vector<uint8_t>& pixels, uint32_t W, uint32_t H, uint32_t cx, uint32_t cy, int radius) {
    glm::vec3 sum(0.0f);
    int count = 0;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int x = std::clamp(static_cast<int>(cx) + dx, 0, static_cast<int>(W) - 1);
            int y = std::clamp(static_cast<int>(cy) + dy, 0, static_cast<int>(H) - 1);
            size_t idx = (static_cast<size_t>(y) * W + x) * 4;
            sum += glm::vec3(pixels[idx], pixels[idx + 1], pixels[idx + 2]);
            ++count;
        }
    }
    return sum / static_cast<float>(std::max(count, 1));
}

} // namespace

int main() {
    std::cout << "[SlipTestVulkanRTGI] Iniciando pruebas Hardware Ray Tracing GI + Path Tracing + LoadScene..." << std::endl;

    ISlipRHI* rhi = SlipRHI::Create(GraphicsAPI::Vulkan, nullptr, true);
    if (!rhi) {
        std::cerr << "[FAIL] SlipRHI::Create(Vulkan, headless=true) fallo." << std::endl;
        return 1;
    }

    VulkanRHI* vkRhi = dynamic_cast<VulkanRHI*>(rhi);
    if (!vkRhi || !vkRhi->GetSceneRenderer()) {
        std::cerr << "[FAIL] VulkanSceneRenderer no inicializado." << std::endl;
        SlipRHI::Destroy();
        return 1;
    }

    const auto& caps = vkRhi->GetCapabilities();
    if (!caps.supportsRayQuery) {
        std::cerr << "[FAIL] Se requiere VK_KHR_ray_query para ejecutar SlipTestVulkanRTGI." << std::endl;
        SlipRHI::Destroy();
        return 1;
    }

    if (volkInitialize() != VK_SUCCESS) {
        std::cerr << "[FAIL] volkInitialize fallo." << std::endl;
        SlipRHI::Destroy();
        return 1;
    }
    volkLoadInstance(vkRhi->GetInstance());
    volkLoadDevice(vkRhi->GetDevice());

    VulkanContext& ctx = vkRhi->GetContext();
    VulkanSceneRenderer* renderer = vkRhi->GetSceneRenderer();

    constexpr uint32_t W = 256;
    constexpr uint32_t H = 256;

    VulkanTexture outTex;
    if (!outTex.CreateImage2D(
            &ctx, W, H, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, true))
    {
        std::cerr << "[FAIL] No se pudo crear textura offscreen." << std::endl;
        SlipRHI::Destroy();
        return 1;
    }

    VulkanBuffer staging;
    if (!staging.Create(&ctx, static_cast<VkDeviceSize>(W) * H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, false)) {
        std::cerr << "[FAIL] No se pudo crear staging buffer." << std::endl;
        outTex.Destroy(&ctx);
        SlipRHI::Destroy();
        return 1;
    }

    // Build closed Cornell-style box level
    SlipLevel boxLevel;
    boxLevel.AddEntity(CreateBoxEntity("Floor",     glm::vec3(0.0f, -1.8f, 0.5f),  glm::vec3(4.2f, 0.2f, 5.8f), glm::vec3(0.85f, 0.85f, 0.85f)));
    boxLevel.AddEntity(CreateBoxEntity("Ceiling",   glm::vec3(0.0f,  1.8f, 0.5f),  glm::vec3(4.2f, 0.2f, 5.8f), glm::vec3(0.85f, 0.85f, 0.85f)));
    boxLevel.AddEntity(CreateBoxEntity("BackWall",  glm::vec3(0.0f,  0.0f, -2.0f), glm::vec3(4.2f, 3.8f, 0.2f), glm::vec3(0.85f, 0.85f, 0.85f)));
    boxLevel.AddEntity(CreateBoxEntity("FrontWall", glm::vec3(0.0f,  0.0f,  3.2f), glm::vec3(4.2f, 3.8f, 0.2f), glm::vec3(0.75f, 0.75f, 0.75f)));
    boxLevel.AddEntity(CreateBoxEntity("RedWall",   glm::vec3(-1.9f, 0.0f, 0.5f),  glm::vec3(0.2f, 3.8f, 5.8f), glm::vec3(0.95f, 0.04f, 0.04f)));
    boxLevel.AddEntity(CreateBoxEntity("GreenWall", glm::vec3( 1.9f, 0.0f, 0.5f),  glm::vec3(0.2f, 3.8f, 5.8f), glm::vec3(0.06f, 0.90f, 0.08f)));
    boxLevel.AddEntity(CreateBoxEntity("Pedestal",  glm::vec3(0.0f, -1.1f, -0.6f), glm::vec3(0.9f, 1.2f, 0.9f), glm::vec3(0.85f, 0.85f, 0.85f)));

    SlipEntity* pointLightEnt = new SlipEntity("BoxPointLight");
    SetEntityTransform(pointLightEnt, glm::vec3(0.0f, 1.25f, -0.3f));
    auto pl = pointLightEnt->AddComponent<LightComponent>(LightType::Point);
    pl->color = glm::vec3(1.0f, 0.96f, 0.92f);
    pl->intensity = 2.6f;
    pl->range = 12.0f;
    boxLevel.AddEntity(pointLightEnt);

    SlipEntity* sunEnt = new SlipEntity("BoxSunLight");
    SetEntityTransform(sunEnt, glm::vec3(0.0f), glm::vec3(-35.0f, 20.0f, 0.0f));
    auto dl = sunEnt->AddComponent<LightComponent>(LightType::Directional);
    dl->color = glm::vec3(1.0f, 0.98f, 0.95f);
    dl->intensity = 1.6f;
    boxLevel.AddEntity(sunEnt);

    glm::vec3 eye(0.0f, 0.0f, 2.4f);
    glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 50.0f);

    auto& gfx = SlipGraphicsSettings::Get().GetData();
    std::vector<uint8_t> pixelsOff, pixelsRtgi, pixelsPt;

    // 1. GI = Off
    gfx.giMode = GlobalIlluminationMode::Off;
    if (!RenderAndReadback(ctx, renderer, &boxLevel, view, proj, eye, W, H, 1, outTex, staging, pixelsOff)) {
        std::cerr << "[FAIL] Render GI=Off fallo." << std::endl;
        staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
        return 1;
    }
    glm::vec3 offCenter = SampleRegionAvg(pixelsOff, W, H, W / 2, H / 2, 4);
    std::cout << "[INFO] Case 1 (GI=Off) DrawCalls=" << renderer->GetLastDrawCallCount()
              << " BackWallCenter RGB=(" << offCenter.r << ", " << offCenter.g << ", " << offCenter.b << ")" << std::endl;

    // 2. GI = RTGI + Denoiser (8 frames)
    gfx.giMode = GlobalIlluminationMode::RTGI;
    gfx.giBounces = 2;
    gfx.giRaysPerPixel = 2;
    gfx.giIntensity = 1.6f;
    gfx.giDenoiserEnabled = true;
    gfx.giTemporalAlpha = 0.25f;
    gfx.giRayTracedShadows = true;
    renderer->ResetPathTracingAccumulation();

    if (!RenderAndReadback(ctx, renderer, &boxLevel, view, proj, eye, W, H, 8, outTex, staging, pixelsRtgi)) {
        std::cerr << "[FAIL] Render GI=RTGI fallo." << std::endl;
        staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
        return 1;
    }
    bool tlasOk = renderer->HasValidTLAS((8 - 1) % 2);
    uint32_t rtInstCount = renderer->GetLastRtInstanceCount();
    glm::vec3 rtgiLeft = SampleRegionAvg(pixelsRtgi, W, H, static_cast<uint32_t>(W * 0.26f), H / 2, 6);
    glm::vec3 rtgiRight = SampleRegionAvg(pixelsRtgi, W, H, static_cast<uint32_t>(W * 0.74f), H / 2, 6);
    glm::vec3 rtgiRedWall = SampleRegionAvg(pixelsRtgi, W, H, static_cast<uint32_t>(W * 0.08f), H / 2, 4);
    std::cout << "[INFO] Case 2 (GI=RTGI, 8f) TLAS=" << (tlasOk ? "true" : "false")
              << " RtInstances=" << rtInstCount
              << " RedWall=(" << rtgiRedWall.r << "," << rtgiRedWall.g << "," << rtgiRedWall.b << ")"
              << " LeftBackWall RGB=(" << rtgiLeft.r << ", " << rtgiLeft.g << ", " << rtgiLeft.b << ")"
              << " RightBackWall RGB=(" << rtgiRight.r << ", " << rtgiRight.g << ", " << rtgiRight.b << ")" << std::endl;

    if (!tlasOk || rtInstCount < 6 || rtgiRedWall.r <= rtgiRedWall.b || rtgiLeft.r <= rtgiLeft.b || rtgiRight.g <= rtgiRight.r) {
        std::cerr << "[FAIL] Verificacion RTGI fallida (TLAS o color bleeding insuficiente)." << std::endl;
        staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
        return 1;
    }

    // 3. GI = PathTracing (16 frames)
    gfx.giMode = GlobalIlluminationMode::PathTracing;
    gfx.giBounces = 4;
    gfx.giIntensity = 1.4f;
    gfx.ptMaxSamples = 64;
    renderer->ResetPathTracingAccumulation();

    if (!RenderAndReadback(ctx, renderer, &boxLevel, view, proj, eye, W, H, 16, outTex, staging, pixelsPt)) {
        std::cerr << "[FAIL] Render GI=PathTracing fallo." << std::endl;
        staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
        return 1;
    }
    uint32_t ptSamples = renderer->GetPathTracingSampleCount();
    glm::vec3 ptLeft = SampleRegionAvg(pixelsPt, W, H, static_cast<uint32_t>(W * 0.26f), H / 2, 6);
    glm::vec3 ptRight = SampleRegionAvg(pixelsPt, W, H, static_cast<uint32_t>(W * 0.74f), H / 2, 6);
    glm::vec3 ptLeftWall = SampleRegionAvg(pixelsPt, W, H, static_cast<uint32_t>(W * 0.08f), H / 2, 4);
    std::cout << "[INFO] Case 3 (GI=PathTracing, 16f) Samples=" << ptSamples
              << " RedWall RGB=(" << ptLeftWall.r << ", " << ptLeftWall.g << ", " << ptLeftWall.b << ")"
              << " LeftBackWall RGB=(" << ptLeft.r << ", " << ptLeft.g << ", " << ptLeft.b << ")"
              << " RightBackWall RGB=(" << ptRight.r << ", " << ptRight.g << ", " << ptRight.b << ")" << std::endl;

    if (ptSamples != 16 || ptLeftWall.r <= ptLeftWall.b || ptLeft.r <= ptLeft.b || (ptLeft.r + ptLeft.g + ptLeft.b) < 20.0f) {
        std::cerr << "[FAIL] Verificacion Path Tracing fallida." << std::endl;
        staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
        return 1;
    }

    // 4. Regression check: LoadScene / ClearLevel / InvalidateSceneCache / Reload real Main.scene without OpenGL
    if (std::filesystem::exists("assets/scenes/Main.scene")) {
        gfx.giMode = GlobalIlluminationMode::RTGI;
        SlipLevel realLevel;
        if (!realLevel.LoadScene("assets/scenes/Main.scene")) {
            std::cerr << "[FAIL] realLevel.LoadScene(assets/scenes/Main.scene) fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        std::vector<uint8_t> mainPixels;
        if (!RenderAndReadback(ctx, renderer, &realLevel, view, proj, eye, W, H, 2, outTex, staging, mainPixels)) {
            std::cerr << "[FAIL] RenderScene sobre Main.scene fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        renderer->InvalidateSceneCache();
        realLevel.ClearLevel();
        if (!realLevel.LoadScene("assets/scenes/Main.scene")) {
            std::cerr << "[FAIL] Recarga de Main.scene tras ClearLevel fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        if (!RenderAndReadback(ctx, renderer, &realLevel, view, proj, eye, W, H, 1, outTex, staging, mainPixels)) {
            std::cerr << "[FAIL] RenderScene tras recargar Main.scene fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        std::cout << "[INFO] Case 4a (Main.scene Load + Render + Invalidate + Reload) OK. DrawCalls=" << renderer->GetLastDrawCallCount() << std::endl;
    } else {
        std::cout << "[WARN] assets/scenes/Main.scene no encontrado en CWD; caso 4a omitido." << std::endl;
    }

    if (std::filesystem::exists("assets/scenes/NFS_Prototype.scene")) {
        gfx.giMode = GlobalIlluminationMode::RTGI;
        SlipLevel nfsLevel;
        if (!nfsLevel.LoadScene("assets/scenes/NFS_Prototype.scene")) {
            std::cerr << "[FAIL] nfsLevel.LoadScene(assets/scenes/NFS_Prototype.scene) fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        std::vector<uint8_t> nfsPixels;
        if (!RenderAndReadback(ctx, renderer, &nfsLevel, view, proj, eye, W, H, 1, outTex, staging, nfsPixels)) {
            std::cerr << "[FAIL] RenderScene sobre NFS_Prototype.scene fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        renderer->InvalidateSceneCache();
        std::cout << "[INFO] Case 4b (NFS_Prototype.scene Load + Render + Invalidate) OK. DrawCalls=" << renderer->GetLastDrawCallCount() << std::endl;
    }

    // 5. ShaderGraphCompiler -> Vulkan GLSL 460 + SPIR-V 1.6 bytecode (Lit + Unlit)
    {
        SlipShaderGraph::ShaderGraph litGraph;
        litGraph.SetMaterialType(SlipShaderGraph::ShaderGraphType::Lit);
        litGraph.AddProperty("RoughnessScale", SlipShaderGraph::PinType::Float, glm::vec4(0.35f, 0.0f, 0.0f, 0.0f));
        litGraph.AddProperty("UvScale", SlipShaderGraph::PinType::Vec2, glm::vec4(2.0f, 2.0f, 0.0f, 0.0f));
        litGraph.AddProperty("EmissiveTint", SlipShaderGraph::PinType::Vec3, glm::vec4(0.1f, 0.4f, 0.9f, 0.0f));
        litGraph.AddProperty("BaseTint", SlipShaderGraph::PinType::Vec4, glm::vec4(1.0f, 0.8f, 0.6f, 1.0f));
        litGraph.AddProperty("UseAlbedoMap", SlipShaderGraph::PinType::Boolean, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        litGraph.AddProperty("ExtraMatrix", SlipShaderGraph::PinType::Matrix4, glm::vec4(0.0f));
        litGraph.AddProperty("AlbedoMap", SlipShaderGraph::PinType::Texture2D, glm::vec4(0.0f), "assets/textures/checker.png");

        SlipShaderGraph::GeneratedShaderCode litSpv = SlipShaderGraph::ShaderGraphCompiler::CompileToVulkanSpirv(litGraph);
        if (!litSpv.success || litSpv.vertexSpirv.empty() || litSpv.fragmentSpirv.empty() ||
            litSpv.vertexSpirv[0] != 0x07230203u || litSpv.fragmentSpirv[0] != 0x07230203u ||
            litSpv.textureBindings.empty()) {
            std::cerr << "[FAIL] Case 5a ShaderGraph Lit -> SPIR-V fallo: " << litSpv.errorMessage << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }

        SlipShaderGraph::ShaderGraph unlitGraph;
        unlitGraph.SetMaterialType(SlipShaderGraph::ShaderGraphType::Unlit);
        unlitGraph.AddProperty("ColorTint", SlipShaderGraph::PinType::Vec4, glm::vec4(0.2f, 0.8f, 0.4f, 1.0f));
        SlipShaderGraph::GeneratedShaderCode unlitSpv = SlipShaderGraph::ShaderGraphCompiler::CompileToVulkanSpirv(unlitGraph);
        if (!unlitSpv.success || unlitSpv.vertexSpirv.empty() || unlitSpv.fragmentSpirv.empty() ||
            unlitSpv.vertexSpirv[0] != 0x07230203u || unlitSpv.fragmentSpirv[0] != 0x07230203u) {
            std::cerr << "[FAIL] Case 5b ShaderGraph Unlit -> SPIR-V fallo: " << unlitSpv.errorMessage << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }

        std::cout << "[INFO] Case 5 (ShaderGraph -> SPIR-V) LitWords=(VS:" << litSpv.vertexSpirv.size()
                  << ", FS:" << litSpv.fragmentSpirv.size() << ") UnlitWords=(VS:" << unlitSpv.vertexSpirv.size()
                  << ", FS:" << unlitSpv.fragmentSpirv.size() << ")" << std::endl;
    }

    // 6. Hardware Ray-Traced Specular Reflections & Procedural Volumetric Clouds
    {
        // 6a: Smooth chrome sphere reflecting bright red backdrop behind the camera
        renderer->InvalidateSceneCache();
        gfx.giMode = GlobalIlluminationMode::RTGI;
        gfx.giBounces = 2;
        gfx.giRaysPerPixel = 2;
        gfx.giDenoiserEnabled = true;
        gfx.giTemporalAlpha = 0.25f;

        SlipLevel reflLevel;
        {
            SlipEntity* sphereEnt = new SlipEntity("ChromeSphere");
            SetEntityTransform(sphereEnt, glm::vec3(0.0f, 0.0f, 0.0f));
            auto mc = sphereEnt->AddComponent<MeshComponent>();
            std::vector<Vertex> sVerts;
            std::vector<unsigned int> sIndices;
            SlipPrimitive::GenerateSphere(1.0f, 32, 32, sVerts, sIndices);
            mc->model = new SlipMesh(sVerts, sIndices);
            mc->material = new SlipMaterial();
            mc->material->color = glm::vec3(0.95f, 0.95f, 0.95f);
            mc->material->roughness = 0.05f;
            mc->material->metallic = 1.0f;
            reflLevel.AddEntity(sphereEnt);

            reflLevel.AddEntity(CreateBoxEntity(
                "RedBackdrop",
                glm::vec3(0.0f, 0.0f, 4.5f),
                glm::vec3(14.0f, 12.0f, 1.0f),
                glm::vec3(1.0f, 0.04f, 0.04f),
                0.8f,
                0.0f
            ));

            SlipEntity* rLightEnt = new SlipEntity("ReflPointLight");
            SetEntityTransform(rLightEnt, glm::vec3(0.0f, 0.8f, 2.5f));
            auto pl = rLightEnt->AddComponent<LightComponent>(LightType::Point);
            pl->color = glm::vec3(1.0f, 1.0f, 1.0f);
            pl->intensity = 6.0f;
            pl->range = 25.0f;
            reflLevel.AddEntity(rLightEnt);
        }

        glm::vec3 reflEye(0.0f, 0.0f, 2.2f);
        glm::mat4 reflView = glm::lookAt(reflEye, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        std::vector<uint8_t> pixelsRefl;
        if (!RenderAndReadback(ctx, renderer, &reflLevel, reflView, proj, reflEye, W, H, 8, outTex, staging, pixelsRefl)) {
            std::cerr << "[FAIL] Case 6a RenderScene RT Reflections fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        glm::vec3 chromeCenter = SampleRegionAvg(pixelsRefl, W, H, W / 2, H / 2, 4);
        std::cout << "[INFO] Case 6a (RT Reflections) ChromeCenter RGB=("
                  << chromeCenter.r << ", " << chromeCenter.g << ", " << chromeCenter.b << ")" << std::endl;
        if (chromeCenter.r <= chromeCenter.b * 1.4f || chromeCenter.r < 40.0f) {
            std::cerr << "[FAIL] Case 6a reflejo rojo sobre esfera metalica insuficiente." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        renderer->InvalidateSceneCache();

        // 6b: Volumetric clouds via SlipSkyComponent compared against cloudless baseline
        SlipLevel cloudLevel;
        glm::vec3 cloudEye(0.0f, 2.0f, 0.0f);
        float pitchRad = glm::radians(25.0f);
        glm::vec3 cloudTarget = cloudEye + glm::vec3(0.0f, std::sin(pitchRad), -std::cos(pitchRad));
        glm::mat4 cloudView = glm::lookAt(cloudEye, cloudTarget, glm::vec3(0.0f, 1.0f, 0.0f));

        std::vector<uint8_t> pixelsNoCloud, pixelsWithCloud;
        gfx.giMode = GlobalIlluminationMode::RTGI;
        if (!RenderAndReadback(ctx, renderer, &cloudLevel, cloudView, proj, cloudEye, W, H, 1, outTex, staging, pixelsNoCloud)) {
            std::cerr << "[FAIL] Case 6b cloudless baseline fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }

        SlipEntity* skyEnt = new SlipEntity("SkyClouds");
        auto skyComp = skyEnt->AddComponent<SlipSkyComponent>();
        skyComp->params.dynamicAtmosphere = false;
        skyComp->params.cloudCoverage = 0.80f;
        skyComp->params.cloudDensity = 0.14f;
        cloudLevel.AddEntity(skyEnt);

        if (!RenderAndReadback(ctx, renderer, &cloudLevel, cloudView, proj, cloudEye, W, H, 1, outTex, staging, pixelsWithCloud)) {
            std::cerr << "[FAIL] Case 6b with clouds fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }

        double diffAccum = 0.0;
        const size_t totalPix = static_cast<size_t>(W) * H;
        for (size_t i = 0; i < totalPix; ++i) {
            for (int c = 0; c < 3; ++c) {
                diffAccum += std::abs(static_cast<double>(pixelsWithCloud[i * 4 + c]) - static_cast<double>(pixelsNoCloud[i * 4 + c]));
            }
        }
        double meanDiff = diffAccum / static_cast<double>(totalPix * 3);
        std::cout << "[INFO] Case 6b (Volumetric Clouds) MeanAbsDiff=" << meanDiff << std::endl;
        if (meanDiff <= 3.0) {
            std::cerr << "[FAIL] Case 6b nubes volumetricas no alteraron visiblemente el cielo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
    }

    // 7. PostProcessVolumeComponent + Screen-Space Selection Outline (#4296FA)
    {
        SlipLevel ppLevel;
        SlipEntity* ppCube = CreateBoxEntity("PPCube", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.8f), glm::vec3(0.7f, 0.7f, 0.7f), 0.7f, 0.0f);
        ppLevel.AddEntity(ppCube);

        SlipEntity* ppLight = new SlipEntity("PPLight");
        SetEntityTransform(ppLight, glm::vec3(0.0f, 5.0f, 3.0f), glm::vec3(20.0f, 0.0f, 0.0f));
        auto dlc = ppLight->AddComponent<LightComponent>();
        dlc->type = LightType::Directional;
        dlc->intensity = 2.0f;
        dlc->color = glm::vec3(1.0f);
        ppLevel.AddEntity(ppLight);

        glm::vec3 ppEye(0.0f, 0.6f, 4.0f);
        glm::mat4 ppView = glm::lookAt(ppEye, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        gfx.giMode = GlobalIlluminationMode::RTGI;
        gfx.skyEnabled = false;

        std::vector<uint8_t> pixelsBase, pixelsPP, pixelsSel, pixelsNoSel;
        renderer->SetSelectedEntity(nullptr);
        if (!RenderAndReadback(ctx, renderer, &ppLevel, ppView, proj, ppEye, W, H, 1, outTex, staging, pixelsBase)) {
            std::cerr << "[FAIL] Case 7a baseline fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }

        // Case 7a: Green ColorFilter + Bloom + Vignette
        SlipEntity* ppVolEnt = new SlipEntity("PPVolume");
        auto ppv = ppVolEnt->AddComponent<PostProcessVolumeComponent>();
        ppv->isGlobal = true;
        ppv->colorOverride = true;
        ppv->colorFilter = glm::vec3(0.2f, 1.0f, 0.2f);
        ppv->bloomOverride = true;
        ppv->bloomEnabled = true;
        ppv->bloomIntensity = 0.85f;
        ppv->bloomThreshold = 0.25f;
        ppv->vignetteOverride = true;
        ppv->vignetteEnabled = true;
        ppv->vignetteIntensity = 0.90f;
        ppLevel.AddEntity(ppVolEnt);

        if (!RenderAndReadback(ctx, renderer, &ppLevel, ppView, proj, ppEye, W, H, 1, outTex, staging, pixelsPP)) {
            std::cerr << "[FAIL] Case 7a PostProcessVolume render fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        glm::vec3 baseCenter = SampleRegionAvg(pixelsBase, W, H, W / 2, H / 2, 4);
        glm::vec3 ppCenter = SampleRegionAvg(pixelsPP, W, H, W / 2, H / 2, 4);
        glm::vec3 ppCorner = SampleRegionAvg(pixelsPP, W, H, 8, 8, 4);
        std::cout << "[INFO] Case 7a (PostProcessVolume) BaseCenter=(" << baseCenter.r << "," << baseCenter.g << "," << baseCenter.b
                  << ") PPCenter=(" << ppCenter.r << "," << ppCenter.g << "," << ppCenter.b
                  << ") PPCorner=(" << ppCorner.r << "," << ppCorner.g << "," << ppCorner.b << ")" << std::endl;

        if (ppCenter.g <= ppCenter.r * 1.5f || ppCenter.g <= ppCenter.b * 1.5f) {
            std::cerr << "[FAIL] Case 7a PostProcessVolume colorFilter no aplico tint verde en Vulkan." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }

        // Case 7b: Screen-Space Selection Outline (#4296FA) without color filter
        ppv->SetEnabled(false);
        renderer->SetSelectedEntity(ppCube);
        if (!RenderAndReadback(ctx, renderer, &ppLevel, ppView, proj, ppEye, W, H, 1, outTex, staging, pixelsSel)) {
            std::cerr << "[FAIL] Case 7b selected outline render fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }

        auto countOutlinePixels = [&](const std::vector<uint8_t>& img) -> uint32_t {
            uint32_t hits = 0;
            for (uint32_t y = 0; y < H; ++y) {
                for (uint32_t x = 0; x < W; ++x) {
                    size_t idx = (static_cast<size_t>(y) * W + x) * 4u;
                    float r = static_cast<float>(img[idx + 0]);
                    float g = static_cast<float>(img[idx + 1]);
                    float b = static_cast<float>(img[idx + 2]);
                    if (b > 170.0f && b > r * 1.6f && g > r * 1.2f) {
                        ++hits;
                    }
                }
            }
            return hits;
        };

        uint32_t selOutlineHits = countOutlinePixels(pixelsSel);
        renderer->SetSelectedEntity(nullptr);
        if (!RenderAndReadback(ctx, renderer, &ppLevel, ppView, proj, ppEye, W, H, 1, outTex, staging, pixelsNoSel)) {
            std::cerr << "[FAIL] Case 7b unselected render fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        uint32_t noSelOutlineHits = countOutlinePixels(pixelsNoSel);
        std::cout << "[INFO] Case 7b (Selection Outline #4296FA) SelectedEdgePixels=" << selOutlineHits
                  << " UnselectedEdgePixels=" << noSelOutlineHits << std::endl;

        if (selOutlineHits < 25 || noSelOutlineHits >= selOutlineHits) {
            std::cerr << "[FAIL] Case 7b screen-space selection outline no se renderizo correctamente." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }

        gfx.skyEnabled = true;
    }

    // 8. Phase 1: Realtime Path Tracing (SVGF Mode, 1-2 SPP, Blue Noise + Full NEE + Demodulation)
    {
        gfx.giMode = GlobalIlluminationMode::PathTracing;
        gfx.ptMode = PathTracingMode::RealtimeSVGF;
        gfx.giBounces = 3;
        gfx.giRaysPerPixel = 2;
        gfx.giIntensity = 1.4f;
        gfx.giDenoiserEnabled = true;
        renderer->ResetPathTracingAccumulation();

        std::vector<uint8_t> pixelsRtPtA, pixelsRtPtB;
        if (!RenderAndReadback(ctx, renderer, &boxLevel, view, proj, eye, W, H, 1, outTex, staging, pixelsRtPtA)) {
            std::cerr << "[FAIL] Case 8a PathTracing RealtimeSVGF (frame 1) fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }

        // Simulate camera motion to a second viewpoint at 2 SPP without progressive convergence wait
        glm::vec3 movedEye = eye + glm::vec3(0.18f, -0.05f, -0.22f);
        glm::mat4 movedView = glm::lookAt(movedEye, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        if (!RenderAndReadback(ctx, renderer, &boxLevel, movedView, proj, movedEye, W, H, 1, outTex, staging, pixelsRtPtB)) {
            std::cerr << "[FAIL] Case 8b PathTracing RealtimeSVGF (camera move frame 2) fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }

        glm::vec3 rtPtRedWall = SampleRegionAvg(pixelsRtPtA, W, H, static_cast<uint32_t>(W * 0.08f), H / 2, 4);
        glm::vec3 rtPtLeft = SampleRegionAvg(pixelsRtPtA, W, H, static_cast<uint32_t>(W * 0.26f), H / 2, 6);
        glm::vec3 rtPtRight = SampleRegionAvg(pixelsRtPtA, W, H, static_cast<uint32_t>(W * 0.74f), H / 2, 6);
        glm::vec3 rtPtMovedCenter = SampleRegionAvg(pixelsRtPtB, W, H, W / 2, H / 2, 8);
        std::cout << "[INFO] Case 8 (PT RealtimeSVGF 2 SPP + BlueNoise + Demodulation)"
                  << " RedWall=(" << rtPtRedWall.r << "," << rtPtRedWall.g << "," << rtPtRedWall.b << ")"
                  << " LeftBack=(" << rtPtLeft.r << "," << rtPtLeft.g << "," << rtPtLeft.b << ")"
                  << " RightBack=(" << rtPtRight.r << "," << rtPtRight.g << "," << rtPtRight.b << ")"
                  << " MovedCenter=(" << rtPtMovedCenter.r << "," << rtPtMovedCenter.g << "," << rtPtMovedCenter.b << ")" << std::endl;

        if (rtPtRedWall.r <= rtPtRedWall.b ||
            (rtPtLeft.r + rtPtLeft.g + rtPtLeft.b) < 20.0f ||
            (rtPtMovedCenter.r + rtPtMovedCenter.g + rtPtMovedCenter.b) < 20.0f)
        {
            std::cerr << "[FAIL] Case 8 verificacion Path Tracing Tiempo Real (SVGF Fase 1) fallida." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }

        // 9. Phase 2: Primary G-Buffer (Albedo RGBA16F, Normal+Rough RGBA16F, LinearDepth+InstID RG32F, Motion2D RG16F & Ping-Pong)
        const VulkanRayTracedGI& rtGi = renderer->GetRayTracedGI();
        auto halfToFloat = [](uint16_t h) -> float {
            uint32_t s = (h >> 15) & 0x0001u;
            uint32_t e = (h >> 10) & 0x001Fu;
            uint32_t m = h & 0x03FFu;
            uint32_t fBits = 0;
            if (e == 0) {
                if (m != 0) {
                    e = 1;
                    while ((m & 0x0400u) == 0) { m <<= 1; --e; }
                    m &= 0x03FFu;
                    fBits = (s << 31) | ((e + (127 - 15)) << 23) | (m << 13);
                } else {
                    fBits = (s << 31);
                }
            } else if (e == 31) {
                fBits = (s << 31) | 0x7F800000u | (m << 13);
            } else {
                fBits = (s << 31) | ((e + (127 - 15)) << 23) | (m << 13);
            }
            float outF = 0.0f;
            std::memcpy(&outF, &fBits, sizeof(float));
            return outF;
        };

        auto readbackImageBytes = [&](const VulkanTexture& tex, VkDeviceSize byteCount, std::vector<uint8_t>& outBuf) -> bool {
            if (!tex.IsValid()) return false;
            VulkanBuffer tmpBuf;
            if (!tmpBuf.Create(&ctx, byteCount, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, false)) return false;
            VkCommandBuffer c = ctx.BeginSingleTimeCommands();
            VulkanTexture::TransitionImageLayout(c, tex.GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VkBufferImageCopy reg{};
            reg.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            reg.imageSubresource.layerCount = 1;
            reg.imageExtent = { W, H, 1 };
            vkCmdCopyImageToBuffer(c, tex.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tmpBuf.GetBuffer(), 1, &reg);
            VulkanTexture::TransitionImageLayout(c, tex.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            ctx.EndSingleTimeCommands(c);
            outBuf.resize(static_cast<size_t>(byteCount));
            std::memcpy(outBuf.data(), tmpBuf.GetMapped(), static_cast<size_t>(byteCount));
            tmpBuf.Destroy(&ctx);
            return true;
        };

        // Frame A: Stationary camera at `movedView` (matches previous frame's viewProj -> 0 motion vector)
        if (!RenderAndReadback(ctx, renderer, &boxLevel, movedView, proj, movedEye, W, H, 1, outTex, staging, pixelsRtPtA)) {
            std::cerr << "[FAIL] Case 9 stationary frame fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        uint32_t ppIdxA = rtGi.GetPtCurrPingPongIndex();

        std::vector<uint8_t> stationaryMotionRaw;
        if (!readbackImageBytes(rtGi.GetPtMotion(), static_cast<VkDeviceSize>(W) * H * 4ULL, stationaryMotionRaw)) {
            std::cerr << "[FAIL] Case 9 readback stationary motion fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        const size_t centerPixIdx = static_cast<size_t>(H / 2) * W + (W / 2);
        const uint16_t* statMot16 = reinterpret_cast<const uint16_t*>(stationaryMotionRaw.data());
        glm::vec2 statMot(halfToFloat(statMot16[centerPixIdx * 2 + 0]), halfToFloat(statMot16[centerPixIdx * 2 + 1]));

        // Frame B: Pan camera horizontally -> verify ping-pong swap, RG32F linear depth + instanceID, RGBA16F normal+rough, and non-zero 2D motion vector
        glm::vec3 panEye = movedEye + glm::vec3(0.45f, 0.0f, 0.0f);
        glm::mat4 panView = glm::lookAt(panEye, glm::vec3(0.45f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        if (!RenderAndReadback(ctx, renderer, &boxLevel, panView, proj, panEye, W, H, 1, outTex, staging, pixelsRtPtB)) {
            std::cerr << "[FAIL] Case 9 panned frame fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        uint32_t ppIdxB = rtGi.GetPtCurrPingPongIndex();
        uint32_t prevPPIdxB = rtGi.GetPtPrevPingPongIndex();

        std::vector<uint8_t> depthInstRaw, normRoughRaw, panMotionRaw;
        if (!readbackImageBytes(rtGi.GetPtDepthInst(ppIdxB), static_cast<VkDeviceSize>(W) * H * 8ULL, depthInstRaw) ||
            !readbackImageBytes(rtGi.GetPtNormalRough(ppIdxB), static_cast<VkDeviceSize>(W) * H * 8ULL, normRoughRaw) ||
            !readbackImageBytes(rtGi.GetPtMotion(), static_cast<VkDeviceSize>(W) * H * 4ULL, panMotionRaw))
        {
            std::cerr << "[FAIL] Case 9 readback G-Buffer textures fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }

        const float* depthInstF = reinterpret_cast<const float*>(depthInstRaw.data());
        float centerLinearDepth = depthInstF[centerPixIdx * 2 + 0];
        float centerInstanceId  = depthInstF[centerPixIdx * 2 + 1];

        const uint16_t* normRough16 = reinterpret_cast<const uint16_t*>(normRoughRaw.data());
        glm::vec3 centerN(
            halfToFloat(normRough16[centerPixIdx * 4 + 0]),
            halfToFloat(normRough16[centerPixIdx * 4 + 1]),
            halfToFloat(normRough16[centerPixIdx * 4 + 2])
        );
        float centerRough = halfToFloat(normRough16[centerPixIdx * 4 + 3]);

        const uint16_t* panMot16 = reinterpret_cast<const uint16_t*>(panMotionRaw.data());
        glm::vec2 panMot(halfToFloat(panMot16[centerPixIdx * 2 + 0]), halfToFloat(panMot16[centerPixIdx * 2 + 1]));

        std::cout << "[INFO] Case 9 (Phase 2 G-Buffer + Motion2D + PingPong)"
                  << " PingPong=(" << ppIdxA << "->" << ppIdxB << ", prev=" << prevPPIdxB << ")"
                  << " LinearDepth=" << centerLinearDepth
                  << " InstanceID=" << centerInstanceId
                  << " NormalLen=" << glm::length(centerN)
                  << " Roughness=" << centerRough
                  << " StationaryMotion=(" << statMot.x << "," << statMot.y << ")"
                  << " PanMotion=(" << panMot.x << "," << panMot.y << ")" << std::endl;

        if (ppIdxA == ppIdxB || prevPPIdxB != ppIdxA ||
            centerLinearDepth <= 1.0f || centerLinearDepth >= 50.0f ||
            centerInstanceId < 1.0f ||
            std::abs(glm::length(centerN) - 1.0f) > 0.05f ||
            centerRough < 0.03f || centerRough > 1.01f ||
            glm::length(statMot) > 1e-3f ||
            std::abs(panMot.x) < 0.005f ||
            !rtGi.GetPtIrradianceHistory(ppIdxB).IsValid() ||
            !rtGi.GetPtIrradianceHistory(prevPPIdxB).IsValid())
        {
            std::cerr << "[FAIL] Case 9 verificacion de G-Buffer, Motion Vectors 2D o Ping-Pong fallida." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }

        // 10. Phase 3: SVGF Temporal Reprojection + Moments (mu1, mu2, historyLength) + 3x3 Spatial Variance + 4-Pass A-Trous Wavelet
        renderer->ResetPathTracingAccumulation();
        std::vector<uint8_t> svgfMomentsF1Raw, svgfMomentsF6Raw, svgfFilteredIrrRaw;

        // Render Frame 1 at 1 SPP -> verify historyLength == 1 and 3x3 spatial variance + 4-pass A-Trous executes
        gfx.giRaysPerPixel = 1;
        if (!RenderAndReadback(ctx, renderer, &boxLevel, view, proj, eye, W, H, 1, outTex, staging, pixelsRtPtA)) {
            std::cerr << "[FAIL] Case 10a SVGF Frame 1 (1 SPP) fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        uint32_t f1PP = rtGi.GetPtCurrPingPongIndex();
        if (!readbackImageBytes(rtGi.GetPtMomentsHistory(f1PP), static_cast<VkDeviceSize>(W) * H * 8ULL, svgfMomentsF1Raw)) {
            std::cerr << "[FAIL] Case 10a readback MomentsHistory Frame 1 fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        const uint16_t* momF1 = reinterpret_cast<const uint16_t*>(svgfMomentsF1Raw.data());
        float histLenF1 = halfToFloat(momF1[centerPixIdx * 4 + 2]);

        // Render 5 more frames with stationary camera at 1 SPP -> historyLength must accumulate to 6.0 via temporal reprojection
        if (!RenderAndReadback(ctx, renderer, &boxLevel, view, proj, eye, W, H, 5, outTex, staging, pixelsRtPtB)) {
            std::cerr << "[FAIL] Case 10b SVGF Frames 2..6 (1 SPP) fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        uint32_t f6PP = rtGi.GetPtCurrPingPongIndex();
        if (!readbackImageBytes(rtGi.GetPtMomentsHistory(f6PP), static_cast<VkDeviceSize>(W) * H * 8ULL, svgfMomentsF6Raw) ||
            !readbackImageBytes(rtGi.GetPtFilteredIrradiance(), static_cast<VkDeviceSize>(W) * H * 8ULL, svgfFilteredIrrRaw))
        {
            std::cerr << "[FAIL] Case 10b readback MomentsHistory/FilteredIrr Frame 6 fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        const uint16_t* momF6 = reinterpret_cast<const uint16_t*>(svgfMomentsF6Raw.data());
        float mu1F6     = halfToFloat(momF6[centerPixIdx * 4 + 0]);
        float mu2F6     = halfToFloat(momF6[centerPixIdx * 4 + 1]);
        float histLenF6 = halfToFloat(momF6[centerPixIdx * 4 + 2]);

        const uint16_t* filtIrr16 = reinterpret_cast<const uint16_t*>(svgfFilteredIrrRaw.data());
        glm::vec3 centerFiltIrr(
            halfToFloat(filtIrr16[centerPixIdx * 4 + 0]),
            halfToFloat(filtIrr16[centerPixIdx * 4 + 1]),
            halfToFloat(filtIrr16[centerPixIdx * 4 + 2])
        );
        float centerFiltVar = halfToFloat(filtIrr16[centerPixIdx * 4 + 3]);

        // Measure spatial noise (standard deviation across uniform back wall patch) with SVGF OFF vs SVGF ON at 1 SPP
        auto computePatchStdDev = [&](const std::vector<uint8_t>& img, uint32_t cx, uint32_t cy, int rad) -> float {
            glm::vec3 mean = SampleRegionAvg(img, W, H, cx, cy, rad);
            float varSum = 0.0f;
            int cnt = 0;
            for (int dy = -rad; dy <= rad; ++dy) {
                for (int dx = -rad; dx <= rad; ++dx) {
                    int x = std::clamp(static_cast<int>(cx) + dx, 0, static_cast<int>(W) - 1);
                    int y = std::clamp(static_cast<int>(cy) + dy, 0, static_cast<int>(H) - 1);
                    size_t idx = (static_cast<size_t>(y) * W + x) * 4u;
                    glm::vec3 d = glm::vec3(img[idx], img[idx + 1], img[idx + 2]) - mean;
                    varSum += glm::dot(d, d) / 3.0f;
                    ++cnt;
                }
            }
            return std::sqrt(varSum / static_cast<float>(std::max(cnt, 1)));
        };

        gfx.giDenoiserEnabled = false;
        std::vector<uint8_t> pixelsRaw1Spp;
        if (!RenderAndReadback(ctx, renderer, &boxLevel, view, proj, eye, W, H, 1, outTex, staging, pixelsRaw1Spp)) {
            std::cerr << "[FAIL] Case 10c raw 1 SPP render fallo." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }
        gfx.giDenoiserEnabled = true;

        float rawNoiseStd  = computePatchStdDev(pixelsRaw1Spp, W / 2, H / 2, 10);
        float svgfNoiseStd = computePatchStdDev(pixelsRtPtB, W / 2, H / 2, 10);

        std::cout << "[INFO] Case 10 (Phase 3 SVGF Temporal + Moments + 4-Pass A-Trous)"
                  << " HistLen=(" << histLenF1 << "->" << histLenF6 << ")"
                  << " Moments=(" << mu1F6 << "," << mu2F6 << ")"
                  << " FilteredIrr=(" << centerFiltIrr.r << "," << centerFiltIrr.g << "," << centerFiltIrr.b << ", var=" << centerFiltVar << ")"
                  << " PatchStdDev(Raw1SPP=" << rawNoiseStd << " -> SVGF=" << svgfNoiseStd << ")" << std::endl;

        if (std::abs(histLenF1 - 1.0f) > 0.1f ||
            histLenF6 < 5.5f ||
            mu1F6 <= 0.01f || mu2F6 <= 0.0001f ||
            (centerFiltIrr.r + centerFiltIrr.g + centerFiltIrr.b) <= 0.1f ||
            centerFiltVar < 0.0f ||
            svgfNoiseStd >= rawNoiseStd)
        {
            std::cerr << "[FAIL] Case 10 verificacion SVGF Fase 3 fallida." << std::endl;
            staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
            return 1;
        }

        // 11. Phase 4: Dedicated Final Modulation (Irradiance * Albedo + Emissive) + Post-Process Pipeline Integration
        {
            // Verify Albedo demodulation/remodulation contrast preservation between Red Wall and Green Wall at 1 SPP
            glm::vec3 svgfRedWall   = SampleRegionAvg(pixelsRtPtB, W, H, static_cast<uint32_t>(W * 0.08f), H / 2, 4);
            glm::vec3 svgfGreenWall = SampleRegionAvg(pixelsRtPtB, W, H, static_cast<uint32_t>(W * 0.92f), H / 2, 4);

            // Add a PostProcessVolumeComponent (Bloom + warm color filter) on top of Realtime SVGF Path Tracing
            SlipEntity* svgfPpEnt = new SlipEntity("SvgfPPVol");
            auto svgfPpv = svgfPpEnt->AddComponent<PostProcessVolumeComponent>();
            svgfPpv->isGlobal = true;
            svgfPpv->bloomOverride = true;
            svgfPpv->bloomEnabled = true;
            svgfPpv->bloomIntensity = 0.75f;
            svgfPpv->bloomThreshold = 0.25f;
            svgfPpv->colorOverride = true;
            svgfPpv->colorFilter = glm::vec3(1.15f, 1.0f, 0.75f);
            boxLevel.AddEntity(svgfPpEnt);

            std::vector<uint8_t> pixelsSvgfPost;
            if (!RenderAndReadback(ctx, renderer, &boxLevel, view, proj, eye, W, H, 1, outTex, staging, pixelsSvgfPost)) {
                std::cerr << "[FAIL] Case 11 SVGF + PostProcess render fallo." << std::endl;
                staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
                return 1;
            }
            svgfPpv->SetEnabled(false);

            glm::vec3 prePostCenter  = SampleRegionAvg(pixelsRtPtB, W, H, W / 2, H / 2, 6);
            glm::vec3 postProcCenter = SampleRegionAvg(pixelsSvgfPost, W, H, W / 2, H / 2, 6);

            std::cout << "[INFO] Case 11 (Phase 4 Final Modulation + PostProcess)"
                      << " RedWall=(" << svgfRedWall.r << "," << svgfRedWall.g << "," << svgfRedWall.b << ")"
                      << " GreenWall=(" << svgfGreenWall.r << "," << svgfGreenWall.g << "," << svgfGreenWall.b << ")"
                      << " PrePostCenter=(" << prePostCenter.r << "," << prePostCenter.g << "," << prePostCenter.b << ")"
                      << " PostProcCenter=(" << postProcCenter.r << "," << postProcCenter.g << "," << postProcCenter.b << ")" << std::endl;

            if (svgfRedWall.r <= svgfRedWall.g * 2.0f ||
                svgfGreenWall.g <= svgfGreenWall.r * 1.5f ||
                postProcCenter.r <= postProcCenter.b)
            {
                std::cerr << "[FAIL] Case 11 verificacion de Modulacion Final + Post-Procesado fallida." << std::endl;
                staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
                return 1;
            }
        }

        // 12. Dual-Stream SVGF (Demodulated DiffuseIrradiance + Unmodulated SpecularRadiance + Roughness < 0.15 Spatial Bypass)
        {
            renderer->InvalidateSceneCache();
            gfx.giMode = GlobalIlluminationMode::PathTracing;
            gfx.ptMode = PathTracingMode::RealtimeSVGF;
            gfx.giBounces = 2;
            gfx.giRaysPerPixel = 2;
            gfx.giDenoiserEnabled = true;
            gfx.svgfAtrousIterations = 3;
            gfx.svgfSigmaLuminance = 2.0f;
            gfx.svgfSigmaNormal = 128.0f;
            gfx.svgfSigmaDepth = 1.5f;

            SlipLevel specSvgfLevel;
            {
                SlipEntity* sphereEnt = new SlipEntity("MirrorSphereSVGF");
                SetEntityTransform(sphereEnt, glm::vec3(0.0f, 0.0f, 0.0f));
                auto mc = sphereEnt->AddComponent<MeshComponent>();
                std::vector<Vertex> sVerts;
                std::vector<unsigned int> sIndices;
                SlipPrimitive::GenerateSphere(1.0f, 32, 32, sVerts, sIndices);
                mc->model = new SlipMesh(sVerts, sIndices);
                mc->material = new SlipMaterial();
                mc->material->color = glm::vec3(0.95f, 0.95f, 0.95f);
                mc->material->roughness = 0.05f; // < 0.15 -> spatial A-Trous bypass for sharp mirror reflections
                mc->material->metallic = 1.0f;
                specSvgfLevel.AddEntity(sphereEnt);

                specSvgfLevel.AddEntity(CreateBoxEntity(
                    "RedBackdropSVGF",
                    glm::vec3(0.0f, 0.0f, 4.5f),
                    glm::vec3(14.0f, 12.0f, 1.0f),
                    glm::vec3(1.0f, 0.04f, 0.04f),
                    0.8f,
                    0.0f
                ));

                SlipEntity* rLightEnt = new SlipEntity("SpecSvgfPointLight");
                SetEntityTransform(rLightEnt, glm::vec3(0.0f, 0.8f, 2.5f));
                auto pl = rLightEnt->AddComponent<LightComponent>(LightType::Point);
                pl->color = glm::vec3(1.0f, 1.0f, 1.0f);
                pl->intensity = 6.0f;
                pl->range = 25.0f;
                specSvgfLevel.AddEntity(rLightEnt);
            }

            glm::vec3 reflEye(0.0f, 0.0f, 2.2f);
            glm::mat4 reflView = glm::lookAt(reflEye, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            std::vector<uint8_t> pixelsSpecSvgf;
            if (!RenderAndReadback(ctx, renderer, &specSvgfLevel, reflView, proj, reflEye, W, H, 4, outTex, staging, pixelsSpecSvgf)) {
                std::cerr << "[FAIL] Case 12 RenderScene Dual-Stream SVGF Specular fallo." << std::endl;
                staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
                return 1;
            }

            uint32_t specPP = rtGi.GetPtCurrPingPongIndex();
            std::vector<uint8_t> tempSpecRaw, filtSpecRaw;
            if (!readbackImageBytes(rtGi.GetPtSpecularHistory(specPP), static_cast<VkDeviceSize>(W) * H * 8ULL, tempSpecRaw) ||
                !readbackImageBytes(rtGi.GetPtFilteredSpecular(), static_cast<VkDeviceSize>(W) * H * 8ULL, filtSpecRaw))
            {
                std::cerr << "[FAIL] Case 12 readback SpecularHistory/FilteredSpecular fallo." << std::endl;
                staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
                return 1;
            }

            const uint16_t* tempSpec16 = reinterpret_cast<const uint16_t*>(tempSpecRaw.data());
            const uint16_t* filtSpec16 = reinterpret_cast<const uint16_t*>(filtSpecRaw.data());
            glm::vec3 centerTempSpec(
                halfToFloat(tempSpec16[centerPixIdx * 4 + 0]),
                halfToFloat(tempSpec16[centerPixIdx * 4 + 1]),
                halfToFloat(tempSpec16[centerPixIdx * 4 + 2])
            );
            glm::vec3 centerFiltSpec(
                halfToFloat(filtSpec16[centerPixIdx * 4 + 0]),
                halfToFloat(filtSpec16[centerPixIdx * 4 + 1]),
                halfToFloat(filtSpec16[centerPixIdx * 4 + 2])
            );
            glm::vec3 mirrorCenterLdr = SampleRegionAvg(pixelsSpecSvgf, W, H, W / 2, H / 2, 4);
            float bypassDiff = glm::length(centerTempSpec - centerFiltSpec);

            std::cout << "[INFO] Case 12 (Dual-Stream SVGF Diffuse+Specular | Roughness<0.15 Bypass)"
                      << " TempSpec=(" << centerTempSpec.r << "," << centerTempSpec.g << "," << centerTempSpec.b << ")"
                      << " FiltSpec=(" << centerFiltSpec.r << "," << centerFiltSpec.g << "," << centerFiltSpec.b << ")"
                      << " BypassDiff=" << bypassDiff
                      << " MirrorCenterLDR=(" << mirrorCenterLdr.r << "," << mirrorCenterLdr.g << "," << mirrorCenterLdr.b << ")" << std::endl;

            if (centerFiltSpec.r <= 0.05f ||
                centerFiltSpec.r <= centerFiltSpec.b * 1.5f ||
                bypassDiff > 1e-3f ||
                mirrorCenterLdr.r <= mirrorCenterLdr.b * 1.4f ||
                mirrorCenterLdr.r < 40.0f)
            {
                std::cerr << "[FAIL] Case 12 verificacion de reflejos especulares SVGF (roughness < 0.15) fallida." << std::endl;
                staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
                return 1;
            }
            renderer->InvalidateSceneCache();
        }

        // 13. ReSTIR DI + ReSTIR GI (Paso 3): Multi-Light Cyberpunk-style rig (12 Point Lights + Sun) at 1 SPP
        {
            SlipLevel restirLevel;
            restirLevel.AddEntity(CreateBoxEntity("RFloor", glm::vec3(0.0f, -1.8f, 0.0f), glm::vec3(8.0f, 0.2f, 8.0f), glm::vec3(0.75f, 0.75f, 0.78f), 0.22f, 0.1f));
            restirLevel.AddEntity(CreateBoxEntity("RBack",  glm::vec3(0.0f,  0.5f, -3.2f), glm::vec3(8.0f, 5.0f, 0.2f), glm::vec3(0.82f, 0.82f, 0.82f), 0.75f, 0.0f));
            restirLevel.AddEntity(CreateBoxEntity("RPillar", glm::vec3(0.0f, -0.5f, -1.2f), glm::vec3(0.8f, 2.4f, 0.8f), glm::vec3(0.85f, 0.25f, 0.20f), 0.55f, 0.0f));

            for (int li = 0; li < 12; ++li) {
                float ang = static_cast<float>(li) * (6.2831853f / 12.0f);
                SlipEntity* lEnt = new SlipEntity("NeonLight_" + std::to_string(li));
                SetEntityTransform(lEnt, glm::vec3(std::cos(ang) * 2.4f, 0.6f + 0.3f * std::sin(ang * 2.0f), -1.0f + std::sin(ang) * 1.8f));
                auto plc = lEnt->AddComponent<LightComponent>(LightType::Point);
                plc->color = (li % 2 == 0) ? glm::vec3(0.2f, 0.85f, 1.0f) : glm::vec3(1.0f, 0.30f, 0.75f);
                plc->intensity = 1.6f;
                plc->range = 8.0f;
                restirLevel.AddEntity(lEnt);
            }

            gfx.giMode = GlobalIlluminationMode::PathTracing;
            gfx.ptMode = PathTracingMode::RealtimeSVGF;
            gfx.giBounces = 3;
            gfx.giRaysPerPixel = 1;
            gfx.restirDIEnabled = true;
            gfx.restirGIEnabled = true;
            gfx.giDenoiserEnabled = true;
            renderer->ResetPathTracingAccumulation();

            std::vector<uint8_t> pixelsRestirOn, pixelsRestirRaw;
            if (!RenderAndReadback(ctx, renderer, &restirLevel, view, proj, eye, W, H, 6, outTex, staging, pixelsRestirOn)) {
                std::cerr << "[FAIL] Case 13a ReSTIR DI+GI render fallo." << std::endl;
                staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
                return 1;
            }

            gfx.giDenoiserEnabled = false;
            if (!RenderAndReadback(ctx, renderer, &restirLevel, view, proj, eye, W, H, 1, outTex, staging, pixelsRestirRaw)) {
                std::cerr << "[FAIL] Case 13b raw 1-SPP render fallo." << std::endl;
                staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
                return 1;
            }
            gfx.giDenoiserEnabled = true;

            glm::vec3 restirWall = SampleRegionAvg(pixelsRestirOn, W, H, W / 2, static_cast<uint32_t>(H * 0.25f), 8);
            float restirStd = computePatchStdDev(pixelsRestirOn, W / 2, static_cast<uint32_t>(H * 0.25f), 8);
            float rawMultiStd = computePatchStdDev(pixelsRestirRaw, W / 2, static_cast<uint32_t>(H * 0.25f), 8);
            std::cout << "[INFO] Case 13 (Paso 3 ReSTIR DI + ReSTIR GI, 12 Lights @ 1 SPP)"
                      << " WallRGB=(" << restirWall.r << "," << restirWall.g << "," << restirWall.b << ")"
                      << " PatchStdDev(Raw=" << rawMultiStd << " -> ReSTIR+SVGF=" << restirStd << ")" << std::endl;

            if ((restirWall.r + restirWall.g + restirWall.b) < 25.0f || restirStd > rawMultiStd) {
                std::cerr << "[FAIL] Case 13 verificacion ReSTIR DI + GI fallida." << std::endl;
                staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
                return 1;
            }
            renderer->InvalidateSceneCache();
        }

        // 14. NRD ReBLUR Hit-Distance Denoiser (Paso 2) + Virtual Specular Reprojection under camera pan
        {
            gfx.giMode = GlobalIlluminationMode::PathTracing;
            gfx.ptMode = PathTracingMode::RealtimeNRD;
            gfx.giBounces = 3;
            gfx.giRaysPerPixel = 1;
            gfx.restirDIEnabled = true;
            gfx.restirGIEnabled = true;
            gfx.giDenoiserEnabled = true;
            gfx.specVirtualParallax = 1.0f;
            renderer->ResetPathTracingAccumulation();

            std::vector<uint8_t> pixelsReblurStat, pixelsReblurPan;
            if (!RenderAndReadback(ctx, renderer, &boxLevel, view, proj, eye, W, H, 6, outTex, staging, pixelsReblurStat)) {
                std::cerr << "[FAIL] Case 14a RealtimeNRD (ReBLUR) stationary fallo." << std::endl;
                staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
                return 1;
            }
            glm::vec3 panEyeNrd = eye + glm::vec3(0.25f, 0.0f, -0.15f);
            glm::mat4 panViewNrd = glm::lookAt(panEyeNrd, glm::vec3(0.10f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            if (!RenderAndReadback(ctx, renderer, &boxLevel, panViewNrd, proj, panEyeNrd, W, H, 2, outTex, staging, pixelsReblurPan)) {
                std::cerr << "[FAIL] Case 14b RealtimeNRD (ReBLUR) camera pan fallo." << std::endl;
                staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
                return 1;
            }

            float reblurStd = computePatchStdDev(pixelsReblurStat, W / 2, H / 2, 10);
            glm::vec3 reblurRed = SampleRegionAvg(pixelsReblurStat, W, H, static_cast<uint32_t>(W * 0.08f), H / 2, 4);
            glm::vec3 reblurPanC = SampleRegionAvg(pixelsReblurPan, W, H, W / 2, H / 2, 6);
            std::cout << "[INFO] Case 14 (Paso 2 NRD ReBLUR Hit-Distance + Fast-History Anti-Lag)"
                      << " RedWall=(" << reblurRed.r << "," << reblurRed.g << "," << reblurRed.b << ")"
                      << " PanCenter=(" << reblurPanC.r << "," << reblurPanC.g << "," << reblurPanC.b << ")"
                      << " BackWallStdDev=" << reblurStd << std::endl;

            if (reblurRed.r <= reblurRed.b * 1.5f || (reblurPanC.r + reblurPanC.g + reblurPanC.b) < 20.0f || reblurStd >= rawNoiseStd) {
                std::cerr << "[FAIL] Case 14 verificacion NRD ReBLUR fallida." << std::endl;
                staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
                return 1;
            }
        }

        // 15. DLSS + Ray Reconstruction AOVs & Streamline Bridge (Paso 4)
        {
            gfx.giMode = GlobalIlluminationMode::PathTracing;
            gfx.ptMode = PathTracingMode::RealtimeDLSSRR;
            gfx.dlssQualityMode = 1;
            renderer->ResetPathTracingAccumulation();

            std::vector<uint8_t> pixelsDlssRR;
            if (!RenderAndReadback(ctx, renderer, &boxLevel, view, proj, eye, W, H, 3, outTex, staging, pixelsDlssRR)) {
                std::cerr << "[FAIL] Case 15 RealtimeDLSSRR render fallo." << std::endl;
                staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
                return 1;
            }

            std::vector<uint8_t> hitDistRaw, specAlbedoRaw;
            if (!readbackImageBytes(rtGi.GetPtHitDistAndDepth(), static_cast<VkDeviceSize>(W) * H * 16ULL, hitDistRaw) ||
                !readbackImageBytes(rtGi.GetPtSpecAlbedo(), static_cast<VkDeviceSize>(W) * H * 8ULL, specAlbedoRaw))
            {
                std::cerr << "[FAIL] Case 15 readback DLSS-RR AOV buffers fallo." << std::endl;
                staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
                return 1;
            }

            const float* hdF = reinterpret_cast<const float*>(hitDistRaw.data());
            float cDiffHitT = hdF[centerPixIdx * 4 + 0];
            float cSpecHitT = hdF[centerPixIdx * 4 + 1];
            float cWinZ     = hdF[centerPixIdx * 4 + 2];
            float cPrevLinZ = hdF[centerPixIdx * 4 + 3];

            const uint16_t* sa16 = reinterpret_cast<const uint16_t*>(specAlbedoRaw.data());
            glm::vec3 cSpecAlb(
                halfToFloat(sa16[centerPixIdx * 4 + 0]),
                halfToFloat(sa16[centerPixIdx * 4 + 1]),
                halfToFloat(sa16[centerPixIdx * 4 + 2])
            );
            glm::vec2 jitter1 = VulkanStreamline::ComputeHaltonJitterPixels(1u, 16u);
            glm::vec2 jitter2 = VulkanStreamline::ComputeHaltonJitterPixels(2u, 16u);
            glm::vec3 rrCenter = SampleRegionAvg(pixelsDlssRR, W, H, W / 2, H / 2, 6);

            std::cout << "[INFO] Case 15 (Paso 4 DLSS-RR AOVs + Streamline)"
                      << " NvidiaRTX=" << (VulkanStreamline::Get().IsNvidiaRTXDetected() ? "true" : "false")
                      << " HitDist=(diff:" << cDiffHitT << ", spec:" << cSpecHitT << ", winZ:" << cWinZ << ", prevZ:" << cPrevLinZ << ")"
                      << " SpecAlbedo=(" << cSpecAlb.r << "," << cSpecAlb.g << "," << cSpecAlb.b << ")"
                      << " HaltonJitter1=(" << jitter1.x << "," << jitter1.y << ")"
                      << " CenterRGB=(" << rrCenter.r << "," << rrCenter.g << "," << rrCenter.b << ")" << std::endl;

            if (cDiffHitT <= 0.0f || cWinZ <= 0.0f || cWinZ >= 1.0f || cPrevLinZ <= 1.0f ||
                cSpecAlb.r < 0.0f || cSpecAlb.r > 1.0f ||
                glm::length(jitter1 - jitter2) < 0.05f ||
                (rrCenter.r + rrCenter.g + rrCenter.b) < 20.0f)
            {
                std::cerr << "[FAIL] Case 15 verificacion AOVs DLSS Ray Reconstruction fallida." << std::endl;
                staging.Destroy(&ctx); outTex.Destroy(&ctx); SlipRHI::Destroy();
                return 1;
            }
        }

        gfx.giRaysPerPixel = 2;
        gfx.ptMode = PathTracingMode::Progressive;
    }

    staging.Destroy(&ctx);
    outTex.Destroy(&ctx);
    SlipRHI::Destroy();

    std::cout << "[PASS] SlipTestVulkanRTGI completado con exito." << std::endl;
    return 0;
}
