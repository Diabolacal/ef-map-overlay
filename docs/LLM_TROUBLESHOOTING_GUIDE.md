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

## 8. Combat telemetry implementation details

### Intent
Provide real-time combat damage visualization with dual-line sparkline (dealt/taken), session persistence, hit quality analytics, and accurate representation of weapon fire patterns.

### Architecture
| Component | Role |
| --- | --- |
| Helper log watcher | Parses combat damage events from EVE Frontier combat logs, extracts amount, direction (dealt/taken), hit quality, counterparty. |
| Telemetry aggregator | Maintains `totalDamageDealt`, `totalDamageTaken`, `sessionStart_`, hit quality counters (10 types) in memory; persists to `combat_session.json` (planned). |
| Overlay renderer | Calculates rolling DPS from damage deltas, renders dual-line sparkline with stable peak tracking, displays hit quality breakdown. |

### Data flow
1. **Event capture:** Helper's `CombatLogFileWatcher` detects combat damage lines, parses player dealt/taken direction, damage amount, hit quality keywords.
2. **Hit quality detection:** Parser checks for "miss", "glanc", "penetrat", "smash" keywords before normal damage extraction. Misses handled separately (different log patterns).
3. **Aggregation:** `CombatTelemetryAggregator::add()` increments cumulative damage totals, updates hit quality counters, maintains 30s sliding window for recent damage.
4. **DPS calculation:** `recordCombatDamageLocked()` computes DPS via 10-second rolling window: `dps = (damageNow - damageBaseline) / 10s`. Stores timestamped DPS values in deque.
5. **Activity detection:** If total damage unchanged for 2 seconds, DPS drops to zero immediately (fast tail-off when combat ends).
6. **Display:** Overlay plots raw DPS data points directly (no interpolation) as dual lines: orange (dealt), red (taken). X positions shift as time advances (smooth scrolling), Y values locked to stable data (zero oscillation).

### Key decisions
- **Dual-line sparkline (~144px):** 2x mining sparkline height for better combat visibility. Orange for dealt damage (positive), red for taken damage (danger signal).
- **Raw data plotting (no interpolation):** Initially tried 250ms sampling + interpolation for smooth scrolling, but this caused 3-4Hz oscillation on sharp peaks due to synthetic values fluctuating frame-to-frame. Solution: plot actual `combatDamageValues_` data points directly, let ImGui's `AddPolyline` handle rendering. X positions shift smoothly, Y values stable → zero oscillation.
- **Stable peak tracking:** `combatPeakDps_` persists across frames with 1% per second decay. Quantized to nearest 1.0 DPS and only updated every 100ms minimum to prevent rescaling bounce.
- **2-second activity window:** Drops DPS to zero if damage totals unchanged for 2s. Prevents 20-30s tail-off delay that would occur with pure 10s window averaging.
- **Hit quality tracking:** 10 counters (5 dealt + 5 taken) for Miss, Glancing, Standard, Penetrating, Smashing. Tracked independently via keyword detection in combat log.
- **Miss-specific parsing:** Misses use different patterns than hits ("you miss X" vs "you hit X"). Added dedicated miss detection before normal damage parsing to capture these events with amount=0.0.

### Constants & thresholds
```cpp
// Overlay renderer (src/overlay/overlay_renderer.cpp)
constexpr std::uint64_t kCombatHistoryWindowMs = 120000;      // 2-minute rolling history
constexpr std::uint64_t kDpsCalculationWindowMs = 10000;      // 10s DPS calculation baseline
constexpr std::uint64_t kRecentActivityWindowMs = 2000;       // 2s activity detection for tail-off
constexpr float kPeakQuantum = 1.0f;                          // Peak quantization to nearest 1.0 DPS
constexpr std::uint64_t kPeakUpdateThrottleMs = 100;          // Minimum 100ms between peak updates

// Helper aggregator (src/helper/log_watcher.cpp)
constexpr std::chrono::seconds kDefaultWindow{30};            // 30s recent damage window
```

