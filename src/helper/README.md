# Helper Application Skeleton

This directory will contain the Windows helper/tray application responsible for:

- Registering the `ef-overlay://` protocol and exposing a localhost API for the web app.
- Discovering and attaching to the EVE Frontier process (DirectX 12 pipeline).
- Managing overlay session lifecycle, configuration persistence, and logging.

## Initial setup checklist
- [x] Decide on implementation language/tooling (**C++20 + MSVC**, native end-to-end).
- [x] Define project structure (CMake with FetchContent-managed dependencies).
- [x] Stand up localhost HTTP listener (stub `/health` and `/overlay/state` routes).
- [x] Establish protocol handler registration for `ef-overlay://` deep links + shared-secret enforcement.
- [ ] Implement stub attach/detach commands that log intent without injecting.

Add source files once the implementation plan is finalized.

## Current endpoints

| Method | Path              | Description                                                  | Auth header (`x-ef-overlay-token`) |
|--------|-------------------|--------------------------------------------------------------|------------------------------------|
| GET    | `/health`         | Returns status and uptime counters.                          | Optional                           |
| POST   | `/overlay/state`  | Validates + stores canonical overlay schema, updates IPC.    | Required if `EF_OVERLAY_TOKEN` set |
| GET    | `/overlay/state`  | Returns the last canonical payload snapshot (if any).        | Required if `EF_OVERLAY_TOKEN` set |

Set `EF_OVERLAY_TOKEN` to require a shared secret; omit to run in open mode (development only). `EF_OVERLAY_HOST` and `EF_OVERLAY_PORT` override the default `127.0.0.1:38765` binding.

When a payload is accepted the helper serializes it in canonical form and writes the snapshot into the shared-memory mapping `Local\EFOverlaySharedState` so the overlay DLL can render the latest route preview.

## Protocol workflow

- `ef-overlay-helper.exe --register-protocol` – registers `ef-overlay://` under the current user (writes to `HKCU\Software\Classes`).
- `ef-overlay-helper.exe --unregister-protocol` – removes the scheme.
- Launching with `ef-overlay://...` arguments attempts to forward the command to a running helper; if none responds it boots a new instance, applies the command, and stays running for follow-up requests.

Override the listener address via `EF_OVERLAY_HOST` and `EF_OVERLAY_PORT` environment variables before launching the helper.
