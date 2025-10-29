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

## 6. Progress Summary (2025-10-29)
- ✅ **Helper + injector MVP** – C++ helper boots, exposes HTTP API, launches overlay smoke script, injects DX12 module with hotkey toggle.
- ✅ **DX12 overlay hook** – Swap-chain hook renders ImGui window, handles input capture (F8 toggle, drag/move, edge resize) without impacting gameplay.
- ✅ **Automation** – PowerShell smoke script coordinates helper launch, payload post, and DLL injection for quick manual validation.
- ✅ **Overlay state v2 schema + event queue** – Shared-memory schema updated with player marker, highlights, camera pose, HUD hints, follow flag; overlay ↔ helper event ring buffer published via shared memory + HTTP drain endpoint for testing.
- ✅ **Log watcher groundwork** – Existing local-chat parser already resolves system IDs from rotating gamelogs; helper reports log locations for diagnostics.
- ✅ **EF helper panel preview** – Web app now surfaces a helper panel and Pages preview for validating status flows before production deployment.
- ✅ **Live follow mode bridge** – Helper now streams the player's current system to the web app; follow toggles stay in sync and recenter EF Map automatically.
- ✅ **Visited systems tracking** – All-time + session tracking complete with web app visualization (orange star coloring, hover labels, session dropdown).
- ✅ **Mining telemetry web integration** – Full web app integration of mining telemetry in Helper panel (Mining tab):
  - Session totals (volume, ore types, duration)
  - Per-ore breakdown table (session + recent 120s window)
  - Mining rate sparkline (m³/min) with 250ms interpolation, 7s hold + 3s decay
  - Theme-aware canvas rendering with smooth 60fps scrolling
  - Reset Session button (atomic reset of both mining and combat)
- ✅ **Combat telemetry web integration** – Full web app integration of combat telemetry in Helper panel (Combat tab):
  - Damage dealt/taken totals
  - Hit quality breakdown table (Penetrating, Smashing, Standard, Glancing, Miss)
  - **Current DPS display** (Dealt/Taken/Peak using recent window, not session average)
  - Combat DPS sparkline (dealt = accent, taken = red, raw point plotting)
  - Theme-aware rendering with IntersectionObserver for proper canvas sizing
  - Reset Session button (atomic reset of both mining and combat)
- ✅ **Custom sparkline component** (`Sparkline.tsx`, 618 lines):
  - Canvas-based with requestAnimationFrame for smooth scrolling
  - Right-to-left time axis (most recent on right, matching overlay)
  - Mining: 250ms interpolation + decay logic
  - Combat: Raw data point plotting (no interpolation)
  - IntersectionObserver fixes tab-switching width measurement issues
- ✅ **Phase 5 complete** – All 7 features shipped:
  - Visited systems tracking (all-time + session, web visualization)
  - Session history dropdown (timezone-aware, filtering)
  - Next system in route (overlay display, auto-advance, clipboard)
  - Bookmark creation (personal + tribe, WebSocket integration)
  - Follow mode toggle (already implemented in Phases 1-4)
  - Legacy cleanup (removed debug buttons and dev notes)
  - **Proximity Scanner (P-SCAN)** – Scan for network nodes after deploying portable structures (refinery/printer), with World API v2 integration, prerequisite checks (follow mode + auth), and distance conversion (km/light seconds)
- ⏸️ **Native starfield renderer polish** – Camera-aligned renderer exists but visual tuning is paused after artifact investigation; revisit after telemetry polish.
- ⏸️ **3D starmap replication** – Deprioritized in favor of telemetry and combat tooling; no near-term milestones scheduled.
- ▢ **Installer & signing** – Deferred until we stabilize feature set; helper currently launched manually.

### Production Deployment Policy *(2025-10-29)*
**All Phase 5 features PLUS signed installer (Phase 6) must be complete before deploying to EF Map production.**

