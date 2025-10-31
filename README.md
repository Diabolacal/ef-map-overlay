# EF-Map Overlay Helper

**Bring real-time EF-Map data directly into EVE Frontier.** Windows native application that renders live route guidance, mining/combat telemetry, and session tracking as an in-game overlay—no alt-tabbing required.

## ✨ Features

- **Live Route Overlay** – Next system display with auto-advance as you jump; one-click clipboard for in-game links
- **Mining Telemetry** – Real-time yield tracking (m³/min), per-ore breakdown, rolling rate sparklines with session totals
- **Combat Telemetry** – DPS tracking (dealt/taken/peak), hit quality analytics, dual-line combat graphs
- **Session Tracking** – Visited systems (all-time + per-session), automatic position sync with EF-Map web app
- **Bookmark Management** – Create personal/tribe bookmarks directly from overlay
- **P-SCAN** – Proximity scanner for network nodes after deploying portable structures
- **Privacy-First** – All telemetry stays local on your machine; no data uploads

## 🚀 Installation

### Option 1: Microsoft Store *(Recommended - auto-updates)*
**Status**: ✅ **LIVE** (Published 2025-10-30)

**[Install from Microsoft Store](ms-windows-store://pdp/?productid=9NP71MBTF6GF)** or search "EF-Map Overlay Helper"

- Store ID: `9NP71MBTF6GF`
- Web link: https://apps.microsoft.com/detail/9NP71MBTF6GF
- One-click install with automatic updates and trusted Microsoft certificate

### Option 2: GitHub Releases *(Available now)*
**Download**: [v1.0.0 ZIP](https://github.com/Diabolacal/ef-map-overlay/releases/tag/v1.0.0)

1. Extract ZIP to any folder
2. Run `install.ps1` (PowerShell script creates shortcuts + registers protocol)
3. Launch "EF Map Overlay Helper" from Start Menu

## 🎮 Usage

1. **Launch helper** – System tray icon appears with EF-Map logo
2. **Start EVE Frontier** – Game can run in any display mode (windowed/fullscreen)
3. **Calculate route** – Open [EF-Map web app](https://ef-map.com), calculate a route
4. **Route auto-sends to helper** – No button clicks needed; route appears in overlay immediately
5. **Press F8 in-game** – Toggle overlay visibility; drag to reposition, resize from edges
6. **Jump through route** – Next system auto-advances as you navigate

### Overlay Tabs
- **Overview** – Current route progress, next system details, bookmark creation
- **Mining** – Session totals, per-ore breakdown, m³/min sparkline
- **Combat** – DPS graphs (dealt/taken), hit quality stats, peak tracking
- **P-SCAN** – Network node proximity after deploying structures

## 🔧 Building from Source

**Prerequisites**: Visual Studio 2022 or MSVC Build Tools (C++20), CMake 3.20+

```powershell
# Configure
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build Debug
cmake --build build --config Debug

# Build Release
cmake --build build --config Release

# Run tests
cmake --build build --config Release --target ef_overlay_tests
cd build
ctest -C Release --output-on-failure
```

**Outputs**:
- `build/src/helper/Release/ef-overlay-helper.exe` – Main helper application
- `build/src/overlay/Release/ef-overlay.dll` – DirectX 12 overlay module
- `build/src/injector/Release/ef-overlay-injector.exe` – DLL injection utility

### Manual Testing
See detailed smoke test procedures in the full README sections below (sections retained from original for developer reference).

## 📚 Documentation

### For Users
- **[Microsoft Store Listing](https://apps.microsoft.com/detail/9NP71MBTF6GF)** – Official Store page with screenshots
- **[Overlay Plan](docs/initiatives/GAME_OVERLAY_PLAN.md)** – Feature roadmap with phase status

### For Contributors
- **[AGENTS.md](AGENTS.md)** – Workflow guardrails for AI agents and contributors
- **[Troubleshooting Guide](docs/LLM_TROUBLESHOOTING_GUIDE.md)** – Architecture overview + diagnostic paths
- **[Decision Log](docs/decision-log.md)** – Technical decision history

### For Microsoft Store Releases (LLMs/Maintainers)
- **[BUILD_RELEASE_GUIDE.md](packaging/msix/BUILD_RELEASE_GUIDE.md)** – Complete process for building and packaging Store releases
- **[PRE_UPLOAD_CHECKLIST.md](packaging/msix/PRE_UPLOAD_CHECKLIST.md)** – Mandatory verification before uploading to Partner Center
- **[verify_msix_contents.ps1](packaging/msix/verify_msix_contents.ps1)** – Automated verification script (run before every upload)

## 🗺️ Related Projects

- **[EF-Map](https://github.com/Diabolacal/EF-Map)** – Primary web application (React + Three.js starmap)
- **[EVE Frontier Tools](https://github.com/VULTUR-EveFrontier/eve-frontier-tools)** – Universe data extraction toolkit

## 🏗️ Architecture

| Component | Technology | Purpose |
|-----------|-----------|---------|
| Helper | C++20, Win32 | HTTP API, protocol handler, log watcher, WebSocket bridge |
| Overlay DLL | C++20, DirectX 12, ImGui | Swap-chain hook, in-game rendering, input capture |
| Injector | C++20, MinHook | DLL injection into game process |
| Web Integration | WebSocket, REST | Bidirectional sync with EF-Map web app |

**Data Flow**: Game logs → Helper (parses position/events) → Overlay (renders HUD) ↔ Web App (route calculations, bookmarks)

All telemetry processing happens locally. No personal data leaves your machine.

## 🎯 Roadmap Status

| Phase | Status | Key Deliverables |
|-------|--------|-----------------|
| 1 – Helper ↔ UI Foundation | ✅ Complete | WebSocket bridge, follow mode, tray integration |
| 2 – Mining Telemetry | ✅ Complete | Yield tracking, sparklines, session persistence |
| 3 – Combat Telemetry | ✅ Complete | DPS graphs, hit quality analytics, 2s tail-off |
| 4 – Follow Mode | ✅ Complete | Auto map recentering, position streaming |
| 5 – Route Navigation | ✅ Complete | Next system display, visited tracking, bookmarks, P-SCAN |
| 6 – Packaging & Distribution | ✅ IN CERTIFICATION | GitHub Releases live, Microsoft Store submitted |

**Next**: Phase 6 certification complete → production deployment

## 🤝 Contributing

1. Read `AGENTS.md` for workflow expectations
2. Check `docs/decision-log.md` for recent architectural choices
3. Keep changes focused and minimal
4. Update roadmap (`docs/initiatives/GAME_OVERLAY_PLAN.md`) for significant features
5. Mirror cross-repo changes in EF-Map when needed

## 📜 License

MIT License - see [LICENSE](LICENSE)

EVE Frontier is a trademark of CCP Games. This project is not affiliated with or endorsed by CCP Games.

---

**Community Recognition**: EF-Map featured on [CCP's official EVE Frontier Community Gallery](https://evefrontier.com/en/community-gallery)

For issues, feature requests, or questions: [GitHub Issues](https://github.com/Diabolacal/ef-map-overlay/issues)

Happy exploring o7
