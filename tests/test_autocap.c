/* Test the automatic frame rate limit (games/totk/third_party/d2r_gfx/gfx/nvn/totk_autocap.h) with synthetic frame times:
   the rate it settles on, how fast it drops, how slowly it rises, and what it ignores. */
#include "../games/totk/third_party/d2r_gfx/gfx/nvn/totk_autocap.h"
#include <math.h>
#include <stdio.h>

static int failures;
static void expect(int ok, const char *what) { if (!ok) { fprintf(stderr, "FAIL: %s\n", what); failures++; } }

/* A frame paced the way the display link paces it: shown after at least k refreshes, or at the next refresh after its work. */
static double paced(const acap_t *s, double work) {
    double tick = 1000.0 / s->hz, t = ceil(work / tick - 1e-9) * tick;
    return t < s->k * tick ? s->k * tick : t;
}
static unsigned seed = 1;
static double noise(void) { seed = seed * 1103515245u + 12345u; return ((seed >> 16) & 0x7fff) / 32767.0; }   /* 0..1 */
/* Runs seconds of frames whose work is base..base+spread ms; returns the time (s) of the first change, or -1. */
static double run(acap_t *s, double seconds, double base, double spread, int skip) {
    double t = 0, first = -1;
    while (t < seconds * 1000) {
        double w = base + spread * noise(), wall = paced(s, w);
        acap_frame(s, w, wall, skip);
        t += wall;
        if (s->changed && first < 0) first = t / 1000;
    }
    return first;
}