**Rationale:** The first-run user experience must be smooth and low-friction. Users clicking "EF Helper" in the main website should encounter:
- A signed, installable package (no manual build steps)
- Clear installation flow with tray integration
- All routing/navigation features working out-of-box

**Preview deployments** (Cloudflare Pages branches) will continue for validation, but production merge blocked until:
1. Phase 5 features complete (Next System in Route, Bookmark Creation, Legacy Cleanup)
2. Phase 6 installer + signing complete
3. Helper panel documentation updated with download links
4. First-run smoke test validates end-to-end flow

## 7. Roadmap (Q4 2025)
The helper, overlay, and EF map web panel will continue to evolve together. All gameplay telemetry stays on the pilot’s machine; Cloudflare usage metrics remain lightweight and limited to helper adoption counters (e.g., connected sessions).

### ✅ Phase 1 – Helper ↔ UI grounding
- Validated helper state reporting across the local helper, overlay HUD, and EF helper panel.
- Delivered synchronized follow-mode toggles and system recentering between helper and web app.
- Hardened tray/runtime UX so operators can launch the helper without stray console windows.

**Validation:** helper status transitions display correctly in the Pages preview; usage counters increment in `/api/stats`; tray diagnostics show log paths and last update timestamps.

### ✅ Phase 2 – Mining telemetry foundation *(complete)*
- Extended log watcher to accumulate mining yield totals and rolling rates (per ore type) using real game logs.
- Implemented session persistence via JSON (`%LocalAppData%\EFOverlay\data\mining_session.json`) with restore on helper restart.
- Rate calculation uses 10-second rolling window with EMA smoothing (α=0.3) for stable, smooth sparkline curves during active mining.
- Decay behavior after mining stops: computed via interpolation (4s hold + 6s linear decay to zero), displayed as vertical drop after scrolling left (accurate representation).
- Surfaced graphs in helper window and overlay HUD with session reset control.
- Telemetry remains local; only aggregate adoption metrics (helper connected) reported to Cloudflare.

**Validation complete:** live mining sessions update graphs smoothly; session totals persist across restarts; reset clears all data; sparkline shows clean curves without jitter.

### ✅ Phase 3 – Combat telemetry *(complete)*
- Full combat damage tracking with dual-line sparkline (dealt/taken) and hit quality analytics for in-session visibility.
- Implemented DPS calculation via 10-second rolling window showing weapon fire patterns; 2-second activity detection for fast tail-off when combat ends.
- Dual-line sparkline (~144px, 2x mining height): orange for dealt damage, red for taken damage, 2-minute window.
- Hit quality tracking: Miss, Glancing, Standard, Penetrating, Smashing tracked separately for dealt/taken (10 counters total).
- Session tracking: sessionStartMs, sessionDurationSeconds, cumulative damage totals (displayed during active session).
- UI displays: combat totals, hit quality breakdown, session duration, current DPS, peak DPS, hover tooltips showing time + both DPS values.
- Reset button clears in-memory session state and HUD display; intentionally lightweight (no disk persistence) for combat site DPS monitoring.
- Technical achievements:
  - Eliminated 3-4Hz peak oscillation by removing interpolation and plotting raw DPS data points directly.
  - Implemented stable peak tracking with 1% decay and quantization to prevent rescaling bounce.
  - Added miss-specific parsing to detect misses ("you miss X") separately from normal hits.
  - Fast 2-second tail-off when combat ends (instead of 20-30s delay).

**Validation complete:** live combat updates sparkline with sharp weapon fire spikes; peaks stay rock-solid (zero oscillation); hit quality counters increment correctly; fast tail-off when combat ends; hover tooltips accurate; reset button clears display correctly. Feature complete for overlay use case; more extensive telemetry may be surfaced in web app later.

### ✅ Phase 4 – Live location follow mode
- Follow mode now streams the pilot’s current system from the helper into EF Map, recentering automatically.
- Toggles remain in sync across helper, overlay, and web UI; temporary user interaction pauses are honored.

**Validation:** system jumps recentre the map; toggles propagate state within one heartbeat; manual interaction resumes control without breaking sync.

