# Microsoft Store Resubmission Checklist
**Date**: October 30, 2025  
**Package**: EFMapHelper-v1.0.0.msix  
**Location**: `C:\ef-map-overlay\releases\EFMapHelper-v1.0.0.msix`

## ✅ Issues Fixed

### Issue 1: Default/Placeholder Images ✅ RESOLVED
- **Problem**: Root-level PNG assets were 188-627 byte placeholders
- **Fix**: Deleted placeholder files; MSIX now uses branded EF-Map logo assets
- **Verification**: Extracted MSIX shows proper asset sizes:
  - `Square150x150Logo.png`: 6.9 KB (was 514 bytes)
  - `Square44x44Logo.png`: 1.4 KB (was 188 bytes)
  - `StoreLogo.png`: 1.6 KB (was 196 bytes)
  - `Wide310x150Logo.png`: 11.9 KB (was 627 bytes)
  - `SplashScreen.png`: 43.3 KB (new)

### Issue 2: Undisclosed Dependencies ✅ RESOLVED
- **Problem**: Microsoft policy 10.2.4.1 requires explicit dependency disclosure in first 2 lines
- **Fix**: Added 2-line disclosure to Store listing (see below)

---

## 📦 Package Verification

**MSIX Manifest Identity** (verified via extraction):
```xml
Name="Ef-Map.EF-MapOverlayHelper"
Publisher="CN=9523ACA0-C1D5-4790-88D6-D95FA23F0EF9"
Version="1.0.0.0"
PublisherDisplayName="Ef-Map"
DisplayName="EF-Map Overlay Helper"
```

**Package Size**: 1.26 MB  
**Assets Included**: 5 PNG files (all branded, no placeholders)  
**Binaries**: ef-overlay-helper.exe, ef-overlay.dll, ef-overlay-injector.exe

---

## 📝 Store Listing Updates Required

### **Product Description - First Two Lines** (CRITICAL)

Copy-paste these EXACT two lines as the **first two lines** of your Product Description in Partner Center:

```
**Requires:** Microsoft Visual C++ Redistributable (automatically installed if missing) and DirectX 12 (Windows 10 version 2004+).
**Bring real-time EF-Map data directly into EVE Frontier.** Windows native application that renders live route guidance, mining/combat telemetry, and session tracking as an in-game overlay—no alt-tabbing required.
```

### Why This Satisfies Microsoft's Policy
- ✅ **First line** explicitly discloses VC++ Redistributable dependency (policy 10.2.4.1)
- ✅ **DirectX 12** requirement clearly stated
- ✅ **"Automatically installed if missing"** clarifies user experience
- ✅ **Second line** maintains product pitch seamlessly
- ✅ **Under 2-line requirement** (Microsoft requires disclosure within first two lines)

---

## 🚀 Submission Steps

1. **Log into Microsoft Partner Center**  
   https://partner.microsoft.com/dashboard

2. **Navigate to Your App**  
   Apps and Games → EF-Map Overlay Helper

3. **Create New Submission**  
   Click "Start update" or "New submission"

4. **Upload Package**  
   - Packages section
   - Upload: `C:\ef-map-overlay\releases\EFMapHelper-v1.0.0.msix`
   - Wait for package validation (2-5 minutes)

5. **Update Product Description**  
   - Store listings → English (United States)
   - **REPLACE THE FIRST TWO LINES** with the disclosure text above
   - Keep remaining description unchanged

6. **Review & Submit**  
   - Verify all sections show green checkmarks
   - Submit for certification
   - Expected turnaround: 1-3 business days

---

## 📋 Pre-Submission Verification

- [x] MSIX package built successfully
- [x] Manifest contains correct Partner Center identity values
- [x] Assets are branded (not placeholders)
- [x] Version is 1.0.0.0
- [x] Publisher certificate matches Partner Center
- [x] Two-line dependency disclosure prepared
- [ ] **YOU**: Upload MSIX to Partner Center
- [ ] **YOU**: Update Store listing description
- [ ] **YOU**: Submit for certification

---

## 📞 Support

If certification fails again:
1. Check Partner Center notification email for specific failure reason
2. Cross-reference with `packaging/msix/STORE_SUBMISSION_GUIDE.md`
3. Contact via GitHub Issues: https://github.com/Diabolacal/ef-map-overlay/issues

**Expected Outcome**: Certification approval within 1-3 days, automatic publishing to Microsoft Store.

---

*Generated: October 30, 2025*  
*Package: EFMapHelper-v1.0.0.msix*  
*Size: 1.26 MB*
