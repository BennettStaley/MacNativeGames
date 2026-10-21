/* Test where the Mac's cheats come from (games/totk/runtime/cheat_files.h): the file named after the build ID in the
   cheats folder and in each mod that is on (runtime/mods.c), in load order, the keys the choices are saved under and how
   the Cheats tab reads a file's sections. On a scratch folder:
   cc -std=c11 tests/test_cheat_files.c */
#include "../games/totk/runtime/mods.c"
#include "../games/totk/runtime/cheat_vm.c"
#include "../games/totk/runtime/cheat_files.h"
#include <stdio.h>
#include <utime.h>

static int failures;
static void expect(int ok, const char *what) { if (!ok) { fprintf(stderr, "FAIL: %s\n", what); failures++; } }
static char root[1024];
static void put(const char *rel, const char *text) {
    char p[2048]; snprintf(p, sizeof p, "%s/%s", root, rel);
    for (char *s = p + strlen(root) + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    FILE *f = fopen(p, "w"); if (!f) { perror(p); exit(1); } fputs(text, f); fclose(f);
}
static void path_of(const char *rel, char *out, size_t cap) { snprintf(out, cap, "%s/%s", root, rel); }

#define BID "277178B7DBA1B6D4"
static const char *const CHEATS = "[Title of the file]\n\n{Master}\n04000000 00000000 D503201F\n\n[Link]\n20000001\n\n"
                                  "[Inf. Health]\n040A0000 001AE99C 1E212020\n\n[Link]\n20000000\n\n[Moon jump]\n80000100\n20000000\n";

int main(void) {
    snprintf(root, sizeof root, "%s/test-cheat-files-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(root)) { perror("mkdtemp"); return 1; }
    char mods_path[1100], folder[1100], app[1100], p[4096];
    path_of("mods", mods_path, sizeof mods_path); path_of("cheats", folder, sizeof folder); path_of("app/cheats.txt", app, sizeof app);
    setenv("TOTK_MODS_DIR", mods_path, 1);

    expect(cheat_file_matches(BID ".txt", BID) && cheat_file_matches("277178b7dba1b6d4.TXT", BID), "the build ID's file in any case");
    expect(!cheat_file_matches("082CE09B06E33A12.txt", BID) && !cheat_file_matches(BID ".txt.bak", BID) && !cheat_file_matches(BID, BID),
           "another build ID or another extension is not read");

    put("app/cheats.txt", CHEATS);
    put("cheats/082CE09B06E33A12.txt", CHEATS);                  /* another game version: never read */
    put("cheats/277178b7dba1b6d4.txt", CHEATS);
    put("mods/A Cheats/cheats/277178b7dba1b6d4.txt", CHEATS);   /* Yuzu / Ryujinx layout, a cheats-only mod */
    put("mods/B Off/cheats/" BID ".txt", CHEATS);
    put("mods/B Off/romfs/UI/Foo.txt", "x");
    put("mods/C Atmosphere/atmosphere/contents/0100F2C0115B6000/cheats/" BID ".txt", CHEATS);
    put("mods/D Other/cheats/082CE09B06E33A12.txt", CHEATS);
    put("mods/E Plain/romfs/UI/Foo.txt", "x");
    put("mods/mods.txt", "C Atmosphere\n-B Off\nA Cheats\n");
    expect(mods_scan() == 5, "five mods");

    ModInfo m; int a = -1, e = -1;
    for (int i = 0; !mods_get(i, &m); i++) { if (!strcmp(m.folder, "A Cheats")) a = i; if (!strcmp(m.folder, "E Plain")) e = i; }
    expect(a >= 0 && !mods_get(a, &m) && (m.flags & MOD_HAS_CHEATS) && !(m.flags & MOD_EMPTY), "a cheats-only mod is not empty");
    expect(!strcmp(mods_notice(MOD_HAS_CHEATS), "Has cheats: listed in Settings › Cheats while the mod is on."), "its line in the Mods tab");
    expect(e >= 0 && !mods_get(e, &m) && !(m.flags & MOD_HAS_CHEATS), "a mod without cheats/ has none");
    expect(a >= 0 && mods_cheat_file(a, BID, p, sizeof p) && strstr(p, "A Cheats/cheats/277178b7dba1b6d4.txt"), "a mod's file, in its own case");
    expect(a >= 0 && !mods_cheat_file(a, "082CE09B06E33A12", p, sizeof p), "no file for another build ID in that mod");

    CheatSource s[CHEAT_SOURCES_MAX], again[CHEAT_SOURCES_MAX];
    int n = cheat_sources(app, folder, BID, s, CHEAT_SOURCES_MAX);
    expect(n == 4, "the app, the folder and the two mods that are on with this build's file");
    expect(n == 4 && s[0].kind == CHEAT_SRC_APP && !strcmp(s[0].id, "app"), "the app's own file first");
    expect(n == 4 && s[1].kind == CHEAT_SRC_FOLDER && strstr(s[1].path, "/cheats/277178b7dba1b6d4.txt"), "then the cheats folder's file for this build");
    expect(n == 4 && !strcmp(s[2].id, "mod:C Atmosphere") && !strcmp(s[3].id, "mod:A Cheats"), "then the mods in load order (Atmosphère layout found too)");
    expect(n == 4 && !strcmp(s[3].label, "Mod: A Cheats"), "a mod's group is named after the mod");
    expect(cheat_sources(NULL, "0", BID, again, CHEAT_SOURCES_MAX) == 2, "no app file and the folder off: only the mods");

    expect(cheat_sources_same(s, n, s, n), "the same files are the same");
    mods_set_enabled("B Off", 1);
    int n2 = cheat_sources(app, folder, BID, again, CHEAT_SOURCES_MAX);
    expect(n2 == 5 && !cheat_sources_same(s, n, again, n2), "a mod turned on adds its file (restart to read it)");
    mods_set_enabled("B Off", 0);
    char changed[1100]; path_of("cheats/277178b7dba1b6d4.txt", changed, sizeof changed);
    struct utimbuf t = {1000000000, 1000000000}; utime(changed, &t);
    n2 = cheat_sources(app, folder, BID, again, CHEAT_SOURCES_MAX);
    expect(n2 == n && !cheat_sources_same(s, n, again, n2), "a file changed since it was read");

    char key[400];
    cheat_key("app", "Inf. Health", key, sizeof key);
    expect(!strcmp(key, "Inf. Health"), "the app's own cheats keep the older bare name");
    cheat_key("mod:A Cheats", "Inf. Health", key, sizeof key);
    expect(!strcmp(key, "mod:A Cheats\tInf. Health"), "other files' cheats: source and name, so equal names stay apart");

    Cheat c[16]; char err[128];
    int k = cheat_parse(CHEATS, c, 16, err, sizeof err);
    expect(k == 6, "six sections");
    if (k == 6) {
        expect(cheat_role(&c[0]) == CHEAT_ROLE_HEADING, "a section without words is a title");
        expect(cheat_role(&c[1]) == CHEAT_ROLE_MASTER, "{Master} is a master code");
        expect(cheat_role(&c[2]) == CHEAT_ROLE_HEADING && !strcmp(c[2].name, "Link"), "20000001 opens a group");
        expect(cheat_role(&c[3]) == CHEAT_ROLE_CHEAT, "a cheat");
        expect(cheat_role(&c[4]) == CHEAT_ROLE_HIDDEN, "20000000 closes it");
        expect(cheat_role(&c[5]) == CHEAT_ROLE_CHEAT, "a cheat that ends a condition is still a cheat");
    }

    char cmd[1200]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); if (system(cmd)) fprintf(stderr, "could not remove %s\n", root);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("cheat files: all checks passed\n");
    return 0;
}
