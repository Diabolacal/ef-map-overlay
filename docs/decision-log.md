<!-- Overlay decision log created 2025-09-26. Mirror significant cross-repo events with `EF-Map-main/docs/decision-log.md` (sibling repository) and include a `Cross-repo` note per entry when applicable. -->

## 2025-09-26 – Repository bootstrap & guardrails mirror
- Goal: Establish baseline guardrails for the EF-Map overlay helper repository and mirror shared documentation from the main EF-Map project.
- Files: `AGENTS.md`, `.github/copilot-instructions.md`, `docs/decision-log.md`, `docs/initiatives/GAME_OVERLAY_PLAN.md`, workspace README.
- Diff: +4 files (docs only).
- Risk: low (documentation setup).
- Gates: build ⏳ (tooling not yet established) | tests ⏳ | smoke ⏳
- Cross-repo: EF-Map main decision log (2025-09-26) – note referencing overlay repo bootstrap.
- Follow-ups: Populate helper/overlay source structure; document build/test commands once code exists.

## 2025-09-26 – Source scaffolding & roadmap alignment
- Goal: Create initial filesystem layout for helper, overlay, and tooling modules with placeholder documentation outlining responsibilities.
- Files: `src/helper/README.md`, `src/overlay/README.md`, `tools/README.md`, repository `README.md` update.
- Diff: +3 files, README update (docs only).
- Risk: low (structure + documentation).
- Gates: build n/a | tests n/a | smoke n/a (no executable code yet).
- Cross-repo: Refer to EF-Map main decision log (2025-09-26) for coordination context; no code impact on main repo.
- Follow-ups: Decide implementation language + build system, add initial build/test scripts, begin helper prototype.

## 2025-09-26 – CMake build scaffold + stubs
- Goal: Introduce a Windows-focused CMake build system with minimal helper executable and overlay DLL placeholders.
- Files: `CMakeLists.txt`, `src/helper/CMakeLists.txt`, `src/helper/main.cpp`, `src/overlay/CMakeLists.txt`, `src/overlay/dllmain.cpp`, `.gitignore`, `tools/README.md`, root `README.md` build section.
- Diff: +5 source files, README/tooling updates.
- Risk: low (build scaffold only).
- Gates: build ⚪ (not executed yet) | tests ⚪ | smoke ⚪ (no runtime code beyond stubs).
- Cross-repo: Informational only; no main repo impact yet.
- Follow-ups: Flesh out helper protocol listener, add overlay rendering harness, integrate automated build in CI.

## 2025-09-26 – Native helper stack decision
- Goal: Lock helper implementation to a native C++20 toolchain and enumerate core dependencies for upcoming work.
- Files: `README.md` (build status + next steps).
- Diff: documentation update only.
- Risk: low.
- Gates: build n/a | tests n/a | smoke n/a.
- Cross-repo: No immediate changes in EF-Map main; coordinate once helper API endpoints are exposed.
- Follow-ups: Integrate MinHook, spdlog, cpp-httplib, nlohmann/json, ImGui into the CMake pipeline.

## 2025-09-26 – Helper HTTP stub + overlay renderer loop
- Goal: Stand up a localhost API skeleton for the helper and exercise a background ImGui loop in the overlay module.
- Files: `src/helper/helper_server.{hpp,cpp}`, `src/helper/main.cpp`, `src/helper/CMakeLists.txt`, `src/overlay/overlay_renderer.{hpp,cpp}`, `src/overlay/dllmain.cpp`, `src/overlay/CMakeLists.txt`, `README.md`, `src/helper/README.md`, `src/overlay/README.md`, `cmake/Dependencies.cmake` (minor policy tweak), configure/build commands rerun.
- Diff: +4 source files, substantial updates to helper/overlay entry points and documentation.
- Risk: medium (introduces threaded HTTP listener and ImGui lifecycle management).
- Gates: build ✅ (`cmake --build ... --config Debug`) | tests ⚪ (not yet implemented) | smoke ⚪ (manual runtime validation pending DirectX hook).
- Cross-repo: EF-Map main unaffected; integration hooks will follow once API contracts solidify.
- Follow-ups: Expose authenticated command endpoints, wire IPC to overlay thread, and connect render loop to actual DX12 swap-chain via MinHook.

## 2025-09-26 – Protocol handler + authenticated command intake
- Goal: Register the `ef-overlay://` protocol on Windows, enforce a shared-secret on helper APIs, and support forwarding commands via HTTP or direct invocation.
- Files: `src/helper/main.cpp`, `src/helper/helper_server.{hpp,cpp}`, `src/helper/protocol_registration.{hpp,cpp}`, `src/helper/CMakeLists.txt`, root `README.md`, `src/helper/README.md`.
- Diff: +2 source files, major updates to helper entry point and server auth flow, README additions.
- Risk: medium (registry writes, authentication gate).
- Gates: build ✅ (`cmake --build ... --config Debug`) | tests ⚪ | smoke ⚪ (manual click/URI test pending).
- Cross-repo: No immediate EF-Map main changes; overlay deep-link payload schema remains TODO.
- Follow-ups: Define structured payload schema feeding the overlay, bridge to DX12 hooks, and add CI/static analysis with third-party warning suppression.

