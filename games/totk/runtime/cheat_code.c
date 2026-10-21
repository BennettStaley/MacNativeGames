#include "cheat_code.h"
#include <stdio.h>
#include <string.h>

#define NIB(x, i) (((x) >> (28 - 4 * (i))) & 0xF)

/* Does [a, a + n) overlap any of the sorted, disjoint [start, end) pairs? */
static int overlaps_any(const uint32_t *r, unsigned pairs, uint64_t a, uint64_t n) {
    unsigned lo = 0, hi = pairs;
    while (lo < hi) { unsigned mid = (lo + hi) / 2; if (r[2 * mid + 1] <= a) lo = mid + 1; else hi = mid; }   // first pair ending after a
    return lo < pairs && r[2 * lo] < a + n;
}
/* Is t strictly inside one of the pairs (after its first word)? */
static int inside_any(const uint32_t *r, unsigned pairs, uint64_t t) {
    unsigned lo = 0, hi = pairs;
    while (lo < hi) { unsigned mid = (lo + hi) / 2; if (r[2 * mid + 1] <= t) lo = mid + 1; else hi = mid; }
    return lo < pairs && r[2 * lo] < t;
}
static int64_t sext(uint64_t v, unsigned bits) { return (int64_t)(v << (64 - bits)) >> (64 - bits); }
/* The PC-relative target of instruction w at offset at: 1 branch (B, BL, B.cond, BC.cond, CBZ/CBNZ, TBZ/TBNZ),
   2 data address (ADR, LDR / LDRSW / PRFM / SIMD literal), 3 data page (ADRP); 0 when w is not PC-relative. */
static int pc_target(uint32_t w, uint64_t at, uint64_t *to) {
    if ((w & 0x7C000000) == 0x14000000) { *to = at + (uint64_t)(sext(w & 0x3FFFFFF, 26) * 4); return 1; }
    if ((w & 0xFF000000) == 0x54000000 || (w & 0x7E000000) == 0x34000000) { *to = at + (uint64_t)(sext(w >> 5 & 0x7FFFF, 19) * 4); return 1; }
    if ((w & 0x7E000000) == 0x36000000) { *to = at + (uint64_t)(sext(w >> 5 & 0x3FFF, 14) * 4); return 1; }
    uint64_t adr = (uint64_t)(w >> 5 & 0x7FFFF) << 2 | (w >> 29 & 3);
    if ((w & 0x9F000000) == 0x10000000) { *to = at + (uint64_t)sext(adr, 21); return 2; }
    if ((w & 0x9F000000) == 0x90000000) { *to = (at & ~0xFFFull) + (uint64_t)(sext(adr, 21) * 4096); return 3; }
    if ((w & 0x3B000000) == 0x18000000) { *to = at + (uint64_t)(sext(w >> 5 & 0x7FFFF, 19) * 4); return 2; }
    return 0;
}
/* Instructions the Apple-silicon rewrite replaces wherever they occur (svc, the thread-pointer and counter registers). */
static const char *needs_rewrite(uint32_t w) {
    if ((w & 0xFFE0001F) == 0xD4000001) return "an svc";
    uint32_t sys = w & 0xFFFFFFE0;
    if (sys == 0xD53BD040 || sys == 0xD51BD040 || sys == 0xD53BD060) return "a thread-register access";
    if (sys == 0xD53BE020 || sys == 0xD53BE040) return "a counter read";
    return NULL;
}

/* Registers whose value is known at the current point of straight-line code (follows Atmosphère: they start at 0 each
   tick and the master code runs first). Anything set under a condition or in a loop becomes unknown. */
typedef struct { uint64_t v[16]; unsigned known, depth, loops; } Abs;
static int known(const Abs *s, unsigned r) { return s->known >> r & 1; }
static void forget(Abs *s, unsigned r) { s->known &= ~(1u << r); }
static void set_reg(Abs *s, unsigned r, uint64_t v) {
    if (s->depth || s->loops) { forget(s, r); return; }
    s->v[r] = v; s->known |= 1u << r;
}
static uint64_t widthmask(uint64_t v, unsigned t) { return t >= 8 ? v : v & ((1ull << (8 * t)) - 1); }
static int int_op(uint64_t a, unsigned op, uint64_t b, unsigned t, uint64_t *out) {
    switch (op) {
    case 0: *out = a + b; break; case 1: *out = a - b; break; case 2: *out = a * b; break;
    case 3: *out = b < 64 ? a << b : 0; break; case 4: *out = b < 64 ? a >> b : 0; break;
    case 5: *out = a & b; break; case 6: *out = a | b; break; case 7: *out = ~a; break; case 8: *out = a ^ b; break;
    case 9: *out = a; break;
    default: return 0;
    }
    *out = widthmask(*out, t);
    return 1;
}

