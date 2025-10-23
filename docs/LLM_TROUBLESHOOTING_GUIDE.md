# EF-Map Overlay – LLM Troubleshooting Guide

> **Purpose:** Give agents a fast, opinionated entry point for understanding the overlay helper project and diagnosing common failures without rereading the entire repository. Keep this document in sync with `AGENTS.md`, `.github/copilot-instructions.md`, and the initiative plan.

## 1. Quick orientation
- **What lives here:** Windows helper app, DirectX 12 overlay DLL, injector utilities, log parsers, and packaging/tooling for the in-game overlay experience.
- **What lives elsewhere:** Web UI, Cloudflare Worker APIs, and data preparation pipelines remain in `EF-Map-main`.
- **Current milestones (2025-10-15):**
  - Follow mode bridge is live (helper ↔ EF Map sync).
  - Mining telemetry UI is in validation (yield + rate graphs).
  - 3D star map replication is paused.
- **Key docs:**
  - `docs/initiatives/GAME_OVERLAY_PLAN.md` – roadmap + acceptance criteria.
  - `README.md` – build instructions, packaging plan, smoke tests.
  - `docs/decision-log.md` – chronological context (mirror updates in the main repo when relevant).

## 2. Architecture snapshot
| Layer | Responsibilities | Primary location |
| --- | --- | --- |
| Helper (tray app) | HTTP API, shared memory publisher, protocol registration, log watchers, telemetry aggregation. | `src/helper/` |
| Overlay DLL | DX12 swap-chain hook, ImGui rendering, input handling, JSON snapshot consumption. | `src/overlay/` |
| Shared schema | Canonical structs, serialization helpers, shared-memory contracts. | `src/shared/` |
| Injector tools | Process attach utilities, smoke scripts, diagnostics. | `src/injector/`, `tools/` |
| Web integration* | Browser helper panel, Cloudflare APIs, usage metrics. | `EF-Map-main` (`eve-frontier-map/`, `_worker.js`) |

> \*Keep the plan and payload contracts synchronized with the web app repository. Any schema change here **must** be mirrored in `EF-Map-main` and logged in both decision logs.

### Data flow
1. EF Map browser panel detects helper availability via localhost (`/status`).
2. Routes, follow-mode toggles, or telemetry commands post to helper (`/overlay/state`, `/session/*`).
3. Helper validates payloads, persists canonical JSON, and writes to shared memory (`Local\\EFOverlaySharedState`).
4. Overlay DLL polls shared memory, renders HUD using ImGui, and pushes events back through an HTTP drain endpoint when needed.
5. Optional features (mining/combat telemetry) reuse helper log watchers to enrich the shared snapshot.

## 3. Build & verification recipes
Use PowerShell unless noted.

```powershell
# Configure (only needed once per machine)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build helper + overlay (Debug)
cmake --build build --config Debug

# Run tests (Release config example)
cmake --build build --config Release --target ef_overlay_tests
ctest -C Release --output-on-failure
```

### Smoke checklist
1. Launch helper (`build/src/helper/<Config>/ef-overlay-helper.exe`) from an **external PowerShell window** (outside VS Code). Integrated terminals routinely fail to bind the helper correctly during injection.
2. Call `GET /health` to confirm the HTTP API is listening.
3. Push a sample payload to `/overlay/state`; confirm helper logs show schema acceptance.
4. Inject overlay DLL into `exefile.exe` with `ef-overlay-injector.exe` (stick with the process name—`exefile.exe` is stable even though the PID changes every launch).
5. Observe HUD updates in-game (or via a DX capture harness if the game client is unavailable).
6. Toggle follow mode from the browser panel (EF-Map-main) to validate bidirectional state.