int main(void) {
    acap_t s;
    acap_reset(&s, 120);
    expect(s.k == 1 && s.kmin == 1 && s.kmax == 4, "120 Hz: 120 / 60 / 40 / 30");
    acap_reset(&s, 60);  expect(s.kmin == 1 && s.kmax == 2, "60 Hz: 60 / 30");
    acap_reset(&s, 144); expect(s.kmin == 2 && s.kmax == 4, "144 Hz: 72 / 48 / 36 (above 120 the game would run fast)");
    acap_reset(&s, 100); expect(s.kmax == 3, "100 Hz: 100 / 50 / 33");

    /* The M3 Pro at 1080p: about 98 fps (work 9.3..11.6 ms) on 120 Hz: down to 60 within the first window, and it stays. */
    acap_reset(&s, 120);
    double at = run(&s, 3, 9.3, 2.3, 0);
    expect(s.k == 2 && at > 0 && at < 3, "98 fps on 120 Hz: 60 fps within 3 s");
    run(&s, 120, 9.3, 2.3, 0);
    expect(s.k == 2, "98 fps: stays at 60 for two minutes (p95 far above 85% of 8.3 ms)");

    /* Light enough for 120 (work 5..6.5 ms): stays. */
    acap_reset(&s, 120); run(&s, 60, 5, 1.5, 0); expect(s.k == 1, "6 ms frames keep 120");

    /* Rise only after 10 s of calm: from 60 with 5..6.5 ms frames. */
    acap_reset(&s, 120); s.k = 2; acap_clear_window(&s);
    at = run(&s, 30, 5, 1.5, 0);
    expect(s.k == 1 && at >= 10 && at < 14, "back to 120 after 10-14 s of calm, not before");

    /* Heavy scenes: 20 ms -> 40 fps (25 ms budget, 23 ms with margin); 30 ms -> 30 fps. */
    acap_reset(&s, 120); run(&s, 5, 19, 2, 0); expect(s.k == 3, "19-21 ms frames: 40 fps");
    acap_reset(&s, 120); run(&s, 5, 28, 3, 0); expect(s.k == 4, "28-31 ms frames: 30 fps");
    acap_reset(&s, 120); run(&s, 5, 45, 5, 0); expect(s.k == 4, "nothing slower than 30 fps");

    /* A sudden heavy scene at 120: down within about one evaluation step (0.5 s) plus the frames to reach p90. */
    acap_reset(&s, 120); run(&s, 20, 5, 1, 0);
    at = run(&s, 5, 12, 1, 0);
    expect(s.k == 2 && at > 0 && at < 1.2, "a heavy scene drops 120 -> 60 within 1.2 s");

    /* What it ignores: shader-compile stalls above 100 ms, sparse long frames (under 1 in 10), skipped frames. */
    acap_reset(&s, 120);
    for (double t = 0; t < 60000; ) {
        static int i; double w = (++i % 240 == 0) ? 350 : (i % 25 == 0 ? 11 : 5.5 + noise());
        double wall = paced(&s, w); acap_frame(&s, w, wall, 0); t += wall;
    }
    expect(s.k == 1, "120 kept through 350 ms stalls every 2 s and 4% frames at 11 ms");
    /* A 300 ms screenshot followed by 20 frames at twice the GPU time (clocked down) at 60 fps: no drop. */
    acap_reset(&s, 120); s.k = 2; acap_clear_window(&s); run(&s, 5, 9, 2, 0);
    for (int rep = 0; rep < 4; rep++) {
        acap_frame(&s, 300, 300, 0);
        for (int i = 0; i < 20; i++) acap_frame(&s, 24, 25, 0);
        run(&s, 8, 9, 2, 0);
    }
    expect(s.k == 2, "the frames after a long stall do not drop 60 to 30");
    acap_reset(&s, 120); s.k = 2; acap_clear_window(&s);
    run(&s, 30, 2, 0.5, 1);
    expect(s.k == 2, "skipped (loading / title) frames never raise the rate");
    acap_reset(&s, 120); run(&s, 30, 20, 2, 1);
    expect(s.k == 1, "skipped frames never lower it either");

    /* Hysteresis: a scene hovering around the 120 budget (7..9 ms) settles at 60 and does not flap back up. */
    acap_reset(&s, 120); int changes = 0;
    for (double t = 0; t < 120000; ) { double w = 7 + 2 * noise(), wall = paced(&s, w); acap_frame(&s, w, wall, 0); t += wall; changes += s.changed != 0; }
    expect(s.k == 2 && changes <= 3, "7-9 ms frames: settle at 60, at most one try back up in two minutes");

    /* A scene that reads calm when paced at 60 (6-7 ms: the GPU overlaps our wait) but costs 11-12 ms back to back:
       a failed rise is remembered for 5 minutes, so ten minutes see at most 3 tries. */
    acap_reset(&s, 120); s.k = 2; acap_clear_window(&s); int rises = 0;
    for (double t = 0; t < 600000; ) { double w = s.k == 2 ? 6 + noise() : 11 + noise(), wall = paced(&s, w); acap_frame(&s, w, wall, 0); t += wall; rises += s.changed > 0; }
    expect(rises >= 1 && rises <= 3 && s.k == 2 && s.rise_need > ACAP_RISE_MS, "failed rises back off and are remembered");
    /* The clock-down trap: at 30 fps the GPU idles and clocks down, so frames read 24-26 ms although the scene holds 60
       (9-10 ms) once the GPU is busy again. Tries while 30 holds climb back to 60 within a minute. */
    acap_reset(&s, 120); s.k = 4; acap_clear_window(&s);
    { double t = 0, at60 = -1;
      while (t < 120000) { double w = (s.k >= 4 ? 24 : s.k == 3 ? 14 : 9) + noise(), wall = paced(&s, w); acap_frame(&s, w, wall, 0); t += wall; if (s.k == 2 && at60 < 0) at60 = t; }
      expect(s.k == 2 && at60 > 0 && at60 < 60000, "out of the clock-down trap: 30 -> 40 -> 60 within a minute"); }
    /* A rise that holds resets the back-off. */
    { double before = s.rise_need; s.k = 2; acap_clear_window(&s); run(&s, 400, 5, 1, 0);
      expect(s.k == 1 && before > ACAP_RISE_MS && s.rise_need == ACAP_RISE_MS, "a rise that holds resets the calm needed to 10 s"); }

    /* 60 Hz display: 60 / 30. */
    acap_reset(&s, 60); run(&s, 5, 18, 3, 0); expect(s.k == 2, "18-21 ms on 60 Hz: 30 fps");
    acap_reset(&s, 60); run(&s, 30, 10, 3, 0); expect(s.k == 1, "10-13 ms on 60 Hz: 60 fps");

    if (failures) { fprintf(stderr, "autocap: %d failure(s)\n", failures); return 1; }
    printf("autocap: all checks passed\n");
    return 0;
}
