# Empirical Challenger Report: Milestone 2 — GLSL Compilation, Re-Compilation & Material Stress Test

**Verdict**: `REQUEST_CHANGES`

---

## 1. Observation

### 1.1 Baseline Build and Test Suite Executions
- **Target `SlipEngine` Build**:
  Command: `cmake --build build --config Debug --target SlipEngine`
  Result: Exit code 0, 0 compiler errors, 0 warnings.
- **Target `SlipTestShaderGraph` Build & Execution**:
  Command: `cmake --build build --config Debug --target SlipTestShaderGraph`
  Result: Exit code 0.
  Command: `.\build\Debug\SlipTestShaderGraph.exe`
  Result: 170 / 170 passed cleanly (exit code 0).
  *Critical Investigation Finding*: `test_shader_graph.cpp` tested an internal mock class `SlipTest::ShaderGraphCompiler` implemented directly inside `test_shader_graph.cpp` rather than testing the engine's actual production classes `SlipEngine::ShaderGraphCompiler` (`src/render/shadergraph/ShaderGraphCompiler.cpp`) and `SlipMaterial::LoadShaderGraph` (`src/render/SlipMaterial.cpp`).

### 1.2 Empirical Stress Harness Execution (`SlipTestGLSLChallenger.exe`)
An empirical challenge harness (`SlipTestGLSLChallenger`) was authored and executed in a live headless OpenGL 3.3 Core profile context on an NVIDIA GeForce RTX 5060 GPU:
```
================================================================================
   SlipEngine Milestone 2 EMPIRICAL CHALLENGER STRESS SUITE (OpenGL GLSL)       
================================================================================
[SlipEngine] Inicializando SDL2...
[SlipEngine] OpenGL inicializado con exito. Version: 3.3.0 NVIDIA 617.14
[SlipEngine] Cargando escena de juego: assets/scenes/Main.scene
[INFO] Headless OpenGL Context loaded successfully.
[INFO] Vendor:   NVIDIA Corporation
[INFO] Renderer: NVIDIA GeForce RTX 5060/PCIe/SSE2
[INFO] GLSL:     3.30 NVIDIA via Cg compiler
--------------------------------------------------------------------------------

>>> Part 1: GLSL Generation & Real OpenGL Compilation / Linking <<<
  [CHALLENGE] 1.1 Complex Lit PBR Graph with Math, Trig & Noise ... [PASS] (3.21 ms)
  [CHALLENGE] 1.2 Dynamic Unlit Graph with Animated Time & Color ... [PASS] (1.24 ms)
  [CHALLENGE] 1.3 Deep DAG Chain (100+ Nodes) Stress Compilation ... [PASS] (50.99 ms)
  [CHALLENGE] 1.4 Multi-Procedural Noise and Voronoi Evaluation ... [PASS] (1.76 ms)
  [CHALLENGE] 1.5 Dead Code Elimination with 75 Orphan Nodes ... [PASS] (6.28 ms)
  [CHALLENGE] 1.6 Mobile GLES 3.0 Target Header & Precision Emitter ... [PASS] (0.38 ms)
  [CHALLENGE] 1.7 Disconnected MasterNode Safe Fallback Defaults ... [PASS] (1.62 ms)
  [CHALLENGE] 1.8a Normal Mapping Unconnected Sampler Fallback [BUG: texture(0.0, uv)] ... 
    [OPENGL ERROR Normal Mapping] Fragment Shader Compile Error: 0(238) : error C1115: unable to find compatible overloaded function "texture(float, vec2)"

[FAILED] (18.58 ms)
  [CHALLENGE] 1.8b Normal Mapping Connected Sampler Property ... 
    [OPENGL ERROR Normal Mapping Connected] Fragment Shader Compile Error: 0(237) : error C7554: OpenGL requires sampler variables to be explicitly declared as uniform
0(237) : error C1054: initialization of non-variable "n_Input_Property_1_Out"

[FAILED] (3.62 ms)
  [CHALLENGE] 1.9 Blackboard Uniforms & Texture Slot 20 Allocation ... [PASS] (0.41 ms)

>>> Part 2: SlipMaterial Parameter Overrides & Uniform Reflection Stress <<<
  [CHALLENGE] 2.1 SlipMaterial Dynamic Parameter Getters & Setters ... [PASS] (0.04 ms)
  [CHALLENGE] 2.2 Rapid Parameter Overrides Stress (10,000 Updates) ... [PASS] (10.25 ms)
  [CHALLENGE] 2.3 Uniform Upload & Real GPU Memory Reflection (glGetUniformfv) ... [PASS] (3.68 ms)

>>> Part 3: Runtime Re-compilation & Shader Resource Management <<<
  [CHALLENGE] 3.1 Re-compilation & Clean OpenGL Program Replacement ... [PASS] (4.48 ms)
  [CHALLENGE] 3.2 Rapid Re-compilation Cycle Stress (50 Iterations) ... [PASS] (69.81 ms)
  [CHALLENGE] 3.3 Parameter Overrides Persistence Across Re-compilations ... [PASS] (6.02 ms)
  [CHALLENGE] 3.4 Material Destruction OpenGL Program Cleanup ... [PASS] (2.96 ms)

>>> Part 4: Asset Serialization & Full Round-Trip Disk Integration <<<
  [CHALLENGE] 4.1 Full Round-Trip (.shadergraph -> SlipMaterial -> .mat) ... [PASS] (8.69 ms)
  [CHALLENGE] 4.2 Non-Existent or Corrupt Graph Graceful Handling ... [PASS] (1.15 ms)

================================================================================
                  CHALLENGER STRESS SUITE EXECUTION SUMMARY                     
================================================================================
  TOTAL CHALLENGES: 19
  PASSED:           17
  FAILED:           2
================================================================================
```