## 4. Diagnostics toolbox
| Symptom | First checks | Deep dive |
| --- | --- | --- |
| Helper refuses HTTP connections | Confirm helper process running, port (default 38765) free, environment variables (`EF_OVERLAY_HOST/PORT`). | Inspect helper log under `%LOCALAPPDATA%/EFOverlay/logs/` (or console output). |
| Overlay not rendering | Verify injector success message, confirm DLL path, ensure game running in DX12 mode. | Enable debug logging (`EF_OVERLAY_VERBOSE=1`), attach RenderDoc to process, watch for swap-chain hooks failing. |
| Follow mode stuck | Check helper follow flag (`GET /session/state`), confirm chat parser reading latest log, ensure EF Map panel subscribed. | Inspect `EF-Map-main` usage logs, re-run helper with `--trace-follow` once implemented. |
| Mining telemetry stale | Validate latest game log paths in helper diagnostics, ensure player mined during test. | Use `tools/log_tail.ps1` (future) to stream raw log lines; confirm parser regex still matches current client format. |
| Protocol link no-op | Confirm `ef-overlay://` scheme registered (`--register-protocol`), check shared secret header, ensure browser not blocking custom scheme. | Examine Windows Event Viewer → Applications for handler launch failures. |

## 5. Common tasks
- **Update shared schema:** Edit `src/shared/` headers + helper/overlay serialization, regenerate any associated tests, document payload changes in both repos.
- **Add telemetry field:** Extend helper aggregator → update shared snapshot JSON → refresh overlay rendering → add validation tests.
- **Adjust installer plan:** Document trade-offs in `docs/decision-log.md`, update Phase 6 section in the initiative plan, coordinate with CI owners.

## 6. Cross-repo coordination
- Mirror updates to `docs/initiatives/GAME_OVERLAY_PLAN.md` and the decision log in `EF-Map-main`.
- For helper-panel UI or Cloudflare Worker hooks, open a paired PR in `EF-Map-main` and reference commits in both logs.
- Keep authentication and session-token handling consistent with `EF-Map-main/docs/LLM_TROUBLESHOOTING_GUIDE.md` (main repo).

## 7. Mining telemetry implementation details

### Intent
Provide real-time mining rate visualization with session persistence, smooth sparkline curves during active mining, and accurate decay representation when mining stops.

### Architecture
| Component | Role |
| --- | --- |
| Helper log watcher | Parses mining yield events from EVE Frontier combat logs, accumulates session totals by ore type. |
| Telemetry aggregator | Maintains `totalVolume_`, `sessionStart_`, `sessionBuckets_` in memory; persists to `%LocalAppData%\EFOverlay\data\mining_session.json`. |
| Overlay renderer | Calculates rolling mining rates from volume deltas, applies EMA smoothing for display, renders sparkline with interpolation-based decay. |

### Data flow
1. **Event capture:** Helper's `CombatLogFileWatcher` detects mining yield lines, extracts volume + timestamp + resource name.
2. **Aggregation:** `MiningTelemetryAggregator::add()` normalizes resource labels, updates session totals, maintains 120s sliding window.
3. **Persistence:** On helper shutdown or reset, aggregator serializes state to JSON; on startup or combat log file switch, `restoreSession()` reloads from disk.
4. **Rate calculation:** Overlay reads volume history from telemetry snapshot, computes rate via 10-second rolling window: `rate = (volumeNow - volumeBaseline) / 10s`.
5. **Display smoothing:** Before rendering, overlay applies EMA (α=0.3) to rate values: `smoothed[i] = α * raw[i] + (1-α) * smoothed[i-1]`. This eliminates jitter without altering raw data or session totals.
6. **Decay visualization:** When mining stops, interpolation lambda applies 4s hold + 6s linear decay to zero. After decay scrolls left (~30s), transitions to accurate vertical drop.

### Key decisions
- **EMA smoothing (α=0.3):** Applied to local copy only (`miningRateValuesCopy`) to preserve raw data integrity. Produces smooth oscilloscope-like curves during mining without compromising session totals or persistence.
- **Session persistence fix:** `refreshCombatFile()` now calls `restoreSession()` after aggregator reset to restore `totalVolume_` and `sessionStart_`, ensuring accumulation continues correctly when EVE Frontier rotates combat log files.
- **Decay approach (Option 1 chosen):** Interpolation-based decay via rendering lambda, not synthetic data samples. Option 2 (generating decay samples at stop time) inadvertently blocked regular updates during mining, causing flatline sparklines. Option 1 provides factually accurate visualization (instant stop → vertical drop after scrolling) without side effects.

