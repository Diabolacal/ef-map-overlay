<!-- This plan mirrors `EF-Map-main/docs/initiatives/GAME_OVERLAY_PLAN.md`. Keep both copies synchronized whenever the roadmap changes. -->

# EF-Map Game Overlay Initiative

## Sync Checklist (when updating this plan)
1. Modify this file and the `EF-Map-main` mirror in the same change set.
2. Confirm language, tables, and dates match exactly between repositories (diff side-by-side if needed).
3. Log roadmap shifts in both decision logs with matching titles and ISO dates so history stays linked.
4. Note any cross-repo code implications in your status update to steer reviewers toward dependent files.


## 1. Purpose
- Provide an in-game overlay for EVE Frontier that surfaces a focused subset of EF Map data (initially routing information).
- Preserve the existing web app while enabling players to summon an overlay on demand without restarting the game client.
- Document an implementation roadmap using an in-process DirectX 12 hook coordinated by a locally installed helper application.

## 2. Scope
- **In scope:** Windows helper app, DX12 overlay DLL, communication bridge from the EF Map web UI, packaging and first-run experience, feasibility spike that renders arbitrary content in-game.
- **Out of scope (for now):** Production UI design, full routing workflow inside the overlay, multi-client coordination, anti-cheat negotiations.

## 3. Assumptions
- EVE Frontier remains free of anti-cheat/anti-injection protections or the developers explicitly permit this helper.
- Users are comfortable installing a signed helper app and launching EF Map in a desktop browser.
- We control distribution of the overlay helper (no third-party storefront requirements).

## 4. Target User Flow
1. Player runs EVE Frontier (any display mode).
2. Player opens the EF Map web app in a browser and calculates a route.
3. Player clicks **“Open in game overlay”**.
4. Browser triggers the local helper (custom protocol + localhost API).
5. Helper injects the overlay DLL into the existing EF process; overlay appears and can be moved/resized.
6. When finished, player hides or detaches the overlay via hotkey or web UI.

## 5. Architecture Overview
| Component | Responsibilities |
| --- | --- |
| Web app (unchanged core) | Adds overlay CTA, detects helper availability (poll localhost), serializes minimal payload (e.g., current route ID). |
| Overlay helper (tray app) | Registers custom URL protocol, hosts localhost HTTP endpoint, injects overlay DLL on demand, manages updates and settings. |
| DX12 overlay DLL | Hooks `IDXGISwapChain` present, renders overlay (ImGui or similar), handles input routing, exposes IPC back to helper/web. |
| Packaging & updates | MSI/MSIX installer, Authenticode signing, auto-update channel (optional).

### Communication pairing
- **Custom protocol** `ef-overlay://attach?session=<token>` for explicit actions from the browser.
- **Local HTTP API** `http://127.0.0.1:<port>/status`, `/attach`, `/dismiss` for health checks and richer commands.

## 6. Progress Summary (2025-10-13)
- ✅ **Helper + injector MVP** – C++ helper boots, exposes HTTP API, launches overlay smoke script, injects DX12 module with hotkey toggle.
- ✅ **DX12 overlay hook** – Swap-chain hook renders ImGui window, handles input capture (F8 toggle, drag/move, edge resize) without impacting gameplay.
- ✅ **Automation** – PowerShell smoke script coordinates helper launch, payload post, and DLL injection for quick manual validation.
- ✅ **Overlay state v2 schema + event queue** – Shared-memory schema updated with player marker, highlights, camera pose, HUD hints, follow flag; overlay ↔ helper event ring buffer published via shared memory + HTTP drain endpoint for testing.
- ✅ **Log watcher groundwork** – Catalog resolver and shared schema fields are ready for live telemetry (system name → canonical ID); integration work now leads roadmap.
- ⏸️ **Native starfield renderer polish** – Camera-aligned renderer exists but visual tuning is paused after artifact investigation; resume once helper-first backlog lands.
- ▢ **Installer & signing** – Deferred until we stabilize feature set; helper currently launched manually.
- ▢ **Browser CTA** – Web → helper bridge still manual; custom protocol & detection flow to be designed.

## 7. Phase 2 – Helper-first execution (Active)
Focus: deliver helper-driven wins (tray shell, log watcher, telemetry, browser bridge) before revisiting native visual polish. The starfield renderer remains in place but is paused until these user-facing capabilities ship.

> **Guiding principle:** keep pilots informed without alt-tabbing. Invest in automation, telemetry, and control surfaces that the helper can deliver immediately, while preserving the renderer as a follow-up polish item.

### 7.0 Helper runtime UX shell *(in-flight)*
Deliver a lightweight tray experience around the existing helper runtime so operators can launch, monitor, and control the overlay without touching console windows.