---

## 2. Logic Chain

### 2.1 Confirmed Bug 1: Unconnected `TextureSamplerNode` Emits Invalid `texture(0.0, uv)`
1. *Observation*: In `ShaderGraphCompiler.cpp` lines 665–669:
   ```cpp
   if (!foundLink) {
       std::string defVal = FormatGLSLValue(inPort.type, inPort.defaultValue);
       inExpr[inPort.name] = defVal;
       inExpr[inPort.id] = defVal;
   }
   ```
2. *Observation*: In `ShaderNode.cpp` line 82 (`FormatGLSLValue`):
   When `inPort.type == PinType::Texture2D`, it falls through to the default case and returns `"0.0"`.
3. *Observation*: In `TextureSamplerNode::GenerateGLSL` (`ShaderNode.cpp` lines 1038–1041):
   ```cpp
   std::string sampler = inExpr.count("Sampler") ? inExpr.at("Sampler") : "u_MainTexture";
   std::string sampleVar = "texSample_" + id;
   stmts += "    vec4 " + sampleVar + " = texture(" + sampler + ", " + uv + ");\n";
   ```
4. *Deduction*: Because `inExpr["Sampler"]` was set to `"0.0"`, `inExpr.count("Sampler")` evaluates to `true`. Instead of falling back to `"u_MainTexture"` (which line 578 of `ShaderGraphCompiler.cpp` declared for this exact purpose), the generator emits:
   ```glsl
   vec4 texSample_Texture_Sampler_1 = texture(0.0, vTexCoords);
   ```
5. *Empirical Verification*: The OpenGL compiler rejects this invalid signature with:
   `error C1115: unable to find compatible overloaded function "texture(float, vec2)"`.

### 2.2 Confirmed Bug 2: `PinType::Texture2D` Outputs Emit Local Sampler Assignment
1. *Observation*: In `ShaderGraphCompiler.cpp` lines 686–691:
   ```cpp
   std::string varName = "n_" + SanitizeIdentifier(node->id) + "_" + SanitizeIdentifier(outPort.name);
   std::string typeName = GetPinTypeGLSLName(outPort.type);
   ss << "    " << typeName << " " << varName << " = " << expr << ";\n";
   evaluatedOutputs[node->id + "_" + outPort.id] = varName;
   ```
2. *Observation*: When a `PropertyNode` of type `PinType::Texture2D` is connected to a `TextureSamplerNode`'s `Sampler` pin, `outPort.type` is `PinType::Texture2D` and `expr` is `"u_NormalMap"`.
3. *Deduction*: `ShaderGraphCompiler` emits a local variable declaration and assignment for the sampler:
   ```glsl
   sampler2D n_Input_Property_1_Out = u_NormalMap;
   ```
