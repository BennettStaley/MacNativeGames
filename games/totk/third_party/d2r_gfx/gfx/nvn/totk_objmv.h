// TOTK_OBJ_MOTION=1 (default 0; only with TOTK_MFX frame interpolation): per-object motion vectors. Included once by
// nvn_mtl.m (after the readable-tile merge state); not a standalone header.
/* The game renders no velocity buffer (FXAA + FSR1), so frame interpolation had camera motion only and smeared Link,
   NPCs, cloth and foliage. As in NVIDIA RTX Remix (dxvk-remix rtx_draw_call_tracker / rtx_instance_manager), the opaque
   draws of the G-buffer pass are matched with the same draw of the previous frame and their vertices compared:
   - Pass: one whose depth is the G-buffer depth MetalFX takes (mfx_pass_setup: D32F of the colour size, three or more
     colour targets), not layered, not a readable-tile merge, at most six colour targets. Colour attachment OM_ATT (6) gets
     the RGBA16Float motion target (totk_mfx.m), cleared to OBJMV_NONE by the first such pass of the frame.
   - Draw (role MFX_ROLE_MOTION): depth test on, no blending on any target, no alpha-to-coverage, triangles. Every other
     draw of the pass leaves the attachment alone (write mask none, PSO_OBJMV). Discard (alpha-tested foliage) needs
     nothing: a discarded fragment writes no motion either.
   - Vertex variant (om_vs_fn): stores out.gl_Position to cur[(instance - base instance) * nv + (vertex - base vertex)]
     of this frame's position arena and reads prev[] of the previous frame's arena for the same draw (buffer OM_VBUF,
     struct totk_om_draw); both go to the fragment as two extra varyings. Skinning and vertex animation are in the vertex
     shader, so they are in the positions.
   - Fragment variant (om_fs_fn): color(6).rg = prev.xy / prev.w - cur.xy / cur.w (Metal NDC); OBJMV_NONE when the previous
     position is unknown (w <= 0), behind the camera or more than half the screen away (most likely a mismatched draw).
     color(6).b = cur.w, the fragment's view distance. Draws that are not motion draws (border-emulated shaders, lmap,
     terrain and other large meshes) leave attachment 6 alone, so a motion draw hidden by one of them later would keep
     its motion under it (motion "below the terrain"): totk_mfx.m's kernel takes the motion only where this distance is
     the one of the final depth at that pixel (TOTK_OBJ_MOTION_ZTOL), else the camera motion.
   - Identity across frames: (vertex shader, vertex buffer of attribute 0, index buffer, first vertex / base vertex,
     count, instances, vertex range, primitive) and the ordinal among the pass's draws with those (om_key). The previous
     frame's table gives that draw's offset in the previous arena; a new or resized draw writes OBJMV_NONE (camera motion).
     A mesh drawn more than once in this or the previous frame is not matched (ordinals shift when culling changes);
     totk_mfx.m also drops any object motion further than TOTK_OBJ_MOTION_MAXPCT (3% of the width) from the camera's.
   - Arenas: three private buffers, one per game frame in turn: this frame writes one and reads the previous frame's,
     the third was last used two frames ago. They start at 16 MiB each (1M positions) and double at the end of a frame
     whose draws did not fit, up to TOTK_OBJ_MOTION_MB (default 64); a growth costs one frame of camera-only motion (the
     previous frame's positions were in the old arena). Released (deferred past the GPU's use) when interpolation turns
     off or pauses (om_release).
   - What is stored: an indexed draw stores the vertex range its indices actually use (om_irange: min / max index of
     the index buffer, scanned once per (index buffer, count, type) and every OM_IR_AGE frames again), not the whole
     bound vertex buffer, which in TOTK is often a large shared pool range (the first boot test: 2.5M motion draws in
     600 frames, 87% over 3 x 64 MiB). Draws of more than TOTK_OBJ_MOTION_DRAW_MAX positions (vertices x instances,
     default 65536: terrain, large static meshes, dense instanced foliage) store nothing and keep the camera motion,
     which is exact for what does not move independently of the camera; characters, props and cloth fit.
   Camera cuts need nothing here: totk_mfx.m does not interpolate across one, and the frame after has valid positions. */
