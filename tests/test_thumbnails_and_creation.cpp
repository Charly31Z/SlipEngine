#include <glad/glad.h>
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "SlipEngine/SlipEngine.h"
#include "SlipEngine/editor/SlipThumbnailManager.h"
#include "SlipEngine/editor/SlipModelExtract.h"
#include "SlipEngine/editor/panels/ContentBrowserPanel.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/render/SlipMaterial.h"
#include "SlipEngine/render/SlipTexture.h"

#include <iostream>
#include <cassert>
#include <filesystem>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    std::cout << "================================================================================" << std::endl;
    std::cout << "   SlipEngine Thumbnails, Icons & Creation Flow Test Suite                      " << std::endl;
    std::cout << "================================================================================" << std::endl;

    SlipEngine engine;
    if (!engine.Init("SlipThumbnailTestContext", 128, 128)) {
        std::cerr << "[CRITICAL ERROR] Failed to initialize SlipEngine" << std::endl;
        return 1;
    }
    gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);

    int passed = 0;
    int total = 0;

    auto RunTest = [&](const char* name, std::function<bool()> testFn) {
        total++;
        std::cout << "  [TEST " << total << "] " << name << " ... ";
        try {
            if (testFn()) {
                std::cout << "[PASS]" << std::endl;
                passed++;
            } else {
                std::cout << "[FAIL]" << std::endl;
            }
        } catch (const std::exception& ex) {
            std::cout << "[EXCEPTION: " << ex.what() << "]" << std::endl;
        }
    };

    // 1. Procedural Icons
    RunTest("SlipThumbnailManager: Procedural Icons Initialization", [&]() {
        SlipThumbnailManager::Get().Init();
        bool ok = true;
        ok &= (SlipThumbnailManager::Get().GetFolderIcon() != 0);
        ok &= (SlipThumbnailManager::Get().GetScriptIcon() != 0);
        ok &= (SlipThumbnailManager::Get().GetAnimationIcon() != 0);
        ok &= (SlipThumbnailManager::Get().GetShaderGraphIcon() != 0);
        ok &= (SlipThumbnailManager::Get().GetPrefabIcon() != 0);
        ok &= (SlipThumbnailManager::Get().GetSceneIcon() != 0);
        ok &= (SlipThumbnailManager::Get().GetControllerIcon() != 0);
        ok &= (SlipThumbnailManager::Get().GetAudioIcon() != 0);
        ok &= (SlipThumbnailManager::Get().GetFileIcon() != 0);
        return ok;
    });

    // 2. Icon differentiation
    RunTest("SlipThumbnailManager: Unique Distinct Icons for File Types", [&]() {
        unsigned int folder = SlipThumbnailManager::Get().GetThumbnail("my_folder", true, "");
        unsigned int lua = SlipThumbnailManager::Get().GetThumbnail("test.lua", false, ".lua");
        unsigned int anim = SlipThumbnailManager::Get().GetThumbnail("test.anim", false, ".anim");
        unsigned int sg = SlipThumbnailManager::Get().GetThumbnail("test.shadergraph", false, ".shadergraph");
        unsigned int prefab = SlipThumbnailManager::Get().GetThumbnail("test.prefab", false, ".prefab");
        unsigned int scene = SlipThumbnailManager::Get().GetThumbnail("test.scene", false, ".scene");
        unsigned int ctrl = SlipThumbnailManager::Get().GetThumbnail("test.controller", false, ".controller");

        bool ok = true;
        ok &= (folder != lua);
        ok &= (lua != anim);
        ok &= (anim != sg);
        ok &= (sg != prefab);
        ok &= (prefab != scene);
        ok &= (scene != ctrl);
        return ok;
    });

    // 3. Texture Thumbnail PNG generation
    RunTest("SlipThumbnailManager: Texture Thumbnail Downscaling & PNG Generation", [&]() {
        std::string testDir = "tests_temp_thumb";
        SlipFileSystem::CreateDirectoryPath(testDir);
        std::string testImgPath = SlipFileSystem::CombinePath(testDir, "test_raw_tex.png");

        // Create a 256x256 test image
        std::vector<unsigned char> rawImg(256 * 256 * 4, 180);
        for (int y = 0; y < 256; ++y) {
            for (int x = 0; x < 256; ++x) {
                int idx = (y * 256 + x) * 4;
                rawImg[idx] = (unsigned char)x;
                rawImg[idx + 1] = (unsigned char)y;
                rawImg[idx + 2] = 120;
                rawImg[idx + 3] = 255;
            }
        }
        stbi_write_png(testImgPath.c_str(), 256, 256, 4, rawImg.data(), 256 * 4);

        // Generate thumbnail
        bool genOk = SlipThumbnailManager::Get().GenerateTextureThumbnail(testImgPath, true);
        if (!genOk) return false;

        std::string thumbPng = SlipThumbnailManager::Get().GetThumbnailPngPath(testImgPath);
        bool fileExists = SlipFileSystem::Exists(thumbPng);

        unsigned int thumbTex = SlipThumbnailManager::Get().GetThumbnail(testImgPath, false, ".png");
        bool texValid = (thumbTex != 0 && thumbTex != SlipThumbnailManager::Get().GetFileIcon());

        return fileExists && texValid;
    });

    // 4. Material Thumbnail PNG generation
    RunTest("SlipThumbnailManager: Material 3D Offscreen Render & PNG Generation", [&]() {
        std::string testDir = "tests_temp_thumb";
        SlipFileSystem::CreateDirectoryPath(testDir);
        std::string testMatPath = SlipFileSystem::CombinePath(testDir, "TestMatThumb.mat");

        SlipMaterial mat;
        mat.color = glm::vec3(0.9f, 0.2f, 0.2f);
        mat.metallic = 0.8f;
        mat.roughness = 0.2f;
        mat.Save(testMatPath);

        bool genOk = SlipThumbnailManager::Get().GenerateMaterialThumbnail(testMatPath, true);
        if (!genOk) return false;

        std::string thumbPng = SlipThumbnailManager::Get().GetThumbnailPngPath(testMatPath);
        bool fileExists = SlipFileSystem::Exists(thumbPng);

        unsigned int thumbTex = SlipThumbnailManager::Get().GetThumbnail(testMatPath, false, ".mat");
        bool texValid = (thumbTex != 0 && thumbTex != SlipThumbnailManager::Get().GetFileIcon());

        return fileExists && texValid;
    });

    // 5. Creation Modal Flow: Material Name Before File Generation & .meta correctness
    RunTest("Creation Flow: Material Name Pre-Creation & .meta originalFile Check", [&]() {
        std::string testDir = "assets/tests_temp_create";
        SlipFileSystem::CreateDirectoryPath(testDir);

        ContentBrowserPanel panel;
        panel.SetCurrentDirPath(testDir);

        // Verify that before confirmation, no default "NuevoMaterial.mat" exists on disk!
        std::string defaultPath = SlipFileSystem::CombinePath(testDir, "NuevoMaterial.mat");
        if (SlipFileSystem::Exists(defaultPath)) {
            SlipFileSystem::DeleteFileOrDir(defaultPath);
        }

        // Simulate user naming the material "CustomPlayerMaterial" in the modal
        std::string chosenName = "CustomPlayerMaterial";
        std::string targetMatPath = SlipFileSystem::CombinePath(testDir, chosenName + ".mat");
        if (SlipFileSystem::Exists(targetMatPath)) SlipFileSystem::DeleteFileOrDir(targetMatPath);
        if (SlipFileSystem::Exists(targetMatPath + ".meta")) SlipFileSystem::DeleteFileOrDir(targetMatPath + ".meta");

        std::string createdPath = panel.CreateMaterialAsset(chosenName);
        assert(SlipFileSystem::Exists(createdPath));

        // Check that .meta has originalFile == CustomPlayerMaterial.mat (and NOT a default name!)
        AssetMetadata meta;
        bool hasMeta = SlipModelExtract::LoadMeta(createdPath + ".meta", meta);
        bool correctName = (meta.originalFile == chosenName + ".mat");

        return hasMeta && correctName;
    });

    // 6. Rename Flow: Updates .meta originalFile correctly
    RunTest("Rename Flow: Renaming file updates .meta originalFile", [&]() {
        std::string testDir = "assets/tests_temp_create";
        std::string oldPath = SlipFileSystem::CombinePath(testDir, "CustomPlayerMaterial.mat");
        std::string newName = "RenamedPlayerMaterial.mat";
        std::string newPath = SlipFileSystem::CombinePath(testDir, newName);

        if (SlipFileSystem::Exists(newPath)) SlipFileSystem::DeleteFileOrDir(newPath);
        if (SlipFileSystem::Exists(newPath + ".meta")) SlipFileSystem::DeleteFileOrDir(newPath + ".meta");

        ContentBrowserPanel panel;
        panel.SetCurrentDirPath(testDir);
        panel.RenameItem(oldPath, newName);

        bool newExists = SlipFileSystem::Exists(newPath);
        AssetMetadata meta;
        bool hasMeta = SlipModelExtract::LoadMeta(newPath + ".meta", meta);
        bool updatedName = (meta.originalFile == newName);

        return newExists && hasMeta && updatedName;
    });

    // 7. Folder Creation: requires folder name before creation
    RunTest("Creation Flow: Folder Creation with specific name", [&]() {
        std::string testDir = "assets/tests_temp_create";
        std::string folderName = "MiCarpetaEspecial";
        std::string folderPath = SlipFileSystem::CombinePath(testDir, folderName);

        if (SlipFileSystem::Exists(folderPath)) SlipFileSystem::DeleteFileOrDir(folderPath);

        ContentBrowserPanel panel;
        panel.SetCurrentDirPath(testDir);
        panel.CreateNewFolder(folderName);

        bool folderExists = SlipFileSystem::Exists(folderPath) && SlipFileSystem::IsDirectory(folderPath);
        return folderExists;
    });

    // Cleanup temp dirs
    std::error_code ec;
    fs::remove_all("tests_temp_thumb", ec);
    fs::remove_all("assets/tests_temp_create", ec);

    std::cout << "\n================================================================================" << std::endl;
    std::cout << "  TOTAL TESTS: " << total << std::endl;
    std::cout << "  PASSED:      " << passed << std::endl;
    std::cout << "  FAILED:      " << (total - passed) << std::endl;
    std::cout << "================================================================================" << std::endl;

    engine.Shutdown();

    if (passed == total) {
        std::cout << "\n[SUCCESS] ALL " << total << " THUMBNAIL & CREATION TESTS PASSED!\n" << std::endl;
        return 0;
    } else {
        std::cerr << "\n[FAILURE] " << (total - passed) << " test(s) failed!\n" << std::endl;
        return 1;
    }
}