### Constants & thresholds
```cpp
// Overlay renderer (src/overlay/overlay_renderer.cpp)
constexpr std::uint64_t kMiningRateHistoryWindowMs = 120000;  // 2-minute sliding window
constexpr std::uint64_t kMiningRateSmoothingWindowMs = 10000; // 10s rate calculation baseline
constexpr std::uint64_t kMiningCycleMs = 7000;                // 7s hold before decay (6s large laser + 1s margin)
constexpr std::uint64_t kDecayWindowMs = 10000;               // Total 10s window (7s hold + 3s decay)
constexpr float kEmaAlpha = 0.3f;                             // EMA smoothing factor

// Helper aggregator (src/helper/log_watcher.cpp)
constexpr std::chrono::seconds kDefaultWindow{120};           // 120s recent volume window
```

**Note on kMiningCycleMs:** Set to 7 seconds to accommodate large mining lasers (6-second activation cycle) plus 1-second safety margin for network/logging latency. This prevents premature decay during long laser cycles while maintaining smooth behavior for 4-second standard lasers.

### Troubleshooting
| Symptom | First checks | Resolution |
| --- | --- | --- |
| Sparkline jittery during mining | Confirm EMA smoothing applied in renderer. | Check `miningRateValuesCopy` loop; α should be 0.3. |
| Session totals reset to zero | Verify `restoreSession()` called in `refreshCombatFile()`. | Add debug log before/after restore; confirm JSON file exists. |
| Decay not smooth | Check interpolation lambda constants (hold=4s, decay=6s). | Adjust `kMiningCycleMs` if mining laser cycle changed. |
| Rate stuck at old value | Ensure new samples not blocked during active mining. | Remove any `withinDecayWindow` checks outside decay period. |
| Persistence file corrupt | Check JSON schema matches aggregator output. | Delete `mining_session.json`; helper recreates on next mining event. |

### File locations
- **Session persistence:** `%LocalAppData%\EFOverlay\data\mining_session.json`
- **Rate calculation:** `src/overlay/overlay_renderer.cpp` → `recordMiningRateLocked()`
- **EMA smoothing:** `src/overlay/overlay_renderer.cpp` → `renderImGui()` mining panel section
- **Session restore:** `src/helper/log_watcher.cpp` → `refreshCombatFile()` + `restoreSession()`
- **Decay interpolation:** `src/overlay/overlay_renderer.cpp` → sparkline rendering lambda

### Testing checklist
- ✅ Start mining with 2-3 lasers → sparkline shows smooth curves ramping up.
- ✅ Continue mining for 60s → rate stabilizes, no jitter or sudden drops.
- ✅ Stop mining → decay to zero over ~10s, smooth curve.
- ✅ Wait 30s after stopping → vertical drop appears (accurate representation).
- ✅ Restart helper → session totals persist, mining resumes from saved volume.
- ✅ Reset session → totals clear, file deleted, new session starts fresh.

## 8. Known gaps & follow-ups
- Combat telemetry pipeline not started; watch for schema expansion.
- Installer/signing work queued for Phase 6 (currently evaluating Azure Code Signing).
- Need automated integration tests once mining telemetry stabilizes (track in decision log when scheduled).

## 8. Contact & escalation
- If a task touches signed binaries, installer distribution, or cross-repo schema changes, request operator confirmation/tokens before proceeding.
- Record substantive troubleshooting outcomes and mitigations in `docs/decision-log.md` so the next agent can avoid duplicating investigations.

> **Maintenance note:** Update this guide after every major feature milestone or when troubleshooting playbooks change. Keep section numbering stable so cross-references stay valid.