## 2025-09-26 – Canonical overlay schema + shared memory bridge
- Goal: Establish a shared overlay payload schema, publish helper snapshots to shared memory, and surface the data in the overlay renderer.
- Files: `src/shared/{overlay_schema.cpp,overlay_schema.hpp,shared_memory_channel.cpp,shared_memory_channel.hpp,CMakeLists.txt}`, `src/helper/{helper_server.cpp,helper_server.hpp,main.cpp,README.md,CMakeLists.txt}`, `src/overlay/{overlay_renderer.cpp,overlay_renderer.hpp,README.md,CMakeLists.txt}`, root `README.md`, `CMakeLists.txt`.
- Diff: +1 implementation file, updates across helper/overlay runtime and documentation.
- Risk: medium (introduces IPC channel and background parsing/rendering).
- Gates: build ✅ (`cmake --build build --config Release`) | tests ⚪ (not yet implemented) | smoke ⚪ (DX12 integration pending; ImGui preview exercised).
- Cross-repo: EF-Map main unaffected for now; coordinate once web app emits payloads matching the schema.
- Follow-ups: Integrate DX12 swap-chain hook, add automated validation/tests for schema changes, document worker/client handshake once exposed to EF-Map main.

## 2025-09-26 – Overlay schema regression tests
- Goal: Add a lightweight CTest harness to exercise overlay schema parsing/serialization and shared-memory round-trips.
- Files: `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/overlay_tests.cpp`, root `README.md` (test instructions).
- Diff: +2 files, build/doc updates.
- Risk: low (test-only additions).
- Gates: build ✅ (`cmake --build build --config Release --target ef_overlay_tests`) | tests ✅ (`ctest -C Release --output-on-failure`) | smoke n/a.
- Cross-repo: None.
- Follow-ups: Expand coverage once DX12 hook lands (e.g., snapshot dispatch fuzzing), wire tests into future CI workflow.

## 2025-09-26 – DX12 swap-chain hook integration
- Goal: Replace the stub overlay loop with a MinHook-powered DirectX 12 swap-chain hook that renders ImGui using live shared-memory data.
- Files: `src/overlay/{dllmain.cpp,overlay_renderer.{hpp,cpp},overlay_hook.{hpp,cpp},CMakeLists.txt}`, root `README.md`, `src/overlay/README.md`.
- Diff: +2 source files (hook implementation), significant updates to renderer lifecycle and documentation.
- Risk: high (installs runtime hooks in the game process, manages GPU resources).
- Gates: build ✅ (`cmake --build build --config Release`) | tests ✅ (`ctest -C Release --output-on-failure`) | smoke ⚪ (in-game validation pending access to client).
- Cross-repo: No immediate EF-Map main changes; helper payload contract unchanged.
- Follow-ups: Harden hook teardown (resource fences, error handling), capture in-game smoke results, and add CI coverage for hook compilation warnings.

## 2025-09-27 – DX12 overlay smoke test & queue capture fix
- Goal: Diagnose missing in-game overlay rendering, capture the game’s command queue reliably, add a visibility toggle, and verify the overlay UI inside the EVE Frontier client.
- Files: `src/overlay/overlay_hook.cpp`, `src/overlay/overlay_renderer.cpp`, `src/overlay/dllmain.cpp`, `tools/overlay_smoke.ps1`, docs update.
- Diff: ~+220/−90 lines (expanded logging, command-queue capture fix, visibility toggle, automation script).
- Risk: high (live process hooking, GPU command submission adjustments).
- Gates: build ✅ (`cmake --build build --config Release --target ef_overlay_module`) | tests ⚪ (not run; unchanged unit coverage) | smoke ✅ (manual injection with live client screenshot).
- Follow-ups: Polish overlay styling (remove remaining debug UI), evaluate long-run performance impact, surface helper command to toggle overlay remotely, and mirror summary in `EF-Map-main/docs/decision-log.md` once integration plan is drafted.

## 2025-09-27 – Overlay input capture & helper smoke script fixes
- Goal: Allow the overlay window to intercept keyboard/mouse input (F8 toggle, drag-to-move without pass-through), enable resize-from-edge controls, and align the smoke-test script with the helper’s runtime port.
- Files: `src/overlay/overlay_hook.cpp`, `src/overlay/overlay_renderer.cpp`, `tools/overlay_smoke.ps1`.
- Diff: ~+155/−40 lines (Win32 WndProc hook, ImGui IO config, input swallow helpers, resize-from-edge flag, port/env handling tweaks).
- Risk: high (window procedure interception inside game process).
- Gates: build ✅ (`cmake --build build --config Release --target ef_overlay_module`) | tests ⚪ (unchanged) | smoke ⚪ (awaiting in-game verification post-build).
- Gates: smoke ✅ (manual injection via `tools/overlay_smoke.ps1` with helper payload and in-game verification for F8 toggle, drag, scroll, multi-corner resize).
- Follow-ups: Validate long-session stability and consider helper auto-shutdown flag in smoke script; document cross-repo impact once EF-Map main integrates overlay controls.

### Launch steps (reference)
1. `cmake --build build --config Release --target ef_overlay_helper ef_overlay_injector ef_overlay_module`
2. Start the helper + injector: `.	ools\,overlay_smoke.ps1 -GameProcess "exefile.exe"`
	- Optional `-DetachHelper` to run helper in its own console, `-SkipPayload` to avoid posting the sample route.
3. In-game controls: **F8** hide/show, drag title bar to move, drag any edge/corner to resize, mouse wheel to scroll.
4. Stop helper when finished: `Get-Process ef-overlay-helper | Stop-Process`
