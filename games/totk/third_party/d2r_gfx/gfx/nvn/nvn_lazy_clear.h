/* TOTK: lazy clears (upstream PR #159 piece d, idea from nxnative f5646ef, MIT; rewritten for this tree). Included by
   nvn_mtl.m just before draw_inner.

   A SetRenderTargets that leaves full-target clears pending with no draw since used to open a render pass just to land
   them (rp_flush): the cleared attachments were stored, and the next pass that attached them loaded them back. With
   TOTK_LAZY_CLEAR=1 such clears wait instead (lc_push), and the next draw whose fresh pass attaches the same texture,
   level and layer through the same view takes them as its loadAction = Clear (lc_take). Default off.

   A waiting clear is a promise that nothing can see the target's memory before it lands, so it lands (lc_land: one
   clear-only pass per group, in record order, exactly as rp_flush did) before:
     - any replay op other than targets, clears, state / binding changes and uniform updates (lc_keeps): copies and
       blits to or from anything, texture clears, dispatches, counters, events, the end of a top-level command list;
       a uniform update whose destination overlaps a waiting target's memory;
     - any draw that does not take it: a draw sampling it (texture, image or typed-buffer slot, by serial or by
       overlapping memory, so aliasing views count), a draw into another texture / view / level / layer of it, a draw
       inside an open pass (not first load), and every waiting clear the taking draw does not take;
     - any render pass opened by anything but the taking draw (rp_begin_ex: scissored / masked clear quads, MetalFX,
       post-processing, cube-group replays, ...);
     - the end of the submission (mtl_sub_commit), so present, QueueFinish and CPU readback never see one.
   A clear is only deferred when its target is a plain single-sample 2D texture that owns its storage alone: exactly one
   serial holds the shared-storage entry (no format view, no second serial), and no other shared-storage entry, live or
   handed over, overlaps its memory (lc_alone). With TOTK_TEX_SHARE=0 nothing is deferred. A taking pass must have all
   its attachments the same size (loadAction = Clear covers the render area only, and Metal's render area is the
   smallest attachment), else everything lands first. A clear taken by a draw that then skips (e.g. empty scissor) stays
   in R as an ordinary pending clear, and is landed before the next op lc_keeps does not allow (g_lc_held). Off in the
   experimental DM_NVN_TBDR mode (it hoists clear-only passes its own way). Holds no memory: a group is a copy of the
   p_set_rt (texture pointers already held by R between ops) plus the clear values. */
