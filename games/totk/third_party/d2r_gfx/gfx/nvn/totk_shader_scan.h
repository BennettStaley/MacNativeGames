/* The shaders in a game file, for the shared shader list (totk_shader_list.m): the same containers
   scripts/prepare_shader_seed.py reads at build time, parsed natively so that the app can prepare a variant a downloaded
   list names from the player's own game files, without Python.
     BNSH  NVN shader files (alone, in BFSHA archives, and embedded in effect / layout resources and the game module)
     BAHS  agl's shader packages (.sharcb): the engine's own shaders (title screen, post effects, lighting)
     SARC  packages, entered up to 4 deep; a zstd member is decompressed through the caller's function
   Every shader is reported with its NVN control section (the first 0x880 bytes, what the runtime's key hashes), its code
   (length from control+0x704) and where both sit in the buffer that holds them. Out-of-range records are skipped, never
   read. Plain C, header only: tests/shader_list_fetch/probe.m checks it on synthetic files. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

enum { TSS_CONTROL = 0x880, TSS_MAX_CODE = 16 << 20, TSS_MAX_DEPTH = 4 };

/* One shader: source (a RomFS path), members (the SARC members entered, joined by '|', "" for none), stage 0 vertex /
   1 fragment / 5 compute, the offsets of its control section and code in that member's bytes, the code length. */
typedef void (*tss_found)(void *ctx, const char *source, const char *members, int stage, const uint8_t *control,
                          const uint8_t *code, uint32_t length, size_t control_off, size_t code_off);
/* Decompresses a zstd frame (malloc'd result, NULL when it does not decode). */
typedef uint8_t *(*tss_inflate)(const uint8_t *src, size_t n, size_t *out);
typedef struct { tss_found found; tss_inflate inflate; void *ctx; unsigned skipped; } tss_scan;

static inline uint32_t tss_le32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint64_t tss_le64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static inline int tss_range(size_t n, uint64_t off, uint64_t len) { return off <= n && len <= n - off; }
static inline int tss_is_zstd(const uint8_t *d, size_t n) { return n >= 4 && d[0] == 0x28 && d[1] == 0xb5 && d[2] == 0x2f && d[3] == 0xfd; }

/* One BNSH file at d[0..n) (n: what remains of the buffer); base: its offset in the buffer. Returns the BNSH's size, or
   0 when it is not a complete BNSH file. BNSH offsets are relative to the BNSH file itself. */
static size_t tss_bnsh(tss_scan *s, const char *source, const char *members, const uint8_t *d, size_t n, size_t base) {
    if (n < 136 || memcmp(d, "BNSH", 4) || d[12] != 0xff || d[13] != 0xfe || memcmp(d + 96, "grsc", 4)) return 0;
    uint32_t size = tss_le32(d + 28);
    if (size < 136 || size > n) return 0;
    uint32_t count = tss_le32(d + 124); uint64_t variations = tss_le64(d + 128);
    if (count > 65536 || !tss_range(size, variations, (uint64_t)count * 64)) { s->skipped++; return size; }
    static const struct { int stage; unsigned rel; } stages[] = {{0, 8}, {1, 40}, {5, 48}};
    for (uint32_t i = 0; i < count; i++) {
        uint64_t program = tss_le64(d + variations + (uint64_t)i * 64 + 16);
        if (!tss_range(size, program, 56)) { s->skipped++; continue; }
        for (unsigned k = 0; k < 3; k++) {
            uint64_t at = tss_le64(d + program + stages[k].rel);
            if (!at) continue;
            if (!tss_range(size, at + 8, 24)) { s->skipped++; continue; }
            uint64_t control_off = tss_le64(d + at + 8), code_off = tss_le64(d + at + 16);
            uint32_t code_size = tss_le32(d + at + 24), control_size = tss_le32(d + at + 28);
            if (!tss_range(size, control_off, control_size) || !tss_range(size, code_off, code_size)) { s->skipped++; continue; }
            if (control_size < TSS_CONTROL) continue;   /* the SDK's utility BNSH: an older, shorter control layout */
            const uint8_t *control = d + control_off;
            uint32_t c1 = tss_le32(control + 0x700), length = tss_le32(control + 0x704), actual = tss_le32(control + 0x714);
            if ((int)actual != stages[k].stage || !length || c1 > length || length > code_size || length > TSS_MAX_CODE) { s->skipped++; continue; }
            s->found(s->ctx, source, members, stages[k].stage, control, d + code_off, length, base + control_off, base + code_off);
        }
    }
    return size;
}

