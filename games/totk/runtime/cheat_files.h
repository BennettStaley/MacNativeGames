/* Where the Mac's cheats come from (runtime/cheats.c reads them at launch; Settings › Cheats lists them). Plain C, header
   only and without game state, so tests/test_cheat_files.c builds it together with runtime/mods.c.

   Atmosphère cheat files are named after the main module's build ID (<BID>.txt, any case: the cheat databases spell it
   both ways). Three places, read in this order, each file a group of its own in the Cheats tab:
     1. the app's own file, cheats/cheats.txt in the game folder (build_app.py --cheats, which already checked its build
        ID; TOTK_CHEATS_FILE overrides);
     2. the cheats folder: <BID>.txt in ~/Library/Application Support/MacNativeGames/TOTK/cheats (TOTK_CHEATS_DIR; 0 = none);
     3. every mod that is on, in the load order: <mod>/cheats/<BID>.txt (runtime/mods.h; Yuzu / Ryujinx layout).
   A file for another build ID is never read. The chosen cheats are saved by key, the source and the section name, so two
   files with a "[Inf. Health]" each keep their own choice; the app's own file keeps the bare name of the older format. */
#pragma once
#include "cheat_vm.h"
#include "mods.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

enum { CHEAT_SRC_APP, CHEAT_SRC_FOLDER, CHEAT_SRC_MOD };
enum { CHEAT_SOURCES_MAX = 64 };
typedef struct {
    int kind;                 /* CHEAT_SRC_* */
    char id[300];             /* "app", "folder", "mod:<folder>": the first half of a cheat's key */
    char label[300];          /* what the Cheats tab calls the group */
    char path[4096];
    long long mtime, size;    /* when read, to notice a file that changed since (restart to load it) */
} CheatSource;

/* name is <build_id>.txt, in any case. */
static inline int cheat_file_matches(const char *name, const char *build_id) {
    size_t n = strlen(build_id);
    return n && !strncasecmp(name, build_id, n) && !strcasecmp(name + n, ".txt");
}
/* The file for build_id in dir (out: its path); 1 if there is one. */
static inline int cheat_file_in(const char *dir, const char *build_id, char *out, size_t cap) {
    if (!dir || !*dir || !build_id || !*build_id) return 0;
    DIR *d = opendir(dir); if (!d) return 0;
    struct dirent *e; int found = 0;
    while (!found && (e = readdir(d))) {
        char p[4400]; struct stat st;
        if (!cheat_file_matches(e->d_name, build_id) || snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) continue;
        if (stat(p, &st) || !S_ISREG(st.st_mode)) continue;
        found = snprintf(out, cap, "%s", p) < (int)cap;
    }
    closedir(d);
    return found;
}
static inline int cheat_source_add(CheatSource *out, int n, int max, int kind, const char *id, const char *label, const char *path) {
    struct stat st;
    if (n >= max || stat(path, &st) || !S_ISREG(st.st_mode)) return n;
    CheatSource *s = &out[n]; memset(s, 0, sizeof *s);
    s->kind = kind;
    snprintf(s->id, sizeof s->id, "%s", id); snprintf(s->label, sizeof s->label, "%s", label); snprintf(s->path, sizeof s->path, "%s", path);
    s->mtime = (long long)st.st_mtime; s->size = (long long)st.st_size;
    return n + 1;
}
/* The files to read now, in order (mods as the last mods_scan saw them); returns the count. */
static inline int cheat_sources(const char *app_file, const char *folder, const char *build_id, CheatSource *out, int max) {
    int n = 0; char path[4096];
    if (app_file && *app_file) n = cheat_source_add(out, n, max, CHEAT_SRC_APP, "app", "Built into the app", app_file);
    if (folder && *folder && strcmp(folder, "0") && cheat_file_in(folder, build_id, path, sizeof path))
        n = cheat_source_add(out, n, max, CHEAT_SRC_FOLDER, "folder", "From the cheats folder", path);
    for (int i = 0, count = mods_count(); i < count; i++) {
        ModInfo m;
        if (mods_get(i, &m) || !m.enabled || !mods_cheat_file(i, build_id, path, sizeof path)) continue;
        char id[300], label[300];
        snprintf(id, sizeof id, "mod:%s", m.folder); snprintf(label, sizeof label, "Mod: %s", m.name);
        n = cheat_source_add(out, n, max, CHEAT_SRC_MOD, id, label, path);
    }
    return n;
}
/* 1 when both lists name the same files, unchanged. */
static inline int cheat_sources_same(const CheatSource *a, int na, const CheatSource *b, int nb) {
    if (na != nb) return 0;
    for (int i = 0; i < na; i++)
        if (strcmp(a[i].id, b[i].id) || strcmp(a[i].path, b[i].path) || a[i].mtime != b[i].mtime || a[i].size != b[i].size) return 0;
    return 1;
}

/* A chosen cheat's line in cheats-enabled.txt: "<source id>\t<name>", the app's own file just "<name>" (the older format). */
static inline void cheat_key(const char *source_id, const char *name, char *out, size_t cap) {
    if (!strcmp(source_id, "app")) snprintf(out, cap, "%s", name);
    else snprintf(out, cap, "%s\t%s", source_id, name);
}

/* What a section is in the list. The cheat databases group their cheats with sections that hold one word, 20000000 or
   20000001 (which one opens and which closes differs between databases): the first of a pair is the group's heading, the
   next one with the same name closes it. A section without words is a title or a note, shown as a heading too.
   c[first..k] are the sections of k's file. */
enum { CHEAT_ROLE_CHEAT, CHEAT_ROLE_MASTER, CHEAT_ROLE_HEADING, CHEAT_ROLE_HIDDEN };
static inline int cheat_is_marker(const Cheat *c) { return !c->master && c->n == 1 && (c->w[0] == 0x20000000 || c->w[0] == 0x20000001); }
static inline int cheat_role_at(const Cheat *c, int first, int k) {
    if (c[k].master) return CHEAT_ROLE_MASTER;
    if (!c[k].n) return CHEAT_ROLE_HEADING;
    if (!cheat_is_marker(&c[k])) return CHEAT_ROLE_CHEAT;
    const char *open = NULL;   /* the group open before k, if any */
    for (int i = first; i < k; i++) if (cheat_is_marker(&c[i])) open = open && !strcmp(open, c[i].name) ? NULL : c[i].name;
    return open && !strcmp(open, c[k].name) ? CHEAT_ROLE_HIDDEN : CHEAT_ROLE_HEADING;
}
