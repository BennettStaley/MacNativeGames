/* Ultrawide pictures on the Mac: key 4 switches between the game's 16:9 and a wider aspect ratio (TOTK_ASPECT, default
   21:9; also 32:9, 3440x1440 or 2.39). TOTK_ASPECT set also starts the game with it on. Not on iPhone / iPad.
   The game keeps drawing into its 16:9 canvas, but with the camera's aspect ratio set to the wider one, so the picture
   holds the wider view squeezed horizontally; the present pass then stretches it over the window (nvn_mtl.m
   present_stretch asks runtime_aspect_now), which restores the proportions on a screen of that shape.
   The community's TOTK-Ultrawide mod for emulators (GPL; none of its code is used here) changes the camera parameter
   DefaultAspect (1.777778 in main's read-only data) and a viewport routine's width / height division. DefaultAspect is
   only copied when a camera is made, so it cannot follow a key; instead each place that hands an aspect ratio to the
   perspective projection (sead's +0xb0, found with a watchpoint on the copies) is patched where the value is produced:
   the instruction branches to a stub in main's text padding that runs it and, while a ratio sits on the data page after
   main (+56, 0 = off), puts that instead. The stub keeps its scratch register on the stack, so no liveness is assumed.
   Not adjusted yet: the HUD, drawn into the same 16:9 canvas, is stretched with the picture; HUD markers that follow
   objects in the world and pixel-snapped positions (5 more routines TOTK-Ultrawide widens in 1.2.1) sit off. */
#pragma once
#include "runtime.h"
#include <TargetConditionals.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASPECT_DATA_SLOT 56   /* the ratio's float on the data page after main; 0.0 = the game's own */

static _Atomic(uint32_t) *g_aspect_slot;   /* points into that page once the stubs are in */
static float g_aspect_wide;               /* the ratio key 4 switches to */

/* TOTK_ASPECT as a ratio; 0 when unset or "0". A malformed value stops the start. */
static inline float aspect_requested(void) {
    const char *e = getenv("TOTK_ASPECT");
    if (!e || !*e || !strcmp(e, "0")) return 0;
    double a = 0, w, h; char rest;
    if ((sscanf(e, "%lf:%lf%c", &w, &h, &rest) == 2 || sscanf(e, "%lfx%lf%c", &w, &h, &rest) == 2) && w > 0 && h > 0) a = w / h;
    else if (sscanf(e, "%lf%c", &w, &rest) == 1) a = w;
    if (!(a >= 0.5 && a <= 8)) runtime_fail("TOTK_ASPECT must be <w>:<h>, <w>x<h> or a ratio between 0.5 and 8");
    return (float)a;
}

/* The ratio in force (0 = the game's own 16:9): nvn_mtl.m stretches the picture over the window while it is set. */
float runtime_aspect_now(void) {
    if (!g_aspect_slot) return 0;
    uint32_t bits = atomic_load(g_aspect_slot); float v; memcpy(&v, &bits, 4); return v;
}

/* The settings menu's aspect ratio (settings.c display_apply): ratio 0 = the game's own 16:9. Before the stubs are in it
   is the start ratio. Ignored where the patch is not (iPhone / iPad, other game versions). */
static float g_aspect_pending = -1;
void runtime_aspect_set(float ratio) {
    if (!g_aspect_slot) { g_aspect_pending = ratio; return; }
    uint32_t bits; memcpy(&bits, &ratio, 4); atomic_store(g_aspect_slot, bits);
    if (ratio) g_aspect_wide = ratio;
    fprintf(stderr, "[totk] aspect ratio %s%.4f (settings)\n", ratio ? "" : "16:9, the game's own: ", ratio ? ratio : 16.0f / 9);
}