/* An agl shader package (BAHS): records between the header's start and the code pool are <u64 code offset in the pool,
   u32 control size (0x880 or 0x88c), i32 offset of the control from the record>; records may share a control. */
static void tss_sharcb(tss_scan *s, const char *source, const char *members, const uint8_t *d, size_t n) {
    if (n < 32 || memcmp(d, "BAHS", 4)) return;
    uint32_t start = tss_le32(d + 20), pool_size = tss_le32(d + 24), pool = tss_le32(d + 28);
    if (start < 32 || start > pool || (uint64_t)pool + pool_size > n) return;
    static const uint8_t sizes[2][4] = {{0x80, 0x08, 0, 0}, {0x8c, 0x08, 0, 0}};
    for (int z = 0; z < 2; z++) {
        for (size_t at = start; at + 4 <= pool; at++) {
            if (memcmp(d + at, sizes[z], 4)) continue;
            if (at < 8) continue;
            size_t record = at - 8;
            if (record < start || record % 4 || record + 16 > n) continue;
            uint64_t code_off = tss_le64(d + record); int32_t rel; memcpy(&rel, d + record + 12, 4);
            int64_t control_at = (int64_t)record + rel;
            if (control_at < (int64_t)start || control_at > (int64_t)pool - TSS_CONTROL || code_off >= pool_size) continue;
            const uint8_t *control = d + control_at;
            uint32_t c1 = tss_le32(control + 0x700), length = tss_le32(control + 0x704), st = tss_le32(control + 0x714);
            if ((st != 0 && st != 1 && st != 5) || !length || length > pool_size - code_off || c1 > length || length > TSS_MAX_CODE) continue;
            /* the same shader again (a shared control and code) is reported again; the caller keeps one per key */
            s->found(s->ctx, source, members, (int)st, control, d + pool + code_off, length, (size_t)control_at, (size_t)(pool + code_off));
        }
    }
}

typedef struct { const char *name; const uint8_t *data; size_t n; } tss_member;
/* A SARC package's members (little-endian); returns the count (members has room for cap), -1 when it is not a valid
   SARC. Names come from the string table and must end inside it. */
static int tss_sarc(const uint8_t *d, size_t n, tss_member *members, int cap) {
    if (n < 0x20 || memcmp(d, "SARC", 4) || d[6] != 0xff || d[7] != 0xfe) return -1;
    uint32_t header = (uint32_t)d[4] | (uint32_t)d[5] << 8, start = tss_le32(d + 12);
    if (header < 20 || !tss_range(n, header, 12) || memcmp(d + header, "SFAT", 4) || start > n) return -1;
    unsigned count = (unsigned)d[header + 6] | (unsigned)d[header + 7] << 8;
    size_t names = header + 12 + (size_t)count * 16;
    if (!tss_range(n, names, 8) || memcmp(d + names, "SFNT", 4)) return -1;
    names += 8;
    if ((int)count > cap) return -1;
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *node = d + header + 12 + (size_t)i * 16;
        uint32_t attr = tss_le32(node + 4), lo = tss_le32(node + 8), hi = tss_le32(node + 12);
        size_t at = names + (size_t)(attr & 0xffffff) * 4;
        if (attr >> 24 != 1 || at >= start || hi < lo || !tss_range(n, (uint64_t)start + lo, hi - lo)) return -1;
        const uint8_t *end = memchr(d + at, 0, start - at);
        if (!end || end == d + at) return -1;
        members[i] = (tss_member){(const char *)d + at, d + start + lo, hi - lo};
    }
    return (int)count;
}

