# Worker M1 Dispatch: Core Model, Node Library & Asset Serialization

- Identity: `worker_m1` (`teamwork_worker`)
- Working directory: `c:\Projects\SlipEngine\.agents\teamwork\worker_m1\`
- Parent: `af9ad2dc-4359-4d00-baf7-c2106d2d2f37`
- Reference Files:
  - `c:\Projects\SlipEngine\.agents\teamwork\ORIGINAL_REQUEST.md`
  - `c:\Projects\SlipEngine\PROJECT.md`
  - `c:\Projects\SlipEngine\.agents\teamwork\explorer_m1_1\analysis.md` (Node model & library design)
  - `c:\Projects\SlipEngine\.agents\teamwork\explorer_m1_2\analysis.md` (Serializer & asset pipeline design)
  - `c:\Projects\SlipEngine\.agents\teamwork\explorer_m1_3\analysis.md` (CMake & build verification)

## MANDATORY INTEGRITY WARNING
> DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Files Owned Exclusively
- `include/SlipEngine/render/shadergraph/ShaderGraphTypes.h`
- `include/SlipEngine/render/shadergraph/ShaderNode.h`
- `include/SlipEngine/render/shadergraph/ShaderGraph.h`
- `include/SlipEngine/render/shadergraph/ShaderGraphSerializer.h`
- `src/render/shadergraph/ShaderNode.cpp`
- `src/render/shadergraph/ShaderGraph.cpp`
- `src/render/shadergraph/ShaderGraphSerializer.cpp`
- `src/editor/SlipModelExtract.cpp` (add `.shadergraph` -> `"ShaderGraph"`)
- `CMakeLists.txt` (add shadergraph sources to `ENGINE_SOURCES` and include dir to `SLIP_INCLUDE_DIRS`)

## Implementation Requirements
1. Implement `ShaderGraphTypes.h` with `PinType`, `ShaderGraphType` (Lit, Unlit), `ShaderProperty`, `ShaderPort`, `ShaderLink`, and port compatibility rules.
2. Implement `ShaderNode.h` and `ShaderNode.cpp` with `ShaderNode` base class, `ShaderNodeRegistry`, and full node palette:
   - Math: Add, Subtract, Multiply, Divide, Sin, Cos, Tan, Dot, Cross, Normalize, Length, Distance.
   - Procedural: Min, Max, Clamp, Lerp, Abs, Step, Smoothstep, Remap, Procedural Noise, Voronoi.
   - Time: Time, DeltaTime.
   - Camera/Transform: CameraPosition, ViewMatrix, ProjectionMatrix, WorldPosition, WorldNormal.
   - Texture: Texture2D Sampler, UV Coordinates, Tiling and Offset.
   - Vector/Constants: VectorSplit, VectorCombine, Constant nodes.
   - Master Output Node: Dynamic port reconfiguration for Lit (8 PBR inputs) vs. Unlit (4 inputs).
3. Implement `ShaderGraph.h` and `ShaderGraph.cpp` with node collection, link management, Blackboard public parameters, and cycle validation.
4. Implement `ShaderGraphSerializer.h` and `ShaderGraphSerializer.cpp` using RapidJSON PrettyWriter for lossless round-trip save/load of `.shadergraph` assets.
5. Update `SlipModelExtract.cpp` to recognize `.shadergraph` and assign asset type `"ShaderGraph"`.
6. Update `CMakeLists.txt` to include `src/render/shadergraph/*.cpp` and `include/SlipEngine/render/shadergraph`.
7. Build `SlipEngine` using `cmake --build build --config Debug --target SlipEngine` and ensure 0 compilation errors and 0 warnings.
8. Document all commands, file changes, and verification outcomes in `c:\Projects\SlipEngine\.agents\teamwork\worker_m1\handoff.md`.


## 2026-11-08T14:16:41Z
Received dispatch from parent `af9ad2dc-4359-4d00-baf7-c2106d2d2f37`:
You are worker_m1 (teamwork_worker).
Your working directory is: c:\Projects\SlipEngine\.agents\teamwork\worker_m1\
Your parent is: af9ad2dc-4359-4d00-baf7-c2106d2d2f37
Implement Milestone 1 across your exclusively owned files:
1. `include/SlipEngine/render/shadergraph/ShaderGraphTypes.h`
2. `include/SlipEngine/render/shadergraph/ShaderNode.h`
3. `include/SlipEngine/render/shadergraph/ShaderGraph.h`
4. `include/SlipEngine/render/shadergraph/ShaderGraphSerializer.h`
5. `src/render/shadergraph/ShaderNode.cpp`
6. `src/render/shadergraph/ShaderGraph.cpp`
7. `src/render/shadergraph/ShaderGraphSerializer.cpp`
8. `src/editor/SlipModelExtract.cpp` (add .shadergraph -> "ShaderGraph")
9. `CMakeLists.txt` (add shadergraph sources and include directories)
Run CMake build: `cmake --build build --config Debug --target SlipEngine` (0 errors, 0 warnings).
Document in handoff.md, progress.md.