typedef struct { p_set_rt rt; uint8_t clear_c[8]; MTLClearColor cc[8]; uint8_t clear_d, clear_s; double cd; uint32_t cs; } lc_group;
enum { LC_MAX = 8 };
static lc_group g_lc[LC_MAX];
static uint64_t g_lc_pushed, g_lc_taken, g_lc_landed;   // clears deferred / attachments a later pass took / clear-only passes still opened for them
static int lc_on(void) {
    static int v = -1;
    if (v < 0) { const char *e = getenv("TOTK_LAZY_CLEAR"); v = e && atoi(e) > 0;
        if (v) MLOG("TOTK_LAZY_CLEAR=1: full-target clears with no draw wait for the next pass that attaches them (loadAction = Clear)%s", tbdr_on() ? " (inactive: DM_NVN_TBDR=1)" : ""); }
    return v && !tbdr_on();
}
static void lc_dims(const p_set_rt *r, int k, uint32_t *w, uint32_t *h) {
    uint64_t s; int lv, sl; rt_sub(r, k, &s, &lv, &sl);
    const tex_t *t = k == 8 ? r->depth : r->color[k];
    *w = t ? MAX(t->d.width >> lv, 1u) : 0; *h = t ? MAX(t->d.height >> lv, 1u) : 0;
}
static int lc_same(const p_set_rt *a, int ka, const p_set_rt *b, int kb) {   // the same subresource through the same view (k = 8: depth / stencil)
    uint64_t s1, s2; int l1, l2, x1, x2; rt_sub(a, ka, &s1, &l1, &x1); rt_sub(b, kb, &s2, &l2, &x2);
    if (!s1 || s1 != s2 || l1 != l2 || x1 != x2) return 0;
    int h1 = ka == 8 ? a->has_dview : a->has_cview[ka], h2 = kb == 8 ? b->has_dview : b->has_cview[kb];
    return h1 == h2 && (!h1 || !memcmp(ka == 8 ? &a->dview : &a->cview[ka], kb == 8 ? &b->dview : &b->cview[kb], sizeof(view_t)));
}
static int lc_sub_same(const p_set_rt *a, int ka, const p_set_rt *b, int kb) {   // the same subresource, whatever the view
    uint64_t s1, s2; int l1, l2, x1, x2; rt_sub(a, ka, &s1, &l1, &x1); rt_sub(b, kb, &s2, &l2, &x2);
    return s1 && s1 == s2 && l1 == l2 && x1 == x2;
}
// The texture owns its pool memory alone: one serial on its shared-storage entry, which is the base texture (not a format
// view), and no other entry (live or handed over) overlaps its bytes
static int lc_alone(const tex_t *t, id<MTLTexture> m) {
    if (!stor_on() || !t->mem || !t->L.size) return 0;
    uint64_t ok = obj_key(m); int alone = 1;
    pthread_mutex_lock(&g_smu);
    stor_ent *e = nvn_hm_get(&g_stor_of_obj, &ok);
    if (!e || (__bridge void *)m != e->tex || e->nser != 1 || e->ser[0] != t->serial) alone = 0;
    for (uint32_t i = 0; alone && i < g_by_storage.cap; i++) { const stor_ent *o = g_by_storage.vals[i];
        if (o && o != e && o->k.mem < e->k.mem + e->k.size && e->k.mem < o->k.mem + o->k.size) alone = 0; }
    for (int i = 0; alone && i < g_stor_ndet; i++) { const stor_ent *o = g_stor_det[i];
        if (o != e && o->k.mem < e->k.mem + e->k.size && e->k.mem < o->k.mem + o->k.size) alone = 0; }
    pthread_mutex_unlock(&g_smu);
    return alone;
}
static int lc_plain(const tex_t *t) {   // a target whose clear may wait (see the note above)
    if (!t || (t->d.target != 1 && t->d.target != 7) || t->d.samples > 1 || tex_astc(t)) return 0;
    uint32_t upf; int ubpp; if (bc_unpack(t->d.format, &upf, &ubpp)) return 0;
    id<MTLTexture> m = tex_mtl(t);
    return m && m.textureType == MTLTextureType2D && m.sampleCount == 1 && !pf_block_compressed(m.pixelFormat) && lc_alone(t, m);
}
static int lc_group_plain(void) {   // every attachment R clears is plain
    int any = 0;
    for (int i = 0; i < R.rt.n && i < 8; i++) if (R.clear_c[i]) { if (!lc_plain(R.rt.color[i])) return 0; any = 1; }
    if (R.clear_d || R.clear_s) { if (!lc_plain(R.rt.depth)) return 0; any = 1; }
    return any;
}
// Land every waiting clear, oldest first, each group in a clear-only pass of its own (rp_flush's way)
static void lc_land(void) {
    if (!g_nlc) return;
    if (g_enc) rp_end();
    int n = g_nlc; g_nlc = 0;
    typeof(R) save = R;
    for (int gi = 0; gi < n; gi++) { const lc_group *g = &g_lc[gi]; int any = g->clear_d || g->clear_s; for (int i = 0; i < 8; i++) any |= g->clear_c[i];
        if (!any || !g_cb) continue;
        R.rt = g->rt; R.has_rt = 1; memcpy(R.clear_c, g->clear_c, sizeof R.clear_c); memcpy(R.cc, g->cc, sizeof R.cc);
        R.clear_d = g->clear_d; R.clear_s = g->clear_s; R.cd = g->cd; R.cs = g->cs;
        rp_begin_ex(1); rp_end(); g_lc_landed++; }
    R = save;
}
// SetRenderTargets with full-target clears pending and no draw since: 1 when the clears now wait instead of opening a pass
static int lc_push(void) {
    if (!lc_on() || g_enc || !g_cb || !R.has_rt || !lc_group_plain()) return 0;
    if (g_nlc == LC_MAX) lc_land();
    lc_group *g = &g_lc[g_nlc];
    g->rt = R.rt; memcpy(g->clear_c, R.clear_c, sizeof g->clear_c); memcpy(g->cc, R.cc, sizeof g->cc); g->clear_d = R.clear_d; g->clear_s = R.clear_s; g->cd = R.cd; g->cs = R.cs;
    for (int gi = 0; gi < g_nlc; gi++) { lc_group *o = &g_lc[gi];   // older waiting clears of the same subresources are replaced
        for (int i = 0; i < 8; i++) if (o->clear_c[i]) for (int j = 0; j < 8; j++) if (g->clear_c[j] && lc_sub_same(&o->rt, i, &g->rt, j)) { o->clear_c[i] = 0; break; }
        if ((o->clear_d || o->clear_s) && (g->clear_d || g->clear_s) && lc_sub_same(&o->rt, 8, &g->rt, 8)) { if (g->clear_d) o->clear_d = 0; if (g->clear_s) o->clear_s = 0; } }
    g_nlc++; g_lc_pushed++; g_lc_held = 0;
    memset(R.clear_c, 0, sizeof R.clear_c); R.clear_d = R.clear_s = 0;
    return 1;
}
static int lc_mem_hit(uint64_t lo, uint64_t size) {   // a waiting clear's target overlaps [lo, lo + size)
    if (!size) size = 1;
    for (int gi = 0; gi < g_nlc; gi++) { const lc_group *g = &g_lc[gi];
        for (int k = 0; k < 9; k++) {
            const tex_t *w = k < 8 ? g->rt.color[k] : g->rt.depth;
            if (!w || !(k < 8 ? g->clear_c[k] : (g->clear_d || g->clear_s)) || !w->mem) continue;
            uint64_t m = (uint64_t)(uintptr_t)w->mem, n = w->L.size ? w->L.size : 1;
            if (m < lo + size && lo < m + n) return 1;
        } }
    return 0;
}
static int lc_tex_hit(const treg_t *t) {   // a bound texture that is a waiting clear's target or shares its memory
    for (int gi = 0; gi < g_nlc; gi++) { const lc_group *g = &g_lc[gi];
        for (int k = 0; k < 9; k++) {
            const tex_t *w = k < 8 ? g->rt.color[k] : g->rt.depth;
            if (w && (k < 8 ? g->clear_c[k] : (g->clear_d || g->clear_s)) && w->serial == t->t.serial) return 1; } }
    return t->t.mem && lc_mem_hit((uint64_t)(uintptr_t)t->t.mem, t->t.L.size);
}
// Whether the program reads a texture with a waiting clear (a program with a stage not loaded counts as reading everything)
static int lc_sampled(NvProg *pr) {
    if (!pr || pr->missing) return 1;
    int hit = 0;
    registry_lock();
    for (int st = 0; st < 5 && !hit; st++) { __unsafe_unretained NvShader *sh = pr->sh[st]; if (!sh || sh->stage >= 6) continue;
        for (int i = 0; i < sh->rf.nb && !hit; i++) { const nvn_rbind *b = &sh->rf.b[i]; uint64_t h;
            if (b->kind == NVN_RB_IMG) h = b->reg < 32 ? S.img[sh->stage][b->reg] : 0;
            else if (b->kind == NVN_RB_TEX || b->kind == NVN_RB_TBUF) h = b->reg < 128 ? S.tex[sh->stage][b->reg] : 0;
            else continue;
            const treg_t *t = h ? bound_tex(h) : NULL; if (t && lc_tex_hit(t)) hit = 1; } }
    registry_unlock();
    return hit;
}
// The draw about to replay, before it prepares or reads any texture: its fresh pass's attachments take the waiting clears
// of the same subresources through the same views (the newest one; the draw's own pending clear is newer still), and
// every other waiting clear lands first
static void lc_take(NvProg *pr) {
    int ok = !g_enc && R.has_rt && !lc_sampled(pr);
    uint32_t w0 = 0, h0 = 0;
    for (int k = 0; ok && k < 9; k++) { if (k < 8 ? (k >= R.rt.n || !R.rt.color[k]) : !R.rt.depth) continue;   // one render area: every attachment the same size
        uint32_t w, h; lc_dims(&R.rt, k, &w, &h); if (!w0) { w0 = w; h0 = h; } else if (w != w0 || h != h0) ok = 0; }
    if (!ok) { lc_land(); return; }
    int took = 0;
    for (int gi = g_nlc - 1; gi >= 0; gi--) { lc_group *g = &g_lc[gi];
        for (int i = 0; i < 8; i++) if (g->clear_c[i])
            for (int j = 0; j < R.rt.n && j < 8; j++) {
                if (!R.rt.color[j] || !lc_same(&g->rt, i, &R.rt, j)) continue;
                int dup = 0; for (int k = 0; k < j; k++) if (R.rt.color[k] && lc_sub_same(&R.rt, k, &R.rt, j)) dup = 1;
                if (dup) continue;   // the first slot holding this image takes it
                if (!R.clear_c[j]) { R.clear_c[j] = 1; R.cc[j] = g->cc[i]; }
                g->clear_c[i] = 0; g_lc_taken++; took = 1; break; }
        if ((g->clear_d || g->clear_s) && R.rt.depth && lc_same(&g->rt, 8, &R.rt, 8)) {
            if (g->clear_d) { if (!R.clear_d) { R.clear_d = 1; R.cd = g->cd; } g->clear_d = 0; }
            if (g->clear_s) { if (!R.clear_s) { R.clear_s = 1; R.cs = g->cs; } g->clear_s = 0; }
            g_lc_taken++; took = 1; } }
    lc_land();   // its clear-only passes reset g_lc_held
    if (took) { g_lc_held = 1;
        if (g_lc_taken == 1 || g_lc_taken % 20000 == 0) MLOG("[lazy-clear] taken %llu, landed %llu of %llu deferred clears (pass %llu)", (unsigned long long)g_lc_taken, (unsigned long long)g_lc_landed, (unsigned long long)g_lc_pushed, g_stat_pass + 1); }
}
// The ops a waiting clear can wait across: they neither read nor write textures or the memory under them (a uniform
// update is checked against the waiting targets' memory by the caller)
static int lc_keeps(uint32_t op) {
    switch (op) {
        case OP_SET_RT: case OP_CLEAR_COLOR: case OP_CLEAR_DS: case OP_DRAW: case OP_DRAW_IDX: case OP_BEGIN: case OP_JUMP: case OP_CALL:
        case OP_SCISSOR: case OP_BIND_PROGRAM: case OP_BIND_STATE: case OP_BIND_VATTR: case OP_BIND_VSTREAM: case OP_BIND_VBUF:
        case OP_BIND_UBO: case OP_BIND_SSBO: case OP_BIND_TEX: case OP_BIND_SAMP: case OP_BIND_IMG: case OP_STENCIL:
        case OP_POLY_OFFSET: case OP_VIEWPORTS: case OP_DEPTH_RANGE: case OP_DEPTH_CLAMP: case OP_BLEND_COLOR: case OP_COPY_STRIDE:
        case OP_RASTER_DISCARD: case OP_RENDER_ENABLE: case OP_RENDER_COND: case OP_BARRIER: case OP_SET_TEX_POOL: case OP_SET_SAMP_POOL:
        case OP_DISCARD_COLOR: case OP_DISCARD_DS:   // TOTK_DISCARD: a cleared target may become undefined; landing the clear later is still correct
        case OP_UPDATE_UB: return 1;
        default: return 0;
    }
}
// Called before each replay op: land what op could observe
static void lc_before_op(uint32_t op, const void *a) {
    int keep = lc_keeps(op);
    if (keep && op == OP_UPDATE_UB && g_nlc) { uint64_t d, n; memcpy(&d, a, 8); memcpy(&n, (const uint8_t *)a + 8, 8); if (lc_mem_hit(d, n)) keep = 0; }
    if (keep || (op == OP_END && g_ret_n)) return;   // a sub-list's end returns to its caller
    lc_land();
    if (g_lc_held) { g_lc_held = 0; rp_flush(); }   // a taken clear whose draw skipped: land it like rp_flush did before this op
}
