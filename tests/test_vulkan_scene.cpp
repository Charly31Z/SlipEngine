#include "SlipEngine/render/rhi/SlipRHI.h"
#include "SlipEngine/render/vulkan/VulkanRHI.h"
#include "SlipEngine/render/vulkan/VulkanSceneRenderer.h"
#include "SlipEngine/render/vulkan/VulkanBuffer.h"
#include "SlipEngine/render/vulkan/VulkanTexture.h"
#include "SlipEngine/render/SlipMesh.h"
#include "SlipEngine/render/SlipMaterial.h"
#include "SlipEngine/render/SlipPrimitive.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipComponent.h"

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <volk.h>

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

int main() {
    std::cout << "[SlipTestVulkanScene] Iniciando prueba headless de escena Vulkan (G-Buffer + PBR + Sky)..." << std::endl;

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

    if (volkInitialize() != VK_SUCCESS) {
        std::cerr << "[FAIL] volkInitialize fallo." << std::endl;
        SlipRHI::Destroy();
        return 1;
    }
    volkLoadInstance(vkRhi->GetInstance());
    volkLoadDevice(vkRhi->GetDevice());

    VulkanContext& ctx = vkRhi->GetContext();
    VulkanSceneRenderer* renderer = vkRhi->GetSceneRenderer();

    // Build test level with cube + directional light
    SlipLevel level;
    SlipEntity* cubeEnt = new SlipEntity("TestCube");
    auto mc = cubeEnt->AddComponent<MeshComponent>();
    std::vector<Vertex> cubeVerts;
    std::vector<unsigned int> cubeIndices;
    SlipPrimitive::GenerateCube(1.6f, cubeVerts, cubeIndices);
    mc->model = new SlipMesh(cubeVerts, cubeIndices);
    mc->material = new SlipMaterial();
    mc->material->color = glm::vec3(0.95f, 0.38f, 0.16f);
    mc->material->roughness = 0.45f;
    mc->material->metallic = 0.05f;
    cubeEnt->rotation = glm::vec3(15.0f, 30.0f, 0.0f);
    level.AddEntity(cubeEnt);

    SlipEntity* sunEnt = new SlipEntity("TestSun");
    auto lc = sunEnt->AddComponent<LightComponent>(LightType::Directional);
    lc->color = glm::vec3(1.0f, 0.98f, 0.95f);
    lc->intensity = 3.2f;
    sunEnt->rotation = glm::vec3(-40.0f, 25.0f, 0.0f);
    level.AddEntity(sunEnt);

    constexpr uint32_t W = 256;
    constexpr uint32_t H = 256;

    VulkanTexture outTex;
    if (!outTex.CreateImage2D(
            &ctx, W, H, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, true))
    {
        std::cerr << "[FAIL] No se pudo crear textura offscreen de salida." << std::endl;
        SlipRHI::Destroy();
        return 1;
    }

    VulkanBuffer staging;
    const VkDeviceSize readbackBytes = static_cast<VkDeviceSize>(W) * H * 4;
    if (!staging.Create(&ctx, readbackBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, false)) {
        std::cerr << "[FAIL] No se pudo crear staging buffer para readback." << std::endl;
        outTex.Destroy(&ctx);
        SlipRHI::Destroy();
        return 1;
    }

    glm::vec3 eye(0.0f, 0.8f, 3.8f);
    glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);

    VkCommandBuffer cmd = ctx.BeginSingleTimeCommands();
    VulkanTexture::TransitionImageLayout(cmd, outTex.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

    bool ok = renderer->RenderScene(
        cmd, 0, &level, view, proj, eye,
        outTex.GetImage(), outTex.GetImageView(), W, H,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        1
    );
    if (!ok) {
        ctx.EndSingleTimeCommands(cmd);
        std::cerr << "[FAIL] VulkanSceneRenderer::RenderScene devolvio false." << std::endl;
        staging.Destroy(&ctx);
        outTex.Destroy(&ctx);
        SlipRHI::Destroy();
        return 1;
    }

    VulkanTexture::TransitionImageLayout(cmd, outTex.GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { W, H, 1 };
    vkCmdCopyImageToBuffer(cmd, outTex.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.GetBuffer(), 1, &region);
    ctx.EndSingleTimeCommands(cmd);

    const uint8_t* pixels = static_cast<const uint8_t*>(staging.GetMapped());
    if (!pixels) {
        std::cerr << "[FAIL] Staging mapped pointer es null." << std::endl;
        staging.Destroy(&ctx);
        outTex.Destroy(&ctx);
        SlipRHI::Destroy();
        return 1;
    }

    auto getPixel = [&](uint32_t x, uint32_t y) {
        size_t idx = (static_cast<size_t>(y) * W + x) * 4;
        return glm::ivec4(pixels[idx], pixels[idx + 1], pixels[idx + 2], pixels[idx + 3]);
    };

    glm::ivec4 centerPix = getPixel(W / 2, H / 2);
    glm::ivec4 cornerPix = getPixel(8, 8);

    std::cout << "[INFO] DrawCalls: " << renderer->GetLastDrawCallCount() << std::endl;
    std::cout << "[INFO] Center Pixel (Cube): RGBA(" << centerPix.r << ", " << centerPix.g << ", " << centerPix.b << ", " << centerPix.a << ")" << std::endl;
    std::cout << "[INFO] Corner Pixel (Sky):  RGBA(" << cornerPix.r << ", " << cornerPix.g << ", " << cornerPix.b << ", " << cornerPix.a << ")" << std::endl;

    bool validDrawCalls = renderer->GetLastDrawCallCount() >= 1;
    bool validCube = (centerPix.r > 20) && (centerPix.r > centerPix.b);
    bool validSky = (cornerPix.b > 20) && (cornerPix.b >= cornerPix.r);
    bool distinct = (std::abs(centerPix.r - cornerPix.r) + std::abs(centerPix.b - cornerPix.b)) > 30;
    bool hasSpirvCache = std::filesystem::exists("cache/spirv") && !std::filesystem::is_empty("cache/spirv");

    std::cout << "[INFO] SPIR-V cache populated: " << (hasSpirvCache ? "true" : "false") << std::endl;

    staging.Destroy(&ctx);
    outTex.Destroy(&ctx);
    SlipRHI::Destroy();

    if (!validDrawCalls || !validCube || !validSky || !distinct || !hasSpirvCache) {
        std::cerr << "[FAIL] Verificacion de renderizado fallida (draw=" << validDrawCalls
                  << ", cube=" << validCube << ", sky=" << validSky
                  << ", distinct=" << distinct << ", cache=" << hasSpirvCache << ")." << std::endl;
        return 1;
    }

    std::cout << "[PASS] SlipTestVulkanScene completado con exito." << std::endl;
    return 0;
}
