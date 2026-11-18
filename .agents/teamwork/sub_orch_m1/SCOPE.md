# Scope: Milestone 1 — Core Model, Node Library & Serialization

## Objective
Implement the foundational data model, comprehensive node library, property declaration, asset serialization, and asset pipeline integration for SlipEngine Shader Graph.

## Features Owned
- **F01**: Graph Data Structures & Node Representation (ports, pins, types, node links, graph container)
- **F02**: Comprehensive Node Library — Math (Add, Subtract, Multiply, Divide, Sin, Cos, Tan, Dot, Cross, Normalize, Length, Distance)
- **F03**: Procedural & Utility Nodes (Min, Max, Clamp, Lerp, Abs, Step, Smoothstep, Remap, Procedural Noise/Voronoi)
- **F04**: Time & Animation Nodes (Time, DeltaTime)
- **F05**: Camera & Transform Nodes (CameraPos, ViewMatrix, ProjMatrix, WorldPos, WorldNormal)
- **F06**: Texture Sampling Nodes (Texture2D, UV Coordinates, Tiling and Offset)
- **F07**: Master Output Node with Lit vs. Unlit material type switching (Lit provides PBR surface pins: albedo, normal, roughness, metallic, emissive, AO, alpha; Unlit provides direct color/emissive)
- **F08**: Dedicated `.shadergraph` asset file format serialization and deserialization (save/load round-trip with full lossless topology)
- **F09**: Engine Asset Pipeline Integration: `.shadergraph` extension recognition and `.meta` UUIDv4 handling in `SlipFileSystem` and `SlipModelExtract`
- **F13**: Public exposed properties model (declaration of float, vec2, vec3, vec4/color, and texture parameters with defaults)

## Files Owned
- `include/SlipEngine/render/shadergraph/ShaderGraphTypes.h`
- `include/SlipEngine/render/shadergraph/ShaderNode.h`
- `include/SlipEngine/render/shadergraph/ShaderGraph.h`
- `include/SlipEngine/render/shadergraph/ShaderGraphSerializer.h`
- `src/render/shadergraph/ShaderNode.cpp`
- `src/render/shadergraph/ShaderGraph.cpp`
- `src/render/shadergraph/ShaderGraphSerializer.cpp`
- `src/tools/SlipFileSystem.cpp` (asset extension handling)
- `src/editor/SlipModelExtract.cpp` (asset type deduction)
- `CMakeLists.txt` (register new source files in `SlipEngine`)

## Iteration Configuration
Sub-orchestrator executes Explorer (3) -> Worker (1) -> Reviewer (2) -> Challenger (2) -> Auditor (1) -> Gate.
Build and unit test verification required before gate evaluation.
Audit is BINARY VETO.
