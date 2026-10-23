/* TOTK_MSL_FAST: selective fast math as a text pass over translated MSL, applied right before it is compiled
   (nvn_mtl.m totk_msl_library). The compile options stay MTLMathModeSafe; only chosen operations get the fast:: forms:
     - bit 1 (TOTK_MSL_FAST_DIV, default on with TOTK_MSL_FAST=1): `float NAME = A / B;` -> `float NAME = fast::divide(A, B);`
     - bit 2 (TOTK_MSL_FAST_FUNCS, default on): log2( exp2( sqrt( rsqrt( -> fast::log2( ... in `floatN NAME = ...;` lines
   Only fragment shaders are touched (vertex positions must stay invariant between passes; compute shaders do integer
   work through floats). A value that reaches an integer reinterpretation or conversion stays precise: every identifier
   inside as_type<non-float>(...) / int(...) / uint(...) (and their vectors) is a sink, sinks propagate backwards through
   `NAME = EXPR;` statements until nothing changes, and a statement whose target is a sink is never rewritten. That keeps
   Maxwell's integer-division idiom (`float _a = 1.0 / _b; int _c = as_type<int>(_a) + (-2);`) exactly rounded.
   The parse is conservative: A and B must be single tokens without spaces (SPIRV-Cross spaces every binary operator),
   anything else is left alone. Pure C, no Metal: tests/msl_fast/probe.c runs it on sample snippets. */
#ifndef TOTK_MSL_FAST_H
#define TOTK_MSL_FAST_H
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum { MSLF_DIV = 1, MSLF_FUNCS = 2 };
typedef struct { int divs, divs_kept, calls; } mslf_stats;

/* --- a small string set (identifiers) --- */
typedef struct { char **k; size_t cap, n; } mslf_set;
static size_t mslf_hash(const char *s, size_t n) { size_t h = 1469598103934665603ull; for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)s[i]) * 1099511628211ull; return h; }
static int mslf_set_has(const mslf_set *t, const char *s, size_t n) {
    if (!t->cap) return 0;
    for (size_t i = mslf_hash(s, n) & (t->cap - 1);; i = (i + 1) & (t->cap - 1)) {
        if (!t->k[i]) return 0;
        if (!strncmp(t->k[i], s, n) && !t->k[i][n]) return 1;
    }
}
static int mslf_set_add(mslf_set *t, const char *s, size_t n) {   /* 1 when newly added */
    if (mslf_set_has(t, s, n)) return 0;
    if ((t->n + 1) * 2 > t->cap) {
        mslf_set u = { calloc(t->cap ? t->cap * 2 : 256, sizeof(char *)), t->cap ? t->cap * 2 : 256, t->n };
        for (size_t i = 0; i < t->cap; i++) if (t->k[i]) {
            size_t j = mslf_hash(t->k[i], strlen(t->k[i])) & (u.cap - 1);
            while (u.k[j]) j = (j + 1) & (u.cap - 1);
            u.k[j] = t->k[i];
        }
        free(t->k); *t = u;
    }
    size_t i = mslf_hash(s, n) & (t->cap - 1); while (t->k[i]) i = (i + 1) & (t->cap - 1);
    t->k[i] = malloc(n + 1); memcpy(t->k[i], s, n); t->k[i][n] = 0; t->n++;
    return 1;
}
static void mslf_set_free(mslf_set *t) { for (size_t i = 0; i < t->cap; i++) free(t->k[i]); free(t->k); memset(t, 0, sizeof *t); }

static int mslf_idc(int c) { return isalnum(c) || c == '_'; }
/* Add every identifier of s[0..n) that is not a member name (after '.' or "->") nor part of a number literal. */
static int mslf_add_idents(mslf_set *t, const char *s, size_t n) {
    int added = 0;
    for (size_t i = 0; i < n;) {
        if (isdigit((unsigned char)s[i])) { while (i < n && (mslf_idc((unsigned char)s[i]) || s[i] == '.')) i++; continue; }
        if (!mslf_idc((unsigned char)s[i])) { i++; continue; }
        size_t b = i; while (i < n && mslf_idc((unsigned char)s[i])) i++;
        int member = b > 0 && (s[b - 1] == '.' || (b > 1 && s[b - 1] == '>' && s[b - 2] == '-'));
        if (!member) added += mslf_set_add(t, s + b, i - b);
    }
    return added;
}
/* s points at '('; returns the index just past the matching ')' within n, or 0. */
static size_t mslf_match(const char *s, size_t n) {
    int d = 0;
    for (size_t i = 0; i < n; i++) { if (s[i] == '(') d++; else if (s[i] == ')' && !--d) return i + 1; else if (s[i] == ';') return 0; }
    return 0;
}

