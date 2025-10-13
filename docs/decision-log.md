## 2025-10-13 – Overlay styling hotfix (foreground accent rollback)
- Goal: Revert the foreground draw-list accent experiment that froze the client on injection while keeping the white resize highlight and top accent.
- Files: `src/overlay/overlay_renderer.cpp`.
- Diff: ~+30/−48 (remove foreground draw calls, trim extra style pushes, restore window draw-list accent with brighter colors, add separator overrides + clip guard, align top accent thickness with hover highlight).
- Risk: low (UI cosmetics only; regression fix).
- Gates: build ✅ (`cmake --build build --config Debug`) | tests ✅ (`ctest -C Debug --output-on-failure`) | smoke ⏳ (pending user validation post-injection).
- Cross-repo: None.
- Follow-ups: Revisit top accent approach later—consider dedicated overlay layer or viewport hook rather than foreground draw list.

## 2025-10-13 – Helper auto-detects EVE client process
- Goal: Resolve the target game client automatically so helper-triggered injections no longer require a manual PID lookup.
- Files: `src/helper/helper_runtime.cpp`.
- Diff: ~+120/−0 (process enumeration helper, UTF-8 conversion, injection messaging tweaks).
- Risk: low (helper-side process spawning only).
- Gates: build ✅ (`cmake --build build --config Debug`) | tests ✅ (`ctest -C Debug`, reran flaky queue test) | smoke ⏳ (manual helper inject next session).
- Cross-repo: None.
- Follow-ups: Point release of helper once Release binaries regenerated; consider adding retry loop to watch for client launch.

## 2025-10-13 – Overlay styling polish (accent + resize parity)
- Goal: Force the resize edge highlight to match the in-game white glow and ensure the active window draws a visible top accent.
- Files: `src/overlay/overlay_renderer.cpp`.
- Diff: ~+35/−8 (additional ImGui color overrides, foreground accent draw call).
- Risk: low (UI cosmetics only).
- Gates: build ✅ (`cmake --build build --config Debug`) | tests ✅ (`ctest -C Debug --output-on-failure`) | smoke ⏳ (user verifying in live client after reinject).
- Cross-repo: None.
- Follow-ups: Confirm accent visibility across multiple resolutions and tweak inactive accent tone once HUD palette is finalized.

## 2025-10-13 – Overlay window styling parity
- Goal: Restyle the ImGui debug window so it matches EVE Frontier UI (white active accent, neutral resize hints, ellipsis menu placeholder).
- Files: `src/overlay/overlay_renderer.cpp`.
- Diff: ~+120/−35 (style pushes, custom accent drawing, ellipsis glyph, follow-up tuning for accent thickness/hover behavior).
- Risk: low (UI cosmetics only).
- Gates: build ✅ (`cmake --build build --config Debug`) | tests ✅ (`ctest -C Debug`).
- Cross-repo: None.
- Follow-ups: Wire ellipsis menu into upcoming context actions; revisit colors once final HUD palette is locked.

## 2025-10-12 – Star catalog name fallback for map view
- Goal: Allow map rendering when helper routes carry system names instead of numeric IDs by indexing the star catalog by normalized name and teaching the renderer to fall back accordingly.
- Files: `src/shared/star_catalog.{hpp,cpp}`, `src/overlay/starfield_renderer.cpp`.
- Diff: ~+150/−10 (catalog index + renderer fallback + projection helper adjustments).
- Risk: medium (touches renderer hot path and catalog loader).
- Gates: build ✅ (`cmake --build build --config Release`) | tests ✅ (`ctest -C Release`) | smoke ✅ (helper restarted, overlay reinjected, live client ready for user verification).
- Cross-repo: None (catalog binary already shared with EF-Map main).
- Follow-ups: Monitor for ambiguous name collisions; if collisions appear, extend resolver with manual overrides and surface fallback status in helper diagnostics.

## 2025-10-12 – Overlay map HUD toggle
- Goal: Introduce map view HUD with route markers/labels and keep debug panel accessible via F7.
- Files: `src/overlay/overlay_renderer.cpp`
- Diff: ~240 ++ / 240 --
- Risk: medium
- Gates: build ✅ (Release) tests ✅ (ctest Release) smoke ❌ (manual game attach deferred)
- Cross-repo: EF-Map-main map view branch (starfield renderer helpers)
- Follow-ups: Run in-game smoke to verify markers and labels align with starfield focus once helper is reattached.

