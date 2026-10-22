/* The community cheat list (docs/settings.md, Cheats): the Mac app downloads sthetix/nx-cheats-db's file for this title,
   cheats/<title ID>.json, and keeps the part for the running build as an Atmosphère cheat file (<build ID>.txt in the
   cheats-db folder), which runtime/cheats.c reads like any other. No codes are in this repository.

   The database file is one JSON object: {"<build ID>": {"[Name]": "[Name]\n<words>\n", ...}, ..., "attribution": {...}}.
   Each value is a whole section, header included; the object keeps the file's order (group markers and master codes depend
   on it), which is why this reads the JSON itself instead of through a dictionary. The download is untrusted: strict JSON
   (RFC 8259 strings, nesting at most 32 deep), and anything else rejects the whole file (the cached list stays).
   Plain C, header only, so tests/test_cheat_catalog.c builds it. */
#pragma once
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

enum { CHEAT_DB_MAX_JSON = 16 << 20, CHEAT_DB_MAX_TEXT = (1 << 20) - 1 /* runtime/cheats.c reads at most 1 MB */ };

typedef struct { const char *p, *end; const char *err; } CheatJson;

static inline void cdb_ws(CheatJson *j) { while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++; }
static inline int cdb_fail(CheatJson *j, const char *why) { if (!j->err) j->err = why; return 0; }
static inline int cdb_hex4(const char *p, unsigned *v) {
    *v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        if (!isxdigit((unsigned char)c)) return 0;
        *v = *v << 4 | (unsigned)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    return 1;
}
/* A string at j->p (its opening quote), decoded to UTF-8 into out (cap bytes, always terminated; NULL: only skipped). The
   length decoded goes to *len (may be NULL); a string longer than cap fails. */
static inline int cdb_string(CheatJson *j, char *out, size_t cap, size_t *len) {
    if (j->p >= j->end || *j->p != '"') return cdb_fail(j, "a string was expected");
    j->p++;
    size_t n = 0;
    for (;;) {
        if (j->p >= j->end) return cdb_fail(j, "a string is not closed");
        unsigned char c = (unsigned char)*j->p++;
        uint32_t cp;
        if (c == '"') break;
        if (c < 0x20) return cdb_fail(j, "a control character in a string");
        if (c != '\\') cp = c;   /* UTF-8 bytes are copied as they are */
        else {
            if (j->p >= j->end) return cdb_fail(j, "a string is not closed");
            char e = *j->p++;
            switch (e) {
            case '"': cp = '"'; break;  case '\\': cp = '\\'; break; case '/': cp = '/'; break;
            case 'b': cp = '\b'; break; case 'f': cp = '\f'; break;  case 'n': cp = '\n'; break;
            case 'r': cp = '\r'; break; case 't': cp = '\t'; break;
            case 'u': {
                unsigned v, lo;
                if (j->end - j->p < 4 || !cdb_hex4(j->p, &v)) return cdb_fail(j, "a bad \\u escape");
                j->p += 4; cp = v;
                if (v >= 0xD800 && v < 0xDC00) {   /* a surrogate pair */
                    if (j->end - j->p < 6 || j->p[0] != '\\' || j->p[1] != 'u' || !cdb_hex4(j->p + 2, &lo) || lo < 0xDC00 || lo > 0xDFFF)
                        return cdb_fail(j, "a lone surrogate in a \\u escape");
                    j->p += 6; cp = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                } else if (v >= 0xDC00 && v < 0xE000) return cdb_fail(j, "a lone surrogate in a \\u escape");
                break;
            }
            default: return cdb_fail(j, "an unknown escape in a string");
            }
            if (cp >= 0x80) {   /* encode */
                unsigned char b[4]; int k;
                if (cp < 0x800) { b[0] = (unsigned char)(0xC0 | cp >> 6); b[1] = (unsigned char)(0x80 | (cp & 0x3F)); k = 2; }
                else if (cp < 0x10000) { b[0] = (unsigned char)(0xE0 | cp >> 12); b[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F)); b[2] = (unsigned char)(0x80 | (cp & 0x3F)); k = 3; }
                else { b[0] = (unsigned char)(0xF0 | cp >> 18); b[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F)); b[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (unsigned char)(0x80 | (cp & 0x3F)); k = 4; }
                for (int i = 0; i < k; i++) { if (out) { if (n + 1 >= cap) return cdb_fail(j, "a string is too long"); out[n] = (char)b[i]; } n++; }
                continue;
            }
        }
        if (out) { if (n + 1 >= cap) return cdb_fail(j, "a string is too long"); out[n] = (char)cp; }
        n++;
    }
    if (out && cap) out[n] = 0;
    if (len) *len = n;
    return 1;
}
static inline int cdb_value(CheatJson *j, int depth);
static inline int cdb_literal(CheatJson *j, const char *w) {
    size_t n = strlen(w);
    if ((size_t)(j->end - j->p) < n || memcmp(j->p, w, n)) return cdb_fail(j, "an unexpected character");
    j->p += n; return 1;
}
/* Skips any value (validated). */
static inline int cdb_value(CheatJson *j, int depth) {
    if (depth > 32) return cdb_fail(j, "nested too deep");
    cdb_ws(j);
    if (j->p >= j->end) return cdb_fail(j, "the file ends early");
    char c = *j->p;
    if (c == '"') return cdb_string(j, NULL, 0, NULL);
    if (c == '{' || c == '[') {
        char close = c == '{' ? '}' : ']';
        j->p++; cdb_ws(j);
        if (j->p < j->end && *j->p == close) { j->p++; return 1; }
        for (;;) {
            if (c == '{') {
                cdb_ws(j);
                if (!cdb_string(j, NULL, 0, NULL)) return 0;
                cdb_ws(j);
                if (j->p >= j->end || *j->p++ != ':') return cdb_fail(j, "':' was expected");
            }
            if (!cdb_value(j, depth + 1)) return 0;
            cdb_ws(j);
            if (j->p < j->end && *j->p == ',') { j->p++; continue; }
            if (j->p < j->end && *j->p == close) { j->p++; return 1; }
            return cdb_fail(j, "',' or the end of a list was expected");
        }
    }
    if (c == 't') return cdb_literal(j, "true");
    if (c == 'f') return cdb_literal(j, "false");
    if (c == 'n') return cdb_literal(j, "null");
    if (c == '-' || isdigit((unsigned char)c)) {
        const char *s = j->p;
        if (*j->p == '-') j->p++;
        while (j->p < j->end && (isdigit((unsigned char)*j->p) || *j->p == '.' || *j->p == 'e' || *j->p == 'E' || *j->p == '+' || *j->p == '-')) j->p++;
        return j->p > s + (*s == '-') ? 1 : cdb_fail(j, "a bad number");
    }
    return cdb_fail(j, "an unexpected character");
}