1. **Runtime extraction** – Factor current helper process control into a reusable `HelperRuntime` that can be driven by both console and tray entry points.
2. **Tray UI scaffold** – Add a notification-area app with start/stop, inject, and "post sample state" commands, plus status indicator for overlay attachment and event activity.
3. **Smoke tooling refresh** – Update `tools/overlay_smoke.ps1` to launch the tray shell automatically and exercise sample payload + injection flows.
4. **Diagnostics hooks** – Surface quick links for logs/config folders and report last error in the tray tooltip to simplify manual testing.

> Packaging/signing remain deferred; this milestone ships a developer-quality shell only, ensuring future features (log watcher, event bridge) have a visible home.

### 7.1 Game log watcher & location telemetry
1. **Log watcher ingestion**
   - Hook gamelog rotation, parse system-change lines, and reuse the existing resolver to publish canonical system IDs.
   - Emit helper heartbeat fields (e.g., last seen system, time since update) for overlay and browser consumption.
2. **Overlay/state wiring**
   - Pipe location updates into shared memory, expose opt-in controls via tray toggle, and relay events to the web client.
3. **Resilience pass**
   - Guard against missing logs, chat spam, multi-client conflicts; document failure modes in helper diagnostics.

### 7.2 Combat & activity telemetry
1. **Combat telemetry overlay** *(helper & overlay)*
   - Tail combat logs to aggregate inbound/outbound DPS in rolling windows and surface quick-glance charts inside the overlay.
   - Provide ImGui-based gauges/graphs, reset heuristics (dock, jump), and privacy controls for pilots who opt out.
2. **Session tracking modules**
   - Capture session duration, death/jump milestones, and mining yield hooks for future analytics; publish to overlay + browser bridge.

### 7.3 Browser integration & event bridge
1. **Helper discovery** – Detect helper availability from the web app, expose “Open in overlay” CTA, and design fallback copy when helper is offline.
2. **Event loop** – Forward overlay events (waypoint complete, opacity toggle) through the helper to the browser via authenticated HTTP/WebSocket channel.
3. **CTA polish** – Provide in-app prompts linking to helper download/install once packaging is ready; for now keep scoped to developer flows.

### 7.4 Native renderer polish *(paused until helper-first goals complete)*
1. **Artifact fix & tuning** – Revisit the diagonal starfield issue once telemetry milestones ship; add waypoint markers, selection glow, and performance instrumentation as follow-up work.
2. **HUD controls** – Resume ImGui control surface for follow-mode, opacity, and route actions after renderer visuals are stable.

### 7.5 Packaging & UX (later)
1. **Installer & production tray polish** *(helper)*
   - Convert the developer tray shell into a signed distribution (MSI/MSIX) once core features stabilize.
   - Layer in auto-update, first-run guidance, and per-display profiles for end users.
   - Fold settings panes (hotkeys, log watcher toggles) into the tray UI once the underlying capabilities are complete.

## 8. Tooling & Tech Stack
- Language: C++20 for DLL & helper core (optionally C# for helper UI via WinUI 3).
- Hooking: MinHook or Microsoft Detours (DX12 friendly).
- UI: ImGui (prototype) → evaluate Ultralight/CEF for HTML rendering.
- Installer: WiX Toolset (MSI) or MSIX + self-updater (Squirrel/Electron-builder) later.
- Signing: OV/EV Authenticode certificate via DigiCert/Sectigo.
- Logging: spdlog / ETW for DLL; helper log file under `%LOCALAPPDATA%/EFOverlay`.

## 9. Risks & Mitigations
| Risk | Mitigation |
| --- | --- |
| Future anti-cheat introduction | Keep relations with devs, add safety kill-switch, disable overlay if blocked. |
| GPU instability or crashes | Minimal overlay pipeline, robust error handling, ability to unhook quickly. |
| Security concerns (malicious sites hitting helper endpoint) | Require CSRF tokens, origin checks, short-lived session keys. |
| User trust | Signed binaries, transparent documentation, opt-in install/uninstall, open-source helper code. |
| Multi-client conflicts | Support per-process attach requests, guard against double injection. |

## 10. Open Questions
- Preferred helper UI framework (WinUI vs native Win32).
- Should overlay auto-attach when EF launches or stay opt-in per session?
- Long-term plan for delivering overlay UI (ImGui vs HTML to texture). Investigate HTML embedding cautiously (WebView2/CEF carry large footprints and potential anti-cheat scrutiny) once native renderer is stable.
- Deployment pipeline hosting (GitHub Releases vs custom CDN).
- How to synchronize route data securely (JWT token exchange? local ephemeral key?).

## 11. Next Steps
1. Ship the helper tray shell MVP (runtime extraction, tray UI, smoke tooling refresh).
2. Prototype the helper log watcher + location toggle, exposing status through the tray.
3. Bridge helper event queue to EF-Map web client once the tray/log watcher are stable.
4. Stand up the combat telemetry overlay using the shared watcher pipeline.
5. Polish the native renderer (waypoint markers, selection glow, performance instrumentation).
6. Design browser CTA & helper discovery flow; mirror plan updates in `EF-Map-main`.
7. Revisit packaging milestone once native rendering & event loop prove stable.