#define OM_ATT 6
#define OM_VBUF 13               // vertex buffer slot of struct totk_om_draw (translated vertex functions use 0, 14 layer, 16-30 attributes)
#define OM_TAB 65536             // draws per frame table (power of two)
#define OM_MAX_DRAW (1u << 21)   // positions of one draw at most (32 MiB)
typedef struct { uint64_t key; uint32_t off, n, gen; } om_ent;
#define OM_IR 16384              // index range cache (power of two)
#define OM_IR_AGE 600            // frames before an index range is scanned again (index buffers rewritten in place)
typedef struct { uint64_t ibuf; uint32_t count, itype, lo, hi; uint64_t frame; int ok; } om_ir;
static struct {
    __unsafe_unretained id<MTLTexture> depth;   // this frame's G-buffer depth (the game's texture, alive for the frame)
    int cleared, ar;                            // the motion target was cleared this frame; ar: this frame's arena
    id<MTLBuffer> arena[3]; uint64_t gpu[3]; uint32_t cap, used, prev_used, mb, mb_max, draw_max;
    uint64_t want;                               // positions this frame's draws asked for (fitting or not)
    om_ent tab[2][OM_TAB]; int t; uint32_t gen;   // tab[t]: this frame's draws, tab[t ^ 1] the previous frame's
    uint64_t frames, draws, matched, fresh, dups, overflow, unknown, big, grown;
    om_ir ir[OM_IR]; uint64_t ir_scans;
} g_om;
static void om_arenas(uint32_t mb) {   // three arenas of mb MiB; the old ones are released after the GPU's use
    for (int i = 0; i < 3; i++) { if (g_om.arena[i]) defer_obj_release(g_om.arena[i]); g_om.arena[i] = nil; g_om.gpu[i] = 0; }
    for (int i = 0; i < 3; i++) {
        g_om.arena[i] = [g_dev newBufferWithLength:(NSUInteger)mb << 20 options:MTLResourceStorageModePrivate];
        if (!g_om.arena[i]) MDIE("TOTK_OBJ_MOTION: position arena of %u MiB", mb);
        res_add(g_om.arena[i]); g_om.gpu[i] = g_om.arena[i].gpuAddress;
    }
    g_om.mb = mb; g_om.cap = (uint32_t)(((uint64_t)mb << 20) / 16);
    g_om.gen += 2;   // positions in the old arenas are gone: no draw of the previous frame matches
}
// Interpolation off or paused (mfx_latch at the present): the arenas go; the next motion draw starts again at 16 MiB.
static void om_release(void) {
    if (!g_om.arena[0]) return;
    uint64_t b = 0; for (int i = 0; i < 3; i++) { b += g_om.arena[i].allocatedSize; defer_obj_release(g_om.arena[i]); g_om.arena[i] = nil; g_om.gpu[i] = 0; }
    g_om.cap = g_om.used = g_om.prev_used = 0; g_om.mb = 0; g_om.want = 0; g_om.gen += 2; g_om.depth = nil; g_om.cleared = 0;
    MLOG("[mfx] TOTK_OBJ_MOTION: position arenas released (%llu MB)", (unsigned long long)(b >> 20));
}
static void om_note_depth(id<MTLTexture> dt) { g_om.depth = dt; }
static int om_role(int prim) {   // a draw of a motion pass that writes motion
    if (!(prim == 4 || prim == 5 || prim == 7) || !S.ds.dt || S.ms.a2c) return 0;
    uint32_t m = R.rt.n >= 32 ? ~0u : (1u << R.rt.n) - 1;
    return S.color.blend_en & m ? 0 : MFX_ROLE_MOTION;
}
static void om_pass_setup(MTL4RenderPassDescriptor *pd) {
    if (!mfx_obj_motion_on()) return;
    id<MTLTexture> dt = pd.depthAttachment.texture, c0 = R.rt.n > 0 ? pd.colorAttachments[0].texture : nil;
    if (!dt || dt != g_om.depth || !c0 || c0.width != dt.width || c0.height != dt.height || dt.sampleCount != 1) return;
    if (R.rt.n > OM_ATT || pd.colorAttachments[OM_ATT].texture || g_pass.layered || g_fz.active) return;
    int nc = 0; for (int i = 0; i < R.rt.n && i < 8; i++) nc += pd.colorAttachments[i].texture != nil;
    if (nc < 3) return;
    MTLRenderPassColorAttachmentDescriptor *a = pd.colorAttachments[OM_ATT];
    a.texture = mfx_obj_target(g_dev, dt.width, dt.height);
    a.loadAction = g_om.cleared ? MTLLoadActionLoad : MTLLoadActionClear; a.clearColor = MTLClearColorMake(OBJMV_NONE, OBJMV_NONE, 0, 0);   // B 0: no distance, never taken
    a.storeAction = MTLStoreActionStore;
    g_om.cleared = 1;
    g_pass.cfmt[OM_ATT] = (uint32_t)MTLPixelFormatRGBA16Float; g_pass.objmv = 1;
    mfx_note_obj();
}
static void om_frame_end(void) {   // at the game's present
    if (!mfx_obj_motion_on()) return;
    g_om.depth = nil; g_om.cleared = 0;
    if (!g_om.used && !g_om.draws) return;   // no motion pass yet
    uint64_t want = g_om.want; g_om.want = 0;
    g_om.t ^= 1; g_om.gen++; g_om.ar = (g_om.ar + 1) % 3; g_om.prev_used = g_om.used; g_om.used = 0; g_om.frames++;
    if (g_om.arena[0] && want > g_om.cap && g_om.mb < g_om.mb_max) {   // this frame's draws did not fit: grow (up to the cap)
        uint32_t mb = g_om.mb; while (mb < g_om.mb_max && ((uint64_t)mb << 20) / 16 < want + want / 4) mb *= 2;
        if (mb > g_om.mb_max) mb = g_om.mb_max;
        MLOG("[mfx] TOTK_OBJ_MOTION: %llu positions wanted, %u fit: position arenas %u -> %u MiB each (cap %u, TOTK_OBJ_MOTION_MB)",
             (unsigned long long)want, g_om.cap, g_om.mb, mb, g_om.mb_max);
        om_arenas(mb); g_om.grown++;
    }
    if (g_om.frames % 600 == 0)
        MLOG("[mfx] TOTK_OBJ_MOTION: up to frame %llu: %llu motion draws, %llu matched the previous frame, %llu new, %llu repeated meshes (camera motion), %llu without a vertex range, %llu over %u positions (camera motion), %llu over the arena (%u of %u positions last frame, arenas %u MiB each, %llu index ranges scanned)",
             (unsigned long long)atomic_load(&g_cur_frame), (unsigned long long)g_om.draws, (unsigned long long)g_om.matched, (unsigned long long)g_om.fresh, (unsigned long long)g_om.dups,
             (unsigned long long)g_om.unknown, (unsigned long long)g_om.big, g_om.draw_max, (unsigned long long)g_om.overflow, g_om.prev_used, g_om.cap, g_om.mb,
             (unsigned long long)g_om.ir_scans);
}
static uint64_t om_mix(uint64_t h, uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); h *= 0xff51afd7ed558ccdull; return h ^ (h >> 33); }
static om_ent *om_find(int t, uint64_t key, uint32_t gen, int make) {
    uint32_t i = (uint32_t)key & (OM_TAB - 1);
    for (int n = 0; n < 64; n++, i = (i + 1) & (OM_TAB - 1)) {
        om_ent *e = &g_om.tab[t][i];
        if (e->gen != gen) { if (!make) return NULL; e->gen = gen; e->key = key; e->n = 0; e->off = 0; return e; }
        if (e->key == key) return e;
    }
    return NULL;
}
typedef struct { uint64_t cur, prev; uint32_t nv, bv, bi, flags, cap, pad; } om_draw;   // = struct totk_om_draw in MSL
/* The vertices an indexed draw uses: lo..hi of its indices (primitive restart values left out). 0: unknown (8-bit
   indices, memory not readable, written by pending GPU work) and the bound vertex buffer is taken as before. */
