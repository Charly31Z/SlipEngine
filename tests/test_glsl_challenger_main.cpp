#include <glad/glad.h>
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "SlipEngine/SlipEngine.h"
#include "test_glsl_challenger.h"
#include <iostream>

ChallengerStats g_GlobalChallengerStats;

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    std::cout << "================================================================================" << std::endl;
    std::cout << "   SlipEngine Milestone 2 EMPIRICAL CHALLENGER STRESS SUITE (OpenGL GLSL)       " << std::endl;
    std::cout << "================================================================================" << std::endl;

    SlipEngine engine;
    if (!engine.Init("SlipGLSLChallengerContext", 128, 128)) {
        std::cerr << "[CRITICAL ERROR] Failed to initialize SlipEngine" << std::endl;
        return 1;
    }
    gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);

    std::cout << "[INFO] Headless OpenGL Context loaded successfully." << std::endl;
    std::cout << "[INFO] Vendor:   " << glGetString(GL_VENDOR) << std::endl;
    std::cout << "[INFO] Renderer: " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "[INFO] GLSL:     " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
    std::cout << "--------------------------------------------------------------------------------\n" << std::endl;

    RunGLSLCompilerChallenges();
    RunSlipMaterialChallenges();

    std::cout << "\n================================================================================" << std::endl;
    std::cout << "                  CHALLENGER STRESS SUITE EXECUTION SUMMARY                     " << std::endl;
    std::cout << "================================================================================" << std::endl;
    std::cout << "  TOTAL CHALLENGES: " << g_GlobalChallengerStats.total << std::endl;
    std::cout << "  PASSED:           " << g_GlobalChallengerStats.passed << std::endl;
    std::cout << "  FAILED:           " << g_GlobalChallengerStats.failed << std::endl;
    std::cout << "================================================================================" << std::endl;

    engine.Shutdown();

    if (g_GlobalChallengerStats.failed == 0) {
        std::cout << "\n[SUCCESS] ALL " << g_GlobalChallengerStats.total << " EMPIRICAL CHALLENGES PASSED CLEANLY!\n" << std::endl;
        return 0;
    } else {
        std::cerr << "\n[FAILURE] " << g_GlobalChallengerStats.failed << " challenge(s) failed out of " << g_GlobalChallengerStats.total << "\n" << std::endl;
        return 1;
    }
}
