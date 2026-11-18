# BRIEFING — 2026-11-08T14:49:00Z

## Mission
Design the runtime integration of Shader Graph into `SlipMaterial` including asset referencing, parameter overrides, compilation in init(), uniform binding with custom texture units 20+, and serialization.

## 🔒 My Identity
- Archetype: teamwork_explorer
- Roles: explorer, analyst, designer
- Working directory: c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_2\
- Original parent: af9ad2dc-4359-4d00-baf7-c2106d2d2f37
- Milestone: M2 (Shader Graph Runtime & Material Integration)

## 🔒 Key Constraints
- Read-only investigation — do NOT implement directly in source code
- Files for content delivery, Messages for coordination
- Keep .agents/teamwork/ free of source code/tests/data (metadata only)
- Produce complete code diffs / design in analysis.md and handoff.md

## Current Parent
- Conversation ID: af9ad2dc-4359-4d00-baf7-c2106d2d2f37
- Updated: 2026-11-08T14:49:00Z

## Investigation State
- **Explored paths**:
  - `include/SlipEngine/render/SlipMaterial.h` & `src/render/SlipMaterial.cpp`
  - `include/SlipEngine/render/shadergraph/ShaderGraphTypes.h` & `ShaderGraph.h` & `ShaderGraphSerializer.h`
  - `src/render/shadergraph/ShaderGraphSerializer.cpp`
  - `include/SlipEngine/render/SlipShader.h` & `src/render/SlipShader.cpp`
  - `include/SlipEngine/render/SlipShaderLibrary.h` & `src/render/SlipShaderLibrary.cpp`
  - `include/SlipEngine/render/SlipTexture.h` & `src/render/SlipTexture.cpp`
  - `src/render/SlipRenderQueue.cpp` & `src/editor/panels/ViewportPanel.cpp`
  - `tests/test_shader_graph.cpp` (Tier 1-4 tests, `TestShaderMaterial` mock specifications)
- **Key findings**:
  - `SlipShader::CreateFromSource` compiles GL vertex and fragment source and returns `new SlipShader*` with allocated program ID.
  - Using `std::shared_ptr<SlipShader> m_CustomShader` with custom deleter `[](SlipShader* s) { s->destroy(); delete s; }` inside `SlipMaterial` avoids memory leaks, eliminates double-free on material copies, and keeps `SlipMaterial::shader` valid.
  - Engine reserves texture units 0..19 (0: Albedo, 1: Normal, 2: Roughness, 3: CSM shadow array, 4: Metallic, 5..14: DDGI probes, 15: AO, 16: Emissive, 17: Opacity, 18..19: reflections/env).
  - Custom texture parameters correctly allocate starting at texture unit 20 (up to 31).
  - `SlipRenderQueue.cpp:57-63` currently looks up shaders only via `SlipShaderLibrary::Get(sType)`. It must be patched to check `(cmd.material && cmd.material->shader) ? cmd.material->shader : ...` so graph materials execute their compiled shaders during viewport rendering.
  - Test suite in `tests/test_shader_graph.cpp` validates `.mat` files with `shaderGraph:`, `graphFloat_<name>`, `graphVector_<name>`, `graphTexture_<name>`, while dispatch also requests `graphParam.<name>`. Deserializer must handle all variants seamlessly.
- **Unexplored areas**: None for SlipMaterial integration scope.

## Key Decisions Made
- Use `std::shared_ptr<SlipShader>` for custom graph shader lifecycle with automated OpenGL program cleanup on destruction or graph re-assignment.
- Cache `std::vector<ShaderProperty> graphProperties` in `SlipMaterial` upon graph loading for fast blackboard inspection in ImGui without repeated disk reads.
- Support both `graphFloat_` / `graphVector_` / `graphTexture_` and `graphParam.<name>` / `graphParam.<type>.<name>` in `.mat` serialization and deserialization.
- Flag `SlipRenderQueue.cpp` dispatch fix as an essential cross-cutting recommendation for the M2 implementer.

## Artifact Index
- c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_2\DISPATCH.md — Task dispatches
- c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_2\BRIEFING.md — Persistent context & identity
- c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_2\progress.md — Heartbeat and progress log
- c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_2\analysis.md — Detailed analysis and code diffs
- c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_2\handoff.md — 5-component handoff report
