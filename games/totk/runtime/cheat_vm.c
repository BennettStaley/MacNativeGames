#include "cheat_vm.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Encodings follow Atmosphère's docs/features/cheats.md. Nibbles are numbered from the most significant: in "5TMRI0AA"
   nibble 1 is T (width), 2 M (memory type), 3 R (register)... Where the document leaves a detail open, the behaviour is
   noted at the instruction (and matches Ryujinx's MIT-licensed Tamper implementation). */
#define NIB(x, i) (((x) >> (28 - 4 * (i))) & 0xF)

static int set_err(char *err, size_t errn, const char *fmt, unsigned a, uint32_t b) {
    if (err && errn) snprintf(err, errn, fmt, a, b);
    return 0;
}

/* --- parsing ------------------------------------------------------------------------------------------------------ */
static int is_comment(const char *p, const char *end) { return *p == '#' || (*p == '/' && p + 1 < end && p[1] == '/'); }
static void set_name(Cheat *c, const char *p, const char *end) {
    while (p < end && isspace((unsigned char)*p)) p++;
    while (end > p && isspace((unsigned char)end[-1])) end--;
    size_t len = (size_t)(end - p), room = sizeof c->name - 8;   // room for a " (123)" suffix
    if (len > room) {   // cut at a UTF-8 character boundary
        len = room;
        while (len && ((unsigned char)p[len] & 0xC0) == 0x80) len--;
    }
    for (size_t i = 0; i < len; i++) c->name[i] = (unsigned char)p[i] < 0x20 ? ' ' : p[i];
    c->name[len] = 0;
    if (!len) snprintf(c->name, sizeof c->name, "(unnamed)");
}
static int name_taken(const Cheat *out, int count, int self, const char *name) {
    for (int j = 0; j < count; j++) if (j != self && !strcmp(out[j].name, name)) return 1;
    return 0;
}

int cheat_parse(const char *s, Cheat *out, int max, char *warn, size_t warnn) {
    if (warn && warnn) warn[0] = 0;
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;   // UTF-8 BOM
    int count = 0, dropped = 0;
    unsigned line = 0, stray = 0, stray_line = 0;
    Cheat *cur = NULL;
    while (*s) {
        const char *e = s;
        while (*e && *e != '\n') e++;
        const char *p = s, *end = e;
        s = *e ? e + 1 : e;
        line++;
        while (end > p && isspace((unsigned char)end[-1])) end--;   // also the \r of CRLF
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p == end || is_comment(p, end)) continue;
        if (*p == '[' || *p == '{') {
            char close = *p == '[' ? ']' : '}';
            const char *q = memchr(p + 1, close, (size_t)(end - p - 1));
            if (count == max || dropped) { dropped++; cur = NULL; continue; }
            cur = &out[count++];
            memset(cur, 0, sizeof *cur);
            cur->line = line;
            cur->master = close == '}';
            set_name(cur, p + 1, q ? q : end);
            if (!q) snprintf(cur->error, sizeof cur->error, "line %u: the section name is not closed with '%c'", line, close);
            p = q ? q + 1 : end;
        }
        while (p < end) {   // the words of this line
            while (p < end && isspace((unsigned char)*p)) p++;
            if (p == end || is_comment(p, end)) break;
            const char *t = p;
            while (p < end && !isspace((unsigned char)*p)) p++;
            if (dropped) continue;
            if (!cur) { if (!stray++) stray_line = line; continue; }
            if (cur->error[0]) continue;
            unsigned len = (unsigned)(p - t), hex = 0;
            uint32_t v = 0;
            while (hex < len && hex < 9 && isxdigit((unsigned char)t[hex])) {
                char c = t[hex++];
                v = v << 4 | (uint32_t)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
            }
            if (len != 8 || hex != 8)
                snprintf(cur->error, sizeof cur->error, "line %u: \"%.*s\" is not an 8-digit hex word", line, len > 24 ? 24 : (int)len, t);
            else if (cur->n == CHEAT_MAX_WORDS)
                snprintf(cur->error, sizeof cur->error, "more than %u instruction words", CHEAT_MAX_WORDS);
            else cur->w[cur->n++] = v;
        }
    }
    for (int k = 0; k < count; k++) {
        if (name_taken(out, k, -1, out[k].name)) {   // an earlier section has this name: number this one
            char base[CHEAT_NAME];
            snprintf(base, sizeof base, "%s", out[k].name);
            for (unsigned d = 2; d < 1000; d++) {
                snprintf(out[k].name, sizeof out[k].name, "%s (%u)", base, d);
                if (!name_taken(out, count, k, out[k].name)) break;
            }
        }
        if (!out[k].error[0] && out[k].n) {
            char err[CHEAT_ERR];
            if (!cheat_check(out[k].w, out[k].n, err, sizeof err)) snprintf(out[k].error, sizeof out[k].error, "%s", err);
        }
    }
    if (warn && warnn) {
        size_t at = 0;
        if (stray) at += (size_t)snprintf(warn, warnn, "%u words before the first [name] ignored (line %u)", stray, stray_line);
        if (dropped && at < warnn) snprintf(warn + at, warnn - at, "%s%d sections after the first %d ignored", at ? "; " : "", dropped, max);
    }
    return count;
}

