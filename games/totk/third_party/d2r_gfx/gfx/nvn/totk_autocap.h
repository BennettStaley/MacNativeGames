/* TOTK: the automatic frame rate limit (settings menu "Auto (steady)", framerate.cap = auto, TOTK_UNCAPPED_HZ=auto).
   A game frame shown for a varying number of refreshes judders: at about 98 fps on the M3 Pro's fixed 120 Hz display
   link, 21% of frames stayed up for two refreshes (8.3 / 16.7 ms alternating). Auto picks the highest rate that divides
   the display's (120 Hz: 120 / 60 / 40 / 30; 60 Hz: 60 / 30; 100 Hz: 100 / 50 / 33) that the game's own frame time holds,
   as a number of refreshes per game frame (k). It looks at the frame time without our pacing wait, so a capped game
   still shows what it could do.
   - Window: the newest 2.5 s of frames. Frames that do not show the scene's cost are left out: shader / pipeline
     builds, loading screens, frames with few draws (title, loading), the seconds after an output / scene size change
     (hle_nvn.c), single spikes above 100 ms and the 0.5 s after one: the GPU clocks down during such a stall, and
     after a 240-360 ms screenshot or loading hitch the M3 Pro ran about 20 frames at twice its GPU time, which alone
     pushed the p90 of a 60 fps window past 25 ms.
   - Down at once on sustained misses: p90 of the window above the current rate's budget (more than 1 frame in 10
     late). It goes to the highest rate whose budget the window's p95 fits with 8% to spare.
   - Up after a calm: p95 under 85% of the faster rate's budget for 10 s in a row. Or, while the current rate holds
     (p90 within its budget), a try after 20 s: a frame time measured at a lower rate is not the one the faster rate
     will see, in both directions. It reads low because the GPU finishes the previous frame during our wait (7.0 ms at
     60 fps against 10.6 ms uncapped on the M3 Pro), and it reads high because the GPU clocks down when it idles: with
     the keep-alive off, 1080p frames at 30 fps took 20 ms of GPU and 26 ms in all, so the window never looked calm
     enough for 40 although 40 and 60 held at once when tried.
   - After a change the window starts over (1.5 s before the next decision).
   - A rise that fails (down again within 5 s) doubles the wait before the next one, up to about 5 minutes, and is
     remembered with the p95 that led to it: that rate is not tried again for 5 minutes unless the p95 falls 10% below
     it (a lighter scene). A rise that holds for 30 s resets the wait to 10 s. So a scene that cannot hold the faster
     rate costs one try (about 2 s at the uneven rate) every 5 minutes at most.
   Pure C, header only: hle_nvn.c drives it per frame, tests/test_autocap.c and dm_nvn_probe_present_interval feed it
   synthetic frame times. */
#pragma once
#include <string.h>
#include <stdlib.h>

#define ACAP_N 512
#define ACAP_WIN_MS 2500.0
#define ACAP_EVAL_MS 500.0
#define ACAP_HOLD_MS 1500.0
#define ACAP_RISE_MS 10000.0
#define ACAP_RISE_FRAC 0.85
#define ACAP_DROP_FIT 0.92
#define ACAP_MIN_FPS 29
#define ACAP_MAX_FPS 120
#define ACAP_FAIL_MS 5000.0
#define ACAP_RECOVER_MS 500.0
#define ACAP_HELD_MS 30000.0
#define ACAP_RISE_MAX_MS 320000.0
#define ACAP_PROBE_MS 20000.0
#define ACAP_KMAX 8

typedef struct {
    int hz;                        /* display refresh rate the choice is for */
    int k, kmin, kmax;             /* refreshes per game frame now / fastest / slowest allowed */
    double work[ACAP_N], wall[ACAP_N]; unsigned n, head;   /* ring: unpaced frame time, wall frame time (ms) */
    double since_eval, good, hold, recover; /* ms */
    double p90, p95;               /* last evaluation's window */
    double rise_need, since_rise;  /* calm needed for the next rise (ms, backs off); time since the last rise (<0: none) */
    int changed;                   /* +1 rose / -1 dropped at the last call (for the log), 0 otherwise */
    double clock;                  /* ms of frames seen */
    double fail_p95[ACAP_KMAX + 1], fail_at[ACAP_KMAX + 1];   /* per k: the p95 before a failed rise from k, and when (0: none) */
    double rise_p95; int rise_from, rise_try; /* the last rise: p95 before it, from k, 1 = a try (not calm) */
} acap_t;

