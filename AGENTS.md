# Agents Context – EF-Map Overlay

Purpose: Guardrails for agents working in the dedicated game-overlay repository that complements the primary EF-Map web app. Keep this file short and reference authoritative docs in either repo. When collaborating with the main map project, see `EF-Map-main/AGENTS.md` for the full map workflow (sibling repository).

## Workflow primer (GPT-5 Codex)
- Begin each reply with a short acknowledgement plus the immediate plan.
- Maintain a synced todo list with exactly one item marked `in-progress` at a time.
- Report progress as deltas (what changed since the last update).
- Run quick verification steps (build/tests when available) before handing back results.

## Project quick facts
- What: Windows helper app + DirectX 12 overlay pipeline that surfaces EF-Map data in-game.
- Relationship: The EF-Map web app (repo: `EF-Map-main`) remains the source of map data, APIs, and usage conventions. This repository focuses on the native helper, overlay DLL, and desktop-side tooling.
- Source control: Keep helper/overlay code isolated here; frontend/web worker changes continue to live in `EF-Map-main`.

Useful entry points:
- Guardrails & workflow: `.github/copilot-instructions.md` (this repo) and `EF-Map-main/.github/copilot-instructions.md` for the web app.
- Initiative plan: `docs/initiatives/GAME_OVERLAY_PLAN.md` (mirrors the plan tracked in the main repo).
- Decision history: `docs/decision-log.md` (overlay-specific). For broader EF-Map history, read `EF-Map-main/docs/decision-log.md`.

## Coordination with EF-Map-main
1. Treat the web app as the authoritative producer of overlay data payloads. Do not modify web app behavior from this repo; request changes in `EF-Map-main`.
2. Keep shared docs (plan, guardrails) synchronized between repositories. When updating one copy, update the counterpart or leave a clear note.
3. Reference cross-repo dependencies explicitly in PR descriptions and decision-log entries (include commit hashes or branch names when relevant).

## Common tasks & success criteria (overlay)
- Helper protocol work: Document new protocol verbs in `docs/decision-log.md` and update helper/client contract specs.
- DX12 overlay rendering: Validate on both windowed and fullscreen modes before marking tasks complete.
- Localhost bridge: Use signed requests / short-lived tokens; note any security-sensitive changes in the decision log.
- Installer pipeline: Include signing + packaging verification steps and note certificate usage.

## High-risk surfaces (coordinate before changing)
- **Process injection & DX12 hook** – Files under `src/overlay/` that touch swap-chain hooks or input routing; mistakes can crash the game client. Keep smoke script handy for validation.
- **Helper protocol & IPC contracts** – `src/helper/`, `src/shared/`, and any schema headers mirrored in EF-Map-main. Breaking changes require simultaneous updates to browser payload producers.
- **Security-sensitive storage** – Certificate references, signing scripts, and credential handling under `tools/`. Do not alter signing flow without operator approval.
- **Installer packaging** – CMake/MSI/MSIX definitions under `build/` or `tools/`. Coordinate before changing packaging defaults or update channels.

## Safety & boundaries
- Avoid introducing web app changes here—use `EF-Map-main` for anything involving React/Workers or Cloudflare assets.
- Never commit secrets (certificates, signing keys). Reference secure storage or environment variables instead.
- Keep new tooling modular; prefer scripts under `tools/` with a `DRY_RUN` mode when performing bulk operations.

— Update this file whenever the overlay architecture or cross-repo workflows change materially.