static int om_irange(const p_draw_idx *di, uint32_t *lo, uint32_t *hi) {
    if (!di->ibuf || di->count <= 0 || (di->itype != 1 && di->itype != 2)) return 0;
    uint64_t h = om_mix(om_mix(di->ibuf, (uint64_t)(uint32_t)di->count), (uint64_t)di->itype);
    om_ir *c = &g_om.ir[h & (OM_IR - 1)];
    uint64_t now = g_om.frames;
    if (c->ibuf == di->ibuf && c->count == (uint32_t)di->count && c->itype == (uint32_t)di->itype && now - c->frame < OM_IR_AGE) { *lo = c->lo; *hi = c->hi; return c->ok; }
    uint64_t isz = di->itype == 1 ? 2 : 4, n = (uint64_t)(uint32_t)di->count;
    if (pend_hits(di->ibuf, n * isz)) return 0;   // being written on the GPU: not cached, asked again next time
    void *m; uint64_t o; const uint8_t *ib = NULL;
    if (mtl_addr(di->ibuf, n * isz, &m, &o) && (ib = [(__bridge id<MTLBuffer>)m contents])) ib += o;
    uint32_t l = UINT32_MAX, u = 0;
    if (ib && isz == 2) { const uint16_t *x = (const uint16_t *)ib; for (uint64_t k = 0; k < n; k++) { uint32_t v = x[k]; if (v == 0xffff) continue; if (v < l) l = v; if (v > u) u = v; } }
    else if (ib) { const uint32_t *x = (const uint32_t *)ib; for (uint64_t k = 0; k < n; k++) { uint32_t v = x[k]; if (v == 0xffffffffu) continue; if (v < l) l = v; if (v > u) u = v; } }
    c->ibuf = di->ibuf; c->count = (uint32_t)di->count; c->itype = (uint32_t)di->itype; c->frame = now; c->ok = ib && l <= u; c->lo = l; c->hi = u;
    g_om.ir_scans++;
    *lo = l; *hi = u; return c->ok;
}
// Before a motion draw: its constants (buffer OM_VBUF). flags 1: store positions, 2: prev[] holds the previous frame's.
static void om_bind(id<MTL4RenderCommandEncoder> en, NvProg *pr, const p_draw *dw, const p_draw_idx *di) {
    if (!g_om.arena[0]) {
        if (!g_om.mb_max) {
            const char *e = getenv("TOTK_OBJ_MOTION_MB"); uint64_t mb = e ? strtoull(e, NULL, 10) : 64; if (mb < 4) mb = 4; if (mb > 1024) mb = 1024;
            const char *d = getenv("TOTK_OBJ_MOTION_DRAW_MAX"); uint64_t dm = d ? strtoull(d, NULL, 10) : 65536; if (dm < 64) dm = 64; if (dm > OM_MAX_DRAW) dm = OM_MAX_DRAW;
            g_om.mb_max = (uint32_t)mb; g_om.draw_max = (uint32_t)dm;
        }
        uint32_t mb = g_om.mb_max < 16 ? g_om.mb_max : 16;
        om_arenas(mb); if (!g_om.gen) g_om.gen = 1;
        MLOG("[mfx] TOTK_OBJ_MOTION: three position arenas of %u MiB (%u positions each; they grow to %u MiB when needed); draws of more than %u positions keep the camera motion",
             mb, g_om.cap, g_om.mb_max, g_om.draw_max);
    }
    om_draw p = {0};
    uint32_t inst = 1, nv = 0, bv = 0, bi = 0, count = 0; uint64_t ib = 0, vb = 0; int prim = 0;
    int st = S.nattr > 0 ? S.attr[0].stream : -1;
    if (st >= 0 && st < 16) vb = S.vbuf[st].address;
    if (dw) { prim = dw->prim; count = (uint32_t)dw->count; bv = (uint32_t)dw->first; bi = (uint32_t)dw->base_instance; inst = dw->instances > 1 ? (uint32_t)dw->instances : 1; nv = count; }
    else if (di) {
        prim = di->prim; count = (uint32_t)di->count; bv = (uint32_t)di->base_vertex; bi = (uint32_t)di->base_instance; inst = di->instances > 1 ? (uint32_t)di->instances : 1; ib = di->ibuf;
        uint32_t lo, hi;
        if (om_irange(di, &lo, &hi)) { nv = hi - lo + 1; bv += lo; }   // [[vertex_id]] - bv is then the index - lo
        else if (st >= 0 && st < 16 && S.stream[st].stride && S.vbuf[st].size) nv = (uint32_t)(S.vbuf[st].size / S.stream[st].stride);
        else if (di->itype <= 1) nv = di->itype == 0 ? 256 : 65536;
    }
    uint64_t need = (uint64_t)nv * inst;
    g_om.draws++;
    if (!nv || need > OM_MAX_DRAW) g_om.unknown++;
    else if (need > g_om.draw_max) g_om.big++;
    else if (g_om.want += need, g_om.used + need > g_om.cap) g_om.overflow++;
    else {
        uint64_t base = om_mix(om_mix(om_mix(om_mix((uint64_t)(uintptr_t)(__bridge void *)pr->sh[0], vb), ib), ((uint64_t)bv << 32) | count), ((uint64_t)inst << 32) | nv);
        base = om_mix(base, (uint64_t)prim);
        om_ent *e = NULL; uint64_t key = 0, o = 0;
        for (; o < 256 && !e; o++) {   // the ordinal among this frame's draws with the same identity
            key = om_mix(base, o);
            om_ent *have = om_find(g_om.t, key, g_om.gen, 0);
            if (!have) e = om_find(g_om.t, key, g_om.gen, 1);
        }
        if (e) {
            e->off = g_om.used; e->n = (uint32_t)need; g_om.used += (uint32_t)need;
            p.cur = g_om.gpu[g_om.ar] + 16ull * e->off; p.flags = 1; p.cap = (uint32_t)need;
            /* The same mesh drawn more than once (this frame or the previous one) cannot be matched by ordinal: when
               the camera turns, culling drops or adds copies and the ordinals shift onto other objects (the flicker
               of TOTK_OBJ_MOTION while turning). Such draws keep the camera motion; TOTK_OBJ_MOTION_DUP=1 matches
               them by ordinal as before. */
            static int dup = -1; if (dup < 0) { const char *d = getenv("TOTK_OBJ_MOTION_DUP"); dup = d && atoi(d) > 0; }
            int ambiguous = !dup && (o > 1 || om_find(g_om.t ^ 1, om_mix(base, 1), g_om.gen - 1, 0));
            om_ent *q = ambiguous ? NULL : om_find(g_om.t ^ 1, key, g_om.gen - 1, 0);
            if (q && q->n == need) { p.prev = g_om.gpu[(g_om.ar + 2) % 3] + 16ull * q->off; p.flags |= 2; g_om.matched++; }
            else if (ambiguous) g_om.dups++;
            else g_om.fresh++;
        } else g_om.overflow++;
    }
    p.nv = nv; p.bv = bv; p.bi = bi;
    if (!p.prev) p.prev = g_om.gpu[g_om.ar];   // never read without flag 2; a valid address all the same
    if (!p.cur) p.cur = g_om.gpu[g_om.ar];
    id<MTLBuffer> buf; size_t off = arena_alloc(sizeof p, &buf);
    memcpy((uint8_t *)buf.contents + off, &p, sizeof p);
    enc_r_vbuf(en, OM_VBUF, buf.gpuAddress + off);
}