## 2025-10-12 – Map/debug window unification & catalog packaging
- Goal: Ensure map/debug modes reuse the same ImGui window with resizing preserved and deploy `star_catalog_v1.bin` alongside the injected DLL so the starfield renderer initializes.
- Files: `src/overlay/overlay_renderer.cpp`, runtime asset copy to `build/src/overlay/Release/`.
- Diff: ~180 ++ / 220 --
- Risk: low
- Gates: build ⚪ (Release blocked by DLL in use) | build ✅ (Debug) | tests ✅ (`ctest -C Debug --output-on-failure`) | smoke ✅ (live client confirmed shared window + starfield ready after reinject)
- Cross-repo: Asset mirrors `EF-Map-main/data/star_catalog_v1.bin`; no code changes required in main repo.
- Follow-ups: Re-run Release build once DLL is unloaded, then ship new binary bundle; automate asset copy in build script so future releases include the catalog.
<!-- Overlay decision log created 2025-09-26. Mirror significant cross-repo events with `EF-Map-main/docs/decision-log.md` (sibling repository) and include a `Cross-repo` note per entry when applicable. -->

## 2025-10-12 – Release smoke test (helper + injector)
- Goal: Validate the end-to-end overlay flow (helper HTTP API, shared-memory bridge, injector, in-game rendering) using the refreshed star catalog data.
- Files: docs only (`README.md`).
- Diff: README updated to document the canonical EVE Frontier process name `exefile.exe` and revised injection instructions.
- Risk: low (documentation + runtime validation).
- Gates: build ✅ (Release artifacts from same session) | tests ⚪ (no new code) | smoke ✅ (helper state + in-game overlay screenshot).
- Cross-repo: Informational only; no `EF-Map-main` changes required.
- Follow-ups: Replace debug ImGui panel with actual map visuals, script helper shutdown for automated runs, and schedule the next smoke once log watcher routes feed the overlay.

## 2025-10-12 – Helper heartbeat + overlay auto-hide
- Goal: Hide the in-game overlay automatically when the helper stops (graceful exit or crash) and revive it when the heartbeat resumes.
- Files: `src/shared/overlay_schema.{hpp,cpp}`, `src/helper/helper_server.{hpp,cpp}`, `src/helper/helper_runtime.cpp`, `src/helper/log_watcher.cpp`, `src/overlay/overlay_renderer.{hpp,cpp}`, `tests/overlay_tests.cpp`, `README.md`.
- Diff: ~+220/−40 across schema, helper, renderer, tests, and docs.
- Risk: medium (touches shared-memory schema and injected render path).
- Gates: build ✅ (`cmake --build build --config Release`) | tests ✅ (`ctest -C Release --output-on-failure`) | smoke ✅ (helper forced-stop then restart in live client auto-hid and auto-showed overlay).
- Cross-repo: None (heartbeat metadata is overlay-only at this stage).
- Follow-ups: Re-run in-game smoke with the new DLL, monitor heartbeat timeout (5s) during live play, and surface heartbeat status in the upcoming helper tray UI.

## 2025-10-12 – Camera-aligned starfield + route polyline
- Goal: Align the DX12 starfield with helper-provided camera pose and render live route polylines so the in-game overlay mirrors EF-Map navigation.
- Files: `src/overlay/starfield_renderer.{hpp,cpp}`, `src/overlay/overlay_hook.cpp`.
- Diff: ~+520/−180 lines (constant-buffered renderer, dynamic route buffer, hook wiring).
- Risk: medium (new GPU constant buffer updates and dynamic route uploads in injected process).
- Gates: build ✅ (`cmake --build build --config Debug`) | tests ✅ (`build/tests/Debug/ef_overlay_tests.exe`).
- Cross-repo: Documented in `EF-Map-main/docs/decision-log.md` (2025-10-12 – Overlay camera-aligned renderer sync).
- Follow-ups: Add waypoint markers/selection glow, profile GPU/frame impact in live sessions, and expose renderer health metrics in helper tray diagnostics.

## 2025-10-13 – Overlay roadmap refocus (helper-first)
- Goal: Pause native starfield visualization work and prioritize helper-driven features (log watcher, mining/DPS tracking, in-overlay actions) that deliver immediate value.
- Files: docs only (`docs/decision-log.md`, `docs/initiatives/GAME_OVERLAY_PLAN.md`).
- Diff: n/a (doc updates).
- Risk: low (strategic reprioritization).
- Gates: build ⚪ | tests ⚪ | smoke ⚪ (no code path changes).
- Cross-repo: Mirrored in `EF-Map-main/docs/decision-log.md` with same title/date.
- Follow-ups: Execute helper-first roadmap—ship log watcher + position sync, tray UX shell, session tracking modules, and browser CTA/event bridge before revisiting starfield polish.