#define FAIL(...) do { if (err && errn) snprintf(err, errn, __VA_ARGS__); return 0; } while (0)

/* Walks one program, updating s; with out, collects c's main code writes (and refuses the ones that cannot be split). */
static int walk(const CheatMain *m, const Cheat *c, Abs *s, CheatCode *out, char *err, size_t errn) {
    s->depth = s->loops = 0;   // blocks and loops never span programs (each cheat runs on its own)
    for (unsigned i = 0; i < c->n;) {
        unsigned len = cheat_insn_len(c->w, c->n, i);
        if (!len) FAIL("malformed instruction %08x", c->w[i]);
        const uint32_t *w = &c->w[i];
        uint32_t x = w[0];
        unsigned t = NIB(x, 1), op = NIB(x, 0), top = !s->depth && !s->loops;
        switch (op) {
        case 0x0: {
            unsigned mt = NIB(x, 2), r = NIB(x, 3);
            uint64_t imm = (uint64_t)(x & 0xFF) << 32 | w[1];
            if (mt != 0 || !known(s, r) || s->v[r] + imm >= m->ro_end) { if (out) out->runtime = 1; break; }
            if (!out) break;
            uint64_t off = s->v[r] + imm;
            const char *what = off < m->rx_end ? "code" : "read-only data";
            if (!top) FAIL("%s patch at main+%#llx is under a condition or in a loop (only possible at launch)", what, (unsigned long long)off);
            if ((t != 4 && t != 8) || (off & 3) || (off < m->rx_end && off + t > m->rx_end) || off + t > m->ro_end)
                FAIL("writes part of a word of the game's %s at main+%#llx", what, (unsigned long long)off);
            uint32_t words[2] = {t == 8 ? w[3] : w[2], w[2]};   // a 64-bit value is stored low word first
            for (unsigned j = 0; j < t / 4; j++) {
                unsigned k = 0;
                while (k < out->n && out->w[k].off != off + 4 * j) k++;
                if (k == CHEAT_CODE_WORDS) FAIL("more than %u code words", CHEAT_CODE_WORDS);
                out->w[k] = (CheatWord){off + 4 * j, words[j], -1};
                if (k == out->n) out->n++;
            }
            break;
        }
        case 0xA: {   // a register stored to a static main address: code from a register is not split
            unsigned r = NIB(x, 3), inc = NIB(x, 4), o = NIB(x, 5), xx = NIB(x, 6);
            uint64_t rel = len == 2 ? (uint64_t)(x & 0xF) << 32 | w[1] : 0, addr = 0;
            int fixed = xx == 0 && ((o == 4) || ((o == 3 || o == 5) && known(s, r)));
            if (fixed) addr = (o == 4 ? 0 : s->v[r]) + (o == 3 ? 0 : rel);
            if (out && fixed && addr < m->ro_end) FAIL("stores a register into the game's code or read-only data at main+%#llx (not supported)", (unsigned long long)addr);
            if (out) out->runtime = 1;
            if (inc) { if (known(s, r) && top) s->v[r] += t; else forget(s, r); }
            break;
        }
        case 0x6:
            if (out) out->runtime = 1;
            if (NIB(x, 4)) { unsigned r = NIB(x, 3); if (known(s, r) && top) s->v[r] += t; else forget(s, r); }
            break;
        case 0x1: case 0x8:
            if (out) out->runtime = 1;
            s->depth++;
            break;
        case 0x2:
            if (t == 0 && s->depth) s->depth--;
            break;
        case 0x3:
            forget(s, NIB(x, 3));
            if (t == 0) s->loops++; else if (s->loops) s->loops--;
            if (out) out->runtime = 1;
            break;
        case 0x4: set_reg(s, NIB(x, 3), (uint64_t)w[1] << 32 | w[2]); break;
        case 0x5: forget(s, NIB(x, 3)); if (out) out->runtime = 1; break;
        case 0x7: {
            unsigned r = NIB(x, 3);
            uint64_t v;
            if (known(s, r) && int_op(s->v[r], NIB(x, 4), w[1], t, &v)) set_reg(s, r, v); else forget(s, r);
            break;
        }
        case 0x9: {
            unsigned o = NIB(x, 2), r = NIB(x, 3), a = NIB(x, 4), b = NIB(x, 6), imm = NIB(x, 5);
            uint64_t rhs = imm ? (t == 8 ? (uint64_t)w[1] << 32 | w[2] : w[1]) : s->v[b], v;
            if (known(s, a) && (imm || known(s, b)) && int_op(s->v[a], o, rhs, t, &v)) set_reg(s, r, v); else forget(s, r);
            break;
        }
        case 0xC:
            if (t == 0 || t == 4) { s->depth++; if (out) out->runtime = 1; }
            else if (t == 1) { unsigned d = NIB(x, 3), o = NIB(x, 6); if (o == 0) forget(s, d); else if (o == 3) set_reg(s, d, 0); }
            else if (t == 2) { unsigned o = NIB(x, 2); for (unsigned k = 0; k < 16; k++) if (x >> k & 1) { if (o == 0) forget(s, k); else if (o == 3) set_reg(s, k, 0); } }
            else if (t == 3) { if ((x >> 4 & 0xFF) < 0x80) forget(s, x & 0xF); }
            break;
        default: break;
        }
        i += len;
    }
    return 1;
}

