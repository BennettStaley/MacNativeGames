/* Included only by nvn_mtl.m, just before draw_inner (needs S, the arenas, the render stream and the pass state).
   TOTK_PAR_ENCODE=<threads> (default 0 = off), step 1 (2026-10-23): encoding the G-buffer pass (~2040 draws) on several
   threads with Metal 4 suspend / resume. Design: docs/performance-followups.md, "Parallel encoding of one render pass".

   What this step does, with the switch on:
   - Measures, on the replay thread, every render pass large enough to be split (>= 2 chunks of TOTK_PAR_ENCODE_MIN
     draws, default 128): the replay thread's wall time inside it, its draws, the chunks it would get, and why it would
     have to stay on the serial path (fallback reasons below, checked at every draw).
   - Takes the draw-state snapshot (a copy of S, the only state the per-draw path reads besides the caches) at the
     planned chunk borders, so its cost is measured where the parallel path will take it.
   - Holds the infrastructure the next step plugs in, unused while no pass is encoded in parallel: per-worker command
     allocator rings with their fences, per-worker arena contexts (argument blocks / uniform copies), a persistent worker
     pool, and the command-buffer chain that q4_commit commits together with the replay's own command buffer in one
     commit:count: (suspend / resume needs one commit; tests/par_encode/probe.m checks the layout on this machine).
   The pass itself is still encoded serially (render stream, thread B). Log every 600 frames: `[par] ...`. */
#include <pthread.h>
#include <stdatomic.h>

enum { PAR_MAX = 8, PAR_ALLOC_RING = 4, PAR_PASS_SLOTS = 256, PAR_CHAIN_MAX = PAR_MAX + 1 };
enum {   /* why a candidate pass would have to stay on the serial path (bits, counted per pass) */
    PAR_FB_NOT_STREAMED = 1 << 0,   /* not on the encode stage: diagnostics / TBDR / MetalFX encode on the replay thread */
    PAR_FB_UPD = 1 << 1,            /* renamed uniform updates (g_nupd) live in the pass: upd_guard_draw is order dependent */
    PAR_FB_LAZY_CLEAR = 1 << 2,     /* TOTK_LAZY_CLEAR clears waiting / taken in the pass */
    PAR_FB_CUBE_GROUP = 1 << 3,     /* cube-face-set grouping pending */
    PAR_FB_FROZEN = 1 << 4,         /* frozen / fused / hoisted / deferred pass machinery active */
    PAR_FB_LAYERED = 1 << 5,        /* layered pass (draws re-issued per layer) */
    PAR_FB_VISIBILITY = 1 << 6,     /* occlusion counting inside the pass (visibility offsets are per draw, order matters) */
    PAR_FB_MERGED = 1 << 7,         /* same-target continuation: the pass spans several NVN render-target binds */
    PAR_FB_COUNT_ = 8,
};
static const char *const par_fb_names[PAR_FB_COUNT_] = {"not-streamed", "uniform-updates", "lazy-clear", "cube-group", "frozen/hoisted/deferred", "layered", "visibility", "merged-targets"};

static int par_threads(void) {
    static int v = -1;
    if (v < 0) { const char *e = getenv("TOTK_PAR_ENCODE"); v = e ? atoi(e) : 0; if (v < 0) v = 0; if (v > PAR_MAX) v = PAR_MAX; if (v == 1) v = 0;
        if (v) MLOG("[par] TOTK_PAR_ENCODE=%d (step 1: measurement and infrastructure; passes are still encoded serially)", v); }
    return v;
}
static int par_min_draws(void) {
    static int v = -1;
    if (v < 0) { const char *e = getenv("TOTK_PAR_ENCODE_MIN"); v = e ? atoi(e) : 128; if (v < 16) v = 16; }
    return v;
}

/* ---- Draw-state snapshot: S at a chunk's first draw ---- */
typedef __typeof__(S) par_state_t;
typedef struct { par_state_t s; uint64_t draw; } par_snap_t;
static par_snap_t *g_par_snap;   /* PAR_MAX entries, allocated on first use */
static void par_snapshot_take(int chunk, uint64_t draw) {
    if (!g_par_snap) { g_par_snap = calloc(PAR_MAX, sizeof *g_par_snap); if (!g_par_snap) MDIE("[par] snapshot allocation failed"); }
    memcpy(&g_par_snap[chunk].s, &S, sizeof S); g_par_snap[chunk].draw = draw;
}