/* Shader variants. Cached per (vertex, fragment) shader pair / per fragment shader; nil: the source cannot take the
   rewrite (logged once), the draw then writes no motion (camera motion there). */
static os_unfair_lock g_om_lock = OS_UNFAIR_LOCK_INIT;
static NSMutableDictionary<NSString *, id> *g_om_fn;
static NSString *om_param(NSString *src, NSString *attr) {   // the name of an entry parameter with [[attr]], or nil
    NSRegularExpression *re = [NSRegularExpression regularExpressionWithPattern:[NSString stringWithFormat:@"\\buint\\s+(\\w+)\\s*\\[\\[%@\\]\\]", attr] options:0 error:nil];
    NSTextCheckingResult *m = [re firstMatchInString:src options:0 range:NSMakeRange(0, src.length)];
    return m ? [src substringWithRange:[m rangeAtIndex:1]] : nil;
}
static id<MTLFunction> om_cached(NSString *key, int *have) {
    os_unfair_lock_lock(&g_om_lock); id f = g_om_fn[key]; os_unfair_lock_unlock(&g_om_lock);
    *have = f != nil; return f == VARIANT_NONE ? nil : f;
}
static void om_cache(NSString *key, id<MTLFunction> f) {
    os_unfair_lock_lock(&g_om_lock); if (!g_om_fn) g_om_fn = [NSMutableDictionary dictionary]; if (!g_om_fn[key]) g_om_fn[key] = f ? (id)f : VARIANT_NONE; os_unfair_lock_unlock(&g_om_lock);
}
static NSString *om_vs_src(NvShader *vs, NvShader *fs, NSString **why) {
    NSString *src = sh_src(vs), *head = @"struct main0_out\n{\n", *entry = @"vertex main0_out main0(";
    if ([src rangeOfString:head].location == NSNotFound || [src rangeOfString:entry].location == NSNotFound || [src rangeOfString:@"return out;"].location == NSNotFound) { *why = @"no main0_out / entry / return out"; return nil; }
    NSRegularExpression *pre = [NSRegularExpression regularExpressionWithPattern:@"float4\\s+(\\w+)\\s*\\[\\[position[^\\]]*\\]\\]" options:0 error:nil];
    NSTextCheckingResult *pm = [pre firstMatchInString:src options:0 range:NSMakeRange(0, src.length)];
    if (!pm) { *why = @"no [[position]] output"; return nil; }
    NSString *pos = [src substringWithRange:[pm rangeAtIndex:1]];
    NSString *vid = om_param(src, @"vertex_id"), *iid = om_param(src, @"instance_id");
    NSMutableString *params = [NSMutableString stringWithFormat:@"constant totk_om_draw& totk_om [[buffer(%d)]], ", OM_VBUF];
    if (!vid) { vid = @"totk_om_vid"; [params appendString:@"uint totk_om_vid [[vertex_id]], "]; }
    if (!iid) { iid = @"totk_om_iid"; [params appendString:@"uint totk_om_iid [[instance_id]], "]; }
    // the fragment shader's varyings the vertex shader leaves unwritten, as vs_fn_variant adds them
    NSDictionary *in = fs && sh_has_src(fs) ? msl_varyings(fs) : nil, *out = in ? msl_varyings(vs) : nil;
    NSMutableString *add = [NSMutableString string];
    for (NSNumber *l in [in.allKeys sortedArrayUsingSelector:@selector(compare:)]) if (!out[l]) [add appendFormat:@"    %@ totk_unwritten_locn%@ [[user(locn%@)]];\n", in[l], l, l];
    [add appendString:@"    float4 totk_om_c [[user(totk_om_c)]];\n    float4 totk_om_p [[user(totk_om_p)]];\n"];
    NSString *decl = @"struct totk_om_draw { device float4 *cur; device float4 *prev; uint nv, bv, bi, flags, cap, pad; };\n\n";
    src = [src stringByReplacingCharactersInRange:[src rangeOfString:head] withString:[NSString stringWithFormat:@"%@%@%@", decl, head, add]];
    src = [src stringByReplacingCharactersInRange:[src rangeOfString:entry] withString:[entry stringByAppendingString:params]];
    if ([src rangeOfString:[entry stringByAppendingFormat:@"%@)", params]].location != NSNotFound)   // the entry had no parameters
        src = [src stringByReplacingOccurrencesOfString:[entry stringByAppendingFormat:@"%@)", params] withString:[entry stringByAppendingFormat:@"%@)", [params substringToIndex:params.length - 2]]];
    NSString *store = [NSString stringWithFormat:
        @"{ uint totk_om_v = %@ - totk_om.bv; uint totk_om_i = (%@ - totk_om.bi) * totk_om.nv + totk_om_v; float4 totk_om_q = float4(0.0, 0.0, 0.0, -1.0);\n"
         "      if ((totk_om.flags & 1u) != 0u && totk_om_v < totk_om.nv && totk_om_i < totk_om.cap) { totk_om.cur[totk_om_i] = out.%@; if ((totk_om.flags & 2u) != 0u) totk_om_q = totk_om.prev[totk_om_i]; }\n"
         "      out.totk_om_c = out.%@; out.totk_om_p = totk_om_q; return out; }", vid, iid, pos, pos];
    return [src stringByReplacingOccurrencesOfString:@"return out;" withString:store];
}
/* TOTK_OBJ_MOTION_DIAG (diagnostic, default 0): the motion draws still use the motion variants (same vertex functions,
   same extra varyings and attachment 6), but the fragment variant writes a constant: 1 OBJMV_NONE (the interpolation then
   uses the camera motion everywhere, as without TOTK_OBJ_MOTION), 2 a zero motion. Flicker that stays with =1 comes from
   the changed G-buffer draws (geometry / depth), flicker that goes away comes from the motion values. */