/* One statement line: leading space, an optional type, a target NAME (with optional .x / [i] after it), " = ", EXPR, ";".
   Fills the name and the expression; returns 0 when the line is not such a statement. */
typedef struct { const char *ind, *type, *name, *expr; size_t nind, ntype, nname, nexpr; } mslf_stmt;
static int mslf_parse(const char *l, size_t n, mslf_stmt *st) {
    memset(st, 0, sizeof *st);
    size_t i = 0; while (i < n && (l[i] == ' ' || l[i] == '\t')) i++;
    st->ind = l; st->nind = i;
    if (n < i + 5 || l[n - 1] != ';') return 0;
    const char *eq = NULL;
    for (size_t j = i; j + 2 < n; j++) if (l[j] == ' ' && l[j + 1] == '=' && l[j + 2] == ' ') { eq = l + j; break; }
    if (!eq) return 0;
    /* LHS: [type ]target */
    const char *lb = l + i, *le = eq; const char *sp = NULL;
    for (const char *p = lb; p < le; p++) if (*p == ' ') { if (sp) return 0; sp = p; }
    const char *tg = sp ? sp + 1 : lb;
    if (sp) { st->type = lb; st->ntype = (size_t)(sp - lb); }
    if (!(isalpha((unsigned char)*tg) || *tg == '_')) return 0;
    const char *ne = tg; while (ne < le && mslf_idc((unsigned char)*ne)) ne++;
    if (ne < le && *ne != '.' && *ne != '[') return 0;
    st->name = tg; st->nname = (size_t)(ne - tg);
    st->expr = eq + 3; st->nexpr = (size_t)(l + n - 1 - st->expr);
    return st->nexpr > 0;
}
static int mslf_is_float_type(const char *t, size_t n) {
    return (n == 5 && !strncmp(t, "float", 5)) || (n == 6 && !strncmp(t, "float", 5) && t[5] >= '2' && t[5] <= '4');
}
/* A single token without spaces, with balanced () and []: an identifier, a literal, a member/index access chain. */
static int mslf_atom(const char *s, size_t n) {
    if (!n) return 0;
    int p = 0, b = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == ' ' || c == ',' || c == '?' || c == ':' || c == '=' || c == '<' || c == '>' || c == '&' || c == '|' || c == '/' || c == '+' || c == '%' || c == '^' || c == '!') return 0;
        if (c == '*' && !(i > 0 && s[i - 1] == '(')) return 0;   /* only a dereference "(*" */
        if (c == '-' && i > 0) return 0;                          /* only a leading minus */
        if (c == '(') p++; else if (c == ')' && --p < 0) return 0;
        if (c == '[') b++; else if (c == ']' && --b < 0) return 0;
    }
    return !p && !b;
}

/* The sinks: identifiers whose exact bits or integer conversion matter, closed backwards over assignments. */
static void mslf_sinks(const char *src, size_t len, mslf_set *t) {
    for (const char *p = src; (p = strstr(p, "as_type<")) && p < src + len; p++) {
        const char *ty = p + 8;
        if (!strncmp(ty, "float", 5) || !strncmp(ty, "half", 4)) continue;
        const char *gt = strchr(ty, '>'); if (!gt || gt[1] != '(') continue;
        size_t e = mslf_match(gt + 1, (size_t)(src + len - gt - 1)); if (e) mslf_add_idents(t, gt + 1, e);
    }
    static const char *conv[] = {"int(", "uint(", "int2(", "uint2(", "int3(", "uint3(", "int4(", "uint4(", "short(", "ushort(", NULL};
    for (int c = 0; conv[c]; c++) for (const char *p = src; (p = strstr(p, conv[c])) && p < src + len; p++) {
        if (p > src && (mslf_idc((unsigned char)p[-1]))) continue;
        const char *o = p + strlen(conv[c]) - 1;
        size_t e = mslf_match(o, (size_t)(src + len - o)); if (e) mslf_add_idents(t, o, e);
    }
    for (int changed = 1; changed;) {
        changed = 0;
        for (const char *l = src, *end = src + len; l < end;) {
            const char *nl = memchr(l, '\n', (size_t)(end - l)); size_t n = nl ? (size_t)(nl - l) : (size_t)(end - l);
            mslf_stmt st;
            if (mslf_parse(l, n, &st) && mslf_set_has(t, st.name, st.nname)) changed |= mslf_add_idents(t, st.expr, st.nexpr) > 0;
            l += n + 1;
        }
    }
}

