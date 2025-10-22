# EF-Map Overlay Architecture

## Overview

This document describes the technical architecture of the EF-Map overlay system, which enables real-time data display inside the EVE Frontier game client. The system is designed to be **reusable** for other EVE Frontier applications and consists of three main components:

1. **Native Helper Application** (Windows C++20) - Background process that manages protocol registration, HTTP/WebSocket APIs, and IPC coordination
2. **DirectX 12 Overlay DLL** - Injected module that hooks the game's swap chain to render UI elements
3. **Browser Bridge** (TypeScript/WebSocket) - Client-side library for web applications to communicate with the helper

The architecture is intentionally decoupled: the helper and overlay provide generic infrastructure for any EVE Frontier DApp, while the browser implementation is application-specific.

---

## System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        Browser (Your DApp)                       │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  helperBridge.ts (TypeScript)                              │ │
│  │  - WebSocket connection to 127.0.0.1:38766                 │ │
│  │  - HTTP fallback to 127.0.0.1:38765                        │ │
│  │  - Sends overlay state (JSON payloads)                     │ │
│  │  - Receives events from overlay                            │ │
│  └────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                              ↓ WebSocket/HTTP
┌─────────────────────────────────────────────────────────────────┐
│              Native Helper (ef-overlay-helper.exe)               │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  HTTP Server (127.0.0.1:38765)                             │ │
│  │  - POST /overlay/state (accept JSON payload)               │ │
│  │  - GET  /overlay/state (read current state)                │ │
│  │  - GET  /overlay/events?since=N (poll event queue)         │ │
│  │  - POST /settings/follow (toggle follow mode)              │ │
│  │  - GET  /telemetry/current (mining/combat stats)           │ │
│  │  - GET  /health (uptime, connection status)                │ │
│  └────────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  WebSocket Server (127.0.0.1:38766)                        │ │
│  │  - Pushes overlay_state updates (reactive)                 │ │
│  │  - Pushes event batches (reactive)                         │ │
│  │  - Sends periodic pings                                    │ │
│  └────────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Shared Memory Writer                                      │ │
│  │  - Writes to Local\EFOverlaySharedState (64 KiB)           │ │
│  │  - Contains versioned JSON snapshot                        │ │
│  └────────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Event Queue Reader                                        │ │
│  │  - Reads from Local\EFOverlayEventQueue (ring buffer)      │ │
│  │  - Polls overlay events (visibility toggles, dismissals)   │ │
│  └────────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Protocol Handler (ef-overlay://)                          │ │
│  │  - Registered in Windows Registry (HKCU)                   │ │
│  │  - Invokes helper with deep link parameters                │ │
│  └────────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Optional: Log Watcher & Telemetry Parser                  │ │
│  │  - Monitors EVE Frontier logs for mining/combat events     │ │
│  │  - Parses structured data and enriches overlay state       │ │
│  └────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                          ↓ Shared Memory IPC
┌─────────────────────────────────────────────────────────────────┐
│        DirectX 12 Overlay DLL (ef-overlay.dll)                   │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Shared Memory Reader                                      │ │
│  │  - Polls Local\EFOverlaySharedState every frame            │ │
│  │  - Parses JSON payload into C++ structures                 │ │
│  └────────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Event Queue Writer                                        │ │
│  │  - Writes user interactions to Local\EFOverlayEventQueue   │ │
│  │  - Example: visibility toggles, waypoint advances          │ │
│  └────────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  DX12 Swap Chain Hook (MinHook)                            │ │
│  │  - Hooks IDXGISwapChain3::Present                          │ │
│  │  - Hooks ResizeBuffers for viewport changes                │ │
│  │  - Injects ImGui rendering into game's command queue       │ │
│  └────────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  ImGui Renderer                                            │ │
│  │  - Renders overlay UI (route, markers, telemetry)          │ │
│  │  - Handles input (hotkeys, mouse clicks)                   │ │
│  └────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                              ↓ Injected into
┌─────────────────────────────────────────────────────────────────┐
│              EVE Frontier Client (exefile.exe)                   │
│  - Game runs normally                                            │
│  - Overlay appears on top of game rendering                      │
│  - No game code modification required                            │
└─────────────────────────────────────────────────────────────────┘
```

---

## Component Details

### 1. Native Helper Application

**Technology:** C++20, MSVC, Windows API
**Dependencies:** cpp-httplib, nlohmann/json, asio, spdlog (all via CMake FetchContent)
**Build:** CMake → Visual Studio 2022 solution

#### Responsibilities

1. **HTTP API Server** (`127.0.0.1:38765`)
   - Accepts overlay state from browser applications via POST `/overlay/state`
   - Validates JSON payloads against schema (see Data Contract section)
   - Serves current state back to browsers via GET `/overlay/state`
   - Provides event polling endpoint GET `/overlay/events?since=N`
   - Exposes telemetry endpoints for mining/combat stats (optional)

2. **WebSocket Server** (`127.0.0.1:38766`)
   - Pushes real-time overlay state updates to connected browsers
   - Broadcasts event batches when overlay emits user interactions
   - Maintains persistent connection with heartbeat pings
   - Auto-reconnects on connection loss

3. **Shared Memory Management**
   - Writes validated JSON to `Local\EFOverlaySharedState` (64 KiB Windows named mapping)
   - Includes schema version, timestamp, and full payload
   - Atomic updates to prevent tearing during overlay reads

4. **Event Queue Coordination**
   - Reads events from `Local\EFOverlayEventQueue` (ring buffer, 64 slots × 512 bytes)
   - Assigns sequential IDs to events for browser polling
   - Forwards events to WebSocket clients in real-time

5. **Protocol Registration**
   - Registers `ef-overlay://` scheme in Windows Registry (`HKCU\Software\Classes`)
   - Browser deep links invoke helper with URL-encoded payloads
   - Example: `ef-overlay://overlay-state?token=...&payload=<json>`

6. **Optional: Game Log Parsing**
   - Watches EVE Frontier log files for structured events
   - Parses mining yields, combat damage, system jumps
   - Enriches overlay state with real-time telemetry (see EF-Map implementation for patterns)

#### Key Files

- `src/helper/helper_server.cpp` - HTTP/WebSocket server implementation
- `src/helper/protocol_registration.cpp` - Windows registry integration
- `src/helper/log_watcher.cpp` - Log file monitoring (optional)
- `src/shared/shared_memory_channel.cpp` - IPC primitives
- `src/shared/event_channel.cpp` - Event queue implementation

#### Configuration

Environment variables (all optional):
```bash
EF_OVERLAY_HOST=127.0.0.1        # HTTP bind address
EF_OVERLAY_PORT=38765            # HTTP port (WS is +1)
EF_OVERLAY_TOKEN=your-secret     # Shared secret for auth (omit for dev)
```

Command-line arguments:
```bash
ef-overlay-helper.exe                    # Start normally
ef-overlay-helper.exe --register-protocol   # Register ef-overlay://
ef-overlay-helper.exe --unregister-protocol # Remove protocol
ef-overlay-helper.exe "ef-overlay://..."    # Handle deep link
```

---

### 2. DirectX 12 Overlay DLL

**Technology:** C++20, DirectX 12, MinHook, ImGui
**Dependencies:** MinHook, ImGui (backends: dx12 + win32), spdlog
**Build:** CMake → produces `ef-overlay.dll`

#### Responsibilities

1. **Swap Chain Hooking**
   - Uses MinHook to intercept `IDXGISwapChain3::Present` and `ResizeBuffers`
   - Lazily initializes ImGui when valid swap chain + command queue detected
   - Recreates render targets on window resize

2. **Shared Memory Reading**
   - Background thread polls `Local\EFOverlaySharedState` at ~60Hz
   - Parses JSON payload into C++ structures (`overlay::OverlayState`)
   - Thread-safe handoff to render thread via mutex

3. **ImGui Rendering**
   - Renders overlay UI on top of game rendering
   - Displays route waypoints, player markers, telemetry widgets
   - Handles input (F8 to toggle visibility, mouse clicks)

4. **Event Emission**
   - Writes user interactions to `Local\EFOverlayEventQueue`
   - Example events: visibility toggles, waypoint advances, hint dismissals
   - Helper reads queue and forwards to browser

5. **Heartbeat Monitoring**
   - Auto-hides overlay if helper stops (detected via missing heartbeat updates)
   - Reappears when helper resumes without manual F8 toggle

#### Key Files

- `src/overlay/overlay_hook.cpp` - Swap chain hooking with MinHook
- `src/overlay/overlay_renderer.cpp` - ImGui rendering logic
- `src/overlay/dllmain.cpp` - DLL entry point and thread startup
- `src/shared/shared_memory_channel.cpp` - Reader implementation
- `src/shared/event_channel.cpp` - Event writer

#### Injection Process

The overlay DLL is injected into the running game process (`exefile.exe`) using a separate injector utility:

1. Build the project: `cmake --build build --config Release`
2. Start the helper: `ef-overlay-helper.exe`
3. Launch EVE Frontier (windowed mode recommended)
4. Run injector: `ef-overlay-injector.exe exefile.exe C:\path\to\ef-overlay.dll`

The injector uses Windows API (`CreateRemoteThread` + `LoadLibrary`) to load the DLL into the game process. Once injected, the DLL's `DllMain` starts a background thread that:
- Opens the shared memory mapping
- Starts polling for overlay state
- Hooks the swap chain when rendering is detected

**Important:** The overlay DLL **must not** crash or block the game's render thread. All heavy operations run on background threads.

---

### 3. Browser Bridge (TypeScript)

**Technology:** TypeScript, WebSocket API, Fetch API
**Location:** `EF-Map-main/eve-frontier-map/src/utils/helperBridge.ts` (reference implementation)

#### Responsibilities

1. **WebSocket Connection**
   - Connects to `ws://127.0.0.1:38766` (or `wss://` if configured)
   - Receives real-time overlay state updates and event batches
   - Auto-reconnects on connection loss with exponential backoff

2. **HTTP Fallback**
   - Uses `fetch()` to POST overlay state when WebSocket unavailable
   - Polls GET `/overlay/events?since=N` for event updates
   - Queries GET `/settings/follow` for follow-mode status

3. **State Management**
   - Maintains reactive state object (compatible with React, Vue, etc.)
   - Notifies subscribers when helper connection changes
   - Tracks last overlay update timestamp, event ID, dropped events

4. **API Methods**
   ```typescript
   interface HelperBridge {
     connect(): void;
     disconnect(): void;
     subscribe(listener: (state: HelperBridgeState) => void): () => void;
     isConnected(): boolean;
     setFollowMode(enabled: boolean): Promise<boolean>;
     refreshFollowMode(): Promise<boolean>;
   }
   ```

#### Usage Example

```typescript
import { createHelperBridge } from './helperBridge';

// Initialize bridge (usually at app startup)
const bridge = createHelperBridge({
  host: '127.0.0.1',
  httpPort: 38765,
  wsPort: 38766,
  token: 'optional-shared-secret',
  autoReconnect: true
});

// Subscribe to state changes
const unsubscribe = bridge.subscribe((state) => {
  console.log('Helper phase:', state.phase); // 'connected', 'not_found', etc.
  console.log('Latest overlay:', state.latestOverlayState);
});

// Connect to helper
bridge.connect();

// Send overlay state to helper (appears in-game)
await fetch('http://127.0.0.1:38765/overlay/state', {
  method: 'POST',
  headers: {
    'Content-Type': 'application/json',
    'x-ef-overlay-token': 'optional-shared-secret'
  },
  body: JSON.stringify({
    version: 4,
    generated_at_ms: Date.now(),
    route: [
      { system_id: '30000001', display_name: 'Tanoo', distance_ly: 0.0, via_gate: false },
      { system_id: '30000003', display_name: 'Mahnna', distance_ly: 3.47, via_gate: true }
    ],
    notes: 'Test route from browser'
  })
});

// Clean up
unsubscribe();
bridge.disconnect();
```

#### Protocol Deep Links

The browser can also trigger helper actions via protocol URLs:

```javascript
// Example: open a route in the overlay
const payload = encodeURIComponent(JSON.stringify({
  version: 4,
  generated_at_ms: Date.now(),
  route: [/* ... */]
}));

window.location.href = `ef-overlay://overlay-state?token=your-secret&payload=${payload}`;
```

If the helper is not running, Windows will launch it automatically (after protocol registration). If already running, the helper receives the command via HTTP forwarding.

---

## Data Contract

### Overlay State Schema (Version 4)

The helper and overlay communicate using a versioned JSON schema. All fields except `version` are optional.

```typescript
interface OverlayState {
  version: number;                        // Schema version (current: 4)
  generated_at_ms: number;                // Unix epoch milliseconds (helper backfills if 0)
  heartbeat_ms?: number;                  // Helper updates this every 5 seconds
  source_online?: boolean;                // Helper sets to true when browser connected

  // Route display
  route?: OverlayRouteNode[];
  notes?: string;
  active_route_node_id?: string;          // Highlight specific waypoint

  // Player position
  player_marker?: {
    system_id: string;
    display_name: string;
    is_docked: boolean;
  };

  // System highlights
  highlighted_systems?: {
    system_id: string;
    display_name: string;
    category: string;                     // Example: 'bookmark', 'objective'
    note?: string;
  }[];

  // Camera control (future: 3D star map)
  camera_pose?: {
    position: { x: number; y: number; z: number };
    look_at: { x: number; y: number; z: number };
    up?: { x: number; y: number; z: number };
    fov_degrees?: number;
  };

  // HUD notifications
  hud_hints?: {
    id: string;
    text: string;
    dismissible: boolean;
    active: boolean;
  }[];

  // Follow mode (helper auto-updates player position)
  follow_mode_enabled?: boolean;

  // Telemetry widgets (optional)
  telemetry?: {
    combat?: {
      total_damage_dealt: number;
      total_damage_taken: number;
      recent_damage_dealt: number;        // Last N seconds
      recent_damage_taken: number;
      recent_window_seconds: number;
      last_event_ms: number;
    };
    mining?: {
      total_volume_m3: number;
      recent_volume_m3: number;           // Last N seconds
      recent_window_seconds: number;
      last_event_ms: number;
      session_start_ms: number;
      session_duration_seconds: number;
      buckets?: {                         // Per-ore breakdown
        id: string;
        label: string;
        session_total: number;
        recent_total: number;
      }[];
    };
    history?: {                           // Time-series data (5-min slices)
      slice_seconds: number;
      capacity: number;
      saturated: boolean;
      slices: {
        start_ms: number;
        duration_seconds: number;
        damage_dealt: number;
        damage_taken: number;
        mining_volume_m3: number;
      }[];
      reset_markers_ms: number[];         // Session reset timestamps
    };
  };
}

interface OverlayRouteNode {
  system_id: string;
  display_name: string;
  distance_ly: number;
  via_gate: boolean;
}
```

**Validation:** The helper validates every incoming payload against this schema. Invalid payloads return HTTP 400 with an error message.

**Backward Compatibility:** Schema version increments when breaking changes occur. The overlay DLL checks the version field and gracefully degrades if newer than expected.

---

### Event Schema

Events flow from overlay → helper → browser. Each event has a sequential ID assigned by the helper.

```typescript
enum OverlayEventType {
  None = 0,
  ToggleVisibility = 1,           // User pressed F8
  FollowModeToggled = 2,          // User toggled follow mode
  WaypointAdvanced = 3,           // User clicked "Next" waypoint
  HudHintDismissed = 4,           // User dismissed a hint
  CustomJson = 1000               // Application-specific JSON payload
}

interface OverlayEvent {
  id: number;                     // Sequential ID (helper-assigned)
  type: OverlayEventType;
  timestamp_ms: number;           // Unix epoch milliseconds
  payload: string;                // JSON string (type-specific)
}

interface EventBatch {
  events: OverlayEvent[];
  next_since: number;             // Use for next poll: GET /overlay/events?since=N
  dropped: number;                // Total events dropped due to full queue
}
```

**Event Polling (HTTP):**
```bash
GET /overlay/events?since=42
Authorization: x-ef-overlay-token: your-secret
```

**Event Streaming (WebSocket):**
The helper pushes event batches automatically:
```json
{
  "type": "event_batch",
  "events": [
    { "id": 43, "type": 1, "timestamp_ms": 1727373375123, "payload": "{}" }
  ],
  "next_since": 43,
  "dropped": 0
}
```

---

## Inter-Process Communication (IPC)

### Shared Memory: State Channel

**Name:** `Local\EFOverlaySharedState`
**Size:** 64 KiB
**Access:** Helper writes, overlay reads

**Binary Layout:**
```
Offset   Size   Field
------   ----   -----
0x0000   4      Magic (0x454F5653 = "EOVS")
0x0004   4      Schema version (uint32)
0x0008   8      Updated at (uint64 milliseconds)
0x0010   4      Payload length (uint32)
0x0014   N      JSON payload (UTF-8, null-terminated)
```

**Synchronization:** The helper writes atomically using a mutex. The overlay reads without locking (stale reads are acceptable; the schema version + timestamp provide coherence checks).

**Performance:** The overlay polls this memory ~60 times per second. JSON parsing cost is negligible (<0.1ms for typical payloads).

---

### Shared Memory: Event Queue

**Name:** `Local\EFOverlayEventQueue`
**Size:** 32 KiB (64 slots × 512 bytes)
**Access:** Overlay writes, helper reads

**Ring Buffer Layout:**
```
struct EventSlot {
    uint16_t type;           // OverlayEventType
    uint16_t payload_length; // Bytes in payload
    uint64_t timestamp_ms;   // Unix epoch milliseconds
    char payload[512 - 12];  // JSON string
};

struct EventQueue {
    uint32_t write_index;    // Atomic, wraps at 64
    uint32_t read_index;     // Atomic, wraps at 64
    uint32_t dropped;        // Atomic counter
    EventSlot slots[64];
};
```

**Synchronization:** Uses atomic compare-and-swap for write_index and read_index. When the queue is full, the overlay increments the `dropped` counter and discards the event.

**Performance:** Lock-free for both writer and reader. Events are typically consumed within milliseconds.

---

## Security Considerations

### Shared Secret (Optional)

When `EF_OVERLAY_TOKEN` is set, the helper requires authentication:

1. **HTTP:** Include `x-ef-overlay-token` header or `token` query parameter
2. **WebSocket:** Include `token` query parameter in connection URL
3. **Protocol Links:** Include `token` parameter: `ef-overlay://...?token=secret`

Without the correct token, requests return HTTP 401 Unauthorized.

**Recommendation:** Always set a token in production to prevent unauthorized overlay control.

---

### Localhost-Only Binding

The helper binds to `127.0.0.1` by default, which prevents remote connections. Do **not** bind to `0.0.0.0` unless you implement additional authentication (e.g., TLS + API keys).

---

### DLL Injection Risks

Injecting code into a game process carries inherent risks:

1. **Anti-Cheat:** Some games flag injected DLLs as cheats. EVE Frontier currently does not have aggressive anti-cheat, but this may change.
2. **Stability:** A buggy overlay can crash the game. Extensive testing is required.
3. **Code Signing:** Unsigned DLLs may trigger Windows SmartScreen warnings.

**Mitigation:**
- Code sign the DLL with an Authenticode certificate (Azure Code Signing recommended)
- Sandbox the overlay (no network access, no file writes except logs)
- Implement crash handlers that log errors without terminating the game

---

## Adapting for Your Application

### Minimal Integration Steps

1. **Use the Helper As-Is**
   - The helper is application-agnostic. No changes needed unless you want custom endpoints.

2. **Implement Browser Bridge**
   - Copy `helperBridge.ts` from EF-Map repository
   - Adapt React components or use vanilla JavaScript
   - Send overlay state via POST `/overlay/state`
   - Subscribe to WebSocket for real-time updates

3. **Customize Overlay Rendering**
   - Fork the overlay DLL project
   - Modify `overlay_renderer.cpp` to display your application's data
   - Keep the shared memory reader and event writer unchanged

4. **Define Your Schema**
   - Extend `OverlayState` with application-specific fields
   - Bump `version` field if breaking changes
   - Update both helper validation and overlay parsing

---

### Example: Adapting for a Different DApp

**Use Case:** Display a real-time inventory tracker in-game

**Steps:**

1. **Extend Schema:**
   ```typescript
   interface InventoryOverlayState extends OverlayState {
     inventory?: {
       items: { id: string; name: string; quantity: number }[];
       total_value_isk: number;
     };
   }
   ```

2. **Browser Integration:**
   ```typescript
   const bridge = createHelperBridge({ autoReconnect: true });
   bridge.connect();

   // Update inventory every 5 seconds
   setInterval(async () => {
     const items = await fetchInventoryFromSmartContract();
     await fetch('http://127.0.0.1:38765/overlay/state', {
       method: 'POST',
       headers: { 'Content-Type': 'application/json' },
       body: JSON.stringify({
         version: 4,
         generated_at_ms: Date.now(),
         inventory: {
           items: items.map(i => ({ id: i.id, name: i.name, quantity: i.qty })),
           total_value_isk: calculateTotalValue(items)
         }
       })
     });
   }, 5000);
   ```

3. **Overlay Rendering:**
   - Modify `overlay_renderer.cpp` to parse `inventory` field
   - Render a scrollable list of items using ImGui
   - Add hotkey to toggle inventory window visibility

4. **No Helper Changes Required**
   - The helper forwards your custom schema to the overlay unchanged

---

## Build Instructions

### Prerequisites

- Windows 10/11 (x64)
- Visual Studio 2022 or MSVC Build Tools
- CMake 3.20+
- Internet connection (first build downloads dependencies)

### Build Steps

```powershell
# Clone repository
git clone https://github.com/your-org/ef-map-overlay.git
cd ef-map-overlay

# Generate Visual Studio solution
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build Release configuration
cmake --build build --config Release

# Outputs:
#   build/src/helper/Release/ef-overlay-helper.exe
#   build/src/overlay/Release/ef-overlay.dll
#   build/src/injector/Release/ef-overlay-injector.exe
```

### Running Tests

```powershell
cmake --build build --config Release --target ef_overlay_tests
cd build
ctest -C Release --output-on-failure
```

---

## Troubleshooting

### Helper Not Starting

**Symptom:** `ef-overlay-helper.exe` exits immediately

**Solutions:**
- Check if port 38765 is already in use: `netstat -ano | findstr 38765`
- Run without token: unset `EF_OVERLAY_TOKEN`
- Check logs in `%LOCALAPPDATA%\EFOverlay\logs\`

---

### Overlay Not Appearing

**Symptom:** Injection succeeds but no UI visible in-game

**Checklist:**
1. Verify helper is running: `curl http://127.0.0.1:38765/health`
2. Check shared memory: POST a test payload and verify HTTP 202 response
3. Press F8 to toggle visibility (overlay may be hidden)
4. Ensure game is in windowed or borderless mode (fullscreen exclusive may conflict)
5. Check overlay logs: `%LOCALAPPDATA%\EFOverlay\logs\overlay.log`

---

### Browser Not Connecting

**Symptom:** `helperBridge` reports `phase: 'not_found'`

**Solutions:**
- Verify WebSocket port: `netstat -ano | findstr 38766`
- Disable browser CORS blockers (localhost should be exempt)
- Use HTTP fallback: check if POST `/overlay/state` returns 202
- Inspect browser console for WebSocket errors

---

### Events Not Forwarding

**Symptom:** Overlay emits events but browser doesn't receive them

**Checklist:**
1. Verify event queue is not full: GET `/overlay/events?since=0` should return `dropped: 0`
2. Check WebSocket connection: should receive `event_batch` messages
3. Ensure polling loop is running: `setInterval` or WebSocket message handler
4. Check helper logs for event processing errors

---

## Performance Characteristics

### Latency

- **Browser → Helper:** ~5ms (HTTP POST) or ~1ms (WebSocket)
- **Helper → Overlay:** <1ms (shared memory write)
- **Overlay → Rendering:** <1ms (next frame)
- **Total Latency:** ~10ms (browser to screen)

### Throughput

- **Max Payload Size:** 64 KiB (shared memory limit)
- **Max Event Rate:** ~1000 events/sec (before queue saturation)
- **WebSocket Bandwidth:** ~100 KB/s (JSON overhead included)

### Resource Usage

- **Helper Process:** ~20 MB RAM, <1% CPU (idle)
- **Overlay DLL:** ~5 MB RAM, <2% CPU (rendering active)
- **Browser Bridge:** ~1 MB RAM, <0.1% CPU (WebSocket idle)

---

## References

### Source Code Locations

- **Helper:** `src/helper/` (C++20)
- **Overlay:** `src/overlay/` (C++20 + DirectX 12)
- **Shared IPC:** `src/shared/` (C++20)
- **Browser Bridge:** `EF-Map-main/eve-frontier-map/src/utils/helperBridge.ts` (TypeScript)

### External Dependencies

- [cpp-httplib](https://github.com/yhirose/cpp-httplib) - HTTP server
- [asio](https://think-async.com/Asio/) - WebSocket server
- [nlohmann/json](https://github.com/nlohmann/json) - JSON parsing
- [spdlog](https://github.com/gabime/spdlog) - Logging
- [MinHook](https://github.com/TsudaKageyu/minhook) - Function hooking
- [ImGui](https://github.com/ocornut/imgui) - Overlay UI

### Documentation

- `docs/LLM_TROUBLESHOOTING_GUIDE.md` - Comprehensive troubleshooting (overlay-specific)
- `docs/decision-log.md` - Implementation decisions and historical context
- `docs/initiatives/GAME_OVERLAY_PLAN.md` - Roadmap and future plans
- `EF-Map-main/AGENTS.md` - Combined workflow expectations (cross-repo)

---

## License & Redistribution

This overlay system is part of the EF-Map project. When forking for your own application:

1. **Reusable Components:** Helper and overlay infrastructure can be used as-is
2. **Attribution:** Credit the original EF-Map project in your documentation
3. **License Compliance:** Follow the license terms in `LICENSE` (check both repos)
4. **Code Signing:** You will need your own Authenticode certificate for distribution
5. **Support:** This is community-maintained software; no official support from CCP Games

---

## Contact & Contributions

For questions or issues:

- **Original Project:** [EF-Map GitHub](https://github.com/Diabolacal/EF-Map)
- **Overlay Repository:** [ef-map-overlay GitHub](https://github.com/Diabolacal/ef-map-overlay)
- **Decision Log:** Document significant adaptations in `docs/decision-log.md`

When contributing back improvements:

1. Submit PRs to the overlay repository for infrastructure changes
2. Submit PRs to EF-Map repository for browser bridge enhancements
3. Keep cross-repo documentation synchronized (`.github/copilot-instructions.md`, `AGENTS.md`)

---

**Document Version:** 1.0 (2025-10-22)
**Last Updated:** Compatible with overlay schema version 4
**Maintained By:** EF-Map Community
