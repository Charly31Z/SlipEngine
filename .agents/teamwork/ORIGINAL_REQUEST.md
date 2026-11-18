# Original User Request

## Initial Request — 2026-11-08T13:55:35Z

Build and integrate a production-ready node-based material editor (Shader Graph) for SlipEngine, styled like Unity and Unreal Engine 5, enabling users to create custom animated, procedural, and mathematical shaders with selectable material types (Lit and Unlit), exposed public parameters, and integration into engine materials via a dedicated asset file type.

Working directory: c:\Projects\SlipEngine
Integrity mode: development

## Requirements

### R1. Node-Based Material Editor UI & Material Type Selection
Implement an interactive node editor panel in SlipEngine's editor. The editor must allow users to create nodes, configure node properties, connect inputs and outputs via node links, delete nodes/connections, and navigate the canvas. The editor must allow users to select the material type being edited (including Lit and Unlit), adjusting the master node outputs accordingly (e.g. PBR Lit surface properties vs. Unlit color). The node palette must include mathematical functions (arithmetic, trigonometry, vector math), procedural functions, time/animation inputs (including deltaTime for animated materials), camera matrices, texture sampling, and master output nodes.

### R2. Shader Graph File Asset Type & Engine Material Integration
Introduce a new asset file type for shader graphs that can be saved to and loaded from disk within the project. Update SlipEngine's material system (`SlipMaterial`) to support selecting and assigning created shader graph assets to materials, compiling or resolving the graph into shader programs for rendering according to the chosen material type (Lit or Unlit).

### R3. Public / Exposed Material Parameters
Allow users to declare public exposed properties in a shader graph (such as float parameters, color/vector parameters, and texture parameters). When a material uses a shader graph, its exposed public properties must appear in the material inspector, allowing per-material customization of values and textures.

### R4. Automated Testing & Verification Examples
Implement automated verification tests covering shader graph data serialization (save/load round-trip), GLSL shader code generation for both Lit and Unlit modes, and material assignment. Provide sample shader graph assets (including an animated material using deltaTime and procedural math) demonstrating functional rendering in the engine.

## Acceptance Criteria

### Node Graph Editor Panel & Material Type Selection
- [ ] The node editor panel opens within SlipEngine and allows adding nodes, moving them, connecting compatible ports, and disconnecting links.
- [ ] Users can toggle or select the material type (e.g. Lit vs. Unlit), which updates the available master output inputs (Lit provides PBR surface inputs such as albedo, normal, roughness, metallic, emissive; Unlit provides direct color/emissive output).
- [ ] The node library includes math operations, procedural functions, time variables (e.g. deltaTime), camera matrices, and texture samplers.
- [ ] Users can edit node inputs directly on compatible nodes or connect upstream node outputs.

### Asset Serialization & Material Integration
- [ ] Shader graphs save to and load from a dedicated asset file format without loss of graph structure, connections, node parameters, or selected material type (Lit/Unlit).
- [ ] Materials in SlipEngine can reference a saved shader graph asset as their shader source.
- [ ] Shaders generated from the graph compile successfully in SlipEngine's OpenGL pipeline and render on meshes using `SlipMaterial` with proper Lit or Unlit shading.

### Exposed Parameters Inspector
- [ ] Public variables declared in the shader graph (floats, vectors/colors, textures) are exposed in the material inspector.
- [ ] Changing an exposed property's value or texture on a material updates the rendered appearance of objects using that material.

### Automated Tests & Sample Graphs
- [ ] Automated test suite runs and verifies:
  - Serialization and deserialization of shader graph node trees and connections.
  - Correct GLSL code generation for graphs in both Lit and Unlit configurations combining time, math, and texture nodes.
- [ ] At least one sample animated shader graph using deltaTime and procedural functions is included in the project assets and loads successfully.
