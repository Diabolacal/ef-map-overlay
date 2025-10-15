## EF-Map Overlay Helper

This repository contains the native helper application and DirectX 12 overlay components that integrate EF-Map data into the EVE Frontier client.

### Related repositories
- [`EF-Map-main`](../EF-Map-main): primary web app, data pipelines, and Cloudflare worker APIs. Keep shared docs (guardrails, initiative plans) synchronized across both repos.

### Key docs
- `AGENTS.md` – condensed guardrails for agents working in this repo.
- `.github/copilot-instructions.md` – detailed workflow expectations and architecture notes.
- `docs/LLM_TROUBLESHOOTING_GUIDE.md` – first-stop orientation and troubleshooting map for this project.
- `docs/initiatives/GAME_OVERLAY_PLAN.md` – roadmap for the overlay initiative (mirrors the copy in `EF-Map-main`).
- `docs/decision-log.md` – overlay-specific decision history.

### Current status (2025-10-15)
- ✅ **Follow mode shipped:** the helper now streams the pilot’s current system into EF Map; toggles stay in sync across helper, overlay, and browser surfaces.
- 🟡 **Mining telemetry in validation:** yield parsing, rolling-rate calculations, and first-pass overlay widgets are implemented and being tuned using live sessions.
- ⏸️ **3D star map replication paused:** native starfield work is shelved while telemetry and combat tooling take priority.

Refer to `docs/initiatives/GAME_OVERLAY_PLAN.md` for a detailed breakdown of phase status and acceptance criteria.

### Roadmap snapshot
| Phase | Status | Highlights |
| --- | --- | --- |
| 1 – Helper ↔ UI grounding | ✅ Complete | Tray/runtime polish, helper state bridge, follow-mode toggles across surfaces. |
| 2 – Mining telemetry foundation | 🟡 Validation | Live log parsing, session graphs, reset controls, local-only storage. |
| 3 – Combat telemetry | ⏳ Queued | Extend log pipeline to DPS/events with privacy safeguards. |
| 4 – Follow mode | ✅ Complete | System streaming between helper and web app; recentering works in live play. |
| 5 – Bookmark & route UX | ⏳ Pending design | Overlay bookmark actions and next-system callouts. |
| 6 – Packaging & release | ⏳ Planning | Signed installer workflow, helper tray UX, update channel. |

### Helper packaging & signing (preview)
- Target installer tech: WiX MSI vs MSIX vs Squirrel under evaluation; current lean is to adopt **Microsoft Azure Code Signing (ACS)** to streamline Authenticode signatures.
- Signed binaries are required before broad distribution; store certificates securely (HSM-backed or ACS key vault) and document access procedures in the decision log when finalized.
- Plan to host versioned installers on Cloudflare (R2 + Pages Worker) and surface download links inside the EF helper panel once ready.

Packaging tasks remain part of the Phase 6 backlog; contributions should align with the roadmap and document any environment prerequisites.

### Project structure
- `src/helper/` – Windows helper/tray app skeleton (protocol registration, process attach, logging).
- `src/shared/` – Canonical overlay schema definitions and shared-memory transport helpers.
- `src/overlay/` – DirectX 12 overlay DLL scaffold (swap-chain hook, render loop, IPC contract).
- `tools/` – placeholder for packaging, diagnostics, and automation scripts.
- `docs/` – guardrails, initiative plan, and decision log.

### Build & test status
- Helper/overlay build systems: **C++20 / MSVC** via CMake (native end-to-end).
- Automated tests: _TBD_ – plan unit/integration coverage alongside implementation.

### Staying in sync with universe updates
- Whenever the EF-Map-main universe dataset (`map_data_v2.db`) is regenerated, re-export the compact star catalog (`python tools/export_star_catalog.py` in the main repo) and copy the resulting `star_catalog_v1.*` files into this repository’s `data/` folder.
- After copying, rebuild and run `ef_overlay_tests` to confirm the helper and overlay can parse the refreshed catalog before committing the updated binaries.
- If the catalog schema or filename changes, adjust loader paths in `src/shared/star_catalog.*` and update documentation accordingly.
- Monitor the main repo decision log for indexer/world address changes that may require helper configuration updates.

