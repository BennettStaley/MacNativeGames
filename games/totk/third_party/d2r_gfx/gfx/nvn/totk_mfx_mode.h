// TOTK: MetalFX frame interpolation modes, shared by totk_mfx.m (Objective-C) and the pacing in hle_nvn.c (C).
// Key 2 cycles off -> 30+30 -> half the refresh rate (50+50 at 100 Hz) -> uncapped -> off (user, 2026-10-16 night).
#pragma once
enum { MFX_OFF, MFX_30, MFX_HALF, MFX_MAX, MFX_NMODES };
int mfx_enabled(void);   // the mode of this game frame (0 = off)
/* GPU memory (totk_mfx.m mfx_present_tick): above MFX_BP_HIGH percent of the budget frame interpolation and per-object
   motion are paused, resumed under MFX_BP_LOW percent; after MFX_BP_MAX pauses they stay off for the session. A mode
   chosen while the use plus its estimate (mfx_need_bytes) is above MFX_BP_HIGH percent is not turned on at all. */
#define MFX_BP_HIGH 95
#define MFX_BP_LOW 85
#define MFX_BP_MAX 3
/* For the settings menu, next to the chosen mode: running as chosen, off, or not running for lack of GPU memory. */
enum { MFX_STATE_OFF, MFX_STATE_ON, MFX_STATE_PAUSED_MEMORY /* resumes on its own under MFX_BP_LOW percent */,
       MFX_STATE_OFF_MEMORY /* paused MFX_BP_MAX times: off for this session unless chosen again */,
       MFX_STATE_REFUSED_MEMORY /* chosen but never turned on: the estimate did not fit */ };
int mfx_interp_state(void);
/* A rough estimate of the GPU memory interpolation takes at an output of w x h (totk_mfx.m MFX_BYTES_PER_PX), plus the
   per-object motion arenas when TOTK_OBJ_MOTION is on; 0 x 0: the last output size it ran at (0 if never). Not a
   measurement: MetalFX does not report the memory of its interpolator. */
unsigned long long mfx_need_bytes(unsigned w, unsigned h);