/* --- decoding ----------------------------------------------------------------------------------------------------- */
unsigned cheat_insn_len(const uint32_t *w, unsigned n, unsigned i) {
    if (i >= n) return 0;
    uint32_t x = w[i];
    unsigned t = NIB(x, 1), len;
    switch (NIB(x, 0)) {
    case 0x0: case 0x1: len = t == 8 ? 4 : 3; break;
    case 0x2: len = 1; break;
    case 0x3: if (t > 1) return 0; len = t == 0 ? 2 : 1; break;   // 300R0000 VVVVVVVV start / 310R0000 end
    case 0x4: len = 3; break;
    case 0x5: len = 2; break;
    case 0x6: len = 3; break;
    case 0x7: len = 2; break;
    case 0x8: len = 1; break;
    case 0x9: len = NIB(x, 5) ? (t == 8 ? 3 : 2) : 1; break;
    case 0xA: { unsigned o = NIB(x, 5); len = (o == 2 || o == 4 || o == 5) ? 2 : 1; break; }
    case 0xC:
        switch (t) {
        case 0:   // C0TcSX..
            switch (NIB(x, 5)) {
            case 0: case 2: len = 2; break;
            case 1: case 3: case 5: len = 1; break;
            case 4: len = NIB(x, 2) == 8 ? 3 : 2; break;
            default: return 0;
            }
            break;
        case 1: case 2: case 3: len = 1; break;   // C1 / C2 / C3
        case 4: len = 3; break;                   // C4r00000 kkkkkkkk kkkkkkkk
        default: return 0;
        }
        break;
    case 0xF:
        if (t != 0xF) return 0;
        switch (NIB(x, 2)) {
        case 0: case 1: len = 1; break;   // FF0 pause / FF1 resume
        case 0xF: { unsigned k = NIB(x, 5); if (k > 4) return 0; len = (k == 0 || k == 2) ? 2 : 1; break; }   // FFF debug log
        default: return 0;
        }
        break;
    default: return 0;   // 0xB reserved, 0xD/0xE unassigned
    }
    return i + len <= n ? len : 0;
}

static int width_ok(unsigned t) { return t == 1 || t == 2 || t == 4 || t == 8; }
static int cond_ok(unsigned c) { return c >= 1 && c <= 6; }