## 2025-10-02 – Native starfield renderer spike (DX12 point cloud)
- Goal: Render the EF-Map star catalog inside the overlay using a lightweight DX12 pipeline (point sprites + additive blend) as the baseline for native visuals.
- Files: `src/overlay/starfield_renderer.{hpp,cpp}`, `src/overlay/overlay_hook.cpp`, `src/overlay/CMakeLists.txt`, `src/overlay/overlay_renderer.hpp` (indirect include), build regeneration.
- Diff: +2 new overlay source files (~420 LoC) plus ~+140/−15 adjustments across the DX12 hook and build wiring.
- Risk: medium (new GPU pipeline + runtime asset load in injected process).
- Gates: build ✅ (`cmake --build build --config Debug --target ef_overlay_tests`) | tests ✅ (`ctest --test-dir build -C Debug --output-on-failure`).
- Cross-repo: EF-Map main initiative plan updated (2025-10-02 – Native starfield renderer spike) to mark milestone progress.
- Follow-ups: Integrate overlay camera transforms, render route polylines from shared state, tune colors/brightness for parity with web map, and profile GPU cost in live client sessions.

## 2025-10-02 – Star catalog asset loader + helper metadata
- Goal: Load the exported EF-Map star catalog inside the helper runtime, expose catalog metadata via HTTP/status APIs, and add a shared parser + tests so the overlay can consume the binary format.
- Files: `src/shared/star_catalog.{hpp,cpp}`, `src/shared/CMakeLists.txt`, `tests/overlay_tests.cpp`, `src/helper/helper_runtime.{hpp,cpp}`, `src/helper/helper_server.{hpp,cpp}`, `CMakeLists.txt` (indirect via shared lib), `build` regeneration (cmake configure).
- Diff: +2 new source files (~310 LoC) plus ~+190/−20 changes across helper runtime/server, tests, and build wiring.
- Risk: medium (helper startup now depends on catalog asset; new HTTP surface for metadata).
- Gates: build ✅ (`cmake --build build --config Debug --target ef_overlay_tests`) | tests ✅ (`ctest --test-dir build -C Debug --output-on-failure`).
- Cross-repo: EF-Map main decision log (2025-10-02 – Star catalog exporter + overlay asset copy).
- Follow-ups: Feed catalog into the DX12 renderer, extend asset format with stargate adjacency once overlay requirements stabilize, and surface catalog telemetry in the upcoming tray UI.

## 2025-10-01 – Helper WebSocket hub + EF-Map bridge handshake
- Goal: Promote the helper’s WebSocket hub (real-time overlay state/events) and finalize the browser handshake so EF-Map consumes live payloads without HTTP polling.
- Files: `src/helper/helper_websocket.{hpp,cpp}`, `src/helper/helper_server.{hpp,cpp}`, `cmake/Dependencies.cmake`, `src/helper/CMakeLists.txt`, helper docs (minor).
- Diff: +2 new source files (~560 LoC) plus ~+220/−80 adjustments to helper server lifecycle and CMake dependency wiring.
- Risk: medium (long-lived network listeners + broadcast threading).
- Gates: build ✅ (`cmake --build build --config Release`) | tests ✅ (`ctest -C Release`) | smoke ⏳ (tray/WebSocket badge documentation next).
- Cross-repo: EF-Map main decision log (2025-10-01 – Helper WebSocket bridge + frontend badge).
- Follow-ups: Surface helper tray/Web UI indicators, add WebSocket smoke script, and expand unit coverage for handshake failure paths.

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

## 2025-10-01 – Log watcher system ID resolver
- Goal: Resolve Local chat system names to canonical EF system IDs so overlay payloads match shared schema expectations.
- Files: `src/helper/{log_watcher.{hpp,cpp},system_resolver.{hpp,cpp},system_resolver_data.hpp,helper_runtime.{hpp,cpp},CMakeLists.txt}`, `tests/overlay_tests.cpp`.
- Diff: +3 source files, +1 generated header (~24k entries), updates to helper runtime/log watcher wiring, new unit tests.
- Risk: medium (large embedded dataset + runtime resolution affects log watcher output).
- Gates: build ✅ (`cmake --build build --config Release`) | tests ✅ (`ctest --test-dir build -C Release`) | smoke ⏳ (tray/runbook validation next session).
- Cross-repo: Dataset generated from `EF-Map-main/all_solarsystems.json`; no code changes required in main repo yet.
- Follow-ups: Expose helper event bridge to EF-Map client; handle duplicate system names (`D:28NL`, `Alghoarismi`) via manual mapping if needed.

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

