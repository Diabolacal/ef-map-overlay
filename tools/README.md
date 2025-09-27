# Overlay Tooling

Utility scripts used during development, packaging, and diagnostics for the EF-Map overlay project will live here.

## Planned scripts
- Build wrappers (`build.ps1`, CI-friendly scripts) that invoke the CMake toolchain with consistent configuration flags.
- Installer automation (MSI/MSIX packaging, signing, artifact publishing).
- DX12 overlay diagnostics (frame capture toggles, log collection).
- Helper/overlay communication testers (local HTTP + IPC probes).

Each script should:
- Support a `DRY_RUN` option when performing destructive actions.
- Emit concise JSON summaries for logs and CI pipelines.
- Document prerequisites (PowerShell modules, SDK versions, etc.).

Populate this folder as tooling tasks arise.
