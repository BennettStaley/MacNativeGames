// TOTK: system load for the bottom-right line (user, 2026-10-16 night: the load of every CPU core and of the GPU, on one
// line like the FPS counter). See totk_sysload.c.
#pragma once
#include <stddef.h>
#include <stdint.h>

enum { SYSLOAD_MAX_CORES = 32 };
/* Percentages, -1 = not known. p / e: every performance / efficiency core in logical-CPU order. gpu_sys: the GPU's own
   utilisation as macOS reports it (the whole GPU: other apps and our keep-alive spin kernels included, so near 100 % while
   the game runs). gpu_game: the share of time the GPU spent on our command buffers (the game's rendering, MetalFX; not the
   keep-alive). */
/* thread_max: the busiest thread of this process, % of one core (-1 = not known yet); thread_name: its name (for the log). */
/* Power, watts averaged over the last SYSLOAD_POWER_SECONDS (user, 2026-10-23: "can my charger keep up or are we draining"),
   negative = not known: cpu_w / gpu_w from the SoC's energy counters (IOReport "Energy Model": CPU Energy, GPU Energy; no
   root needed); sys_w: the whole Mac as the battery controller measures it (SystemLoad: SoC, display, DRAM, everything);
   in_w: what the charger delivers (SystemPowerIn); bat_w: into the battery (+ charging, - draining; Amperage x Voltage,
   NaN when no battery); charger_w: the charger's rating (0 = none connected, -1 = not known); left_min: play time left
   while the battery drains (user, 2026-10-23), the energy still in it (AppleRawCurrentCapacity x Voltage) over the drain
   averaged across SYSLOAD_DRAIN_SECONDS (steadier than the 10 s figure), -1 when not draining or not known. */
enum { SYSLOAD_POWER_SECONDS = 10, SYSLOAD_DRAIN_SECONDS = 60 };
typedef struct { int np, ne, p[SYSLOAD_MAX_CORES], e[SYSLOAD_MAX_CORES], gpu_sys, gpu_game, thread_max; char thread_name[32];
                 int have_power, charger_w, left_min; double cpu_w, gpu_w, sys_w, in_w, bat_w; } sysload_info;

int sysload_enabled(void);                       // TOTK_LOAD_OVERLAY=0 hides the line (Mac default on, iOS off)
void sysload_start(void);                        // starts the sampler thread once (every 0.5 s)
int sysload_get(sysload_info *out);              // 0 before the first sample
void sysload_note_gpu(double start_s, double end_s);   // one of our command buffers ran on the GPU (CACurrentMediaTime seconds)
/* "P 55% 56% 55% 53%  E 43% 29% 20% 12%  9%  7%  T 47%  GPU 99% (70%)" (T: the busiest thread) */
void sysload_text(char *out, size_t n, const sysload_info *in);
/* "CPU 9.0 W  GPU 21.3 W  Total 42 W  Charger 100 W (36 W in)  Battery -6 W draining, 2 h 05 min left"; returns 1 when the battery is draining (shown
   highlighted), 0 otherwise, -1 when there is nothing to show (out is then empty). */
int sysload_power_text(char *out, size_t n, const sysload_info *in);
/* Busy time of command buffers that complete in order: the union of their [start, end) intervals. Returns what was added. */
typedef struct { double last_end, busy; } sysload_busy;
double sysload_busy_add(sysload_busy *b, double start_s, double end_s);