static inline int acap_cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static inline double acap_budget(const acap_t *s, int k) { return 1000.0 * k / s->hz; }
static inline int acap_fps(const acap_t *s) { return s->hz > 0 && s->k > 0 ? (s->hz + s->k / 2) / s->k : 0; }

static inline void acap_reset(acap_t *s, int hz) {
    memset(s, 0, sizeof *s);
    s->hz = hz > 0 ? hz : 60;
    s->kmin = 1; while (s->hz / s->kmin > ACAP_MAX_FPS) s->kmin++;
    s->kmax = s->kmin; while (s->hz / (s->kmax + 1) >= ACAP_MIN_FPS) s->kmax++;
    if (s->kmax > ACAP_KMAX) s->kmax = ACAP_KMAX;
    s->k = s->kmin; s->rise_need = ACAP_RISE_MS; s->since_rise = -1;
}
static inline void acap_clear_window(acap_t *s) { s->n = 0; s->head = 0; s->since_eval = 0; s->good = 0; s->hold = ACAP_HOLD_MS; }

/* One game frame. work_ms: the frame time without our pacing wait; wall_ms: the frame time as it happened; skip: the
   frame does not show the scene's cost (left out of the window, its time still passes). Returns k. */
static inline int acap_frame(acap_t *s, double work_ms, double wall_ms, int skip) {
    s->changed = 0;
    if (wall_ms < 0) wall_ms = 0;
    s->clock += wall_ms;
    if (s->hold > 0) s->hold -= wall_ms;
    if (s->since_rise >= 0) { s->since_rise += wall_ms; if (s->since_rise > ACAP_HELD_MS) { s->since_rise = -1; s->rise_need = ACAP_RISE_MS; } }
    if (s->recover > 0) { s->recover -= wall_ms; skip = 1; }
    if (work_ms >= 100.0) { s->recover = ACAP_RECOVER_MS; skip = 1; }
    if (!skip && work_ms > 0) {
        s->work[s->head] = work_ms; s->wall[s->head] = wall_ms > work_ms ? wall_ms : work_ms;
        s->head = (s->head + 1) % ACAP_N; if (s->n < ACAP_N) s->n++;
    }
    s->since_eval += wall_ms;
    if (s->since_eval < ACAP_EVAL_MS) return s->k;
    double step = s->since_eval; s->since_eval = 0;
    /* the newest samples covering ACAP_WIN_MS */
    double v[ACAP_N], t = 0; unsigned m = 0;
    for (unsigned i = 0; i < s->n && t < ACAP_WIN_MS; i++) { unsigned j = (s->head + ACAP_N - 1 - i) % ACAP_N; v[m++] = s->work[j]; t += s->wall[j]; }
    if (s->hold > 0 || t < ACAP_HOLD_MS * 0.9 || m < 20) { s->good = 0; return s->k; }
    qsort(v, m, sizeof *v, acap_cmp);
    s->p90 = v[m * 9 / 10]; s->p95 = v[m * 95 / 100];
    if (s->p90 > acap_budget(s, s->k) && s->k < s->kmax) {
        int k = s->k + 1; while (k < s->kmax && s->p95 > ACAP_DROP_FIT * acap_budget(s, k)) k++;
        if (s->since_rise >= 0 && s->since_rise < ACAP_FAIL_MS) { s->rise_need *= 2; if (s->rise_need > ACAP_RISE_MAX_MS) s->rise_need = ACAP_RISE_MAX_MS;
            s->fail_p95[s->rise_from] = s->rise_p95; s->fail_at[s->rise_from] = s->clock; }
        s->since_rise = -1;
        s->k = k; s->changed = -1; acap_clear_window(s); return s->k;
    }
    int calm = s->p95 < ACAP_RISE_FRAC * acap_budget(s, s->k - 1), steady = s->p90 <= acap_budget(s, s->k);
    int failed = s->fail_at[s->k] > 0 && s->clock - s->fail_at[s->k] < ACAP_RISE_MAX_MS && s->p95 >= 0.9 * s->fail_p95[s->k];
    if (s->k > s->kmin && (calm || steady) && !failed) {
        s->good += step;
        if (s->good >= (calm ? s->rise_need : s->rise_need > ACAP_PROBE_MS ? s->rise_need : ACAP_PROBE_MS)) {
            s->rise_p95 = s->p95; s->rise_from = s->k; s->rise_try = !calm; s->k--; s->changed = 1; s->since_rise = 0; acap_clear_window(s); }
    } else s->good = 0;
    return s->k;
}