typedef struct { char *p; size_t n, cap; } mslf_buf;
static void mslf_put(mslf_buf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) { b->cap = (b->n + n + 1) * 2; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
}

/* Rewrites src (len bytes) with the parts in `mask`. Returns a malloc'd NUL-terminated string, or NULL when nothing
   changed (not a fragment shader, nothing eligible). */
static char *totk_msl_fast_rewrite(const char *src, size_t len, int mask, mslf_stats *stats) {
    mslf_stats s0 = {0}; if (!stats) stats = &s0;
    if (!(mask & (MSLF_DIV | MSLF_FUNCS))) return NULL;
    int frag = 0;
    for (const char *p = src; (p = strstr(p, "fragment ")) && p < src + len; p++) if (p == src || p[-1] == '\n') { frag = 1; break; }
    if (!frag) return NULL;
    mslf_set sinks = {0}; mslf_sinks(src, len, &sinks);
    mslf_buf out = {0}; int changed = 0;
    static const char *fns[] = {"rsqrt(", "sqrt(", "log2(", "exp2(", NULL};
    for (const char *l = src, *end = src + len; l < end;) {
        const char *nl = memchr(l, '\n', (size_t)(end - l)); size_t n = nl ? (size_t)(nl - l) : (size_t)(end - l);
        mslf_stmt st; int done = 0;
        if (mslf_parse(l, n, &st) && st.type && mslf_is_float_type(st.type, st.ntype)) {
            int sink = mslf_set_has(&sinks, st.name, st.nname);
            const char *slash = NULL;
            for (size_t j = 0; j + 2 < st.nexpr; j++) if (st.expr[j] == ' ' && st.expr[j + 1] == '/' && st.expr[j + 2] == ' ') { slash = st.expr + j; break; }
            if ((mask & MSLF_DIV) && st.ntype == 5 && st.name + st.nname == st.expr - 3 && slash
                && mslf_atom(st.expr, (size_t)(slash - st.expr)) && mslf_atom(slash + 3, (size_t)(st.expr + st.nexpr - slash - 3))) {
                if (sink) stats->divs_kept++;
                else {
                    mslf_put(&out, l, (size_t)(st.expr - l)); mslf_put(&out, "fast::divide(", 13);
                    mslf_put(&out, st.expr, (size_t)(slash - st.expr)); mslf_put(&out, ", ", 2);
                    mslf_put(&out, slash + 3, (size_t)(st.expr + st.nexpr - slash - 3)); mslf_put(&out, ");", 2);
                    stats->divs++; done = changed = 1;
                }
            } else if ((mask & MSLF_FUNCS) && !sink) {
                size_t start = out.n; int any = 0;
                mslf_put(&out, l, (size_t)(st.expr - l));
                for (size_t j = 0; j < st.nexpr;) {
                    int hit = 0;
                    const char *q = st.expr + j;
                    if (!(q > st.expr && (mslf_idc((unsigned char)q[-1]) || q[-1] == ':' || q[-1] == '.')))
                        for (int f = 0; fns[f]; f++) if (!strncmp(q, fns[f], strlen(fns[f]))) {
                            mslf_put(&out, "fast::", 6); mslf_put(&out, fns[f], strlen(fns[f])); j += strlen(fns[f]); hit = 1; any++; break; }
                    if (!hit) { mslf_put(&out, q, 1); j++; }
                }
                mslf_put(&out, ";", 1);
                if (any) { stats->calls += any; done = changed = 1; } else out.n = start;
            }
        }
        if (!done) mslf_put(&out, l, n);
        if (nl) mslf_put(&out, "\n", 1);
        l += n + 1;
    }
    mslf_set_free(&sinks);
    if (!changed) { free(out.p); return NULL; }
    return out.p;
}
#endif