### Building (preview)
The repository now includes an initial CMake scaffold for both the helper executable and overlay DLL. A minimal Windows toolchain (Visual Studio 2022 or recent MSVC Build Tools) is required.

```powershell
# Configure (Visual Studio generator shown; adjust as needed)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build Debug configuration
cmake --build build --config Debug

# Notable outputs:
#   build/src/shared/Debug/ef_overlay_shared.lib  (shared schema + IPC helpers)
#   build/src/helper/Debug/ef-overlay-helper.exe  (HTTP API + protocol handler)
#   build/src/overlay/Debug/ef-overlay.dll        (ImGui overlay module)

# Run tests (Release config example)
cmake --build build --config Release --target ef_overlay_tests
cd build
ctest -C Release --output-on-failure
```

> ℹ️  The CMake project uses `FetchContent` to download MinHook, spdlog, cpp-httplib, nlohmann/json, and ImGui automatically during the first configure step. An internet connection is required the first time you generate the build tree.

Future updates will add packaging scripts and CI integration once the prototypes evolve beyond the stub stage.

### Manual smoke test (injector)

Use the `ef-overlay-injector.exe` utility to load the overlay DLL into a running EVE Frontier client for quick validation. Run PowerShell **as Administrator** so the injector can open the game process.

1. Build the Release configuration so the latest binaries are available:

```powershell
cmake --build build --config Release
```

2. Launch `ef-overlay-helper.exe` (Release build) in a separate **external** PowerShell window (outside VS Code). The helper must run in a standalone console to bind correctly; the integrated terminal routinely blocks injection. Leave it running so the overlay module can read shared state:

```powershell
cd build/src/helper/Release
.\ef-overlay-helper.exe
```

3. (Optional) Push a sample payload to confirm shared-memory updates before injecting:

```powershell
$payload = @{
	route = @(
		@{ system_id = "30000001"; display_name = "Tanoo"; distance_ly = 0.0; via_gate = $false }
	)
} | ConvertTo-Json -Depth 4

Invoke-RestMethod -Method Post -Uri http://127.0.0.1:38765/overlay/state -Body $payload -ContentType 'application/json'
```

4. Start the EVE Frontier client (windowed mode is easiest for observation).

5. Identify the process by name—stick with **`exefile.exe`**. The retail EVE Frontier client keeps this name across launches, while the PID changes every session. Use the following command to confirm it is running (adjust only if CCP renames the binary in a future build):

```powershell
Get-Process -Name exefile -ErrorAction SilentlyContinue | Select-Object Id, ProcessName
```

6. Inject the overlay DLL using the injector. Prefer the process name (`exefile.exe`) rather than a PID so the command stays valid across launches; supply the absolute path to the freshly built `ef-overlay.dll`:

```powershell
cd C:/ef-map-overlay/build/src/injector/Release
.\ef-overlay-injector.exe exefile.exe C:/ef-map-overlay/build/src/overlay/Release/ef-overlay.dll
```

	Successful injection prints `[info] Injection completed (PID=...)`. If you see access errors, confirm the shell is elevated and the DLL path exists.

7. Tab back into the game client. The overlay should appear with the sample payload (version, timestamp, route summary). Interact with the game to ensure frame pacing feels normal.

8. To validate teardown, close the game client. Confirm that no crash dialogs appear and that the helper process remains alive. Re-open the client and rerun the injector to repeat the cycle as needed. When the helper exits (cleanly or due to a crash) the overlay now auto-hides within ~5 seconds; relaunching the helper brings the window back automatically without pressing F8, though you still need to inject a fresh DLL after a client restart.

9. (Optional) Exercise the helper heartbeat watchdog without restarting the game:

```powershell
taskkill /IM ef-overlay-helper.exe /F
Start-Sleep -Seconds 6
Start-Process -FilePath C:/ef-map-overlay/build/src/helper/Release/ef-overlay-helper.exe -WorkingDirectory C:/ef-map-overlay/build/src/helper/Release
```

	The overlay should disappear shortly after the helper stops and come back on its own once the helper is running again.

During testing, keep the helper console visible for schema validation errors. For deeper diagnostics, attach a debugger or use Sysinternals DebugView to capture `OutputDebugString` messages emitted by the overlay module.

### Overlay state schema
The helper and overlay exchange JSON payloads conforming to a canonical schema:

