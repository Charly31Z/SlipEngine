# M2 Explorer 2: SlipMaterial Runtime Integration

- Identity: `explorer_m2_2` (`teamwork_explorer`)
- Working directory: `c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_2\`
- Parent: `af9ad2dc-4359-4d00-baf7-c2106d2d2f37`
- Reference Files:
  - `c:\Projects\SlipEngine\.agents\teamwork\ORIGINAL_REQUEST.md`
  - `c:\Projects\SlipEngine\PROJECT.md`
  - `c:\Projects\SlipEngine\include\SlipEngine\render\SlipMaterial.h`
  - `c:\Projects\SlipEngine\src\render\SlipMaterial.cpp`

## Mission
Design the runtime integration of Shader Graph into `SlipMaterial`:
1. `include/SlipEngine/render/SlipMaterial.h`:
   - Add fields: `std::string shaderGraphPath;`
   - Add parameter override containers:
     - `std::unordered_map<std::string, float> graphFloatParams;`
     - `std::unordered_map<std::string, glm::vec4> graphVectorParams;`
     - `std::unordered_map<std::string, std::string> graphTextureParams;`
   - Add methods: `bool LoadShaderGraph(const std::string& path);`, `void SetGraphFloat(...)`, `void SetGraphVector(...)`, `void SetGraphTexture(...)`.
2. `src/render/SlipMaterial.cpp`:
   - In `init()`: if `shaderGraphPath` is non-empty, load the graph asset via `ShaderGraphSerializer`, invoke `ShaderGraphCompiler::Compile(graph)`, compile shader via `SlipShader::CreateFromSource`, and assign to `this->shader`.
   - In `bindMaterialProperties(SlipShader* targetShader)`: bind the graph's exposed parameters to their corresponding GLSL uniforms. For custom texture parameters, assign texture units starting at unit 20 (units 0..19 reserved by engine subsystems).
   - In `loadMatFromFile(path)` and `Save(savePath)`: serialize `shaderGraph: <path>` and `graphParam.<name>: <value>`.
3. Provide complete code diffs and recommendations in `c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_2\analysis.md` and complete `handoff.md`.

## 2026-11-08T14:42:25Z
You are explorer_m2_2 (SlipMaterial Integration Explorer).
Your working directory is: c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_2\
Read:
- c:\Projects\SlipEngine\.agents\teamwork\ORIGINAL_REQUEST.md
- c:\Projects\SlipEngine\PROJECT.md
- c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_2\DISPATCH.md
- c:\Projects\SlipEngine\include\SlipEngine\render\SlipMaterial.h
- c:\Projects\SlipEngine\src\render\SlipMaterial.cpp

Design the runtime integration of Shader Graph into `SlipMaterial`:
- Storing `shaderGraphPath` and parameter override maps (`float`, `vec4`, `texture`).
- Compiling/resolving graph shader in `init()` via `SlipShader::CreateFromSource`.
- Binding exposed parameter uniforms in `bindMaterialProperties()` (using texture units 20+ for custom textures).
- Serializing and loading `.shadergraph` references and parameter overrides in `.mat` files.
Write findings and code diffs to analysis.md and complete handoff.md in your working directory. Send a message to parent af9ad2dc-4359-4d00-baf7-c2106d2d2f37 when finished.