/* Everything in one buffer: a BAHS package, a SARC (members entered), or any bytes holding complete BNSH files. */
static void tss_visit(tss_scan *s, const char *source, const char *members, const uint8_t *d, size_t n, int depth) {
    if (n >= 4 && !memcmp(d, "BAHS", 4)) { tss_sharcb(s, source, members, d, n); return; }
    if (n >= 4 && !memcmp(d, "SARC", 4)) {
        if (depth >= TSS_MAX_DEPTH) return;
        enum { CAP = 16384 };
        tss_member *list = malloc(CAP * sizeof *list);
        int count = list ? tss_sarc(d, n, list, CAP) : -1;
        for (int i = 0; i < count; i++) {
            size_t ml = strlen(members), nl = strlen(list[i].name);
            if (strchr(list[i].name, '|') || ml + nl + 2 > 4096) { s->skipped++; continue; }
            char path[4096]; snprintf(path, sizeof path, "%s%s%s", members, ml ? "|" : "", list[i].name);
            if (tss_is_zstd(list[i].data, list[i].n)) {
                size_t m = 0; uint8_t *raw = s->inflate ? s->inflate(list[i].data, list[i].n, &m) : NULL;
                if (raw) tss_visit(s, source, path, raw, m, depth + 1); else s->skipped++;
                free(raw);
            } else tss_visit(s, source, path, list[i].data, list[i].n, depth + 1);
        }
        free(list);
        return;
    }
    static const uint8_t magic[8] = {'B', 'N', 'S', 'H', 0, 0, 0, 0};
    for (size_t pos = 0; pos + 8 <= n; ) {
        const uint8_t *hit = memmem(d + pos, n - pos, magic, 8);
        if (!hit) break;
        pos = (size_t)(hit - d);
        size_t size = tss_bnsh(s, source, members, hit, n - pos, pos);
        pos += size > 8 ? size : 8;
    }
}

/* A whole game file (raw bytes as stored): zstd decompressed first. Only containers can hold shaders. */
static void tss_file(tss_scan *s, const char *source, const uint8_t *raw, size_t n) {
    if (tss_is_zstd(raw, n)) {
        size_t m = 0; uint8_t *d = s->inflate ? s->inflate(raw, n, &m) : NULL;
        if (d) tss_visit(s, source, "", d, m, 0); else s->skipped++;
        free(d);
        return;
    }
    tss_visit(s, source, "", raw, n, 0);
}

/* The bytes of a member chain ("a|b", "" for the file itself) inside a decompressed file: a pointer into data or into a
   buffer the caller frees (*owned), NULL when a member is not there. */
static const uint8_t *tss_member_bytes(tss_inflate inflate, const uint8_t *data, size_t n, const char *members, size_t *out, uint8_t **owned) {
    *owned = NULL;
    const uint8_t *cur = data; size_t cn = n;
    char chain[4096]; snprintf(chain, sizeof chain, "%s", members);
    for (char *part = *chain ? chain : NULL, *next; part; part = next) {
        char *bar = strchr(part, '|'); next = bar ? bar + 1 : NULL; if (bar) *bar = 0;
        enum { CAP = 16384 };
        tss_member *list = malloc(CAP * sizeof *list);
        int count = list ? tss_sarc(cur, cn, list, CAP) : -1, found = -1;
        for (int i = 0; i < count; i++) if (!strcmp(list[i].name, part)) { found = i; break; }
        if (found < 0) { free(list); free(*owned); *owned = NULL; return NULL; }
        const uint8_t *md = list[found].data; size_t mn = list[found].n; free(list);
        if (tss_is_zstd(md, mn)) {
            size_t m = 0; uint8_t *raw = inflate ? inflate(md, mn, &m) : NULL;
            free(*owned); *owned = raw;
            if (!raw) return NULL;
            cur = raw; cn = m;
        } else { cur = md; cn = mn; }
    }
    *out = cn;
    return cur;
}
