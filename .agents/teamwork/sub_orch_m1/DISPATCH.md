# Dispatch: Sub-Orchestrator Milestone 1

## Mission
You are the Sub-Orchestrator for Milestone 1 (M1: Core Model, Node Library & Asset Serialization).
Working directory: `c:\Projects\SlipEngine\.agents\teamwork\sub_orch_m1\`
Your parent is the Project Orchestrator: `af9ad2dc-4359-4d00-baf7-c2106d2d2f37`.

Read:
- `c:\Projects\SlipEngine\.agents\teamwork\ORIGINAL_REQUEST.md`
- `c:\Projects\SlipEngine\PROJECT.md`
- `c:\Projects\SlipEngine\.agents\teamwork\sub_orch_m1\SCOPE.md`

## Required Workflow
Follow the Project Pattern procedure:
1. Assess your scope and execute the standard iteration loop (2B):
   a. Spawn 3 Explorers (`teamwork_explorer`) to investigate exact signatures, classes, serialization formatting, and CMakeLists integration.
   b. Spawn 1 Worker (`teamwork_worker`) to implement the changes across owned files and verify build (`cmake --build build --config Debug --target SlipEngine`).
   c. Spawn 2 Reviewers (`teamwork_reviewer`) independently to inspect code quality, edge cases, and layout compliance.
   d. Spawn 2 Challengers (`teamwork_challenger`) to empirically stress-test serialization round-trip, cycle detection, node creation, and port connectivity.
   e. Spawn 1 Forensic Auditor (`teamwork_auditor`) for integrity verification.
   f. Gate check in `GATE_STATUS.md`. All criteria must pass. Audit is a BINARY VETO.
2. Maintain your own `BRIEFING.md`, `progress.md`, `GATE_STATUS.md`, and `handoff.md`.
3. When Milestone 1 is verified and passed, deliver `handoff.md` and notify parent `af9ad2dc-4359-4d00-baf7-c2106d2d2f37`.