```json
{
	"version": 1,
	"generated_at_ms": 1727373375123,
	"route": [
		{
			"system_id": "30000001",
			"display_name": "Tanoo",
			"distance_ly": 0.0,
			"via_gate": false
		}
	],
	"notes": "Optional summary shown in overlay"
}
```

- `version` defaults to the current schema version (`1`).
- `generated_at_ms` records when the payload was produced (Unix epoch milliseconds). If omitted the helper will backfill the current timestamp.
- `route` is an ordered list of waypoints with resolved system identifiers, display names, jump distances, and an indicator when a segment uses a stargate.
- `notes` is optional freeform text rendered beneath the route summary.

### Helper HTTP API (stub)
Launching `ef-overlay-helper.exe` starts a localhost server (default `127.0.0.1:38765`) that exposes early test endpoints:

```powershell
# set shared secret (required for protocol links and HTTP mutations)
$env:EF_OVERLAY_TOKEN = "dev-token"

# start the helper in one terminal session (keep it running)
.\build\src\helper\Debug\ef-overlay-helper.exe

# in a second terminal, confirm process health
Invoke-RestMethod -Method Get -Uri http://127.0.0.1:38765/health

# push a dummy overlay payload that matches the canonical schema
$payload = @{
	route = @(
		@{ system_id = "30000001"; display_name = "Tanoo"; distance_ly = 0.0; via_gate = $false },
		@{ system_id = "30000003"; display_name = "Mahnna"; distance_ly = 3.47; via_gate = $true }
	)
	notes = "Prototype payload injected via HTTP"
} | ConvertTo-Json -Depth 4

Invoke-RestMethod -Method Post -Uri http://127.0.0.1:38765/overlay/state -Body $payload -ContentType 'application/json' -Headers @{ 'x-ef-overlay-token' = $env:EF_OVERLAY_TOKEN }

# read back the latest payload snapshot
Invoke-RestMethod -Method Get -Uri http://127.0.0.1:38765/overlay/state -Headers @{ 'x-ef-overlay-token' = $env:EF_OVERLAY_TOKEN }
```

The helper validates every incoming payload against the schema, persists the canonical JSON snapshot, and publishes it into a shared-memory segment (`Local\\EFOverlaySharedState`) consumed by the overlay module. Environment variables:

- `EF_OVERLAY_HOST` / `EF_OVERLAY_PORT` – override the HTTP bind endpoint.
- `EF_OVERLAY_TOKEN` – shared secret required for HTTP mutations and protocol links (omit for unauthenticated development).
- `EF_OVERLAY_SNAPSHOT_PATH` – _(reserved for future file-based diagnostics)._ 

#### Protocol registration

Register the `ef-overlay://` scheme (per-user) so deep links invoke the helper:

```powershell
.\build\src\helper\Debug\ef-overlay-helper.exe --register-protocol
# To remove later
.\build\src\helper\Debug\ef-overlay-helper.exe --unregister-protocol
```

With a protocol registered and the helper running, links such as `ef-overlay://overlay-state?token=dev-token&payload=%7B%22route%22%3A...%7D` are routed to the local instance. If a helper is already listening the new invocation forwards the command over HTTP; otherwise it boots a fresh helper and applies the command before remaining available for subsequent requests.

### Shared memory bridge
- Helper writes canonical overlay snapshots to a 64 KiB shared-memory mapping (`Local\EFOverlaySharedState`).
- Overlay module polls the mapping on a worker thread, parses the JSON payload, and renders the latest route summary using ImGui.
- The render UI currently displays schema version, payload age, optional notes, and a preview of the first dozen waypoints to validate the data contract.

### DirectX 12 swap-chain hook
- The overlay DLL now hooks `IDXGISwapChain3::Present` / `ResizeBuffers` via MinHook and renders the ImGui overlay directly inside the game’s swap chain.
- The hook lazily initializes ImGui’s Win32 + DX12 backends once a valid swap chain and command queue are observed, and recreates render targets on resize events.
- Rendering uses the shared-memory state supplied by the helper, so any accepted payload appears in-game with minimal latency.

### Next steps
- Implement session toggles / hotkeys to enable or hide overlay elements.
- Extend the CMake project with CI builds and packaging once the runtime path stabilizes.
- Coordinate with `EF-Map-main` when overlay work requires web app or API changes.
