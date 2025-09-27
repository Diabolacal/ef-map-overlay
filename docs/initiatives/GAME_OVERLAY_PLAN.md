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

## 6. Progress Summary (2025-09-27)
- ✅ **Helper + injector MVP** – C++ helper boots, exposes HTTP API, launches overlay smoke script, injects DX12 module with hotkey toggle.
- ✅ **DX12 overlay hook** – Swap-chain hook renders ImGui window, handles input capture (F8 toggle, drag/move, edge resize) without impacting gameplay.
- ✅ **Automation** – PowerShell smoke script coordinates helper launch, payload post, and DLL injection for quick manual validation.
- ▢ **Installer & signing** – Deferred until we stabilize feature set; helper currently launched manually.
- ▢ **Browser CTA** – Web → helper bridge still manual; custom protocol & detection flow to be designed.

## 7. Phase 2 – Native Content Integration (Next milestones)
Focus: transform the overlay from a text HUD into a native starfield view synchronized with the EF-Map web app, while laying groundwork for bidirectional controls.

> **Guiding principle:** the overlay should complement, not clone, the browser app. Prioritize context-aware cues (live system updates, safety alerts, quick actions) that keep pilots in-game without alt-tabbing. Multi-monitor users already have full EF-Map; our differentiator is immediacy and automation tied to game state.

### 7.1 Data & messaging contracts
1. **Overlay state v2 spec** *(EF-Map-main & helper)*
   - Expand `overlay_state` schema with camera pose, highlighted systems, player marker, and UI hints.
   - Define binary layout (size, versioning) for shared-memory payloads; budget for larger textures/buffers.
2. **Overlay ↔ helper events** *(helper & overlay)*
   - Add event queue (shared memory or named pipe) for overlay-generated interactions (button clicks, text submit).
   - Helper exposes `/overlay/event` endpoint (HTTP/WebSocket) to forward events to the browser with session auth.

### 7.2 Rendering pipeline
3. **Star catalog asset** *(EF-Map-main tooling)*
   - Export compact binary (positions, system_id, name hashes) from existing map data.
   - Helper packages asset and provides checksum/version handshake to overlay.
4. **Native starfield renderer spike** *(overlay DLL)*
   - Upload static buffers (stars as instanced point sprites) and draw polyline routes with lightweight shaders.
   - Match EF-Map aesthetics (brightness falloff, selection glow) and ensure <1 ms GPU cost per frame.
5. **HUD controls** *(overlay DLL & helper)*
   - Build ImGui panel with buttons/toggles for follow-mode, overlay opacity, route step actions.
   - Confirm focus handling (keyboard navigation, text input) and end-to-end event delivery back to the web app.

### 7.3 Live data inputs
6. **Game log watcher** *(helper)*
   - Monitor EVE Frontier gamelog directory, parse system-change lines, resolve to canonical IDs.
   - Publish player location to overlay state (optional toggle) and propagate to EF-Map web client when enabled.
   - Evaluate additional log sources (chat/local intel) for high-value alerts without overwhelming players.
7. **Browser integration** *(EF-Map-main)*
   - Detect helper availability, expose “Open in overlay” CTA, push state updates via HTTP/WebSocket.
   - Handle overlay events (e.g., mark waypoint done) and refresh UI accordingly.

### 7.4 Packaging & UX (deferred until feature complete)
8. **Tray application & installer** *(helper)*
   - Wrap helper services in tray UI (start/stop overlay, enable log tracking, configure shortcuts).
   - Ship MSI/MSIX installer with Authenticode signing and auto-update channel.
   - Provide per-display profiles (single vs multi-monitor) so overlay defaults match player setups.

## 8. Tooling & Tech Stack
- Language: C++20 for DLL & helper core (optionally C# for helper UI via WinUI 3).
- Hooking: MinHook or Microsoft Detours (DX12 friendly).
- UI: ImGui (prototype) → evaluate Ultralight/CEF for HTML rendering.
- Installer: WiX Toolset (MSI) or MSIX + self-updater (Squirrel/Electron-builder) later.
- Signing: OV/EV Authenticode certificate via DigiCert/Sectigo.
- Logging: spdlog / ETW for DLL; helper log file under `%LOCALAPPDATA%/EFOverlay`.

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
1. Finalize overlay state v2 schema & event channel contract (EF-Map-main + helper).
2. Implement star catalog export and native renderer spike (overlay repo).
3. Prototype helper log watcher + location toggle.
4. Design browser CTA & helper discovery flow; mirror plan updates in `EF-Map-main`.
5. Revisit packaging milestone once native rendering & event loop prove stable.