int cheat_check(const uint32_t *w, unsigned n, char *err, size_t errn) {
    for (unsigned i = 0; i < n;) {
        unsigned len = cheat_insn_len(w, n, i);
        if (!len) return set_err(err, errn, i < n && cheat_insn_len(w, ~0u, i) ? "word %u: instruction %08x is cut off" : "word %u: unknown instruction %08x", i, w[i]);
        uint32_t x = w[i];
        unsigned t = NIB(x, 1);
        int ok = 1;
        switch (NIB(x, 0)) {
        case 0x0: ok = width_ok(t) && NIB(x, 2) <= 4; break;
        case 0x1: ok = width_ok(t) && NIB(x, 2) <= 4 && cond_ok(NIB(x, 3)) && NIB(x, 4) <= 1; break;
        case 0x2: ok = t <= 1; break;
        case 0x3: ok = t == 1 || w[i + 1] != 0; break;   // a loop of 0 iterations would run 2^64 times
        case 0x5: ok = width_ok(t) && NIB(x, 2) <= 4 && NIB(x, 4) <= 3; break;
        case 0x6: case 0xA:
            ok = width_ok(t);
            if (NIB(x, 0) == 0xA) { unsigned o = NIB(x, 5); ok = ok && o <= 5 && (o < 3 || NIB(x, 6) <= 4); }
            break;
        case 0x7: ok = width_ok(t) && NIB(x, 4) <= 4; break;
        case 0x9: { unsigned op = NIB(x, 2); ok = width_ok(t) && op <= 13 && (op < 10 || t == 4 || t == 8) && NIB(x, 5) <= 1; break; }
        case 0xC:
            if (t == 0) {
                unsigned kind = NIB(x, 5);
                ok = width_ok(NIB(x, 2)) && cond_ok(NIB(x, 3)) && (kind > 1 || NIB(x, 6) <= 4);
            } else if (t == 1) ok = NIB(x, 6) <= 3;
            else if (t == 2) ok = NIB(x, 2) <= 3;
            else if (t == 4) ok = NIB(x, 2) <= 1;
            break;
        default: break;
        }
        if (!ok) return set_err(err, errn, "word %u: unsupported or malformed instruction %08x", i, x);
        i += len;
    }
    return 1;
}

/* --- execution ---------------------------------------------------------------------------------------------------- */
static uint64_t mask(uint64_t v, unsigned t) { return t == 8 ? v : v & ((1ull << (8 * t)) - 1); }
static int compare(uint64_t a, unsigned cond, uint64_t b) {
    switch (cond) {
    case 1: return a > b;
    case 2: return a >= b;
    case 3: return a < b;
    case 4: return a <= b;
    case 5: return a == b;
    default: return a != b;   // 6 (cheat_check refused the rest)
    }
}
static float f32(uint64_t v) { uint32_t b = (uint32_t)v; float f; memcpy(&f, &b, 4); return f; }
static uint64_t bits32(float f) { uint32_t b; memcpy(&b, &f, 4); return b; }
static double f64(uint64_t v) { double d; memcpy(&d, &v, 8); return d; }
static uint64_t bits64(double d) { uint64_t b; memcpy(&b, &d, 8); return b; }
/* Integer operations 0-9 (types 7 and 9), float operations 10-13 (type 9: T 4 single, T 8 double). */
static int arith(uint64_t a, unsigned op, uint64_t b, unsigned t, uint64_t *out) {
    switch (op) {
    case 0: *out = a + b; break;
    case 1: *out = a - b; break;
    case 2: *out = a * b; break;
    case 3: *out = b < 64 ? a << b : 0; break;
    case 4: *out = b < 64 ? a >> b : 0; break;
    case 5: *out = a & b; break;
    case 6: *out = a | b; break;
    case 7: *out = ~a; break;
    case 8: *out = a ^ b; break;
    case 9: *out = a; break;
    case 10: case 11: case 12: case 13:
        if (t == 4) {
            float x = f32(a), y = f32(b);
            *out = bits32(op == 10 ? x + y : op == 11 ? x - y : op == 12 ? x * y : x / y);
        } else if (t == 8) {
            double x = f64(a), y = f64(b);
            *out = bits64(op == 10 ? x + y : op == 11 ? x - y : op == 12 ? x * y : x / y);
        } else return 0;
        return 1;
    default: return 0;
    }
    *out = mask(*out, t);
    return 1;
}
static int opens_block(uint32_t x) { unsigned op = NIB(x, 0); return op == 1 || op == 8 || (op == 0xC && (NIB(x, 1) == 0 || NIB(x, 1) == 4)); }