static const CheatWord *find_word(const CheatWord *w, unsigned n, uint64_t off) {
    for (unsigned k = 0; k < n; k++) if (w[k].off == off) return &w[k];
    return NULL;
}

int cheat_code_scan(const CheatMain *m, const Cheat *const *masters, int nmasters, const Cheat *c,
                    const CheatWord *prior, unsigned nprior, CheatCode *out, char *err, size_t errn) {
    memset(out, 0, sizeof *out);
    out->conflict = -1;
    if (err && errn) err[0] = 0;
    Abs s = {0};
    s.known = 0xFFFF;   // every register starts at 0
    for (int k = 0; k < nmasters; k++) if (masters[k] != c && !walk(m, masters[k], &s, NULL, NULL, 0)) s.known = 0;
    if (!walk(m, c, &s, out, err, errn)) return 0;
    if (out->n && !m->changed) FAIL("this build has no map of the instructions the Apple-silicon rewrite changed");
    for (unsigned k = 0; k < out->n; k++) {
        uint64_t at = out->w[k].off;
        uint32_t want = out->w[k].value, img, now;
        if (at < m->text_end && (overlaps_any(m->changed, m->nchanged, at, 4) || overlaps_any(m->moved, m->nmoved, at, 4)))
            FAIL("patches main+%#llx, an instruction the Apple-silicon rewrite changed", (unsigned long long)at);
        if (!m->image_word || !m->image_word(m->ctx, at, &img)) FAIL("cannot read the verified game image at main+%#llx", (unsigned long long)at);
        memcpy(&now, m->mem + at, 4);
        const CheatWord *p = find_word(prior, nprior, at);
        if (p) { if (p->value != want && out->conflict < 0) out->conflict = p->cheat; }
        else if (now != img) FAIL("overlaps the game runtime's own patch at main+%#llx", (unsigned long long)at);
        if (at >= m->rx_end) continue;   // read-only data: no instruction checks
        const char *bad = needs_rewrite(want);
        if (bad) FAIL("writes %s at main+%#llx, which the Apple-silicon rewrite would have to replace", bad, (unsigned long long)at);
        uint64_t to;
        switch (pc_target(want, at, &to)) {
        case 1:   // a branch: into the code, not into the dead middle of a moved window, into padding only where a cheat wrote
            if (to >= m->rx_end) FAIL("branches out of the game's code (main+%#llx -> %#llx)", (unsigned long long)at, (unsigned long long)to);
            if (to < m->text_end && inside_any(m->moved, m->nmoved, to))
                FAIL("branches into an instruction window the Apple-silicon rewrite moved (main+%#llx)", (unsigned long long)to);
            if (to >= m->text_end && !find_word(out->w, out->n, to) && !find_word(prior, nprior, to))
                FAIL("branches into unused padding (main+%#llx)", (unsigned long long)to);
            break;
        case 2: case 3:   // a data reference: within the module image (not the runtime's key page or patch area)
            if (to >= m->image_end) FAIL("refers to main+%#llx, outside the game's module", (unsigned long long)to);
            break;
        default: break;
        }
    }
    return 1;
}
