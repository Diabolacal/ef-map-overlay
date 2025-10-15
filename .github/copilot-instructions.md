# Copilot Project Instructions (EF-Map Overlay)

Purpose: This repository hosts the native helper application, DirectX 12 overlay components, and related tooling that integrate EF-Map data directly into the EVE Frontier client. The EF-Map web application and data pipelines remain in the sibling repository `EF-Map-main`. Keep both repos aligned by mirroring relevant guardrails and documenting cross-repo dependencies.

## Operator Quick Start (Non-coder)
1. Describe the desired overlay/helper change in plain language (feature, bug, or doc tweak).
2. Assistant replies with: checklist, assumptions (≤2), risk class, plan.
3. Confirm scope (request token escalation if touching signed binaries or installer distribution).
4. Assistant patches code/docs, runs available verification (build/tests), and reports gates + follow-ups.
5. Record non-trivial outcomes in `docs/decision-log.md` (overlay) and note if the paired `EF-Map-main` log also needs an entry.
6. Keep `docs/initiatives/GAME_OVERLAY_PLAN.md` synchronized with the copy in `EF-Map-main` whenever the roadmap changes.

For the combined guardrails that apply to both repos, cross-reference `EF-Map-main/.github/copilot-instructions.md` and `EF-Map-main/AGENTS.md`.

### Agent Runbook Index
| Need | Start here | Notes |
| --- | --- | --- |
| Overlay workflow expectations | `AGENTS.md` → “Workflow primer” | Mirrors the main repo’s contract; highlights overlay-specific gates and coordination rules. |
| Build & packaging steps | `docs/LLM_TROUBLESHOOTING_GUIDE.md` (this repo) → “Build & verification recipes” | Overlay-specific commands plus quick smoke checklist. |
| Cloudflare/web integration touchpoints | `EF-Map-main/docs/CLI_WORKFLOWS.md` | Provides Wrangler-centric flows that inform how overlay payload endpoints are validated. |
| Roadmap synchronization | `docs/initiatives/GAME_OVERLAY_PLAN.md` → “Sync checklist” | Ensures both repositories stay aligned when milestones shift. |
| Decision logging | `docs/decision-log.md` + main repo log | Record overlay decisions and cross-link to EF-Map-main entries when shared components move. |

### Model Workflow Expectations (GPT-5 Codex)
- Start each reply with a brief acknowledgement plus the immediate plan.
- Maintain a synced todo list with exactly one item `in-progress` at a time.
- Report deltas only—highlight what changed since the previous message.
- Run fast verification steps yourself (build/tests/lint) before reporting.

## Architecture Overview (Overlay)
- Helper app (Windows tray/service) manages protocol handling, overlay injection, and configuration files.
- Overlay DLL (DX12) hooks the swap chain present call to render UI (ImGui prototype or texture stream).
- Local bridge communicates with the EF-Map web app via custom protocol `ef-overlay://` and localhost HTTP endpoints.
- Packaging (MSI/MSIX) signs binaries and distributes updates; long-term updater strategy tracked in the initiative plan (current lean: Microsoft Azure Code Signing).
- The EF-Map web app provides payloads (route data, overlays) via existing APIs—changes to those APIs stay in `EF-Map-main`.

## Key Folders / Files (expected)
- `src/helper/` (planned): Windows helper source.
- `src/overlay/` (planned): DX12 overlay DLL.
- `tools/` (planned): Scripts for signing, packaging, diagnostics.
- `docs/initiatives/GAME_OVERLAY_PLAN.md`: Roadmap and milestones.
- `docs/decision-log.md`: Overlay-specific decision history.

## Conventions & Patterns
- Keep helper ↔ overlay communication protocols documented (`docs/protocols/*.md` once created).
- Require explicit confirmation or tokens before modifying installer signing or certificate handling (risk: high).
- Maintain parity with EF-Map web app changes by referencing commit hashes or PR numbers in decision-log entries.
- Prefer modular architecture (helper service, overlay renderer, shared IPC library) to keep testing manageable.
- Provide dry-run modes for any script that touches user systems (registry edits, installer generation, etc.).

### Security & Signing
- Never commit private keys or certificates. Reference secure storage paths and document usage procedures.
- When documenting signing or installer steps, include placeholders (e.g., `<CERT_THUMBPRINT>`) rather than actual values.
- Note any security-relevant behavior in `docs/decision-log.md` and coordinate with `EF-Map-main` if the web app needs adjustments (e.g., token exchange).

### Cross-Repo Coordination
- API or payload changes: update `EF-Map-main` first (or in tandem) and document handshake versions.
- Shared docs: when updating guardrails or the overlay plan, reflect the change in both repositories.
- Decision logs: reference the companion entry in the other repo when actions span both (use ISO date + short title).
- Flag substantive troubleshooting updates in `docs/LLM_TROUBLESHOOTING_GUIDE.md` and mirror any cross-repo implications.

### Response Framing
- Follow the same GPT-5 Codex workflow as the main repo: purposeful preamble, todo list management, delta updates, quality gates.
- When describing verification, note the commands executed (e.g., helper build, DLL compile) and any manual smoke checks.

### Quality Gates (Overlay)
- Build helper and overlay binaries (exact commands will be documented once tooling exists).
- Run unit/integration tests when implemented (document coverage expectations in this file once available).
- Manual smoke: verify overlay attach/detach, latency, and no crash in both windowed and fullscreen modes.
- Security: confirm protocol handlers require trusted origins / session tokens before enabling.
- Operational note: Start the helper from an external PowerShell window (outside VS Code) and inject using the stable process name `exefile.exe` so smoke runs don’t break on PID changes.

### Decision Log Template (reuse)
`## YYYY-MM-DD – <Title>`
`- Goal:`
`- Files:`
`- Diff:` (added/removed LoC)
`- Risk:` low/med/high
`- Gates:` build ✅|❌ tests ✅|❌ smoke ✅|❌
`- Cross-repo:` (link to EF-Map-main entry if applicable)
`- Follow-ups:` (optional)

### When Unsure
- Consult the plan (`docs/initiatives/GAME_OVERLAY_PLAN.md`).
- Coordinate with maintainers in `EF-Map-main` for data contract questions or payload changes.
- Prefer proof-of-concept branches before touching signed releases.

(End) – Update these instructions as the overlay codebase matures (e.g., once helper/overlay folders are populated, add explicit build/test commands).