/* Variable refresh (totk_vrr.h: full screen on a ProMotion / Adaptive-Sync display). The display shows each frame when
   it is ready, so the steps above (whole refreshes per frame) are not needed: Auto aims at a continuous rate instead,
   the rate the window's 95th percentile frame time fits with 5 % to spare, in 5 fps steps, between the display's
   slowest (1 / maximumRefreshInterval, at least 30) and fastest rate (1 / minimumRefreshInterval, at most the
   uncapped ceiling). Quantised to the display's interval steps only when NSScreen.displayUpdateGranularity > 0
   (0 = any interval). The same window, evaluation interval and stall rules as above; down at once when more than
   1 frame in 10 misses the target (to the rate the p95 fits, at least one step); up after 3 s in which the p95 fits a
   rate at least one step faster (a missed VRR target costs a few ms, not a whole refresh, so it may rise sooner). A
   rise that fails within 5 s doubles the wait (up to about 5 minutes) and that rate is not tried again for 5 minutes
   unless the p95 falls 10 % below the one that led to it; a rise that holds for 30 s resets the wait. */
#define AVRR_STEP 5
#define AVRR_FIT 0.95
#define AVRR_RISE_MS 3000.0
#define AVRR_MIN_FPS 30

typedef struct {
    int lo, hi, fps;                 /* the range and the target now (fps) */
    double min_s, gran_s;            /* the display's shortest interval and its granularity (s; 0 = any) */
    double work[ACAP_N], wall[ACAP_N]; unsigned n, head;
    double since_eval, good, hold, recover, clock, p90, p95;
    double rise_need, since_rise, rise_p95, fail_p95, fail_at; int fail_fps;
    int changed;                     /* +1 rose / -1 dropped at the last call, 0 otherwise */
} avrr_t;

/* The highest rate at or below fps the display can show exactly (its interval a whole number of granularity steps above
   its shortest), clamped to the range. */
static inline int avrr_quant(const avrr_t *s, int fps) {
    if (fps > s->hi) fps = s->hi;
    if (fps < s->lo) fps = s->lo;
    if (s->gran_s > 0 && s->min_s > 0) {
        double steps = (1.0 / fps - s->min_s) / s->gran_s; long n = (long)steps; if (steps - n > 1e-6) n++; if (n < 0) n = 0;
        int q = (int)(1.0 / (s->min_s + n * s->gran_s) + 1e-6);
        fps = q < s->lo ? s->lo : q;
    }
    return fps;
}
/* The rate a window with this p95 holds: AVRR_FIT of its budget, rounded down to AVRR_STEP. */
static inline int avrr_fit(const avrr_t *s, double p95) {
    int f = p95 > 0 ? (int)(AVRR_FIT * 1000.0 / p95) : s->hi;
    if (f < s->hi) f -= f % AVRR_STEP;
    return avrr_quant(s, f);
}
/* min_s / max_s / gran_s: NSScreen's minimumRefreshInterval, maximumRefreshInterval, displayUpdateGranularity;
   ceiling: the fastest rate allowed (the uncapped clock's, 0 = none). Starts at the fastest. */