### Phase 5 – Route navigation & bookmarking ✅ **COMPLETE**

#### Feature 1: Visited Systems Tracking ✅ **COMPLETE** *(2025-10-28)*
**Purpose:** Enable players to track system visits across all-time and per-session to avoid backtracking and visualize exploration patterns on EF Map.

**Status: PRODUCTION READY**

**Implementation Summary:**
- ✅ **Helper backend**: All-time tracking implemented (toggle, GET endpoints working)
- ✅ **Helper backend**: Session tracking fully implemented (start/stop/active session endpoints)
- ✅ **Bug fix (2025-10-27)**: Resolved deadlock in start-session endpoint (changed `std::mutex` to `std::recursive_mutex`)
- ✅ **Web app UI**: Toggle tracking button working (HTTP 200)
- ✅ **Web app UI**: Session display shows formatted date/time with timezone awareness
- ✅ **Web app UI**: Merged Start/Stop buttons into single toggle button
- ✅ **Web app visualization**: Visit counts appear in hover labels immediately (coexist with planet count + distance)
- ✅ **Session history dropdown**: 14 sessions loaded, all-time/active/past selection working
- ✅ **Star coloring integration**: Visited systems use orange accent (same as region highlights)

**Technical Achievements:**
- Pure function architecture: Label creation functions don't check global state (no stale closures)
- Refs optimization: Event handlers use refs to access latest data without recreation on every 30s poll
- Inline distance calculation: Fixed "only works when hovering ring edge" bug
- Three-way label integration: Planet count + Visit count + Distance all work together seamlessly

**Known Cosmetic Issue (Documented, Non-Blocking):**
- Star coloring can take up to ~10 seconds (variable, sometimes instant) when toggling between modes (Planet Count ↔ Show Visited Systems, or Highlight Region toggles)
- Root cause unknown after exhaustive investigation (not poll interval, not event listeners, not GPU buffers)
- Impact minimal - stars DO update correctly, just with timing variability
- Decision: Document and ship - functionality correct, delay purely cosmetic

**Data Storage:**
- All-time: `%LocalAppData%\EFOverlay\data\visited_systems.json` (system ID + visit count)
- Sessions: `%LocalAppData%\EFOverlay\data\sessions\session_<id>.json` (timestamped, per-session counts)
- Privacy: All data stays local on pilot's machine; no upload to Cloudflare

**API Endpoints (Helper):**
- `GET /session/visited-systems?type=all` → all-time tracking data
- `GET /session/visited-systems?type=session&session_id=X` → specific session
- `GET /session/visited-systems?type=active-session` → currently active session
- `POST /session/visited-systems/reset-all` → clear all-time data
- `POST /session/start-session` → create new session
- `POST /session/stop-session` → end active session

**Web App Integration:**
- Helper panel tab structure with Overview/Mining/Combat/Connection tabs
- Visit count labels appear on hover when "Show visited systems on map" enabled
- Session history dropdown with 14+ sessions, all-time/active/past filtering
- Orange star coloring for visited systems (integrated into main coloring effect with priority ordering)

**Validation Complete:**
- All-time tracking: Enable toggle → visit systems → reload helper → counts persist ✅
- Session tracking: Start session → visit systems → stop session → timestamps + counts correct ✅
- Map visualization: Visit counts show immediately on hover (no need to toggle other features first) ✅
- Session history: Dropdown loads all sessions, formatting correct with timezone ✅
- Star coloring: Visited systems use orange accent with correct priority (visited → planet count → default) ✅

**Preview URL:** https://feature-visit-labels-v5-refs.ef-map.pages.dev

#### Feature 2: Session History Dropdown ✅ **COMPLETE** *(2025-10-27)*
**Purpose:** Allow users to view and select historical tracking sessions in the Helper panel.

**Status: COMPLETE AND DEPLOYED**