/* ---- Measurement ---- */
static struct {
    int open, cand, chunks, next_chunk; unsigned fb;   /* current pass */
    uint64_t t0, draw0, every, ordinal, frame;
    uint32_t last_draws[PAR_PASS_SLOTS];         /* draws of pass #i of the previous frame: plans the borders before the pass's draw count is known */
    uint64_t win_frames, win_passes, win_draws, win_ns, win_chunks, win_snap_ns, win_snaps, win_fb_passes, win_fb[PAR_FB_COUNT_], win_planned_misses;
    uint64_t log_frame;
} g_par;
static void par_log(uint64_t frame) {
    if (!g_par.win_frames) return;
    double f = (double)g_par.win_frames;
    char fb[400]; int n = 0; fb[0] = 0;
    for (int i = 0; i < PAR_FB_COUNT_; i++) if (g_par.win_fb[i]) n += snprintf(fb + n, sizeof fb - (size_t)n, "%s%s %.2f", n ? ", " : "", par_fb_names[i], (double)g_par.win_fb[i] / f);
    double serial_ms = (double)g_par.win_ns / 1e6 / f;
    double ideal_ms = g_par.win_passes ? serial_ms * (double)g_par.win_passes / (double)g_par.win_chunks : 0;   /* perfect split, no overhead */
    MLOG("[par] to frame %llu (TOTK_PAR_ENCODE=%d, min %d draws a chunk): %.2f candidate passes a frame, %.0f draws, replay thread %.3f ms a frame inside them "
         "(%.2f us a draw), %.2f chunks a pass -> ideal %.3f ms; snapshots %.1f a frame, %.2f us each; passes that would fall back %.2f a frame%s%s%s",
         (unsigned long long)frame, par_threads(), par_min_draws(), (double)g_par.win_passes / f, (double)g_par.win_draws / f, serial_ms,
         g_par.win_draws ? (double)g_par.win_ns / 1e3 / (double)g_par.win_draws : 0.0, g_par.win_passes ? (double)g_par.win_chunks / (double)g_par.win_passes : 0.0, ideal_ms,
         (double)g_par.win_snaps / f, g_par.win_snaps ? (double)g_par.win_snap_ns / 1e3 / (double)g_par.win_snaps : 0.0, (double)g_par.win_fb_passes / f,
         n ? " (" : "", fb, n ? ")" : "");
    memset(&g_par.win_frames, 0, (size_t)((char *)&g_par.log_frame - (char *)&g_par.win_frames));   /* the window's counters, not log_frame */
}
/* Replay thread, right after a render pass's encoder is opened (rp_begin_ex). */
static void par_pass_begin(void) {
    int n = par_threads(); if (!n) return;
    uint64_t f = atomic_load(&g_cur_frame);
    if (f != g_par.frame) {   /* first pass of a new frame */
        if (g_par.frame) g_par.win_frames++;
        g_par.frame = f; g_par.ordinal = 0;
        if (f >= g_par.log_frame + 600) { if (g_par.log_frame) par_log(f); g_par.log_frame = f; }
    }
    uint64_t ord = g_par.ordinal++;
    uint32_t planned = ord < PAR_PASS_SLOTS ? g_par.last_draws[ord] : 0;
    int min = par_min_draws(), chunks = (int)(planned / (uint32_t)min); if (chunks > n) chunks = n;
    g_par.open = 1; g_par.cand = chunks >= 2; g_par.chunks = chunks; g_par.next_chunk = 0; g_par.fb = 0;
    g_par.draw0 = g_stat_draw; g_par.every = g_par.cand ? (planned + (uint32_t)chunks - 1) / (uint32_t)chunks : 0;
    g_par.t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    if (g_par.cand) {   /* state at the first draw is S as it stands now; the snapshot is taken at the draw itself (par_draw) */
        if (!g_rs_active) g_par.fb |= PAR_FB_NOT_STREAMED;
        if (g_lay.active) g_par.fb |= PAR_FB_LAYERED;
        if (g_fz.active || g_hoist_n || g_defer_pend) g_par.fb |= PAR_FB_FROZEN;
    }
}
/* Replay thread, at the top of every draw that reaches draw_inner while a pass is open. */
static inline void par_draw(void) {
    if (!g_par.cand) return;
    uint64_t i = g_stat_draw - g_par.draw0;
    if (g_nupd) g_par.fb |= PAR_FB_UPD;
    if (g_nlc || g_lc_held) g_par.fb |= PAR_FB_LAZY_CLEAR;
    if (g_cg_pending) g_par.fb |= PAR_FB_CUBE_GROUP;
    if (g_vis_cur >= 0) g_par.fb |= PAR_FB_VISIBILITY;
    if (g_rt_merged) g_par.fb |= PAR_FB_MERGED;
    if (g_par.next_chunk < g_par.chunks && i >= (uint64_t)g_par.next_chunk * g_par.every) {   /* first draw of the next chunk (skipped draws do not count) */
        uint64_t t = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        par_snapshot_take(g_par.next_chunk++, g_stat_draw);
        g_par.win_snap_ns += clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t; g_par.win_snaps++;
    }
}
/* Replay thread, when the pass's encoder ends (rp_end, before g_enc is cleared). */
static void par_pass_end(void) {
    if (!par_threads() || !g_par.open) return;
    g_par.open = 0;
    uint64_t draws = g_stat_draw - g_par.draw0, ord = g_par.ordinal ? g_par.ordinal - 1 : 0;
    if (ord < PAR_PASS_SLOTS) g_par.last_draws[ord] = draws > UINT32_MAX ? UINT32_MAX : (uint32_t)draws;
    if (!g_par.cand) return;
    g_par.cand = 0;
    if (draws < (uint64_t)par_min_draws() * 2) { g_par.win_planned_misses++; return; }   /* last frame's plan did not hold */
    g_par.win_passes++; g_par.win_draws += draws; g_par.win_chunks += (uint64_t)g_par.chunks;
    g_par.win_ns += clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - g_par.t0;
    if (g_par.fb) { g_par.win_fb_passes++; for (int b = 0; b < PAR_FB_COUNT_; b++) if (g_par.fb & (1u << b)) g_par.win_fb[b]++; }
}

