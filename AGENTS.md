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
- Tooling: **Context7 MCP** (`context7`): Configured at repository level for GitHub Copilot Agent. Provides up-to-date library documentation (DX12, Windows API, ImGui, CMake, MSI/MSIX packaging). Add "use context7" to prompts or it auto-invokes based on context. See `docs/CONTEXT7_MCP_SETUP.md` for details.

Useful entry points:
- Guardrails & workflow: `.github/copilot-instructions.md` (this repo) and `EF-Map-main/.github/copilot-instructions.md` for the web app.
- Troubleshooting guide: `docs/LLM_TROUBLESHOOTING_GUIDE.md` (overlay-first orientation and playbooks).
- Initiative plan: `docs/initiatives/GAME_OVERLAY_PLAN.md` (mirrors the plan tracked in the main repo).
- Decision history: `docs/decision-log.md` (overlay-specific). For broader EF-Map history, read `EF-Map-main/docs/decision-log.md`.
- **Microsoft Store releases**: `packaging/msix/BUILD_RELEASE_GUIDE.md` (complete build process for LLMs) and `packaging/msix/PRE_UPLOAD_CHECKLIST.md` (mandatory verification before uploads).
- **Chrome DevTools MCP**: For web app integration testing, see `EF-Map-main/AGENTS.md` → "Chrome DevTools MCP Mandate". When testing helper ↔ web app communication, use Chrome DevTools MCP to directly inspect browser console/network requests without user intermediary.

Current status snapshot (2025-10-31):
- Follow mode sync is live across helper, overlay, and EF Map surfaces.
- Mining telemetry widgets exist and are in validation using live sessions.
- Native 3D star map rendering is paused while telemetry + combat tooling take priority.
- Microsoft Store release v1.0.2 packaged with icon fix (previous v1.0.1 was missing tray icon file).

## Coordination with EF-Map-main
1. Treat the web app as the authoritative producer of overlay data payloads. Do not modify web app behavior from this repo; request changes in `EF-Map-main`.
2. Keep shared docs (plan, guardrails) synchronized between repositories. When updating one copy, update the counterpart or leave a clear note.
3. Reference cross-repo dependencies explicitly in PR descriptions and decision-log entries (include commit hashes or branch names when relevant).

## Common tasks & success criteria (overlay)
- Helper protocol work: Document new protocol verbs in `docs/decision-log.md` and update helper/client contract specs.
- DX12 overlay rendering: Validate on both windowed and fullscreen modes before marking tasks complete.
- Localhost bridge: Use signed requests / short-lived tokens; note any security-sensitive changes in the decision log.
- **Microsoft Store release builds**: Follow `packaging/msix/BUILD_RELEASE_GUIDE.md` exactly; always run `verify_msix_contents.ps1` before uploading to Partner Center. Never skip verification.

## Automated Smoke Testing Workflow (CRITICAL)
**Assistant responsibility:** The assistant MUST execute all automated smoke test steps. The user's role is ONLY to:
1. Launch helper externally (Windows Start → PowerShell → navigate + run)
2. Provide visual confirmation of in-game overlay behavior
3. Give instructions for next steps

**Assistant MUST execute automatically:**
1. **Helper launch:** `Start-Process -FilePath <helper-exe> -WorkingDirectory (Split-Path <helper-exe>) -PassThru` creates external PowerShell window (pattern from `tools/overlay_smoke.ps1` line 62)
2. **Injection:** Run `ef-overlay-injector.exe exefile.exe <dll-path>` via `run_in_terminal` and verify `[info] Injection completed`
3. **Route calculation:** Use Chrome DevTools MCP to navigate to preview URL, **click Calculate Route button** (pre-calculated routes don't auto-send), verify POST to helper succeeded
4. **Verification queries:** Check helper status, overlay state, API responses via PowerShell/curl commands

**DO NOT ask user to:**
- Launch helper manually (assistant does this via Start-Process)
- Run injector manually
- Open browser and calculate route
- Check API endpoints via PowerShell

**Rationale:** LLM can execute these steps 10-100x faster than manual user execution. User provides high-level intent and visual confirmation only. This workflow optimization is fundamental to efficient overlay development.

**External PowerShell requirement:** Helper MUST launch via `Start-Process -PassThru` (not VS Code terminal) - integrated terminals fail to bind helper correctly. Use process name `exefile.exe` (not PID) for injection.

## Context7 MCP Integration

**Purpose**: Retrieve up-to-date documentation from this repository and 500+ external libraries without user intervention.

**When to use**:
- Repository documentation lookups (troubleshooting guides, decision logs, specifications, build procedures)
- Library/framework documentation (DX12, Windows API, ImGui, CMake, MSI/MSIX packaging)
- Cross-repo coordination (EF-Map-main web app protocols, payload schemas)
- Any scenario where you'd ask user to attach files

**Library identifiers**:
- This repository: `/diabolacal/ef-map-overlay` (163 code snippets)
- Main web app repository: `/diabolacal/ef-map` (1,143 code snippets)

**Usage patterns**:
- **Automatic invocation**: Context7 activates automatically when context suggests documentation lookup
- **Explicit invocation**: Add "use context7" to prompts when you want to guarantee usage
- **Targeted queries**: `mcp_context7_get-library-docs` with library ID + topic for focused results
- **Library search**: `mcp_context7_resolve-library-id` to find external libraries by name

**Query examples**:
```
"use context7: show overlay smoke testing procedure with external PowerShell requirement"
"use context7: explain Windows API for DX12 swap chain hooking"
"use context7: find Microsoft Store packaging and verification procedures"
"use context7: retrieve helper protocol contracts and IPC schemas"
```

**Benefits over manual file attachment**:
- **Speed**: 10-15 seconds vs 3-5 minutes (12-20x faster)
- **User effort**: Zero interruption vs manual file hunting and attachment
- **Synthesis**: Automatically combines information from multiple doc sources
- **Accuracy**: Includes LIBRARY RULES from `context7.json` configuration (CRITICAL external PowerShell, process name injection, etc.)
- **Freshness**: Retrieves up-to-date documentation from indexed repositories

**What Context7 returns**:
- Library rules (operational guardrails from `context7.json` - CRITICAL smoke testing requirements)
- Code snippets (PowerShell commands, C++ code, CMake configurations, API examples)
- API documentation (helper endpoints, protocol schemas, packaging scripts)
- Cross-references (related files, decision log entries, build guides)

**Performance expectations**:
- Typical query response: 10-15 seconds
- Reduces message exchanges: 1 message vs 3-4 back-and-forth
- Eliminates context-gathering delays entirely

**Configuration**:
- VS Code setup: `.vscode/mcp.json` (local stdio transport)
- Indexing rules: `context7.json` (exclusions, focus areas, operational rules)
- API key: Environment variable in MCP config (never committed)
- Refresh: Automatic via Context7 platform; manual resubmit for major changes

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