/* Key 4 (runtime/input.c). Returns 0 where it is not available (iPhone / iPad, other game versions). */
int runtime_aspect_toggle(void) {
    if (!g_aspect_slot) return 0;
    float next = runtime_aspect_now() ? 0 : g_aspect_wide; uint32_t bits; memcpy(&bits, &next, 4);
    atomic_store(g_aspect_slot, bits);
    fprintf(stderr, "[totk] key 4: aspect ratio %s\n", next ? "wide" : "16:9 (the game's own)");
    if (next) fprintf(stderr, "[totk] aspect ratio %.4f\n", next);
    return 1;
}

static inline uint32_t aspect_branch(uint64_t from, uint64_t to) {
    int64_t d = (int64_t)to - (int64_t)from;
    if ((d & 3) || d < -(1 << 27) || d >= (1 << 27)) runtime_fail("aspect patch: branch out of range");
    return 0x14000000u | ((uint32_t)(d >> 2) & 0x3ffffff);
}

/* A data-page float stub: one instruction that leaves a float in s<d> (fdiv s<d>, ... or ldr s<d>, [x<n>, #imm], both
   position independent) branches to a stub in main's text padding that runs it, loads the float at data page +slot and,
   when it is non-zero, puts it in s<d> (DPF_REPLACE, 32 bytes: the aspect ratio) or multiplies s<d> by it (DPF_MULTIPLY,
   48 bytes: the field of view, so the game's own changes such as aiming still come through). x10 and, to multiply, d16
   are kept on the stack, so no liveness is assumed. dpf_stub only builds the words: the phone compares its embedded code. */
enum { DPF_REPLACE = 32, DPF_MULTIPLY = 48 };   /* = the stub's size in bytes */
static inline void dpf_stub(uint32_t *stub, uint32_t ins, uint64_t site, uint64_t s, uint64_t data_off, unsigned slot, int kind) {
    int64_t pages = (int64_t)(data_off >> 12) - (int64_t)((s + 8) >> 12);   /* the module base is page aligned */
    if (pages < -(1 << 20) || pages >= (1 << 20)) runtime_fail("data-page stub: data page out of ADRP range");
    uint32_t imm = (uint32_t)pages & 0x1fffff, d = ins & 31; unsigned n = 0;
    stub[n++] = ins;                                                          /* the game's own value into s<d> */
    stub[n++] = 0xf81f0fea;                                                   /* str x10, [sp, #-16]! */
    stub[n++] = 0x90000000u | ((imm & 3) << 29) | ((imm >> 2) << 5) | 10;     /* adrp x10, <data page> */
    stub[n++] = 0xb9400000u | ((slot / 4) << 10) | (10 << 5) | 10;            /* ldr w10, [x10, #slot] */
    if (kind == DPF_REPLACE) {
        stub[n++] = 0x3400004au;                                              /* cbz w10, +8: 0 = the game's own */
        stub[n++] = 0x1e270000u | (10 << 5) | d;                              /* fmov s<d>, w10 */
    } else {
        stub[n++] = 0x340000aau;                                              /* cbz w10, +20: 0 = the game's own */
        stub[n++] = 0xfd0007f0;                                               /* str d16, [sp, #8] */
        stub[n++] = 0x1e270150;                                               /* fmov s16, w10 */
        stub[n++] = 0x1e300800u | (d << 5) | d;                               /* fmul s<d>, s<d>, s16 */
        stub[n++] = 0xfd4007f0;                                               /* ldr d16, [sp, #8] */
    }
    stub[n++] = 0xf84107ea;                                                   /* ldr x10, [sp], #16 */
    stub[n] = aspect_branch(s + 4 * n, site + 4); n++;                        /* b <after the instruction> */
    while (n < (unsigned)kind / 4) stub[n++] = 0xd503201f;                    /* nop, never reached */
}
static inline void dpf_fail(const char *what, const char *why) { char msg[160]; snprintf(msg, sizeof msg, "%s patch: %s", what, why); runtime_fail(msg); }
/* Mac and the iOS export: checks each site (its word in want[], or without want any stub-able float instruction) and that
   the padding from stub_off is empty and inside main's last text page, then writes the stubs and the branches. */
