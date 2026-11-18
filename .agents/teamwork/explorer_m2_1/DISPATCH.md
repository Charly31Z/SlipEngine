# M2 Explorer 1: GLSL Code Generator Architecture

- Identity: `explorer_m2_1` (`teamwork_explorer`)
- Working directory: `c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_1\`
- Parent: `af9ad2dc-4359-4d00-baf7-c2106d2d2f37`
- Reference Files:
  - `c:\Projects\SlipEngine\.agents\teamwork\ORIGINAL_REQUEST.md`
  - `c:\Projects\SlipEngine\PROJECT.md`
  - `c:\Projects\SlipEngine\include\SlipEngine\render\shadergraph\ShaderGraph.h`
  - `c:\Projects\SlipEngine\include\SlipEngine\render\shadergraph\ShaderNode.h`
  - `c:\Projects\SlipEngine\src\render\SlipShaderLibrary.cpp` (lines 180-550 for `s_LitFrag`, lines 712-750 for `s_UnlitFrag`)

## Mission
Design `ShaderGraphCompiler` (`include/SlipEngine/render/shadergraph/ShaderGraphCompiler.h` and `src/render/shadergraph/ShaderGraphCompiler.cpp`):
1. Topological DAG traversal starting from `MasterNode` backwards to collect dependencies in execution order.
2. Code emission for:
   - Uniform declarations for exposed blackboard properties (`uniform float u_...;`, `uniform vec4 u_...;`, `uniform sampler2D u_...;`).
   - Time uniforms (`uniform float u_Time;`, `uniform float u_DeltaTime;`).
   - Camera uniforms (`uniform vec3 u_CameraPos;`, `uniform mat4 u_View;`, `uniform mat4 u_Projection;`).
   - Vertex shader emission: standard vertex inputs, matrices, and normal/tangent transformation, or custom vertex displacement if requested.
   - Fragment shader emission:
     - In **Lit mode**: evaluate surface attributes (albedo, normal, roughness, metallic, emissive, AO, alpha) and invoke SlipEngine's standard Cook-Torrance GGX/Smith PBR lighting calculations with cascaded shadows, point lights, spot lights, DDGI, and reflection probes.
     - In **Unlit mode**: evaluate color, emissive, alpha, and write directly to `FragColor`.
3. Provide complete C++ class skeletons and GLSL templates in `c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_1\analysis.md` and complete `handoff.md`.

## 2026-11-08T14:42:20Z
[Message] timestamp=2026-11-08T14:42:20Z sender=af9ad2dc-4359-4d00-baf7-c2106d2d2f37 priority=MESSAGE_PRIORITY_HIGH content=You are explorer_m2_1 (GLSL Code Generator Explorer).
Your working directory is: c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_1\
Read:
- c:\Projects\SlipEngine\.agents\teamwork\ORIGINAL_REQUEST.md
- c:\Projects\SlipEngine\PROJECT.md
- c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_1\DISPATCH.md
- c:\Projects\SlipEngine\include\SlipEngine\render\shadergraph\ShaderGraph.h
- c:\Projects\SlipEngine\include\SlipEngine\render\shadergraph\ShaderNode.h
- c:\Projects\SlipEngine\src\render\SlipShaderLibrary.cpp

Design `ShaderGraphCompiler` for topological GLSL generation:
- Vertex shader generation.
- Fragment shader generation for Lit (PBR surface properties + SlipEngine Cook-Torrance lighting loop) and Unlit (direct color/emissive).
- Uniform declarations for exposed blackboard properties, time variables, and camera matrices.
Write detailed C++ class designs and GLSL template code to analysis.md and complete handoff.md in your working directory. Send a message to parent af9ad2dc-4359-4d00-baf7-c2106d2d2f37 when finished.
