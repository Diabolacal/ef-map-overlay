# DirectX 12 Overlay Skeleton

This directory will host the injected overlay DLL responsible for rendering EF-Map data inside the EVE Frontier client.

## Responsibilities
- Hook `IDXGISwapChain` present/resizing to render overlay content.
- Expose an IPC surface (shared memory, named pipe, or message queue) for the helper to push updates.
- Manage input focus/toggling so the overlay can be shown or hidden without interfering with the game.

## Next steps
- [x] Select hooking approach (**MinHook** sourced via CMake FetchContent).
- [x] Spin up a background stub loop that exercises an ImGui context (no swap-chain binding yet).
- [x] Define data schema for route/overlay payloads shared with the helper.
- [x] Consume shared-memory snapshots to render live route previews.
- [x] Hook `IDXGISwapChain3::Present` / `ResizeBuffers` and render ImGui via DX12 backend.
- [ ] Establish clean teardown paths to avoid destabilizing the game client.

The stub renderer runs on a worker thread (scheduled after `DllMain` completes) to poll shared memory, while the MinHook-powered swap-chain hook renders those diagnostics through ImGui’s DX12 backend. DirectX 12 hook teardown and in-game input toggles remain future work.

Source files will be added as the prototype solidifies.
