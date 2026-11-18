cmake_minimum_required(VERSION 3.22)

# Asegurar que vcpkg y CMake encuentren el NDK de Android
if(NOT DEFINED ENV{ANDROID_NDK_HOME})
    if(DEFINED ANDROID_NDK_HOME)
        set(ENV{ANDROID_NDK_HOME} "${ANDROID_NDK_HOME}")
    elseif(DEFINED ANDROID_NDK)
        set(ENV{ANDROID_NDK_HOME} "${ANDROID_NDK}")
    else()
        set(ENV{ANDROID_NDK_HOME} "C:/Users/rchar/AppData/Local/Android/Sdk/ndk/30.0.16248370")
    endif()
endif()
project(SlipEngineProject VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8 /bigobj /FS)
    add_compile_definitions(NOMINMAX)
endif()

# -------------------------------------------------------------
# DetecciÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â³n de Plataforma y Dependencias GrÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ficas
# -------------------------------------------------------------
if(ANDROID)
    message(STATUS "[SlipEngine] Configurando para Android (OpenGL ES 3.0 + SDL2)")
    add_compile_definitions(
        SLIP_PLATFORM_ANDROID
        GLM_ENABLE_EXPERIMENTAL
    )

    # LibrerÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â­as provistas por el NDK de Android
    set(PLATFORM_LIBS
        GLESv3      # OpenGL ES 3.0
        EGL         # Contexto de ventana EGL
        android     # Interfaz nativa del sistema y Assets
        log         # Salida a Logcat (__android_log_print)
    )

    # En Android Studio la plantilla de SDL2 suele registrar el target SDL2
    # Integracion de dependencias precompiladas de vcpkg para Android
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/arm64-android")
        set(CMAKE_FIND_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/arm64-android" ${CMAKE_FIND_ROOT_PATH})
        set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/arm64-android" ${CMAKE_PREFIX_PATH})
        set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
        set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
        set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
        list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/arm64-android/share/stb")
        list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/arm64-android/share/clipper")
    endif()
        find_package(SDL2 CONFIG REQUIRED)
else()
    message(STATUS "[SlipEngine] Configurando para Windows (Escritorio)")
    add_compile_definitions(
        SLIP_PLATFORM_WINDOWS
        GLM_ENABLE_EXPERIMENTAL
    )

    # Dependencias de PC gestionadas por vcpkg
    find_package(OpenGL REQUIRED)
    set(PLATFORM_LIBS OpenGL::GL)

    find_package(SDL2 CONFIG REQUIRED)
    find_package(glad CONFIG REQUIRED)
    find_package(assimp CONFIG REQUIRED)
    find_package(meshoptimizer CONFIG REQUIRED)
endif()

# Opción para aislar completamente el Editor y las herramientas de cocinado offline (Assimp + meshoptimizer)
# del runtime puro de Android (OpenGL ES 3.0 TBDR).
option(SLIP_ANDROID_ENABLE_EDITOR_TOOLS "Incluir herramientas de cocinado offline y Editor en Android" OFF)

# Dependencias comunes en ambas plataformas
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG QUIET)
find_package(Bullet CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
find_package(BZip2 REQUIRED)
find_package(freetype CONFIG REQUIRED)
find_package(unofficial-lua CONFIG QUIET)
if(TARGET unofficial::lua::lua)
    set(LUA_LIBRARIES unofficial::lua::lua)
    set(Lua_FOUND TRUE)
    set(LUA_FOUND TRUE)
else()
    find_package(Lua REQUIRED)
endif()
find_package(sol2 CONFIG REQUIRED)

# -------------------------------------------------------------
# CÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â³digo Fuente del NÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Âºcleo de SlipEngine
# -------------------------------------------------------------
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shadergraph/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/physics/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scripting/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/tools/*.cpp"
    # "${CMAKE_CURRENT_SOURCE_DIR}/src/tools/SlipUI.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SlipEngine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/SlipClusterBuilder.cpp"
)

# -------------------------------------------------------------
# Directorios de InclusiÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â³n Comunes
# -------------------------------------------------------------
set(SLIP_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/core
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/render
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/render/shadergraph
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/scene
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/gameplay
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/tools
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/scripting
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/editor/texteditor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/texteditor
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/editor
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/editor/panels
    ${CMAKE_CURRENT_SOURCE_DIR}/src/editor
    ${BULLET_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
)

# -------------------------------------------------------------
# Fuentes del Editor
# -------------------------------------------------------------
file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/*.cpp"
)
list(FILTER EDITOR_SOURCES EXCLUDE REGEX ".*main.*\\.cpp$")
list(FILTER EDITOR_SOURCES EXCLUDE REGEX ".*SlipClusterBuilder\\.cpp$")
list(APPEND EDITOR_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/SlipProject.cpp")
list(REMOVE_DUPLICATES EDITOR_SOURCES)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/tools/SlipModelExtract.cpp")
    list(APPEND EDITOR_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/tools/SlipModelExtract.cpp")
endif()

# =============================================================
# COMPILACION PARA ANDROID (Runtime ligero aislado de herramientas de cocinado)
# =============================================================
if(ANDROID)
    # En Android se compila el Editor completo con soporte para ImGui, Assimp, Voxelización (meshoptimizer) y OpenGL ES 3.0
    find_package(assimp CONFIG REQUIRED)
    find_package(meshoptimizer CONFIG REQUIRED)
    add_library(main SHARED 
        ${ENGINE_SOURCES}
        ${EDITOR_SOURCES}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/editor_main.cpp"
    )
    target_link_libraries(main PRIVATE
        imgui::imgui
        assimp::assimp
        meshoptimizer::meshoptimizer
    )

    target_compile_definitions(main PRIVATE SLIP_BUILD_DLL)

    target_include_directories(main PUBLIC ${SLIP_INCLUDE_DIRS})

    find_library(LIB_M m)

    target_link_libraries(main PRIVATE
        glm::glm
        SDL2::SDL2
        SDL2::SDL2main
        LinearMath
        BulletCollision
        BulletDynamics
        Freetype::Freetype
        sol2::sol2
        meshoptimizer::meshoptimizer
        ${LUA_LIBRARIES}
        ${LIB_M}
        ${PLATFORM_LIBS}
    )

else()

    # 1. LibrerÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â­a dinÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡mica del motor (SlipEngine.dll)
    add_library(SlipEngine SHARED ${ENGINE_SOURCES})

    target_compile_definitions(SlipEngine PRIVATE SLIP_BUILD_DLL)

    target_include_directories(SlipEngine PUBLIC ${SLIP_INCLUDE_DIRS})

    target_link_libraries(SlipEngine PUBLIC
        glm::glm
        SDL2::SDL2
        glad::glad
        # imgui removido de SlipEngine (exclusivo de editor)
        LinearMath
        BulletCollision
        BulletDynamics
        Freetype::Freetype
        sol2::sol2
        meshoptimizer::meshoptimizer
        ${LUA_LIBRARIES}
        ${PLATFORM_LIBS}
    )

    # 2. Ejecutable Runner de escritorio (SlipRunner.exe)
    add_executable(SlipRunner "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

    target_compile_definitions(SlipRunner PRIVATE SLIP_USE_DLL)

    target_include_directories(SlipRunner PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine
    )

    target_link_libraries(SlipRunner PRIVATE 
        SlipEngine
        SDL2::SDL2
        SDL2::SDL2main
    )

    # 3. Ejecutable del Editor (SlipEditorApp.exe)

    add_executable(SlipEditorApp 
        ${EDITOR_SOURCES}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/editor_main.cpp"
    )

    target_compile_definitions(SlipEditorApp PRIVATE SLIP_USE_DLL)

    target_include_directories(SlipEditorApp PRIVATE 
        ${SLIP_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/editor
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/editor/panels
    ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/panels
    )

    target_link_libraries(SlipEditorApp PRIVATE 
        SlipEngine
        SDL2::SDL2
        SDL2::SDL2main
        glad::glad
        imgui::imgui
        assimp::assimp
        meshoptimizer::meshoptimizer
    )

    # 4. Suite de Pruebas de Lua e Intellisense
    add_executable(SlipTestLua 
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_lua.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/texteditor/SlipScriptIndexer.cpp"
    )
    target_compile_definitions(SlipTestLua PRIVATE SLIP_USE_DLL)
    target_include_directories(SlipTestLua PRIVATE ${SLIP_INCLUDE_DIRS})
    target_link_libraries(SlipTestLua PRIVATE 
        SlipEngine 
        SDL2::SDL2 
        SDL2::SDL2main
        assimp::assimp
    )

    # 5. Suite de Pruebas de Renderizado, BVH, Culling y V-Clust
    add_executable(SlipTestRenderOpt 
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_render_opt.cpp"
    )
    target_compile_definitions(SlipTestRenderOpt PRIVATE SLIP_USE_DLL)
    target_include_directories(SlipTestRenderOpt PRIVATE ${SLIP_INCLUDE_DIRS})
    target_link_libraries(SlipTestRenderOpt PRIVATE 
        SlipEngine 
        SDL2::SDL2 
        SDL2::SDL2main
        meshoptimizer::meshoptimizer
    )

    # 6. Suite de Pruebas de Terreno V-Clust y Colisiones Bullet
    add_executable(SlipTestTerrain
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_terrain.cpp"
    )
    target_compile_definitions(SlipTestTerrain PRIVATE SLIP_USE_DLL)
    target_include_directories(SlipTestTerrain PRIVATE ${SLIP_INCLUDE_DIRS})
    target_link_libraries(SlipTestTerrain PRIVATE 
        SlipEngine 
        SDL2::SDL2 
        SDL2::SDL2main
        glad::glad
    )

    # 7. Suite de Pruebas de Shader Graph (Tiers 1-4)
    add_executable(SlipTestShaderGraph
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shader_graph.cpp"
    )
    target_compile_definitions(SlipTestShaderGraph PRIVATE SLIP_USE_DLL)
    target_include_directories(SlipTestShaderGraph PRIVATE ${SLIP_INCLUDE_DIRS})
    target_link_libraries(SlipTestShaderGraph PRIVATE 
        SlipEngine 
        SDL2::SDL2 
        SDL2::SDL2main
        glad::glad
    )

    # 8. Suite de Pruebas Empíricas de Topología de Shader Graph (Challenger)
    add_executable(SlipTestTopologyChallenger
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_topology_challenger.cpp"
    )
    target_compile_definitions(SlipTestTopologyChallenger PRIVATE SLIP_USE_DLL)
    target_include_directories(SlipTestTopologyChallenger PRIVATE ${SLIP_INCLUDE_DIRS})
    target_link_libraries(SlipTestTopologyChallenger PRIVATE 
        SlipEngine 
        SDL2::SDL2 
        SDL2::SDL2main
        glad::glad
    )

    # 9. Suite de Pruebas de Estrés y Serialización de Shader Graph (Challenger M1_1)
    add_executable(SlipTestShaderGraphStress
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shader_graph_stress.cpp"
    )
    target_compile_definitions(SlipTestShaderGraphStress PRIVATE SLIP_USE_DLL)
    target_include_directories(SlipTestShaderGraphStress PRIVATE ${SLIP_INCLUDE_DIRS})
    target_link_libraries(SlipTestShaderGraphStress PRIVATE 
        SlipEngine 
        SDL2::SDL2 
        SDL2::SDL2main
        glad::glad
    )

    # 10. Suite de Pruebas Empíricas de Compilación GLSL y Material Dinámico (Challenger M2_1)
    add_executable(SlipTestGLSLChallenger
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_glsl_challenger_main.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_glsl_challenger_compiler.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_glsl_challenger_material.cpp"
    )
    target_compile_definitions(SlipTestGLSLChallenger PRIVATE SLIP_USE_DLL)
    target_include_directories(SlipTestGLSLChallenger PRIVATE ${SLIP_INCLUDE_DIRS})
    target_link_libraries(SlipTestGLSLChallenger PRIVATE 
        SlipEngine 
        SDL2::SDL2 
        SDL2::SDL2main
        glad::glad
    )

    # 11. Suite de Pruebas de Estrés Empírico Milestone 2 (Challenger M2_2)
    add_executable(SlipTestM2Challenger
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_m2_empirical_challenger.cpp"
    )
    target_compile_definitions(SlipTestM2Challenger PRIVATE SLIP_USE_DLL)
    target_include_directories(SlipTestM2Challenger PRIVATE ${SLIP_INCLUDE_DIRS})
    target_link_libraries(SlipTestM2Challenger PRIVATE 
        SlipEngine 
        SDL2::SDL2 
        SDL2::SDL2main
        glad::glad
    )

    # 12. Suite de Pruebas de Miniaturas, Iconos y Flujo de Creación de Archivos
    add_executable(SlipTestThumbnails
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_thumbnails_and_creation.cpp"
        ${EDITOR_SOURCES}
    )
    target_compile_definitions(SlipTestThumbnails PRIVATE SLIP_USE_DLL)
    target_include_directories(SlipTestThumbnails PRIVATE 
        ${SLIP_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/editor
        ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/editor/panels
        ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/panels
    )
    target_link_libraries(SlipTestThumbnails PRIVATE 
        SlipEngine 
        SDL2::SDL2 
        SDL2::SDL2main
        glad::glad
        imgui::imgui
        assimp::assimp
        meshoptimizer::meshoptimizer
    )

    # 13. Suite de Pruebas de Clustered Lighting y Bakeo (1,000+ Luces)
    add_executable(SlipTestClusteredLighting
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_clustered_lighting.cpp"
    )
    target_compile_definitions(SlipTestClusteredLighting PRIVATE SLIP_USE_DLL)
    target_include_directories(SlipTestClusteredLighting PRIVATE ${SLIP_INCLUDE_DIRS})
    target_link_libraries(SlipTestClusteredLighting PRIVATE 
        SlipEngine 
        SDL2::SDL2 
        SDL2::SDL2main
        glad::glad
    )

endif()