4. *Standard Violation*: GLSL 3.30 specification (Section 4.1.7) strictly prohibits samplers as local variables or assignment targets: samplers must only be declared as uniforms or function parameters.
5. *Empirical Verification*: The OpenGL compiler rejects this invalid GLSL with:
   `error C7554: OpenGL requires sampler variables to be explicitly declared as uniform`
   `error C1054: initialization of non-variable "n_Input_Property_1_Out"`.

### 2.3 Robustness of `SlipMaterial` Runtime Integration
On the other hand, the adversarial challenge verified that `SlipMaterial` runtime integration is exceptionally solid:
- 10,000 rapid float/vector parameter updates executed in 10.25 ms without memory corruption.
- Real OpenGL uniform upload and driver reflection via `glGetUniformfv` verified exact matching floats and vectors.
- 50 rapid re-compilation cycles between Lit and Unlit confirmed clean destruction of old OpenGL programs (`glIsProgram(oldID) == GL_FALSE`) and allocation of new programs without leaking.
- Material destructor cleanly deallocates `m_CustomShader` and deletes OpenGL programs.
- `.shadergraph` -> `SlipMaterial` -> `.mat` round-trip persistence operates cleanly.

---

## 3. Caveats
- DDGI probe array sampling and cascaded shadow maps were verified for GLSL compilation and OpenGL linking; full multi-bounce DDGI radiance convergence under active scene rendering is deferred to Milestone 4 integration.
- Shaders were verified in OpenGL 3.3 Core profile on Windows; Mobile GLES 3.0 was verified via generated GLSL header and precision qualifier assertions.

---

## 4. Conclusion & Required Changes

**Verdict**: `REQUEST_CHANGES`

Milestone 2 cannot be approved for integration until texture sampling in `ShaderGraphCompiler` is fixed. The required changes are localized and straightforward:

### Required Fix 1: Fallback for Unconnected `PinType::Texture2D`
In `src/render/shadergraph/ShaderGraphCompiler.cpp` around line 665:
```cpp
if (!foundLink) {
    if (inPort.type == PinType::Texture2D) {
        inExpr[inPort.name] = "u_MainTexture";
        inExpr[inPort.id] = "u_MainTexture";
    } else {
        std::string defVal = FormatGLSLValue(inPort.type, inPort.defaultValue);
        inExpr[inPort.name] = defVal;
        inExpr[inPort.id] = defVal;
    }
}
```

### Required Fix 2: Do Not Emit Local Variables for Samplers
In `src/render/shadergraph/ShaderGraphCompiler.cpp` around line 686:
```cpp
for (const auto& outPort : node->outputs) {
    std::string expr;
    if (outExpr.count(outPort.name)) expr = outExpr[outPort.name];
    else if (outExpr.count(outPort.id)) expr = outExpr[outPort.id];
    else expr = FormatGLSLValue(outPort.type, outPort.defaultValue);

    if (outPort.type == PinType::Texture2D) {
        // Samplers cannot be declared as local variables in GLSL 3.30! Pass through the uniform expression directly.
        evaluatedOutputs[node->id + "_" + outPort.id] = expr;
        evaluatedOutputs[node->id + "_" + outPort.name] = expr;
    } else {
        std::string varName = "n_" + SanitizeIdentifier(node->id) + "_" + SanitizeIdentifier(outPort.name);
        std::string typeName = GetPinTypeGLSLName(outPort.type);
        ss << "    " << typeName << " " << varName << " = " << expr << ";\n";

        evaluatedOutputs[node->id + "_" + outPort.id] = varName;
        evaluatedOutputs[node->id + "_" + outPort.name] = varName;
    }
}
```

---

## 5. Verification Method

To independently verify the bug and reproduction:
1. Build `SlipTestGLSLChallenger`:
   ```powershell
   cmake --build build --config Debug --target SlipTestGLSLChallenger
   ```
2. Run the empirical challenger harness:
   ```powershell
   .\build\Debug\SlipTestGLSLChallenger.exe
   ```
3. Observe the exact compiler errors on challenges 1.8a and 1.8b:
   - 1.8a: `error C1115: unable to find compatible overloaded function "texture(float, vec2)"`
   - 1.8b: `error C7554: OpenGL requires sampler variables to be explicitly declared as uniform`
4. Once the worker applies the two fixes above, re-run `.\build\Debug\SlipTestGLSLChallenger.exe` and assert 19/19 challenges pass with exit code 0.