static inline void dpf_install(Module *m, const char *what, const uint64_t *sites, const uint32_t *want, unsigned nsite, uint64_t stub_off,
                               uint64_t data_off, unsigned slot, int kind) {
    uint64_t page_end = (m->text_end + 0x3fff) & ~0x3fffULL;
    if (stub_off < m->text_end || stub_off + (uint64_t)kind * nsite > page_end) dpf_fail(what, "no room in main's text padding");
    if (data_off + slot + 4 > m->size) dpf_fail(what, "no data page after main");
    for (uint64_t i = 0; i < (uint64_t)kind * nsite; i++) if (m->base[stub_off + i]) dpf_fail(what, "text padding in use");
    for (unsigned i = 0; i < nsite; i++) {
        uint32_t ins; memcpy(&ins, m->base + sites[i], 4);
        if (want ? ins != want[i] : (ins & 0xffe0fc00) != 0x1e201800 && (ins & 0xffc00000) != 0xbd400000) {
            fprintf(stderr, "[totk] %s patch main+%#llx: found %08x\n", what, (unsigned long long)sites[i], ins);
            dpf_fail(what, "the camera code does not match this game version");
        }
    }
    for (unsigned i = 0; i < nsite; i++) {
        uint32_t ins, stub[DPF_MULTIPLY / 4]; uint64_t s = stub_off + (uint64_t)kind * i;
        memcpy(&ins, m->base + sites[i], 4);
        dpf_stub(stub, ins, sites[i], s, data_off, slot, kind);
        memcpy(m->base + s, stub, (size_t)kind);
        uint32_t jump = aspect_branch(sites[i], s);
        memcpy(m->base + sites[i], &jump, 4);
    }
}
/* iOS: 1 when the embedded code carries exactly these stubs, 0 when every site still holds its original word (built
   without them); anything else stops the start. */
static inline int dpf_embedded(Module *m, const char *what, const uint64_t *sites, const uint32_t *want, unsigned nsite, uint64_t stub_off,
                               uint64_t data_off, unsigned slot, int kind) {
    unsigned orig = 0, ok = 0;
    for (unsigned i = 0; i < nsite; i++) {
        uint32_t got, stub[DPF_MULTIPLY / 4]; uint64_t s = stub_off + (uint64_t)kind * i;
        memcpy(&got, m->base + sites[i], 4);
        if (got == want[i]) { orig++; continue; }
        dpf_stub(stub, want[i], sites[i], s, data_off, slot, kind);
        if (got == aspect_branch(sites[i], s) && !memcmp(m->base + s, stub, (size_t)kind)) ok++;
    }
    if (orig == nsite) return 0;
    if (ok != nsite) dpf_fail(what, "embedded code mismatch");
    return 1;
}

/* sites: instructions that leave the ratio in an s register (fdiv s<d>, ... or ldr s<d>, [x<n>, #imm]). Stubs go to
   main's text padding from stub_off, checked to be zero and inside the last text page; data_off is the page the loader
   reserves after main. Mac only. */
static inline void aspect_install(Module *m, const char *version, const uint64_t *sites, unsigned nsite, uint64_t stub_off, uint64_t data_off) {
#if TARGET_OS_IPHONE
    (void)m; (void)version; (void)sites; (void)nsite; (void)stub_off; (void)data_off;
#else
    float start = aspect_requested();
    if (!start && g_aspect_pending > 0) start = g_aspect_pending;   /* the settings' start ratio; TOTK_ASPECT wins */
    g_aspect_wide = start ? start : 21.0f / 9.0f;
    dpf_install(m, "aspect", sites, NULL, nsite, stub_off, data_off, ASPECT_DATA_SLOT, DPF_REPLACE);
    g_aspect_slot = (_Atomic(uint32_t) *)(void *)(m->base + data_off + ASPECT_DATA_SLOT);
    uint32_t bits = 0; if (start) memcpy(&bits, &start, 4);
    atomic_store(g_aspect_slot, bits);
    fprintf(stderr, "[totk] %s ultrawide: key 4 switches 16:9 / %.4f (TOTK_ASPECT), starts %s; %u camera site(s), stubs +%#llx\n",
            version, g_aspect_wide, start ? "wide" : "at 16:9", nsite, (unsigned long long)stub_off);
#endif
}

