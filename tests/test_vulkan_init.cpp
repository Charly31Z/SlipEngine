#include "SlipEngine/render/rhi/SlipRHI.h"
#include "SlipEngine/render/vulkan/VulkanRHI.h"
#include "SlipEngine/render/vulkan/SlipSpirvCompiler.h"

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <volk.h>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

int main() {
    std::cout << "[SlipTestVulkanInit] Iniciando prueba headless de Vulkan RHI + SPIR-V..." << std::endl;

    ISlipRHI* rhi = SlipRHI::Create(GraphicsAPI::Vulkan, nullptr, true);
    if (!rhi) {
        std::cerr << "[FAIL] SlipRHI::Create(Vulkan, headless=true) devolvio nullptr." << std::endl;
        return 1;
    }

    if (rhi->GetAPI() != GraphicsAPI::Vulkan) {
        std::cerr << "[FAIL] GetAPI() no devolvio GraphicsAPI::Vulkan." << std::endl;
        SlipRHI::Destroy();
        return 1;
    }

    const RHIHardwareCapabilities& caps = rhi->GetCapabilities();
    std::cout << "[INFO] GPU: " << caps.deviceName << std::endl;
    std::cout << "[INFO] Vulkan API: " << caps.apiVersionMajor << "." << caps.apiVersionMinor << "." << caps.apiVersionPatch << std::endl;
    std::cout << "[INFO] supportsRayTracing=" << (caps.supportsRayTracing ? "true" : "false") << std::endl;
    std::cout << "[INFO] supportsRayQuery=" << (caps.supportsRayQuery ? "true" : "false") << std::endl;
    std::cout << "[INFO] supportsDynamicRendering=" << (caps.supportsDynamicRendering ? "true" : "false") << std::endl;

    if (caps.deviceName.empty() || caps.apiVersionMajor < 1 || (caps.apiVersionMajor == 1 && caps.apiVersionMinor < 3)) {
        std::cerr << "[FAIL] Capacidades de Vulkan invalidas o inferiores a 1.3." << std::endl;
        SlipRHI::Destroy();
        return 1;
    }

    if (!caps.supportsRayTracing || !caps.supportsRayQuery) {
        std::cerr << "[FAIL] Se esperaba soporte de Hardware Ray Tracing / VK_KHR_ray_query." << std::endl;
        SlipRHI::Destroy();
        return 1;
    }

    const std::string cacheDir = "cache/spirv_test";
    std::error_code ec;
    std::filesystem::remove_all(cacheDir, ec);

    const std::string computeGlsl =
        "#version 460\n"
        "#extension GL_EXT_ray_query : require\n"
        "layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n"
        "void main() {\n"
        "    uint idx = gl_GlobalInvocationID.x;\n"
        "    if (idx == 0u) { }\n"
        "}\n";

    SpirvCompileResult compile1 = SlipSpirvCompiler::CompileGLSL(
        computeGlsl, SpirvShaderStage::Compute, "test_rq.comp", true, cacheDir);
    if (!compile1.success || compile1.spirv.empty() || compile1.spirv[0] != 0x07230203u || compile1.loadedFromCache) {
        std::cerr << "[FAIL] Compilacion inicial SPIR-V fallo: " << compile1.errorLog << std::endl;
        SlipRHI::Destroy();
        return 1;
    }

    SpirvCompileResult compile2 = SlipSpirvCompiler::CompileGLSL(
        computeGlsl, SpirvShaderStage::Compute, "test_rq.comp", true, cacheDir);
    if (!compile2.success || !compile2.loadedFromCache || compile2.spirv != compile1.spirv) {
        std::cerr << "[FAIL] Cache SPIR-V en disco fallo en segunda compilacion." << std::endl;
        SlipRHI::Destroy();
        return 1;
    }
    std::cout << "[INFO] SPIR-V compilado (" << compile1.spirv.size() * 4 << " bytes) y cacheado en " << compile1.cachePath << std::endl;

    VulkanRHI* vkRhi = dynamic_cast<VulkanRHI*>(rhi);
    if (!vkRhi || vkRhi->GetDevice() == VK_NULL_HANDLE) {
        std::cerr << "[FAIL] VulkanRHI o VkDevice invalido." << std::endl;
        SlipRHI::Destroy();
        return 1;
    }

    if (volkInitialize() != VK_SUCCESS) {
        std::cerr << "[FAIL] volkInitialize() en test fallo." << std::endl;
        SlipRHI::Destroy();
        return 1;
    }
    volkLoadInstance(vkRhi->GetInstance());
    volkLoadDevice(vkRhi->GetDevice());

    VkShaderModuleCreateInfo smInfo{};
    smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smInfo.codeSize = compile1.spirv.size() * sizeof(uint32_t);
    smInfo.pCode = compile1.spirv.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vkRhi->GetDevice(), &smInfo, nullptr, &shaderModule) != VK_SUCCESS || shaderModule == VK_NULL_HANDLE) {
        std::cerr << "[FAIL] vkCreateShaderModule fallo con el SPIR-V generado." << std::endl;
        SlipRHI::Destroy();
        return 1;
    }
    vkDestroyShaderModule(vkRhi->GetDevice(), shaderModule, nullptr);

    std::filesystem::remove_all(cacheDir, ec);
    SlipRHI::Destroy();
    std::cout << "[PASS] SlipTestVulkanInit completado con exito." << std::endl;
    return 0;
}
