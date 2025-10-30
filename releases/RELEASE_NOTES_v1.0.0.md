# EF Map Overlay Helper v1.0.0 - Initial Release

## 🎮 What's This?

An in-game overlay for EVE Frontier that displays real-time routing and telemetry data from [ef-map.com](https://ef-map.com).

## ✨ Features

- **Route Navigation**: Auto-advance through waypoints as you jump systems
- **Follow Mode**: Tracks your current system in real-time
- **Mining Telemetry**: Live ore composition and yield tracking
- **Combat Telemetry**: Damage tracking and combat statistics (in development)

## 📦 Installation

⚠️ **Important:** Windows Defender may block this download because it's unsigned. This is expected for DLL injection tools.

### Quick Install Steps:

1. **Download** `EFMapHelper-v1.0.0.zip` below
2. **Unblock** the ZIP: Right-click → Properties → Check "Unblock" → OK
3. **Extract** all files to a folder (e.g., `C:\EFMapHelper\`)
4. **Add Defender Exclusion**:
   - Open Windows Security → Virus & threat protection
   - Manage settings → Add or remove exclusions
   - Add folder: `C:\EFMapHelper\`
5. **Launch**: Right-click `launch_helper.ps1` → Run with PowerShell

Full installation guide: [ef-map.com/help](https://ef-map.com)

## 🔒 Privacy & Security

- **No data collection**: Helper only communicates with ef-map.com
- **Open source**: Full code available in this repository
- **Local processing**: All game data stays on your machine
- See [Privacy Policy](https://ef-map.com/privacy)

## 🐛 Known Issues

- Helper must be launched before starting EVE Frontier
- "Launch Helper" button on ef-map.com won't work (protocol not registered in this version)
- Use PowerShell script or create manual shortcut

## 📝 Requirements

- Windows 10/11 (64-bit)
- EVE Frontier installed
- .NET Runtime (usually pre-installed)

## 🆘 Troubleshooting

**Helper won't start:**
- Check Windows Defender exclusion is set correctly
- Run PowerShell script as Administrator if needed

**Overlay not appearing in-game:**
- Ensure helper is running (system tray icon visible)
- Start helper BEFORE launching EVE Frontier
- Check helper logs in the installation folder

**Need help?** Join our Discord or open an issue on GitHub.

---

**Next Release:** MSIX installer with Start Menu integration and automatic protocol registration coming soon!
