# Microsoft Store Submission Guide for EF Map Overlay Helper

This guide walks you through submitting the EF Map Overlay Helper to the Microsoft Store.

## Prerequisites

- [ ] Microsoft Partner Center account ($19 USD one-time fee)
- [ ] MSIX package created (use `build_msix.ps1`)
- [ ] High-quality screenshots of the overlay in action
- [ ] Privacy policy URL (https://ef-map.com/privacy)

## Part 1: Create Microsoft Partner Center Account

### Step 1: Register Developer Account

1. Go to https://partner.microsoft.com/dashboard
2. Click **"Sign up"** or **"Enroll"**
3. Choose **"Individual"** account type (simpler than Company)
4. Fill in your details:
   - **Country/Region:** United Kingdom
   - **Account type:** Individual
   - **Publisher display name:** Choose a name (e.g., "Diabolacal" or your real name)
5. Pay the $19 USD one-time registration fee
6. Wait for account verification (usually 24-48 hours)

### Step 2: Reserve App Name

1. Once verified, go to Partner Center Dashboard
2. Click **"Apps and games"** → **"New product"** → **"MSIX or PWA app"**
3. Reserve name: **"EF Map Overlay Helper"**
4. Alternative names to try if taken:
   - "EVE Frontier Map Overlay"
   - "EF Map Helper"
   - "EVE Frontier Overlay"

## Part 2: Prepare Store Assets

You'll need to create these assets for the Store listing:

### Required Screenshots (at least 1, up to 10)

- **Resolution:** 1366x768 minimum (1920x1080 recommended)
- **Content:** Show the overlay in action:
  - Screenshot 1: Route navigation overlay on starfield
  - Screenshot 2: Mining telemetry display
  - Screenshot 3: Helper tray icon and settings
  - Screenshot 4: ef-map.com integration

**How to capture:**
1. Launch EVE Frontier with overlay running
2. Use Windows + Shift + S to capture regions
3. Save as PNG files

### App Icon (Required)

- **Size:** 1240x1240 pixels minimum
- **Format:** PNG
- **Content:** The EF-Map logo (can extract from helper resources)

### Promotional Images (Optional but Recommended)

- **Hero image:** 1920x1080 (shown on Store homepage if featured)
- **Poster:** 1080x1920 (portrait orientation)

## Part 3: Build and Upload MSIX Package

### Build the Package

```powershell
cd C:\ef-map-overlay\packaging\msix
.\build_msix.ps1 -Version "1.0.0" -BuildConfig "Release"
```

This creates: `C:\ef-map-overlay\releases\EFMapHelper-v1.0.0.msix`

⚠️ **Important:** Do NOT try to install this MSIX locally! It's unsigned and will fail. It's only for Store upload.

### Upload to Partner Center

1. In Partner Center, click your reserved app name
2. Click **"Start your submission"**
3. Navigate to **"Packages"** section
4. Drag and drop `EFMapHelper-v1.0.0.msix`
5. Wait for package validation (checks manifest, capabilities, etc.)

## Part 4: Fill Out Store Listing

### Properties

- **Category:** Utilities & tools → Developer tools (or Gaming → Gaming utilities if available)
- **Subcategory:** None
- **Privacy policy URL:** https://ef-map.com/privacy
- **Supported platforms:** PC
- **System requirements:** Windows 10/11 64-bit

### Age Ratings

- Select **IARC** rating system
- Answer questionnaire:
  - Violence: None
  - Sex: None
  - Language: None
  - Controlled substances: None
  - Gambling: None
- Expected rating: **3+** or **Everyone**

### Store Listing (English - US)

**Description Template:**

```
EF Map Overlay Helper brings the power of ef-map.com directly into your EVE Frontier gameplay.

FEATURES:
• Route Navigation: Auto-advance through waypoints as you jump systems
• Live System Tracking: Syncs your current location with ef-map.com
• Mining Telemetry: Real-time ore composition and yield tracking
• Combat Statistics: Track damage and combat efficiency (in development)

HOW IT WORKS:
1. Install the helper from Microsoft Store
2. Launch before starting EVE Frontier
3. Visit ef-map.com to calculate routes
4. Routes appear as in-game overlays automatically

REQUIREMENTS:
• EVE Frontier game installed
• Windows 10/11 (64-bit)
• Active internet connection

PRIVACY:
• No personal data collection
• Only communicates with ef-map.com
• All processing happens locally on your machine

OPEN SOURCE:
Full source code available on GitHub: https://github.com/Diabolacal/ef-map-overlay

Need help? Visit ef-map.com or join our Discord community!
```

**Keywords (max 7):**
- EVE Frontier
- overlay
- route planner
- gaming tool
- space navigation
- mining tracker
- game helper

**Screenshots:**
- Upload your 4-10 screenshots (prepared earlier)
- Add captions for each:
  - "Route overlay showing next waypoint"
  - "Mining telemetry with live ore data"
  - "System tray integration"
  - "Seamless ef-map.com integration"

**Trailer (Optional):**
- If you create a video demo, upload it here
- Max 90 seconds, show key features

### Availability

- **Markets:** Worldwide (or select specific countries)
- **Pricing:** Free
- **Visibility:** Public (available to all users)

### Notes to Certification Team

**CRITICAL:** Add this note to help certification understand the DLL injection:

```
This application uses controlled DLL injection to render DirectX overlays within EVE Frontier.

Technical details:
- Injection only targets "exefile.exe" (EVE Frontier client)
- Uses standard hooking techniques (Microsoft Detours pattern)
- No modification of game files or memory manipulation
- Similar approach to Discord overlay, OBS, MSI Afterburner
- Open source for full transparency

Purpose: Displays player-owned routing data from ef-map.com as in-game HUD elements.

This is NOT a cheat or mod - it only displays information the player already has access to through ef-map.com, presented conveniently in-game.
```

## Part 5: Submit for Certification

1. Review all sections for completeness:
   - ✅ Packages uploaded
   - ✅ Properties filled
   - ✅ Age ratings completed
   - ✅ Store listing with screenshots
   - ✅ Pricing set to Free
   - ✅ Notes to certification added

2. Click **"Submit for certification"**

3. Wait for review (typically 1-3 business days)

## Part 6: What Happens Next?

### Scenario A: Certification PASSES ✅

1. You'll receive email: "Your app is in the Store"
2. App appears at: `https://www.microsoft.com/store/apps/[app-id]`
3. Update ef-map.com "Install Helper" button to point to Store link
4. Users install with one click, full Windows integration works
5. **Microsoft signs the package automatically** - no more Defender warnings!

### Scenario B: Certification FAILS ❌

Common rejection reasons:

**Reason 1: DLL Injection Policy Violation**
- Microsoft may consider injection a security risk
- **Response:** Appeal with detailed technical explanation
- Emphasize: Open source, read-only overlay, similar to Discord/OBS
- If appeal fails: Fall back to ZIP distribution or SSL.com signing ($249/year)

**Reason 2: Incomplete Metadata**
- Missing screenshots or unclear description
- **Response:** Update submission with requested changes and resubmit

**Reason 3: Crashes During Testing**
- Tester doesn't have EVE Frontier installed
- **Response:** Add note that EVE Frontier is required for full functionality
- Ensure helper gracefully handles missing game

## Part 7: After Approval

### Update Your Website

Update `DEFAULT_DOWNLOAD_URL` in your web app:

```typescript
// Before (GitHub Releases)
const DEFAULT_DOWNLOAD_URL = 'https://github.com/Diabolacal/ef-map-overlay/releases/latest';

// After (Microsoft Store)
const DEFAULT_DOWNLOAD_URL = 'ms-windows-store://pdp/?ProductId=[YOUR_PRODUCT_ID]';
```

### Monitor Reviews

- Check Partner Center for user reviews
- Respond to feedback
- Update app based on common issues

### Updates

To release v1.1.0:
1. Build new MSIX: `.\build_msix.ps1 -Version "1.1.0"`
2. Partner Center → Your app → "Update"
3. Upload new package
4. Submit for recertification (usually faster than initial review)

## Troubleshooting

### "Package validation failed"

Check:
- Publisher name in manifest matches Partner Center
- Version number is higher than previous submission
- All capabilities are declared correctly

### "App crashes during certification"

Add to notes:
- "EVE Frontier must be installed for full functionality"
- "Helper runs in system tray even without game"
- Provide logs showing graceful degradation

### Alternative Path: Microsoft Store for Business

If consumer Store rejects due to DLL injection:
- Try **Microsoft Store for Business** (enterprise distribution)
- Less strict policies for developer tools
- Requires company account instead of individual

## Cost Summary

| Item | Cost | When |
|------|------|------|
| Partner Center Account | $19 USD | One-time |
| Annual Renewal | $0 | Free after first payment |
| App Submission | $0 | Unlimited |
| Code Signing | $0 | Microsoft signs for you |

**Total: $19 one-time**

Compare to:
- SSL.com eSigner: $249/year
- Azure Trusted Signing: $9.99/month (unavailable in UK)

---

## Quick Checklist

Before submitting, verify:

- [ ] MSIX package built and uploaded
- [ ] 4+ high-quality screenshots captured
- [ ] Privacy policy URL accessible
- [ ] Description clearly explains DLL injection purpose
- [ ] Notes to certification team added
- [ ] Age rating completed
- [ ] All manifest info accurate

**Estimated time from start to finish:** 2-4 hours (first submission)
**Estimated certification time:** 1-3 business days

Good luck! 🚀
