<!-- This plan mirrors `EF-Map-main/docs/initiatives/GAME_OVERLAY_PLAN.md`. Keep both copies synchronized whenever the roadmap changes. -->

# EF-Map Game Overlay Initiative

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

## 6. Phase 1 – Feasibility Spike ("Can we draw anything?")
**Goals**
- Launch helper, inject into a running EF Frontier process without restart.
- Render a placeholder window (e.g., ImGui panel with static text) inside the game across windowed and fullscreen modes.
- Demonstrate round-trip command: browser button → helper → overlay appear.
- Package helper with installer, tray icon, and basic settings dialog.

**Tasks**
1. **Helper skeleton**
   - Create Win32/WinUI helper with tray icon, logging, config file.
   - Register `ef-overlay://` custom protocol, start HTTP listener (localhost).
2. **Injection pipeline**
   - Implement process discovery (by executable name).
   - Inject DLL via `CreateRemoteThread` + `LoadLibrary` or Detours loader.
   - Add safety checks (multiple clients, x86 vs x64).
3. **DX12 hook**
   - Hook swap chain present/resize, set up command queue.
   - Integrate ImGui (or similar) showing static panel.
   - Add hotkey to toggle overlay.
4. **Browser integration**
   - Add helper detection snippet in EF Map (poll `/status`).
   - Add "Open in game" button that triggers `ef-overlay://attach` with sample payload.
5. **Packaging**
   - Create MSI/MSIX installer, sign helper and DLL (Auth23 code signing).
   - Document installation and first-run steps.

**Success criteria**
- Overlay appears above game content devices (windowed + fullscreen borderless + exclusive fullscreen).
- No crashes or noticeable frame drops (>1% frame time delta in testing).
- Helper logs show attach/detach lifecycle; browser receives status feedback (e.g., success toast).

**Deliverables**
- Helper + DLL source tree.
- Build/release pipeline producing signed installer.
- Internal usage doc / troubleshooting guide.

## 7. Phase 2 – Content Integration (Future)
- Embed EF Map overlay UI (e.g., React panel rendered via headless Chromium-to-texture or native ImGui components).
- Secure communications (session tokens between web app and helper).
- Support dynamic updates (route changes, waypoints) via WebSocket or long polling.
- Implement overlay configuration UI (opacity, positioning presets) in helper tray menu.

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
- Long-term plan for delivering overlay UI (ImGui vs HTML to texture).
- Deployment pipeline hosting (GitHub Releases vs custom CDN).
- How to synchronize route data securely (JWT token exchange? local ephemeral key?).

## 11. Next Steps
1. Approve feasibility scope and allocate time for spike (estimate: 2–3 weeks).
2. Acquire signing certificate and define installer strategy.
3. Start prototype repository (e.g., `overlay-helper/` with helper + DLL + docs).
4. Once prototype succeeds, revisit this plan to detail Phase 2 content milestones.