static inline void avrr_reset(avrr_t *s, double min_s, double max_s, double gran_s, int ceiling) {
    memset(s, 0, sizeof *s);
    s->min_s = min_s; s->gran_s = gran_s;
    s->hi = min_s > 0 ? (int)(1.0 / min_s + 0.5) : 60;
    if (ceiling > 0 && s->hi > ceiling) s->hi = ceiling;
    if (s->hi > ACAP_MAX_FPS) s->hi = ACAP_MAX_FPS;
    s->lo = max_s > 0 ? (int)(1.0 / max_s + 0.999) : AVRR_MIN_FPS;
    if (s->lo < AVRR_MIN_FPS) s->lo = AVRR_MIN_FPS;
    if (s->lo > s->hi) s->lo = s->hi;
    s->fps = avrr_quant(s, s->hi); s->rise_need = AVRR_RISE_MS; s->since_rise = -1;
}
static inline void avrr_clear_window(avrr_t *s) { s->n = 0; s->head = 0; s->since_eval = 0; s->good = 0; s->hold = ACAP_HOLD_MS; }

/* One game frame, as acap_frame. Returns the target fps. */
static inline int avrr_frame(avrr_t *s, double work_ms, double wall_ms, int skip) {
    s->changed = 0;
    if (wall_ms < 0) wall_ms = 0;
    s->clock += wall_ms;
    if (s->hold > 0) s->hold -= wall_ms;
    if (s->since_rise >= 0) { s->since_rise += wall_ms; if (s->since_rise > ACAP_HELD_MS) { s->since_rise = -1; s->rise_need = AVRR_RISE_MS; } }
    if (s->recover > 0) { s->recover -= wall_ms; skip = 1; }
    if (work_ms >= 100.0) { s->recover = ACAP_RECOVER_MS; skip = 1; }
    if (!skip && work_ms > 0) {
        s->work[s->head] = work_ms; s->wall[s->head] = wall_ms > work_ms ? wall_ms : work_ms;
        s->head = (s->head + 1) % ACAP_N; if (s->n < ACAP_N) s->n++;
    }
    s->since_eval += wall_ms;
    if (s->since_eval < ACAP_EVAL_MS) return s->fps;
    double step = s->since_eval; s->since_eval = 0;
    double v[ACAP_N], t = 0; unsigned m = 0;
    for (unsigned i = 0; i < s->n && t < ACAP_WIN_MS; i++) { unsigned j = (s->head + ACAP_N - 1 - i) % ACAP_N; v[m++] = s->work[j]; t += s->wall[j]; }
    if (s->hold > 0 || t < ACAP_HOLD_MS * 0.9 || m < 20) { s->good = 0; return s->fps; }
    qsort(v, m, sizeof *v, acap_cmp);
    s->p90 = v[m * 9 / 10]; s->p95 = v[m * 95 / 100];
    if (s->p90 > 1000.0 / s->fps && s->fps > s->lo) {
        int f = avrr_fit(s, s->p95), below = avrr_quant(s, s->fps - AVRR_STEP);
        if (f > below) f = below;
        if (s->since_rise >= 0 && s->since_rise < ACAP_FAIL_MS) { s->rise_need *= 2; if (s->rise_need > ACAP_RISE_MAX_MS) s->rise_need = ACAP_RISE_MAX_MS;
            s->fail_fps = s->fps; s->fail_p95 = s->rise_p95; s->fail_at = s->clock; }
        s->since_rise = -1;
        s->fps = f; s->changed = -1; avrr_clear_window(s); return s->fps;
    }
    int cand = avrr_fit(s, s->p95);
    if (s->fail_at > 0 && s->clock - s->fail_at < ACAP_RISE_MAX_MS && s->p95 >= 0.9 * s->fail_p95 && cand >= s->fail_fps)
        cand = avrr_quant(s, s->fail_fps - AVRR_STEP);   /* below the rate that failed */
    if (cand >= s->fps + AVRR_STEP || (cand > s->fps && cand == s->hi)) {
        s->good += step;
        if (s->good >= s->rise_need) { s->rise_p95 = s->p95; s->since_rise = 0; s->fps = cand; s->changed = 1; avrr_clear_window(s); }
    } else s->good = 0;
    return s->fps;
}
