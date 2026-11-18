# Progress

Last visited: 2026-11-08T15:25:40Z

- Build SlipEngine succeeded (exit code 0).
- SlipTestShaderGraph passed 170/170.
- SlipTestTopologyChallenger passed 25/25.
- SlipTestShaderGraphStress passed 24/24.
- In-depth code review underway:
  - Discovered parameter override clobbering bug in `SlipCompileShaderGraphFile` / `bindGraphParameters` (duplicate keys `"Prop"` and `"u_Prop"` overwrite user overrides with defaults).
  - Discovered Unlit material shader graph bypass in `SlipRenderQueue.cpp` (`sType == "Unlit"` branches away from `bindMaterialProperties`, failing to bind exposed parameters).
  - Discovered GLSL identifier unsanitized names in `TextureSamplerNode` and `ProceduralVoronoiNode`.
  - Discovered uniform name collisions with built-ins (`u_Time`, etc.) and unsanitized property names in `ShaderGraphCompiler`.
  - Discovered that `SlipTestShaderGraph` tests mock classes (`SlipTest`) rather than engine classes (`SlipEngine`), explaining how these defects escaped automated tests.
- Testing compilation of `SlipEditorApp`.