/* ---- Per-worker command allocators (an MTL4CommandAllocator must not be encoded into from two threads) ---- */
typedef struct { id<MTL4CommandAllocator> a[PAR_ALLOC_RING]; uint64_t fence[PAR_ALLOC_RING]; int n, cur; } par_alloc_ring;
static par_alloc_ring g_par_alloc[PAR_MAX];
/* Worker w's command buffer for this submission: an allocator the GPU is done with, a new one, or a wait for the oldest. */
__attribute__((unused)) static id<MTL4CommandBuffer> par_worker_cb(int w) {
    par_alloc_ring *r = &g_par_alloc[w]; int pick = -1; uint64_t done = __atomic_load_n(&g_done, __ATOMIC_ACQUIRE);
    for (int i = 0; i < r->n; i++) if (r->fence[i] <= done) { pick = i; break; }
    if (pick < 0 && r->n < PAR_ALLOC_RING) { NSError *e = nil; r->a[r->n] = [g_dev newCommandAllocatorWithDescriptor:[MTL4CommandAllocatorDescriptor new] error:&e];
        if (!r->a[r->n]) MDIE("[par] command allocator for worker %d: %s", w, e.localizedDescription.UTF8String); pick = r->n++; }
    if (pick < 0) { pick = 0; for (int i = 1; i < r->n; i++) if (r->fence[i] < r->fence[pick]) pick = i; mtl_wait(r->fence[pick], UINT64_MAX); }
    r->cur = pick; r->fence[pick] = UINT64_MAX;   /* in use until the commit gives it the submission's fence */
    [r->a[pick] reset];
    id<MTL4CommandBuffer> cb = [g_dev newCommandBuffer]; [cb beginCommandBufferWithAllocator:r->a[pick]];
    return cb;
}

/* ---- Per-worker arena context: argument blocks / uniform copies without g_cur_arena ---- */
typedef struct { __unsafe_unretained NvArena *cur; NSMutableArray<NvArena *> *sub; } par_arena_ctx;
static par_arena_ctx g_par_arena[PAR_MAX];
__attribute__((unused)) static size_t par_arena_alloc(par_arena_ctx *c, size_t n, __strong id<MTLBuffer> *buf) {
    n = (n + 255) & ~(size_t)255;
    if (!c->cur || c->cur->used + n > (size_t)(4 << 20)) {
        if (!c->sub) c->sub = [NSMutableArray array];
        pthread_mutex_lock(&g_mu); NvArena *a = arena_new(); pthread_mutex_unlock(&g_mu);   /* arena_new mutates g_arenas: under g_mu as on the replay thread */
        [c->sub addObject:a]; c->cur = a;
    }
    size_t off = c->cur->used; c->cur->used += n; *buf = c->cur->buf; return off;
}
/* Replay thread after the workers joined: their arenas retire with this submission like the replay's own. */
__attribute__((unused)) static void par_arena_join(int nworkers) {
    for (int w = 0; w < nworkers; w++) { par_arena_ctx *c = &g_par_arena[w]; if (c->sub.count) [g_sub_arenas addObjectsFromArray:c->sub]; [c->sub removeAllObjects]; c->cur = nil; }
}