## 2025-10-01 – Roadmap refresh: log telemetry focus & HTML embed stance
- Goal: Elevate the game log watcher and combat telemetry overlay milestones, document required event-channel groundwork, and capture guidance on HTML overlay embedding.
- Files: `docs/initiatives/GAME_OVERLAY_PLAN.md`.
- Diff: Adjusted Phase 2 milestones (log watcher detail, new combat telemetry item), refined next steps, added HTML embedding cautionary note.
- Risk: low (documentation only).
- Gates: build n/a | tests n/a | smoke n/a.
- Cross-repo: EF-Map main decision log (2025-10-01 – Roadmap refresh: log telemetry focus & HTML embed stance).
- Follow-ups: Implement helper log watcher + location toggle, prototype combat telemetry overlay, revisit HTML embedding after native renderer spike.

## 2025-10-01 – Overlay schema v2 + event queue infrastructure
- Goal: Finalize the v2 overlay state contract (player marker, highlights, camera pose, HUD hints, follow flag) and stand up a shared-memory event queue with helper HTTP polling for overlay-generated actions.
- Files: `src/shared/overlay_schema.*`, `src/shared/event_channel.*` (new), `src/shared/CMakeLists.txt`, `src/helper/{helper_server.*,main.cpp}`, `src/overlay/overlay_renderer.*`, `tests/overlay_tests.cpp`, `docs/initiatives/GAME_OVERLAY_PLAN.md`.
- Diff: +2 new shared files (event ring buffer), ~+430/-60 lines updating schema, helper/overlay plumbing, and unit tests; plan updated to mark milestone complete.
- Risk: medium (shared-memory layout change + new cross-process queue).
- Gates: build ✅ (`cmake --build build --config Release`) | tests ✅ (`ctest -C Release`) | smoke ⏳ (needs in-game verification of new debug UI + event emission).
- Cross-repo: EF-Map main decision log (2025-10-01 – Overlay schema v2 + event queue infrastructure).
- Follow-ups: Bridge helper event queue to EF-Map web client, feed real payloads from log watcher once implemented, add automated smoke covering event drain endpoint.

## 2025-10-01 – Roadmap reprioritization: helper tray shell MVP
- Goal: Pull the helper tray shell forward in the roadmap to provide a tangible control surface before log watcher / event bridge work, documenting the new sequencing.
- Files: `docs/initiatives/GAME_OVERLAY_PLAN.md` (mirrored in EF-Map main).
- Diff: Added Section 7.0 for tray shell milestone, retitled packaging section, reordered “Next Steps.”
- Risk: low (documentation only).
- Gates: build n/a | tests n/a | smoke n/a.
- Cross-repo: EF-Map main decision log (2025-10-01 – Roadmap reprioritization: helper tray shell MVP).
- Follow-ups: Implement tray shell MVP, then resume log watcher and event bridge milestones as reordered.

## 2025-10-01 – Helper log watcher foundation
- Goal: Monitor live Local chat and combat logs to derive player location/combat activity and publish automatic overlay states from the helper runtime.
- Files: `src/helper/{helper_runtime.*,log_watcher.*,log_parsers.*,CMakeLists.txt}`, `tests/{CMakeLists.txt,overlay_tests.cpp}`, build scripts; added Windows API handling for default log paths.
- Diff: +4 new source files, ~+640/−20 lines touching helper runtime + tests.
- Risk: medium (multithreaded file tailing, shared-memory publication).
- Gates: build ✅ (`cmake --build build --config Debug`) | tests ✅ (`ctest --test-dir build -C Debug`) | smoke ⏳ (pending in-game validation once tray diagnostics land).
- Cross-repo: None yet (EF-Map main unchanged until event bridge wiring).
- Follow-ups: Surface watcher status in tray UI, map system names ↔ IDs, feed combat digest into overlay HUD hints, and schedule a smoke run with live log files.

### Launch steps (reference)
1. `cmake --build build --config Release --target ef_overlay_helper ef_overlay_injector ef_overlay_module`
2. Start the helper + injector: `.	ools\,overlay_smoke.ps1 -GameProcess "exefile.exe"`
	- Optional `-DetachHelper` to run helper in its own console, `-SkipPayload` to avoid posting the sample route.
3. In-game controls: **F8** hide/show, drag title bar to move, drag any edge/corner to resize, mouse wheel to scroll.
4. Stop helper when finished: `Get-Process ef-overlay-helper | Stop-Process`