static int om_diag(void) {
    static int v = -1;
    if (v < 0) { const char *e = getenv("TOTK_OBJ_MOTION_DIAG"); v = e ? atoi(e) : 0; if (v < 0 || v > 2) v = 0;
                 if (v) MLOG("[mfx] TOTK_OBJ_MOTION_DIAG=%d: motion variants drawn, motion written as %s", v, v == 1 ? "none (camera motion)" : "zero"); }
    return v;
}
static NSString *om_fs_src(NvShader *fs, NSString **why) {
    NSString *src = sh_src(fs), *head = @"struct main0_out\n{\n", *entry = @"fragment main0_out main0(", *ihead = @"struct main0_in\n{\n";
    if ([src rangeOfString:head].location == NSNotFound || [src rangeOfString:entry].location == NSNotFound || [src rangeOfString:@"return out;"].location == NSNotFound) { *why = @"no main0_out / entry / return out"; return nil; }
    if ([src rangeOfString:[NSString stringWithFormat:@"[[color(%d)]]", OM_ATT]].location != NSNotFound) { *why = @"writes colour 6 itself"; return nil; }
    NSString *helper = [NSString stringWithFormat:
        @"static inline float4 totk_om_motion(float4 c, float4 p) {\n"
         "    float z = clamp(c.w, 0.0, 65000.0);   // the fragment's view distance: totk_mfx.m keeps the motion only where the final depth is this draw's\n"
         "%@"
         "    if (!(p.w > 1e-6) || !(c.w > 1e-6)) return float4(%.1f, %.1f, z, 0.0);\n"
         "    float2 d = p.xy / p.w - c.xy / c.w;\n"
         "    if (any(isnan(d)) || any(isinf(d)) || any(abs(d) > 1.0)) return float4(%.1f, %.1f, z, 0.0);\n"
         "    return float4(d, z, 0.0);\n}\n\n",
        om_diag() == 1 ? [NSString stringWithFormat:@"    return float4(%.1f, %.1f, z, 0.0);   // TOTK_OBJ_MOTION_DIAG=1\n", OBJMV_NONE, OBJMV_NONE] : om_diag() == 2 ? @"    return float4(0.0, 0.0, z, 0.0);   // TOTK_OBJ_MOTION_DIAG=2\n" : @"",
        OBJMV_NONE, OBJMV_NONE, OBJMV_NONE, OBJMV_NONE];
    NSString *iname = @"in";
    if ([src rangeOfString:ihead].location != NSNotFound) {
        NSRegularExpression *re = [NSRegularExpression regularExpressionWithPattern:@"main0_in\\s+(\\w+)\\s*\\[\\[stage_in\\]\\]" options:0 error:nil];
        NSTextCheckingResult *m = [re firstMatchInString:src options:0 range:NSMakeRange(0, src.length)];
        if (!m) { *why = @"main0_in is not the stage input"; return nil; }
        iname = [src substringWithRange:[m rangeAtIndex:1]];
        src = [src stringByReplacingCharactersInRange:[src rangeOfString:ihead] withString:[ihead stringByAppendingString:@"    float4 totk_om_c [[user(totk_om_c)]];\n    float4 totk_om_p [[user(totk_om_p)]];\n"]];
        src = [src stringByReplacingCharactersInRange:[src rangeOfString:head] withString:[NSString stringWithFormat:@"%@%@    float4 totk_om_mv [[color(%d)]];\n", helper, head, OM_ATT]];
    } else {
        if ([src rangeOfString:@"[[stage_in]]"].location != NSNotFound) { *why = @"stage input of another type"; return nil; }
        NSString *istruct = [ihead stringByAppendingString:@"    float4 totk_om_c [[user(totk_om_c)]];\n    float4 totk_om_p [[user(totk_om_p)]];\n};\n\n"];
        src = [src stringByReplacingCharactersInRange:[src rangeOfString:head] withString:[NSString stringWithFormat:@"%@%@%@    float4 totk_om_mv [[color(%d)]];\n", helper, istruct, head, OM_ATT]];
        NSString *param = @"main0_in in [[stage_in]]";
        NSRange r = [src rangeOfString:entry];
        BOOL empty = [src characterAtIndex:r.location + r.length] == ')';
        src = [src stringByReplacingCharactersInRange:r withString:[entry stringByAppendingString:empty ? param : [param stringByAppendingString:@", "]]];
    }
    return [src stringByReplacingOccurrencesOfString:@"return out;" withString:[NSString stringWithFormat:@"{ out.totk_om_mv = totk_om_motion(%@.totk_om_c, %@.totk_om_p); return out; }", iname, iname]];
}
static id<MTLFunction> om_variant(NSString *key, NvShader *named, NSString *(^make_src)(NSString **why)) {
    int have; id<MTLFunction> f = om_cached(key, &have);
    if (have) return f;
    void (^build)(void) = ^{
        NSString *why = nil, *src = make_src(&why); id<MTLFunction> fn = nil;
        if (src) {
            NSError *e = nil; id<MTLLibrary> lib = variant_library(src, &e);
            fn = lib ? [lib newFunctionWithName:named->fn.name] : nil;
            if (!fn) why = e.localizedDescription ?: @"no function";
        }
        static int said; if (!fn && said++ < 32) MLOG("[mfx] TOTK_OBJ_MOTION: %s: no motion variant (%s); camera motion for its draws", key.UTF8String, why.UTF8String);
        om_cache(key, fn);
    };
    if (variant_defer([@"objmv " stringByAppendingString:key], build)) return nil;
    build();
    return om_cached(key, &have);
}
static id<MTLFunction> om_vs_fn(NvShader *vs, NvShader *fs) {
    if (!vs || !sh_has_src(vs)) return nil;
    return om_variant([NSString stringWithFormat:@"vs %s fs %s", vs->key, fs ? fs->key : "-"], vs, ^NSString *(NSString **why) { return om_vs_src(vs, fs, why); });
}
static id<MTLFunction> om_fs_fn(NvShader *fs) {
    if (!fs || !sh_has_src(fs)) return nil;
    return om_variant([NSString stringWithFormat:@"fs %s", fs->key], fs, ^NSString *(NSString **why) { return om_fs_src(fs, why); });
}
// pso_desc: attachment OM_ATT of a motion pass; the motion functions for a motion draw.
static void om_pso_desc(MTLRenderPipelineDescriptor *d, const pso_key *k, NvShader *vs, NvShader *fs, id<MTLFunction> fsfn, int merged) {
    MTLRenderPipelineColorAttachmentDescriptor *c = d.colorAttachments[OM_ATT];
    c.blendingEnabled = NO; c.writeMask = MTLColorWriteMaskNone;
    if ((int)(k->a2c >> 2 & 3) != MFX_ROLE_MOTION || (k->a2c & (PSO_VBORDER | PSO_LAYERED)) || merged || fsfn || !k->dfmt || !fs) return;
    id<MTLFunction> vf = om_vs_fn(vs, fs), ff = om_fs_fn(fs);
    if (!vf || !ff) return;
    d.vertexFunction = vf; d.fragmentFunction = ff; c.writeMask = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue;
}