### Hit quality parser patterns
```cpp
// Outbound misses
"you miss [target]"
"your [weapon] misses [target]"

// Inbound misses
"[attacker] misses you"
"[attacker] miss you"

// Quality keywords (searched in lowercase)
"miss"      → HitQuality::Miss
"glanc"     → HitQuality::Glancing   (matches "glances off", "glancing")
"penetrat"  → HitQuality::Penetrating (matches "penetrates", "penetrating")
"smash"     → HitQuality::Smashing    (matches "smashes", "smashing")
(default)   → HitQuality::Standard
```

### UI display format
```
Combat totals: 15268.0 dealt | 2449.0 taken
Hits dealt: 60 (9 pen, 13 smash, 31 std, 7 glance) | 0 miss
Hits taken: 109 (35 pen, 21 smash, 42 std, 11 glance) | 0 miss
Session 2.8 min (started 2.8 min ago)

[Dual-line sparkline: orange dealt, red taken]

Current: 0.0 DPS dealt | 26.3 DPS taken
Peak: 206.9 DPS
```

### Troubleshooting
| Symptom | First checks | Resolution |
| --- | --- | --- |
| Sharp peaks oscillating 3-4Hz | Confirm using raw data plotting, not interpolation. | Remove any sampling loops; plot `combatDamageValues_` directly. |
| Peaks bouncing up/down | Check peak tracking uses decay + quantization. | Ensure `combatPeakDps_` updated with `std::ceil(value / kPeakQuantum) * kPeakQuantum`. |
| Slow tail-off (20-30s) | Verify 2s activity detection implemented. | Check `withinRecentActivity` flag in `recordCombatDamageLocked()`. |
| Time direction backwards | Ensure anchoring to `now_ms()`, plotting backwards. | t-0s should be right edge, t-120s left edge. |
| Jerky movement | Confirm plotting raw data points, not sampling. | No interpolation; let X positions shift naturally with time. |
| Miss counters not incrementing | Check miss-specific parsing runs before hits. | Verify miss patterns detected in lowercase `line.find("miss")`. |
| Hover tooltips wrong time | Confirm tooltip uses `anchorMs - value.timestampMs`. | Age calculation should match sparkline X mapping. |

### File locations
- **Session persistence (planned):** `%LocalAppData%\EFOverlay\data\combat_session.json`
- **DPS calculation:** `src/overlay/overlay_renderer.cpp` → `recordCombatDamageLocked()`
- **Sparkline rendering:** `src/overlay/overlay_renderer.cpp` → `renderCombatTab()` lambda
- **Hit quality parsing:** `src/helper/log_parsers.cpp` → `parse_combat_damage_line()`
- **Session tracking:** `src/helper/log_watcher.cpp` → `CombatTelemetryAggregator`

### Testing checklist
- ✅ Engage combat with 3 weapons → sparkline shows sharp orange spikes for weapon fire.
- ✅ Take damage → red line appears showing incoming damage spikes.
- ✅ Sharp peaks stay rock-solid → zero oscillation during combat.
- ✅ Stop combat → DPS drops to zero within 2 seconds (fast tail-off).
- ✅ Hover over sparkline → tooltip shows correct time ago + both DPS values.
- ✅ Hit quality counters increment → breakdown shows pen/smash/std/glance/miss.
- ✅ Session totals persist → restart helper, totals maintained.
- ✅ Reset button → totals clear, history cleared, session restarts.

## 9. Known gaps & follow-ups
- Combat session persistence (`combat_session.json`) not yet implemented; reset button not wired to delete file.
- Installer/signing work queued for Phase 6 (currently evaluating Azure Code Signing).
- Need automated integration tests once combat telemetry stabilizes (track in decision log when scheduled).

## 10. Contact & escalation
- If a task touches signed binaries, installer distribution, or cross-repo schema changes, request operator confirmation/tokens before proceeding.
- Record substantive troubleshooting outcomes and mitigations in `docs/decision-log.md` so the next agent can avoid duplicating investigations.

> **Maintenance note:** Update this guide after every major feature milestone or when troubleshooting playbooks change. Keep section numbering stable so cross-references stay valid.
