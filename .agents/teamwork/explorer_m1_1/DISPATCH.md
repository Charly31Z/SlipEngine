# M1 Explorer 1: Shader Graph Model & Node Library Architecture

- Working Directory: `c:\Projects\SlipEngine\.agents\teamwork\explorer_m1_1\`
- Parent: `af9ad2dc-4359-4d00-baf7-c2106d2d2f37`
- Reference Files:
  - `c:\Projects\SlipEngine\.agents\teamwork\ORIGINAL_REQUEST.md`
  - `c:\Projects\SlipEngine\PROJECT.md`

## Mission
Design the C++ data structures and class hierarchies for Milestone 1:
1. `include/SlipEngine/render/shadergraph/ShaderGraphTypes.h`:
   - `PinType`: Float, Vec2, Vec3, Vec4, Texture2D, etc.
   - `ShaderGraphType`: Lit, Unlit.
   - `ShaderProperty`: id, name, type, defaultValue (`glm::vec4`), defaultTexturePath.
   - `ShaderPort`: id, name, type, isOutput, defaultValue (`glm::vec4`).
   - `ShaderLink`: id, fromNodeId, fromPortId, toNodeId, toPortId.
2. `include/SlipEngine/render/shadergraph/ShaderNode.h` and `src/render/shadergraph/ShaderNode.cpp`:
   - Abstract `ShaderNode` base class with position, id, title, type, inputs, outputs.
   - Node registry / factory for:
     - Math: Add, Subtract, Multiply, Divide, Sin, Cos, Tan, Dot, Cross, Normalize, Length, Distance.
     - Procedural: Min, Max, Clamp, Lerp, Abs, Step, Smoothstep, Remap, Procedural Noise/Voronoi.
     - Time: Time, DeltaTime.
     - Camera/Transform: CameraPosition, ViewMatrix, ProjectionMatrix, WorldPosition, WorldNormal.
     - Texture: Texture2D Sampler, UV Coordinates, Tiling and Offset.
     - Master Output Node: Lit (PBR: Albedo, Normal, Roughness, Metallic, Emissive, AO, Alpha) vs Unlit (Color, Emissive, Alpha).
3. Write detailed recommendations and code skeletons to `c:\Projects\SlipEngine\.agents\teamwork\explorer_m1_1\analysis.md` and complete `handoff.md`.

## 2026-11-08T14:07:14Z
You are explorer_m1_1 (Shader Graph Model & Node Library Explorer).
Your working directory is: c:\Projects\SlipEngine\.agents\teamwork\explorer_m1_1\
Read:
- c:\Projects\SlipEngine\.agents\teamwork\ORIGINAL_REQUEST.md
- c:\Projects\SlipEngine\PROJECT.md
- c:\Projects\SlipEngine\.agents\teamwork\explorer_m1_1\DISPATCH.md

Investigate and design:
1. `include/SlipEngine/render/shadergraph/ShaderGraphTypes.h`
2. `include/SlipEngine/render/shadergraph/ShaderNode.h` and `src/render/shadergraph/ShaderNode.cpp`
3. Node registry covering Math, Procedural, Time, Camera/Transform, Texture, and Master Output Node (Lit vs. Unlit).
Write your findings to analysis.md and complete handoff.md in your working directory. Send a message to parent af9ad2dc-4359-4d00-baf7-c2106d2d2f37 when finished.