/* Field of view (1.4.3; Mac and iPhone / iPad): TOTK_FOV=<percent> 70..140 scales the main camera's vertical angle from
   the start (100 or unset = the game's own), runtime_fov_set changes it live (the settings menu). The camera hands its
   fovy to sead's PerspectiveProjection (+0xa0 fovy, then sin / cos / tan of half of it at +0xa4 / +0xa8 / +0xac, the
   aspect ratio at +0xb0); each site is the load of that angle in one of the two functions that write the whole
   projection (the ones whose aspect ratio aspect_install replaces). A DPF_MULTIPLY stub scales it there, before the half
   angle and its sin / cos / tan are derived, so the projection stays consistent and the game's own changes (bow aim,
   first person, cutscene cameras) keep coming through, scaled. The multiplier is a float on the data page after main
   (+64; 0 = the game's own). iOS cannot write code at run time: the iOS export (TOTK_EXPORT_IOS) always puts the stubs
   into the phone's code; the phone checks them and only writes the multiplier (the data page is read-write there). */
#define FOV_DATA_SLOT 64
static _Atomic(uint32_t) *g_fov_slot;   /* points into the data page once the stubs are in (or found embedded) */
static inline int fov_clamp(int percent) { return percent < 70 ? 70 : percent > 140 ? 140 : percent; }
static inline uint32_t fov_bits(int percent) {
    float v = fov_clamp(percent) == 100 ? 0.0f : fov_clamp(percent) / 100.0f; uint32_t bits; memcpy(&bits, &v, 4); return bits;
}
/* Live: percent 70..140 (clamped), 100 = the game's own. Ignored where the patch is not in (other game versions). */
void runtime_fov_set(int percent) {
    if (!g_fov_slot) { fprintf(stderr, "[totk] field of view %d%%: not available in this build\n", percent); return; }
    atomic_store(g_fov_slot, fov_bits(percent));
    fprintf(stderr, "[totk] field of view %d%%\n", fov_clamp(percent));
}
/* TOTK_FOV as a percentage; 100 when unset. A malformed or out-of-range value stops the start. */
static inline int fov_requested(void) {
    const char *e = getenv("TOTK_FOV"); char rest; int p;
    if (!e || !*e) return 100;
    if (sscanf(e, "%d%c", &p, &rest) != 1 || p < 70 || p > 140) runtime_fail("TOTK_FOV must be a percentage between 70 and 140");
    return p;
}
/* want[]: each site's original word, checked before anything is written (Mac, the iOS export) or against the embedded
   code (iOS, where code built without the stubs leaves the game's own field of view). */
static inline void fov_install(Module *m, const char *version, const uint64_t *sites, const uint32_t *want, unsigned nsite, uint64_t stub_off, uint64_t data_off) {
    int start = fov_requested();
#if TARGET_OS_IPHONE
    if (!dpf_embedded(m, "field of view", sites, want, nsite, stub_off, data_off, FOV_DATA_SLOT, DPF_MULTIPLY)) {
        fprintf(stderr, "[totk] %s field of view: the game's own (not patched at build)\n", version); return;
    }
#else
    dpf_install(m, "field of view", sites, want, nsite, stub_off, data_off, FOV_DATA_SLOT, DPF_MULTIPLY);
#endif
    g_fov_slot = (_Atomic(uint32_t) *)(void *)(m->base + data_off + FOV_DATA_SLOT);
    atomic_store(g_fov_slot, fov_bits(start));
    fprintf(stderr, "[totk] %s field of view: %d%% (TOTK_FOV), live via runtime_fov_set; %u camera site(s), stubs +%#llx\n",
            version, start, nsite, (unsigned long long)stub_off);
}
