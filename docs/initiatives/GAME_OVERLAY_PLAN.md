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

## 6. Progress Summary (2025-10-15)
- ✅ **Helper + injector MVP** – C++ helper boots, exposes HTTP API, launches overlay smoke script, injects DX12 module with hotkey toggle.
- ✅ **DX12 overlay hook** – Swap-chain hook renders ImGui window, handles input capture (F8 toggle, drag/move, edge resize) without impacting gameplay.
- ✅ **Automation** – PowerShell smoke script coordinates helper launch, payload post, and DLL injection for quick manual validation.
- ✅ **Overlay state v2 schema + event queue** – Shared-memory schema updated with player marker, highlights, camera pose, HUD hints, follow flag; overlay ↔ helper event ring buffer published via shared memory + HTTP drain endpoint for testing.
- ✅ **Log watcher groundwork** – Existing local-chat parser already resolves system IDs from rotating gamelogs; helper reports log locations for diagnostics.
- ✅ **EF helper panel preview** – Web app now surfaces a helper panel and Pages preview for validating status flows before production deployment.
- ✅ **Live follow mode bridge** – Helper now streams the player’s current system to the web app; follow toggles stay in sync and recenter EF Map automatically.
- 🟡 **Mining telemetry UI pass** – Yield parsing, rate aggregation, and first overlay widgets are implemented and undergoing final validation with live sessions.
- ⏸️ **Native starfield renderer polish** – Camera-aligned renderer exists but visual tuning is paused after artifact investigation; revisit after telemetry polish.
- ⏸️ **3D starmap replication** – Deprioritized in favor of telemetry and combat tooling; no near-term milestones scheduled.
- ▢ **Installer & signing** – Deferred until we stabilize feature set; helper currently launched manually.
- ▢ **Browser CTA** – Web → helper bridge still manual; custom protocol & detection flow to be designed.

## 7. Roadmap (Q4 2025)
The helper, overlay, and EF map web panel will continue to evolve together. All gameplay telemetry stays on the pilot’s machine; Cloudflare usage metrics remain lightweight and limited to helper adoption counters (e.g., connected sessions).

### ✅ Phase 1 – Helper ↔ UI grounding
- Validated helper state reporting across the local helper, overlay HUD, and EF helper panel.
- Delivered synchronized follow-mode toggles and system recentering between helper and web app.
- Hardened tray/runtime UX so operators can launch the helper without stray console windows.

**Validation:** helper status transitions display correctly in the Pages preview; usage counters increment in `/api/stats`; tray diagnostics show log paths and last update timestamps.

### 🟡 Phase 2 – Mining telemetry foundation *(validation)*
- Extend the log watcher to accumulate mining yield totals and rolling rates (per ore type) using the real game logs—no synthetic fixtures required.
- Surface graphs in the helper window and overlay HUD, with a session reset control.
- Keep telemetry local; only aggregate adoption metrics (helper connected) are reported to Cloudflare for health tracking.

**Validation target:** live mining sessions update graphs in both helper and overlay; reset clears session totals; smoke script confirms no FPS regressions.

### Phase 3 – Combat telemetry *(queued)*
- Reuse the mining pipeline to track DPS in/out and combat events.
- Provide configurable charts and peak indicators in helper/overlay UIs.
- Reconfirm privacy posture (local-only) and reuse minimal usage metrics.

**Validation:** live combat logs update HUD charts; helper diagnostics show recent event counts; overlay remains stable.

### ✅ Phase 4 – Live location follow mode
- Follow mode now streams the pilot’s current system from the helper into EF Map, recentering automatically.
- Toggles remain in sync across helper, overlay, and web UI; temporary user interaction pauses are honored.

**Validation:** system jumps recentre the map; toggles propagate state within one heartbeat; manual interaction resumes control without breaking sync.

### Phase 5 – Bookmark & route enhancements *(pending design)*
- Add an in-game overlay action to create EF Map bookmarks (local helper persists until a remote storage decision is made).
- Display the “next system” in the active route within the overlay (read-only; route export stays manual via notes).

**Validation:** overlay action creates bookmarks visible in the browser; helper logs track the command; route indicator updates with each jump.

### Phase 6 – Packaging & pre-release readiness
- Harden the helper tray experience, bundle with an installer, and update download CTA/links inside the EF helper panel.
- Document installation and smoke steps in both repos; sign binaries when feature set stabilizes.
- Stand up a signed installer workflow:
	- Choose installer tech (WiX MSI vs MSIX vs Squirrel) and wire into overlay CI (current lean: Microsoft Store signing pipeline via Azure Code Signing).
	- Acquire and store Authenticode cert securely; sign helper binaries + installer artifacts.
	- Host versioned installers on Cloudflare (R2 + Pages Worker) with HTTPS download links surfaced in the helper panel.
- Convert the helper into a tray-first runtime:
	- Wrap injector/overlay control inside a GUI-subsystem process so no console window appears.
	- Register startup (Run key or Startup folder) and tray icon actions (launch overlay, view logs, quit) during install.
- Bridge browser actions to the installed helper:
	- Register a custom protocol (`ef-helper://`) for first-run launch from the web app.
	- Keep/extend the localhost API so the web panel can detect running helpers and trigger overlay attach when available.
- Plan for updates:
	- Publish a JSON manifest describing the latest installer; helper tray checks and prompts when a new build is available.
	- Evaluate automatic differential updates once the installer tech is chosen.

**Validation:** installer builds and installs cleanly; first-run walkthrough launches helper and validates overlay attachment; custom protocol opens the tray host from the web panel; decision logs capture release readiness.

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
1. Finish Phase 1 polish: helper tray UX, synchronized status in web panel, minimal usage metrics.
2. Kick off Phase 2 mining telemetry work (helper aggregation + overlay graphs) using live mining sessions for testing.
3. Plan combat telemetry implementation once mining pipeline is validating.
4. Draft UI flows for follow-mode toggles to unblock Phase 4 development.
5. Keep packaging on hold until telemetry and follow mode land.
