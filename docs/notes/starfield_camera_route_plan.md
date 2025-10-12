# Starfield Camera & Route Rendering Plan (2025-10-02)

## Goals
- Align the native overlay starfield with the EF-Map web app camera so both experiences share orientation, zoom, and framing.
- Render the active route (polyline + node markers) in the DX12 overlay to mirror in-browser guidance cues.
- Preserve headroom for future effects (selection glow, safety gradients) without overhauling the pipeline again.

## Coordinate System Assumptions
- Map data uses EF-Map world coordinates (meters) with +X east, +Y up, +Z south (Right-handed). If runtime observations contradict this, we can flip axes in the vertex shader without touching asset data.
- Camera pose emitted from the helper (`overlay::CameraPose`) is authoritative. When absent, fall back to an orbit camera positioned at `center + (0.0, radius * 0.4, radius * 1.6)` looking at `center` with a 60° FOV.
- Route segment positions are read directly from overlay state (system IDs → lookup in star catalog). We assume catalog lookups succeed; on failure we log + skip the segment.

## Rendering Strategy
1. **Starfield**
   - Upload catalog positions unscaled (float3) and apply view-projection in the vertex shader.
   - Constant buffer per frame stores `ViewProj`, camera position, and a point-size scale derived from viewport height.
   - Output `SV_PointSize` from vertex shader to keep sprite size roughly invariant under zoom; blend additively.

2. **Route Polyline**
   - Dynamic upload buffer per frame containing ordered route points in world space.
   - Render as `LINESTRIP` with a lightweight pixel shader that fades distant segments and colors the active/visited/future legs differently.
   - Follow with instanced quads or point sprites for node markers (optional in first pass) to highlight the current waypoint and destination.

3. **Layering**
   - Draw starfield → route polyline → route markers → ImGui HUD, sharing the same command list to minimise barriers.
   - Reuse the existing overlay render targets and fence coordination.

## Performance Guardrails
- Keep vertex buffers in upload memory initially (≤25k stars, ≤128 route nodes). If profiling shows GPU cost >1 ms per frame, migrate to default heap + staging copies.
- Avoid per-frame heap allocations; reuse mapped upload buffers and only rewrite dirty ranges.
- Expose a debug counter (spdlog) for draw call duration if GPU timestamps become necessary.

## Validation
- Unit: overlay tests already cover catalog lookup; add a helper assertion for route-to-star mapping once markers land.
- Runtime: Smoke with helper sample payload (mock camera) and later with real EF client once event bridge streams camera pose.

## Follow-ups
- Integrate safety/ownership tinting once telemetry exposes per-system flags.
- Investigate frustum culling (CPU-side) to trim draws when zoomed tightly.
- Feed star pick results back to helper (ray cast) in a later milestone.