static inline int cdb_put(char *out, size_t cap, size_t *used, const char *s, size_t n) {
    if (*used + n + 1 > cap) return 0;
    memcpy(out + *used, s, n); *used += n; out[*used] = 0;
    return 1;
}

/* Appends build_id's cheats from a database file (json, n bytes) to out (an Atmosphère cheat file; *used bytes so far, out
   always terminated). Returns the number of sections added, 0 when the file has no entry for this build, -1 when it is not
   such a database or the cheats do not fit (err says why; out is left as it was). */
static inline int cheat_db_extract(const char *json, size_t n, const char *build_id, char *out, size_t cap, size_t *used,
                                   char *err, size_t errn) {
    if (err && errn) err[0] = 0;
    if (!json || n > CHEAT_DB_MAX_JSON || !build_id || strlen(build_id) != 16 || !out || !cap || !used || *used >= cap) {
        if (err && errn) snprintf(err, errn, "not a cheat database (too large or no build ID)");
        return -1;
    }
    size_t start = *used;
    int sections = 0;
    static char key[512], val[256 << 10];
    CheatJson j = {json, json + n, NULL};
    if ((unsigned char)j.p[0] == 0xEF && n >= 3 && (unsigned char)j.p[1] == 0xBB && (unsigned char)j.p[2] == 0xBF) j.p += 3;
    cdb_ws(&j);
    if (j.p >= j.end || *j.p != '{') { j.err = "not a JSON object"; goto bad; }
    j.p++; cdb_ws(&j);
    if (j.p < j.end && *j.p == '}') { j.p++; goto end; }
    for (;;) {
        cdb_ws(&j);
        size_t klen;
        if (!cdb_string(&j, key, sizeof key, &klen)) { if (!strcmp(j.err, "a string is too long")) j.err = "a build ID key is too long"; goto bad; }
        cdb_ws(&j);
        if (j.p >= j.end || *j.p++ != ':') { j.err = "':' was expected"; goto bad; }
        cdb_ws(&j);
        if (klen != 16 || strcasecmp(key, build_id)) { if (!cdb_value(&j, 1)) goto bad; }
        else {   /* this build's sections, in order */
            if (j.p >= j.end || *j.p != '{') { j.err = "the build's entry is not an object"; goto bad; }
            j.p++; cdb_ws(&j);
            if (j.p < j.end && *j.p == '}') j.p++;
            else for (;;) {
                cdb_ws(&j);
                size_t vlen;
                if (!cdb_string(&j, key, sizeof key, &klen)) goto bad;
                cdb_ws(&j);
                if (j.p >= j.end || *j.p++ != ':') { j.err = "':' was expected"; goto bad; }
                cdb_ws(&j);
                if (!cdb_string(&j, val, sizeof val, &vlen)) { if (!j.err) j.err = "a cheat is not a string"; goto bad; }
                if (memchr(key, 0, klen) || memchr(val, 0, vlen)) { j.err = "a NUL character in a cheat"; goto bad; }
                const char *v = val; while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') v++;
                if (*v != '[' && *v != '{') {   /* the value without its header: the key names it */
                    int wrapped = key[0] == '[' || key[0] == '{';
                    if ((!wrapped && !cdb_put(out, cap, used, "[", 1)) || !cdb_put(out, cap, used, key, klen) ||
                        (!wrapped && !cdb_put(out, cap, used, "]", 1)) || !cdb_put(out, cap, used, "\n", 1)) goto full;
                }
                if (!cdb_put(out, cap, used, v, strlen(v)) || !cdb_put(out, cap, used, "\n\n", 2)) goto full;
                sections++;
                cdb_ws(&j);
                if (j.p < j.end && *j.p == ',') { j.p++; continue; }
                if (j.p < j.end && *j.p == '}') { j.p++; break; }
                j.err = "',' or '}' was expected"; goto bad;
            }
        }
        cdb_ws(&j);
        if (j.p < j.end && *j.p == ',') { j.p++; continue; }
        if (j.p < j.end && *j.p == '}') { j.p++; break; }
        j.err = "',' or '}' was expected"; goto bad;
    }
end:
    cdb_ws(&j);
    if (j.p != j.end) { j.err = "something follows the JSON object"; goto bad; }
    return sections;
full:
    if (err && errn) snprintf(err, errn, "this build's cheats are larger than %u KB", (unsigned)(cap >> 10));
    *used = start; out[start] = 0;
    return -1;
bad:
    if (err && errn) snprintf(err, errn, "not a cheat database: %s at byte %ld", j.err ? j.err : "malformed", (long)(j.p - json));
    *used = start; out[start] = 0;
    return -1;
}