/* ---- Command-buffer chain: the suspended chunks committed with the replay's command buffer in one commit:count: ----
   Layout (tests/par_encode/probe.m, M3 Pro / macOS 26): [g_cb: work before the pass + chunk 0 Suspending]
   [worker 1..K-2: Resuming | Suspending] [worker K-1: Resuming only, nothing after it] [new g_cb: everything after the
   pass]. Encoding more work after the resuming pass in the last chunk's command buffer faulted the GPU in the probe
   (kIOGPUCommandBufferCallbackErrorInnocentVictim), so the replay continues in a fresh command buffer. */
static id<MTL4CommandBuffer> g_par_chain[PAR_CHAIN_MAX]; static int g_par_chain_n; static int g_par_chain_workers[PAR_CHAIN_MAX];
__attribute__((unused)) static void par_chain_add(id<MTL4CommandBuffer> cb, int worker) {
    if (g_par_chain_n >= PAR_CHAIN_MAX) MDIE("[par] command-buffer chain overflow");
    g_par_chain_workers[g_par_chain_n] = worker; g_par_chain[g_par_chain_n++] = cb;
}
/* q4_commit: commits the chain (already ended) followed by cb. Returns 0 when there is no chain, or when cb is not the
   replay's own command buffer (upload / present / readback command buffers commit alone, ahead of the chain). */
static int par_chain_commit(id<MTL4CommandBuffer> cb, TotkCommitOptions *o) {
    if (!g_par_chain_n || cb != g_cb) return 0;
    id<MTL4CommandBuffer> all[PAR_CHAIN_MAX + 1]; int n = 0;
    for (int i = 0; i < g_par_chain_n; i++) all[n++] = g_par_chain[i];
    all[n++] = cb;
    uint64_t fence = g_fence + 1;   /* the submission's fence: signalled after its last command buffer, so a safe bound for every chunk */
    for (int i = 0; i < g_par_chain_n; i++) { int w = g_par_chain_workers[i]; if (w >= 0) g_par_alloc[w].fence[g_par_alloc[w].cur] = fence; g_par_chain[i] = nil; }
    g_par_chain_n = 0;
    [g_q4 commit:all count:(NSUInteger)n options:o];
    return 1;
}

/* ---- Persistent worker pool: run fn(i, ctx) for i in [0, n), the caller as i = 0 ---- */
typedef void (*par_fn)(int, void *);
static struct { pthread_t th[PAR_MAX]; dispatch_semaphore_t go[PAR_MAX]; _Atomic int left; par_fn fn; void *ctx; int started; } g_par_pool;
static void *par_worker_main(void *arg) {
    int i = (int)(intptr_t)arg; char name[32]; snprintf(name, sizeof name, "TOTK encode worker %d", i); pthread_setname_np(name);
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    for (;;) {
        dispatch_semaphore_wait(g_par_pool.go[i], DISPATCH_TIME_FOREVER);
        @autoreleasepool { g_par_pool.fn(i, g_par_pool.ctx); }
        atomic_fetch_sub_explicit(&g_par_pool.left, 1, memory_order_acq_rel); __builtin_arm_sev();
    }
    return NULL;
}
__attribute__((unused)) static void par_pool_run(int n, par_fn fn, void *ctx) {
    if (n > PAR_MAX) n = PAR_MAX;
    if (g_par_pool.started < n) for (int i = g_par_pool.started ? g_par_pool.started : 1; i < n; i++) {
        g_par_pool.go[i] = dispatch_semaphore_create(0);
        pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 1 << 20);
        if (pthread_create(&g_par_pool.th[i], &at, par_worker_main, (void *)(intptr_t)i)) MDIE("[par] worker creation failed");
        pthread_detach(g_par_pool.th[i]); g_par_pool.started = i + 1;
    }
    g_par_pool.fn = fn; g_par_pool.ctx = ctx; atomic_store_explicit(&g_par_pool.left, n - 1, memory_order_release);
    for (int i = 1; i < n; i++) dispatch_semaphore_signal(g_par_pool.go[i]);
    fn(0, ctx);
    while (atomic_load_explicit(&g_par_pool.left, memory_order_acquire)) __builtin_arm_wfe();
}