- Session dropdown displays all past sessions with formatted timestamps (user's timezone)
- Filter buttons: All-time / Active Session / Past Sessions
- 14+ sessions loading correctly
- Integrated with visited systems visualization

**Validation Complete:** All sessions display correctly, filtering works, timezone formatting accurate ✅

---

#### Feature 3: Next System in Route ✅ **COMPLETE** *(2025-10-28)*
**Purpose:** Display immediate next waypoint with full details and quick clipboard access for in-game link creation.

**Status: COMPLETE AND DEPLOYED**

**UX Flow (zero-friction):**
- When user calculates a route in EF Map, route data **automatically** POSTs to helper (no button click required) ✅
- Overlay immediately displays next system details ✅
- As player jumps, overlay auto-advances to show upcoming waypoint ✅
- User can copy system ID for in-game link creation at any time ✅

**Display Format:**
- **Single-line format:** `Next in route: E5J-F55 (jump) - 59.37 ly | hop 2/4 | 1 planet | 0 nodes  [Copy ID]`
- **Data shown:** System name, connection type (gate/jump), distance (ly), hop position, planet count, network nodes
- **Auto-advance:** When follow mode detects system jump, overlay updates pointer to next waypoint ✅
- **Destination handling:** When one jump from destination, displays "Destination: [System Name]" ✅
- **Copy button:** "Copy ID" places EVE Online link format on clipboard: `<a href="showinfo:5//SYSTEM_ID">SYSTEM_NAME</a>` ✅

**Implementation Complete:**
- ✅ Overlay UI rendering complete (`overlay_renderer.cpp` lines 720-820)
- ✅ Route schema defined (`RouteNode` struct with all required fields)
- ✅ Helper endpoint implemented (`POST /overlay/state`)
- ✅ Win32 clipboard integration with EVE link format
- ✅ Auto-advance logic on system jump (helper preserves route across log watcher updates)
- ✅ Web app route calculation auto-POSTs route data to helper
- ✅ **Bug fix (2025-10-28):** Helper now merges log watcher position updates with web app route data (preserves `active_route_node_id`)
- ✅ **Bug fix (2025-10-28):** Overlay renderer displays `active_route_node_id` directly (removed double-increment)
- ✅ **Polish (2025-10-28):** Fixed character encoding issue (em dash → hyphen), consolidated display on single line
- ✅ **CRITICAL FIX (2025-10-29):** Implemented bidirectional state preservation to prevent web app from clearing log watcher's player_marker (fixed 30-second delay bug)

**Technical Details:**
- **Schema:** `OverlayState` contains `route` (vector of `RouteNode`) + `active_route_node_id` (current position) + `player_marker` (current location)
- **Endpoint:** `POST http://localhost:<helper-port>/overlay/state` (accepts full OverlayState JSON)
- **Auto-send trigger:** Hooked into routing calculation success in `App.tsx` route auto-send logic
- **Data mapping:** Web app transforms route → `RouteNode[]` with system_id, display_name, distance_ly, via_gate, planet_count, network_nodes, route_position, total_route_hops
- **State persistence (CRITICAL):** Helper `ingestOverlayState()` uses **bidirectional preservation**:
  - When source="http" (web app): Preserve `player_marker` from log watcher (authoritative for position)
  - When source="log-watcher": Preserve `route` from web app (authoritative for routing)
  - **This prevents mutual overwriting** that caused 30-second delay before player position appeared
- **Clipboard format:** EVE Online in-game link format enables direct paste into game chat/notes

**Validation Complete:**
- Route calculation sends data to helper ✅
- Overlay displays correct next hop initially ✅
- Overlay persists correct hop after follow mode updates (no reversion) ✅
- Copy ID produces EVE link format ✅
- All route details displayed (distance, hop position, planet count, nodes) ✅
- **Player position displays within 2 seconds of page load** (no more 30-second delay) ✅

**Critical Debugging Lesson (2025-10-29):**
After 3+ hours of failed attempts (immediate log parsing, WebSocket GET on connect), the breakthrough came from using **Chrome DevTools MCP** to inspect actual network traffic. This revealed GET /overlay/state was returning NO `player_marker` field, leading to discovery of the state overwrite bug. **Key takeaway:** Use Chrome DevTools MCP proactively for browser-side debugging - inspecting real responses beats assumptions.

#### Feature 4: Bookmark Creation ✅ **COMPLETE** *(2025-10-28)*
**Purpose:** One-click bookmark creation from in-game overlay with optional tribe sharing for player-created tribes.

**Status: COMPLETE AND DEPLOYED**

**Implementation Summary:**
- ✅ Overlay UI rendering with text input and "Add Bookmark" button
- ✅ Helper endpoint (`POST /bookmarks/create`) broadcasts via WebSocket to web app
- ✅ Web app receives bookmark request and adds to `userOverlayStore` (localStorage)
- ✅ System name extraction from `player_marker.display_name` (not just system_id)
- ✅ Text input styling (muted orange background)
- ✅ Button text polish ("Add Bookmark" instead of "Bookmark")
- ✅ Web app bookmark section removed (bookmarks ONLY in overlay, as requested)

**UX Flow:**
- User types optional notes in text input field
- Clicks "Add Bookmark" → helper extracts system_id + system_name from current overlay state
- Helper broadcasts to web app via WebSocket → `window.__efAddBookmark()` called
- Bookmark appears in personal folder immediately
- Future: Tribe checkbox will appear when auth state populated (not blocking MVP)

**UI (Overlay Overview tab):**
```
[Text input with muted orange background for notes]
[Add Bookmark] button
```

**Technical Details:**
- **Helper endpoint:** `POST http://localhost:<port>/bookmarks/create` accepts `{system_id, notes, for_tribe}`
- **WebSocket message:** `{"type":"bookmark_add_request", "payload":{"system_id":"...", "system_name":"...", "notes":"...", "for_tribe":false}}`
- **Web app handler:** `helperBridge.ts` receives message → calls `window.__efAddBookmark(systemId, systemName, defaultColor, notes)`
- **System name extraction:** Helper reads `player_marker.display_name` from `latestOverlayStateJson_` (added in hotfix 2025-10-28)
- **Storage:** Client-side localStorage via `userOverlayStore.add()`

**Validation Complete:**
- ✅ Bookmark created from overlay appears in web app personal folder
- ✅ System name populates correctly (not just system_id)
- ✅ Notes field works as expected
- ✅ Web app bookmark section removed (no duplication)

**Deferred (Future Phase):**
- Tribe checkbox (requires auth state population from web app → helper)
- Server-side tribe bookmark storage (requires authenticated POST to `/api/bookmarks/create`)

#### Feature 5: Follow Mode Toggle ✅ **COMPLETE** *(Phases 1-4)*
**Purpose:** Maintain existing follow mode control in Overview tab for quick access.

**Status: COMPLETE**

- Toggle button for enabling/disabling follow mode implemented in earlier phases
- No additional work required

#### Feature 6: Legacy Cleanup ✅ **COMPLETE** *(2025-10-23)*
**Purpose:** Remove debug/experimental buttons to keep UI focused and uncluttered.

**Status: COMPLETE**

**Decision Log Reference:** `EF-Map-main/docs/decision-log.md` → "2025-10-23 – Phase 5 partial implementation: Next System in Route + Legacy Cleanup"

**Removed:**
- ✅ "Send waypoint advance event" button (3D star map experiment)
- ✅ Notes section showing log locations and snippets (dev-only info)

**Result:** Overview tab focused on user-actionable features only

---

#### Feature 7: Proximity Scanner (P-SCAN) ✅ **COMPLETE** *(2025-10-29)*
**Purpose:** Enable players to scan for network nodes in their current solar system after deploying portable structures (refinery/printer).

**Status: COMPLETE AND DEPLOYED**

**Decision Log Reference:** `docs/decision-log.md` (overlay repo) → "2025-10-29 – P-SCAN Feature Complete"

**Implementation:**
- ✅ Helper endpoints: `GET /pscan/data`, `POST /pscan/data` for scan results persistence
- ✅ Web app integration: P-SCAN tab in Helper panel with World API v2 queries
- ✅ Overlay rendering: P-SCAN tab with results table and scan button
- ✅ Event flow: Overlay button → helper → WebSocket → web app → World API → helper → overlay display
- ✅ Prerequisite checks: Follow mode + authentication required (warnings + disabled button state)
- ✅ Distance units: Light seconds (≥0.01 ls = 2,998 km), otherwise kilometers
- ✅ Persistence: Scan data survives log-watcher updates (preserved alongside route data)

**UX Flow:**
1. Player deploys portable structure (refinery or printer) in system
2. Opens P-SCAN tab in overlay (F8) or web app Helper panel
3. Clicks "Scan Current System" button
4. Web app queries World API v2 for system data + smart assemblies
5. Calculates distances from player's structure to network nodes
6. Results appear in both overlay and web app with real-time sync

**Display Format (Overlay):**
- Table columns: Node | Owner | Distance
- Muted orange theme matching existing overlay design
- Auto-sorted by distance (closest first)
- Distance conversion: ≥0.01 ls shows as light seconds, otherwise kilometers

**Prerequisites (with warnings):**
- Follow mode must be enabled (tracks current system for API query)
- User must be authenticated (wallet connected)
- Button grayed out and warnings displayed when requirements not met

**Technical Details:**
- World API v2 endpoints:
  - `/v2/solarsystems/{id}` → system data with smart assemblies list
  - `/v2/smartassemblies/{id}` → assembly coordinates for distance calculation
- Player structure detection: Matches owner name "lacal" (fallback: most recent assembly)
- Distance calculation: Euclidean 3D distance from player structure to network nodes
- Event system: `PscanTriggerRequested` event type (overlay → helper WebSocket broadcast)
- Web app hook: `window.__efTriggerPscan()` exposed via useEffect in `HelperBridgePanel.tsx`

**Validation Complete:**
- Scan button triggers from overlay → web app executes scan → results appear in both UIs ✅
- Prerequisites checked correctly (follow mode + auth) with warnings displayed ✅
- Distance calculations accurate (km vs light seconds threshold) ✅
- Data persistence across log-watcher updates (30s intervals) ✅
- Button state synchronized (grayed when prerequisites not met) ✅

**Preview URL:** https://feature-pscan.ef-map.pages.dev

---

**Phase 5 Summary:** All 7 features complete and deployed to preview. Ready for Phase 6 (Packaging & Pre-release).

### Phase 6 – Packaging & pre-release readiness *(not started)*

#### Technical Approach
| Component | Implementation |
| --- | --- |
| Visited systems persistence | Helper maintains JSON files (all-time + per-session) updated on system jump detection. |
| Next system pointer | Overlay tracks current route index; advances on system jump event from follow mode. |
| Bookmark API | Helper exposes `/bookmarks/create` accepting `{system_id, notes, tribe: bool}` → forwards to EF Map worker. |
| Tribe membership check | EF Map worker returns user profile with tribe membership during authentication; helper caches for overlay decisions. |
| Copy to clipboard | Overlay uses Win32 `SetClipboardData()` for system ID copy action. |

#### Data Schemas

**Visited Systems (All-time):**
```json
{
  "version": 1,
  "tracking_enabled": true,
  "systems": {
    "30000001": {"name": "Tanoo", "visits": 42},
    "30000003": {"name": "Mahnna", "visits": 18}
  },
  "last_updated_ms": 1729700000000
}
```

**Visited Systems (Session):**
```json
{
  "version": 1,
  "session_id": "session_20251023_143022",
  "start_time_ms": 1729700000000,
  "end_time_ms": 1729710000000,
  "active": false,
  "systems": {
    "30000001": {"name": "Tanoo", "visits": 5},
    "30000003": {"name": "Mahnna", "visits": 3}
  }
}
```

**Bookmark Creation Request (Helper → EF Map):**
```json
{
  "system_id": "30000001",
  "notes": "Good mining spot",
  "tribe": false,
  "timestamp_ms": 1729700000000
}
```

#### API Endpoints (Helper)

- `GET /session/visited-systems?type=all` → returns all-time tracking data
- `GET /session/visited-systems?type=session&session_id=X` → returns specific session data
- `GET /session/visited-systems?type=active-session` → returns currently active session (if any)
- `POST /session/visited-systems/reset-all` → clears all-time tracking data
- `POST /session/start-session` → creates new timestamped session
- `POST /session/stop-session` → ends active session, finalizes timestamp
- `POST /bookmarks/create` → forwards bookmark to EF Map worker with auth token

#### Web App Integration (EF-Map-main)

- Worker endpoint: `POST /api/overlay-bookmark` accepts bookmark payload, validates auth, writes to KV.
- **Helper panel redesign:** Tab structure similar to Routing window with buttons at top:
  - **Overview tab:** Visited systems toggle status, active session indicator, helper connection status (focused on overlay features, NOT route duplication)
  - **Mining tab:** Mining telemetry display (placeholder for Phase 2 data)
  - **Combat tab:** Combat telemetry display (placeholder for Phase 3 data)
  - **Connection/Installation tab:** Helper detection status, retry button, log locations, installation guide (current helper panel content moves here)
- **Map visualization:** Orange star coloring for visited systems (consistent with region highlights/jump bubble range). Visit counts tracked but not displayed initially; potential future enhancement: reuse Route Systems window logic for visited list.

**Validation:**
- All-time tracking: Enable toggle → visit 5 systems → reload helper → verify counts persist.
- Session tracking: Start session → visit systems → stop session → verify session file contains correct timestamps and counts.
- Map visualization: Visit 5+ systems → verify orange star coloring appears on EF Map.
- Helper panel tabs: Verify all four tabs render correctly, Overview shows tracking status, Connection tab shows helper status + retry button.
- Next system: Calculate route in EF Map → verify overlay shows immediate next → jump to system → verify pointer advances (web app does NOT duplicate this).
- Copy ID: Click Copy button → paste in notepad → verify system ID appears (overlay only, not in web app).
- Personal bookmark: Add bookmark with notes → verify appears in EF Map personal folder.
- Tribe bookmark: Authenticate + join tribe → tick tribe checkbox → add bookmark → verify appears ONLY in tribe folder (not personal).
- No auth bookmark: Log out → verify tribe checkbox hidden → add bookmark → verify goes to personal folder.

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

## 12. Related Initiatives

### Telemetry Web Integration (EF-Map-main)
**Documentation**: `EF-Map-main/docs/initiatives/TELEMETRY_WEB_INTEGRATION.md`  
**Status**: Planning complete, implementation queued for post-Phase 5

**Purpose**: Surface mining and combat telemetry from the overlay helper in the EF-Map web app's Helper panel, enabling users with second monitors to view real-time session stats and sparklines without needing the in-game overlay.

**Scope**:
- Fetch telemetry data from existing helper endpoints (`/telemetry/current`, `/telemetry/history`)
- Display session totals (mining volume, combat damage, hit quality breakdown)
- Custom sparklines matching overlay visual design with EF-Map theme color integration
- Real-time polling (1-2s when active) matching overlay update frequency
- Reset button wired to helper's `POST /telemetry/reset` endpoint

**Key Requirement**: Sparklines must use EF-Map accent theme colors (orange/green/blue/purple), not hardcoded orange. Combat "damage taken" line always red regardless of theme.

**Implementation Estimate**: 3.5-5 hours (all backend infrastructure exists, web-only changes)

**Deferred Until**: After Phase 5 (visited systems) ships to production.

This initiative complements the overlay by providing an alternative telemetry viewing experience for players who prefer browser-based dashboards on secondary displays.
