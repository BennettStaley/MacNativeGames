// TOTK: variable refresh (adaptive sync) presentation in full screen on ProMotion / Adaptive-Sync displays (nvn_mtl.m,
// TOTK_VRR=auto|0|1, setting framerate.vrr). Shared by nvn_mtl.m, the pacing in hle_nvn.c and the Mac settings menu.
//
// Apple (WWDC21 session 10147, https://developer.apple.com/videos/play/wwdc2021/10147/): variable refresh on the Mac
// needs Apple silicon, a VRR display and the app in full screen. A display supports it when NSScreen's
// minimumRefreshInterval differs from its maximumRefreshInterval; displayUpdateGranularity 0 means any interval in
// between. The app presents with presentDrawable:afterMinimumDuration: 1 / fps (their example: 78 fps) or a plain
// present from its own timer. CAMetalDisplayLink.preferredFrameRateRange snaps to divisors of the maximum refresh, so
// the display link cannot pace 77 fps: in VRR the display link is off and the present thread takes its own drawables.
// OpenEmu does the same (GameWindowController.swift turns it on in full screen when min != max; OpenEmuKit
// OpenEmuHelperApp.swift:751 presents with present(drawable, afterMinimumDuration: 1.0 / frameInterval)).
#pragma once

enum {
    MTL_VRR_OFF = 0,        // the setting / TOTK_VRR=0
    MTL_VRR_ACTIVE = 1,     // presenting at the game's own rate
    MTL_VRR_WINDOWED = 2,   // the display can, the window is not full screen
    MTL_VRR_FIXED = 3,      // the window's display has a fixed refresh rate
    MTL_VRR_INTERP = 4,     // MetalFX frame interpolation is on (its timing assumes fixed refreshes)
    MTL_VRR_UNKNOWN = 5,    // no window (yet), or not a Mac
};
int mtl_vrr_state(void);              // the state the last present ran in (before the first present: worked out now)
int mtl_vrr_active(void);             // mtl_vrr_state() == MTL_VRR_ACTIVE
double mtl_vrr_target_fps(void);      // the rate the last present was paced for (0: none yet)
int mtl_vrr_range(double r[3]);       // the window's display: r[0] min interval, r[1] max interval, r[2] granularity (s); returns 1 in full screen, 0 windowed, -1 unknown
void mtl_vrr_set(int on);             // the setting at run time: 1 auto, 0 off
