# Microsoft Store Assets Guide - EF-Map Overlay Helper

## Screenshot Requirements

### Recommended Screenshots (4-6 total)

#### 1. **In-Game Route Overlay** (Required)
- **What to capture:** EVE Frontier game window with route overlay visible
- **Resolution:** 1920x1080 or 1366x768 minimum
- **Shows:** Active route waypoints displayed over gameplay
- **Legal:** YES, you can include game background - this is standard for overlay apps (Discord, OBS, GeForce Experience all do this)
- **Tip:** Capture during active flight with overlay panel visible in corner

#### 2. **DPS/Mining Telemetry Widgets** (Required)
- **What to capture:** In-game view showing DPS graph or mining m³/min display
- **Resolution:** 1920x1080 or 1366x768 minimum
- **Shows:** Real-time performance metrics overlaid on gameplay
- **Tip:** Capture during combat (DPS) or mining operation (m³/min active)

#### 3. **Helper Connection Page** (Recommended)
- **What to capture:** ef-map.com in browser showing Overlay panel with "Connected" status
- **Resolution:** 1920x1080 or 1366x768 minimum
- **Shows:** Green "Connected" indicator, helper status, "Send to Overlay" button
- **Tip:** Navigate to https://ef-map.com → open Overlay panel → show connection status

#### 4. **Visited Systems Tracking** (Recommended)
- **What to capture:** In-game overlay showing visited systems list or web app showing sync
- **Resolution:** 1920x1080 or 1366x768 minimum
- **Shows:** Session tracking feature in action
- **Tip:** Capture after flying through several systems to show populated list

#### 5. **Route Calculation + Send** (Optional but helpful)
- **What to capture:** ef-map.com showing calculated route with "Send to Overlay" button
- **Resolution:** 1920x1080 or 1366x768 minimum
- **Shows:** Web app integration workflow
- **Tip:** Calculate a multi-jump route, highlight the send button

#### 6. **Full Overlay Context** (Optional)
- **What to capture:** Game window with multiple overlay widgets visible simultaneously
- **Resolution:** 1920x1080 or 1366x768 minimum
- **Shows:** Route + telemetry + tracking all active at once

---

## Store Logo Requirements (Windows 10/11)

Based on your screenshot, Microsoft wants these specific sizes for Store display:

### Required Store Display Logos

| Image Type | Size | Format | Purpose |
|------------|------|--------|---------|
| **1:1 App icon** | 300x300 | PNG | Square app icon for Store listing |
| **1:1** | 150x150 | PNG | Smaller square icon variant |
| **1:1** | 71x71 | PNG | Tiny square icon for compact views |

**Note:** These are SEPARATE from your package logos. These are used only in the Store listing display.

### Current Package Logos (Already Created)
You already have these in `staging/Assets/`:
- ✅ Square150x150Logo.png (150x150) - Used in package
- ✅ Square44x44Logo.png (44x44) - Used in package
- ✅ StoreLogo.png (50x50) - Used in package
- ✅ Wide310x150Logo.png (310x150) - Used in package
- ✅ SplashScreen.png (620x300) - Used in package

---

## Logo Creation Instructions

Since you have placeholder logos (white cross on dark background), you should create proper Store display logos:

### Option 1: Use Existing Package Logo (Quick)
If you're happy with the white cross design:
1. Resize `Square150x150Logo.png` to create:
   - 300x300 version (scale 2x)
   - Keep 150x150 as-is
   - Resize to 71x71

### Option 2: Create Branded Logo (Recommended)
Create a simple but recognizable icon:
- **Design ideas:**
  - EF-Map star/navigation compass icon
  - Stylized route line with waypoint markers
  - Overlay window wireframe icon
  - Galaxy/frontier theme with navigation elements

**Design constraints:**
- Simple, recognizable at small sizes (71x71 must be clear)
- Transparent PNG background OR solid color that matches brand
- High contrast for visibility
- No text (icons only work at these sizes)

---

## Quick Creation Script (If keeping placeholder design)

I can create a PowerShell script to generate the required Store logo sizes from your existing 150x150 logo by scaling it. Let me know if you want that, or if you'd prefer to design a custom icon first.

---

## Recommendation

**My advice:**
1. **Screenshots:** Definitely capture in-game with overlay visible - this is the only way to show what the app actually does!
2. **Store Logos:** Upload custom sizes (300x300, 150x150, 71x71) rather than letting Microsoft pull from package. This gives you control over how the Store listing looks.

**Priority:**
1. Get 4 good screenshots showing the app in use (in-game captures are essential)
2. Create or resize logos to the three required sizes
3. Upload everything to Store submission

Let me know if you want me to create a PowerShell script to generate the logo sizes from your existing placeholder, or if you want to design a custom icon first!