/* From the instruction after a failed condition (or a taken else), skips to the matching end (2 0) or, if stop_at_else,
   the matching else (2 1). Returns the index after it; the program's end closes every open block. */
static unsigned skip_block(const Cheat *c, unsigned i, int stop_at_else) {
    int depth = 0;
    while (i < c->n) {
        unsigned len = cheat_insn_len(c->w, c->n, i);
        if (!len) return c->n;
        uint32_t x = c->w[i];
        if (opens_block(x)) depth++;
        else if (NIB(x, 0) == 2) {
            if (depth == 0 && (NIB(x, 1) == 0 || stop_at_else)) return i + len;
            if (NIB(x, 1) == 0) depth--;
        }
        i += len;
    }
    return c->n;
}

void cheat_vm_tick(CheatVm *vm) {
    memset(vm->reg, 0, sizeof vm->reg);
    memset(vm->saved, 0, sizeof vm->saved);
}

int cheat_run(Cheat *c, CheatVm *vm, const CheatEnv *env, char *err, size_t errn) {
    uint64_t *reg = vm->reg, *saved = vm->saved;
    unsigned loop_top[16] = {0};
    unsigned steps = 0, max_steps = env->max_steps ? env->max_steps : CHEAT_STEPS, i = 0;
    if (err && errn) err[0] = 0;
    while (i < c->n) {
        if (++steps > max_steps) return set_err(err, errn, "more than %u steps in one run (endless loop?)%s", max_steps, 0);
        unsigned len = cheat_insn_len(c->w, c->n, i);
        if (!len) goto bad;
        const uint32_t *w = &c->w[i];
        uint32_t x = w[0];
        unsigned t = NIB(x, 1), next = i + len;
        switch (NIB(x, 0)) {
        case 0x0: {   // 0TMR00AA AAAAAAAA VVVVVVVV (VVVVVVVV): [base(M) + reg[R] + A] = V
            unsigned m = NIB(x, 2), r = NIB(x, 3);
            if (!width_ok(t) || m > 4) goto bad;
            uint64_t addr = env->base[m] + reg[r] + ((uint64_t)(x & 0xFF) << 32 | w[1]);
            uint64_t v = t == 8 ? (uint64_t)w[2] << 32 | w[3] : w[2];
            if (!env->write(env->ctx, addr, &v, t)) return set_err(err, errn, "write refused at word %u%s", i, 0);
            break;
        }
        case 0x1: {   // 1TMCXrAA AAAAAAAA VVVVVVVV (VVVVVVVV): if [base(M) (+ reg[r] when X = 1) + A] <C> V
            unsigned m = NIB(x, 2), cond = NIB(x, 3), kind = NIB(x, 4), r = NIB(x, 5);
            if (!width_ok(t) || m > 4 || !cond_ok(cond) || kind > 1) goto bad;
            uint64_t v = 0, want = t == 8 ? (uint64_t)w[2] << 32 | w[3] : w[2];
            uint64_t addr = env->base[m] + (kind ? reg[r] : 0) + ((uint64_t)(x & 0xFF) << 32 | w[1]);
            if (!env->read(env->ctx, addr, &v, t)) return 0;
            if (!compare(mask(v, t), cond, mask(want, t))) next = skip_block(c, next, 1);
            break;
        }
        case 0x2:   // 20000000 end of conditional; 21000000 else: reached while executing, so skip the else branch
            if (t > 1) goto bad;
            if (t == 1) next = skip_block(c, next, 0);
            break;
        case 0x3: {   // 300R0000 VVVVVVVV loop start (reg[R] = count) / 310R0000 loop end
            unsigned r = NIB(x, 3);
            if (t == 0) { if (!w[1]) goto bad; reg[r] = w[1]; loop_top[r] = next; }
            else if (!loop_top[r]) goto bad;
            else if (--reg[r]) next = loop_top[r];
            break;
        }
        case 0x4: reg[NIB(x, 3)] = (uint64_t)w[1] << 32 | w[2]; break;   // 400R0000 VVVVVVVV VVVVVVVV
        case 0x5: {   // 5TMRI.AA AAAAAAAA: reg[R] = [address], address by I (S = nibble 5):
                      //   0: base(M) + A; 1: reg[R] + A; 2: reg[S] + A; 3: base(M) + reg[S] + A.
                      // The value is zero-extended from the width (the document does not say; Ryujinx does the same).
            unsigned m = NIB(x, 2), r = NIB(x, 3), from = NIB(x, 4), s = NIB(x, 5);
            if (!width_ok(t) || m > 4 || from > 3) goto bad;
            uint64_t rel = (uint64_t)(x & 0xFF) << 32 | w[1], v = 0;
            uint64_t addr = from == 0 ? env->base[m] + rel : from == 1 ? reg[r] + rel : from == 2 ? reg[s] + rel : env->base[m] + reg[s] + rel;
            if (!env->read(env->ctx, addr, &v, t)) return 0;
            reg[r] = mask(v, t);
            break;
        }
        case 0x6: {   // 6T0RIor0 VVVVVVVV VVVVVVVV: [reg[R] (+ reg[r])] = value; I: reg[R] += width
            unsigned r = NIB(x, 3), inc = NIB(x, 4), useofs = NIB(x, 5), ro = NIB(x, 6);
            if (!width_ok(t)) goto bad;
            uint64_t v = (uint64_t)w[1] << 32 | w[2];
            if (!env->write(env->ctx, reg[r] + (useofs ? reg[ro] : 0), &v, t)) return set_err(err, errn, "write refused at word %u%s", i, 0);
            if (inc) reg[r] += t;
            break;
        }
        case 0x7: {   // 7T0RC000 VVVVVVVV: reg[R] = reg[R] <op> value (legacy: add sub mul lsl lsr)
            unsigned r = NIB(x, 3), op = NIB(x, 4);
            if (!width_ok(t) || op > 4 || !arith(reg[r], op, w[1], t, &reg[r])) goto bad;
            break;
        }
        case 0x8:   // 8kkkkkkk: if every button in the mask is held
            if ((env->keys & (x & 0x0FFFFFFF)) != (x & 0x0FFFFFFF)) next = skip_block(c, next, 1);
            break;
        case 0x9: {   // 9TCRS0s0: reg[R] = reg[S] <op> reg[s]; 9TCRS100 VVVVVVVV (VVVVVVVV): reg[R] = reg[S] <op> value
            unsigned op = NIB(x, 2), r = NIB(x, 3), s = NIB(x, 4);
            if (!width_ok(t)) goto bad;
            uint64_t b = NIB(x, 5) ? (t == 8 ? (uint64_t)w[1] << 32 | w[2] : w[1]) : reg[NIB(x, 6)];
            if (!arith(reg[s], op, b, t, &reg[r])) goto bad;
            break;
        }
        case 0xA: {   // ATSRIOxa (aaaaaaaa): [address] = reg[S]; I: reg[R] += width
            unsigned s = NIB(x, 2), r = NIB(x, 3), inc = NIB(x, 4), o = NIB(x, 5), xx = NIB(x, 6);
            if (!width_ok(t)) goto bad;
            uint64_t rel = len == 2 ? (uint64_t)(x & 0xF) << 32 | w[1] : 0, addr;
            switch (o) {
            case 0: addr = reg[r]; break;
            case 1: addr = reg[r] + reg[xx]; break;
            case 2: addr = reg[r] + rel; break;
            case 3: if (xx > 4) goto bad; addr = env->base[xx] + reg[r]; break;
            case 4: if (xx > 4) goto bad; addr = env->base[xx] + rel; break;
            case 5: if (xx > 4) goto bad; addr = env->base[xx] + reg[r] + rel; break;
            default: goto bad;
            }
            uint64_t v = reg[s];
            if (!env->write(env->ctx, addr, &v, t)) return set_err(err, errn, "write refused at word %u%s", i, 0);
            if (inc) reg[r] += t;
            break;
        }
        case 0xC:
            if (t == 0) {   // C0TcSX..: if reg[S] <cond> operand
                unsigned wt = NIB(x, 2), cond = NIB(x, 3), s = NIB(x, 4), kind = NIB(x, 5), a = NIB(x, 6), b = NIB(x, 7);
                if (!width_ok(wt) || !cond_ok(cond)) goto bad;
                uint64_t rel = len == 2 && kind != 4 ? (uint64_t)b << 32 | w[1] : 0, v = 0;
                switch (kind) {
                case 0: if (a > 4) goto bad; if (!env->read(env->ctx, env->base[a] + rel, &v, wt)) return 0; break;
                case 1: if (a > 4) goto bad; if (!env->read(env->ctx, env->base[a] + reg[b], &v, wt)) return 0; break;
                case 2: if (!env->read(env->ctx, reg[a] + rel, &v, wt)) return 0; break;
                case 3: if (!env->read(env->ctx, reg[a] + reg[b], &v, wt)) return 0; break;
                case 4: v = wt == 8 ? (uint64_t)w[1] << 32 | w[2] : w[1]; break;
                case 5: v = reg[a]; break;
                default: goto bad;
                }
                if (!compare(mask(reg[s], wt), cond, mask(v, wt))) next = skip_block(c, next, 1);
            } else if (t == 1) {   // C10D0Sx0: restore (x=0) / save (x=1) / clear saved (x=2) / clear register (x=3)
                unsigned d = NIB(x, 3), s = NIB(x, 5), op = NIB(x, 6);
                if (op == 0) reg[d] = saved[s]; else if (op == 1) saved[d] = reg[s]; else if (op == 2) saved[d] = 0; else if (op == 3) reg[d] = 0; else goto bad;
            } else if (t == 2) {   // C2x0XXXX: the same for every register in the mask
                unsigned op = NIB(x, 2);
                if (op > 3) goto bad;
                for (unsigned k = 0; k < 16; k++) if (x >> k & 1) {
                    if (op == 0) reg[k] = saved[k]; else if (op == 1) saved[k] = reg[k]; else if (op == 2) saved[k] = 0; else reg[k] = 0;
                }
            } else if (t == 3) {   // C3000XXx: static register XX < 0x80 read into reg[x], XX >= 0x80 written from reg[x]
                unsigned st = x >> 4 & 0xFF, r = x & 0xF;
                if (st < 0x80) reg[r] = vm->statics[st]; else vm->statics[st] = reg[r];
            } else if (t == 4) {   // C4r00000 kkkkkkkk kkkkkkkk: if every button in the 64-bit mask is held
                unsigned repeat = NIB(x, 2);
                uint64_t want = (uint64_t)w[1] << 32 | w[2], bit = 1ull << (i & 63);
                uint64_t *latch = &c->latch[i / 64];
                int held = want && (env->keys & want) == want;
                if (repeat > 1) goto bad;
                // auto-repeat 0: enter once per press; the mask must stop matching before it fires again
                int enter = held && (repeat || !(*latch & bit));
                if (held) *latch |= bit; else *latch &= ~bit;
                if (!enter) next = skip_block(c, next, 1);
            } else goto bad;
            break;
        case 0xF:   // FF0 pause / FF1 resume / FFF debug log: nothing to do here
            if (t != 0xF) goto bad;
            break;
        default:
            goto bad;
        }
        i = next;
    }
    return 1;
bad:
    return set_err(err, errn, "word %u: unsupported or malformed instruction %08x", i, i < c->n ? c->w[i] : 0);
}
