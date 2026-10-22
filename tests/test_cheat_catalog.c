/* Test the community cheat list (games/totk/runtime/cheat_db.h) and how the Cheats tab shows it (cheat_catalog.h): the
   database JSON read in order for one build, malformed downloads refused as a whole, names cleaned of their numbering,
   helpers, picture cheats, group markers and repeats left out, variants made one choice. No codes from the database: the
   words below are made up. cc -std=c11 tests/test_cheat_catalog.c */
#include "../games/totk/runtime/cheat_vm.c"
#include "../games/totk/runtime/cheat_db.h"
#include "../games/totk/runtime/cheat_catalog.h"
#include <stdio.h>
#include <stdlib.h>

static int failures;
static void expect(int ok, const char *what) { if (!ok) { fprintf(stderr, "FAIL: %s\n", what); failures++; } }
static void expect_str(const char *got, const char *want, const char *what) {
    if (strcmp(got, want)) { fprintf(stderr, "FAIL: %s: \"%s\", expected \"%s\"\n", what, got, want); failures++; }
}
static char text[1 << 16];
static Cheat cheats[64];
static CheatShown shown[64];

#define BID "277178B7DBA1B6D4"
static int extract(const char *json, char *err, size_t errn) {
    size_t used = 0;
    text[0] = 0;
    return cheat_db_extract(json, strlen(json), BID, text, sizeof text, &used, err, errn);
}
static int find(int n, const char *raw) { for (int k = 0; k < n; k++) if (!strcmp(cheats[k].name, raw)) return k; return -1; }

int main(void) {
    char err[200];

    /* ---- the database file ---- */
    const char *db =
        "{\"082CE09B06E33A12\": {\"[Other build]\": \"[Other build]\\n04000000 00000000 00000001\\n\"},\n"
        " \"" BID "\": {\n"
        "   \"[Title]\": \"[Title]\\n\\n\",\n"
        "   \"[\\u2116 2. Second]\": \"[\\u2116 2. Second]\\r\\n040A0000 00000010 D503201F\\n\",\n"
        "   \"[\\u2116 1. First]\": \"[\\u2116 1. First]\\n040A0000 00000020 D503201F\\n\",\n"
        "   \"[No header]\": \"040A0000 00000030 D503201F\",\n"
        "   \"[Emoji \\ud83d\\ude00 \\\"q\\\" \\/]\": \"[Emoji \\ud83d\\ude00 \\\"q\\\" \\/]\\n040A0000 00000040 D503201F\\n\"},\n"
        " \"" BID "3CB1E2770F5F9147709033DB000000000000000000000000\": {\"[Long key]\": \"[Long key]\\n\"},\n"
        " \"attribution\": {\"a.txt\": \"by someone\", \"n\": [1, -2.5e3, true, false, null, {\"x\": []}]}}\n";
    int got = extract(db, err, sizeof err);
    expect(got == 5, "five sections for this build, none of the other builds'");
    int n = cheat_parse(text, cheats, 64, err, sizeof err);
    expect(n == 5, "the text parses into five sections");
    expect(n == 5 && !strcmp(cheats[0].name, "Title") && !strcmp(cheats[1].name, "\xE2\x84\x96 2. Second") && !strcmp(cheats[2].name, "\xE2\x84\x96 1. First"),
           "sections keep the file's order (not sorted), \\u escapes become UTF-8");
    expect(n == 5 && !strcmp(cheats[3].name, "No header") && cheats[3].n == 3, "a value without its header gets the key as header");
    expect(n == 5 && !strcmp(cheats[4].name, "Emoji \xF0\x9F\x98\x80 \"q\" /"), "surrogate pairs, quotes and slashes decode");
    expect(!strstr(text, "Other build") && !strstr(text, "Long key"), "other builds' entries and longer keys are not read");
    expect(extract("{\"attribution\": {}}", err, sizeof err) == 0 && !err[0] && !text[0], "no entry for this build: 0, nothing written");
    expect(extract("{}", err, sizeof err) == 0, "an empty object");

    static const char *const bad[] = {
        "", "[]", "{\"" BID "\": {\"[A]\": \"x\"}", "{\"" BID "\": {\"[A]\": 5}}", "{\"" BID "\": [\"[A]\"]}",
        "{\"" BID "\": {\"[A]\": \"bad \\q escape\"}}", "{\"" BID "\": {\"[A]\": \"lone \\ud800\"}}", "{\"" BID "\": {\"[A]\": \"raw \n line\"}}",
        "{\"" BID "\": {\"[A]\": \"x\"}} trailing", "{\"x\": tru}", "{\"x\": [[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]}",
        "{\"" BID "\": {\"[A]\": \"nul \\u0000 inside\"}}", "{\"x\": 1,}"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        size_t used = 3; memcpy(text, "abc", 4);
        int r = cheat_db_extract(bad[i], strlen(bad[i]), BID, text, sizeof text, &used, err, sizeof err);
        char what[160]; snprintf(what, sizeof what, "malformed file %zu is refused with a reason and leaves the output as it was", i);
        expect(r == -1 && err[0] && used == 3 && !strcmp(text, "abc"), what);
    }
    {   /* does not fit */
        char small[40]; size_t used = 0;
        expect(cheat_db_extract(db, strlen(db), BID, small, sizeof small, &used, err, sizeof err) == -1 && used == 0 && !small[0] && strstr(err, "larger"),
               "a build's cheats larger than the room are refused as a whole");
        expect(cheat_db_extract(db, strlen(db), "12", text, sizeof text, &used, err, sizeof err) == -1, "a build ID is 16 characters");
    }

    /* ---- names ---- */
    static const char *const names[][2] = {
        {"\xE2\x84\x96" "13. Damage Multiplier (2x)", "Damage Multiplier (2x)"}, {"\xE2\x84\x96 1. Inf. Health", "Inf. Health"},
        {"\xE2\x84\x96 1a Max Heart 40", "Max Heart 40"}, {"\xE2\x84\x96" "14b All Cooked Food 1", "All Cooked Food 1"},
        {"#01. Inf. Health", "Inf. Health"}, {"\xC3\xA2\xE2\x80\x9E\xE2\x80\x93" "10. Materials Collected 98", "Materials Collected 98"},
        {"\xE2\x84\x96 8. 888 Arrows", "888 Arrows"}, {"888 Arrows", "888 Arrows"}, {"1. One", "One"}, {"20FPS", "20FPS"},
        {"2x Longer Days", "2x Longer Days"}, {"  Lots   of\tspace  ", "Lots of space"},
        {"\xEE\x82\xA6 \xEE\x82\xA2 MoonJump \xEE\x82\xA6 hover", "ZL X MoonJump ZL hover"}, {"\xEE\x82\xB0 repair", "repair"},
        {"\xE2\x84\x96", "\xE2\x84\x96"}, {"Rupee 999998", "Rupee 999998"}};
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        char out[CHEAT_NAME]; cheat_clean_name(names[i][0], out, sizeof out);
        expect_str(out, names[i][1], "cleaned name");
    }
    expect(cheat_is_graphics("Handheld 480p") && cheat_is_graphics("Default Docked 1080p") && cheat_is_graphics("60FPS")
           && cheat_is_graphics("Disable RRS Lock") && cheat_is_graphics("Saturation 150%") && cheat_is_graphics("Resolution 900p"),
           "resolution, frame rate and picture cheats are recognized");
    expect(!cheat_is_graphics("Inf. Arrow(s)") && !cheat_is_graphics("Show all Korok seeds") && !cheat_is_graphics("Damage Multiplier (30x)")
           && !cheat_is_graphics("Rupee 999998") && !cheat_is_graphics("888 Arrows") && !cheat_is_graphics("Melody lod"),
           "other cheats are not taken for picture cheats");
    expect(cheat_is_helper("Restore Code (Use after unchecking any cheats below)") && cheat_is_helper("Off enable") && !cheat_is_helper("Inf. Health"),
           "helpers that only undo others");
    expect(cheat_glob("Glider Speed (1.5x)*", "glider speed (1.5X) (Hold A)") && !cheat_glob("Inf. Stamina", "Inf. Stamina (Use Only For Master Sword Quest)"),
           "catalog patterns: '*' and any case, otherwise the whole name");
    for (int i = 0; i < CHEAT_CATALOG_N; i++) {   /* the table itself */
        const CheatCatalogEntry *e = &CHEAT_CATALOG[i];
        char what[200]; snprintf(what, sizeof what, "catalog entry \"%s\": a build, a name, a group, a line for what is shown", e->match);
        int about = e->about || e->hide;
        for (int j = 0; j < i && !about; j++) about = e->option && !strcmp(CHEAT_CATALOG[j].name, e->name) && CHEAT_CATALOG[j].about;
        expect(strlen(e->build) == 16 && e->name && *e->name && e->group >= 0 && e->group < CHEAT_GROUP_OTHER && about, what);
        for (int j = 0; j < i; j++) {
            snprintf(what, sizeof what, "catalog entries \"%s\" and \"%s\" do not both match one name", CHEAT_CATALOG[j].match, e->match);
            expect(strcasecmp(CHEAT_CATALOG[j].build, e->build) || !cheat_glob(CHEAT_CATALOG[j].match, e->match), what);
        }
    }

    /* ---- the list ---- */
    const char *list =
        "[Title]\n\n{Master}\n04000000 00000000 00000001\n\n"
        "[Restore Code (Use after unchecking any cheats below)]\n040A0000 00000100 F9400008\n\n"
        "[Link]\n20000001\n\n"
        "[\xE2\x84\x96 1. Inf. Health]\n040A0000 00000200 D503201F\n\n"
        "[\xE2\x84\x96 2. Inf. Stamina]\n040A0000 00000300 1E204040\n\n"
        "[Combat]\n20000001\n\n"
        "[\xE2\x84\x96" "13. Damage Multiplier (2x)]\n040A0000 00000400 1E201008\n\n"
        "[\xE2\x84\x96" "14. Damage Multiplier (4x)]\n040A0000 00000400 1E221008\n\n"
        "[\xE2\x84\x96" "15. Damage Multiplier (10x)]\nZZZ\n\n"
        "[\xE2\x84\x96" "16. Damage Multiplier (30x)]\n040A0000 00000400 1E27D008\n\n"
        "[\xE2\x84\x96" "47. Show all Great Fairy]\n040A0000 00000500 D503201F\n\n"
        "[\xE2\x84\x96" "48. Show all Miskos treasure]\n040A0000 00000500 D503201F\n\n"   /* the same words: a repeat */
        "[\xE2\x84\x96" "62. Handheld 480p]\n040A0000 00000600 52800028\n\n"
        "[\xE2\x84\x96" "70. Disable Fog]\n040A0000 00000700 D503201F\n\n"
        "[\xE2\x84\x96" "71. ReEnable Fog]\n040A0000 00000700 B9400901\n\n"
        "[\xE2\x84\x96" "90. Brand new cheat]\n040A0000 00000800 D503201F\n\n"
        "[\xC3\xA2\xE2\x80\x9E\xE2\x80\x93" "90. Brand new cheat]\n040A0000 00000900 D503201F\n\n"   /* the same name, other words */
        "[Inf. Health]\n040A0000 00000A00 D503201F\n\n"                                              /* the same catalog entry */
        "[Combat]\n20000000\n";
    n = cheat_parse(list, cheats, 64, err, sizeof err);
    cheat_catalog_apply(BID, cheats, n, shown);
    int k;
    expect(shown[find(n, "Title")].hide == CHEAT_HIDE_HEADING && shown[find(n, "Link")].hide == CHEAT_HIDE_HEADING
           && shown[n - 1].hide == CHEAT_HIDE_HEADING, "titles and group markers are not shown");
    expect(shown[find(n, "Master")].hide == CHEAT_HIDE_MASTER, "master codes are not listed");
    expect(shown[find(n, "Restore Code (Use after unchecking any cheats below)")].hide == CHEAT_HIDE_HELPER, "Restore Code is a helper");
    k = find(n, "\xE2\x84\x96 1. Inf. Health");
    expect(k >= 0 && !shown[k].hide && !strcmp(shown[k].name, "Infinite health") && shown[k].group == CHEAT_GROUP_PLAYER && shown[k].about
           && !strcmp(shown[k].key, "Inf. Health") && shown[k].choice == -1, "a catalog cheat: its name, group, line; saved under the cleaned name");
    int x2 = find(n, "\xE2\x84\x96" "13. Damage Multiplier (2x)"), x4 = x2 + 1, x10 = x2 + 2, x30 = x2 + 3;
    expect(!shown[x2].hide && shown[x2].choice == x2 && shown[x4].choice == x2 && shown[x10].choice == x2 && shown[x30].choice == x2,
           "damage variants are one choice whose row is the first");
    expect(!strcmp(shown[x4].option, "×4") && !strcmp(shown[x30].option, "×30") && !strcmp(shown[x4].name, "Damage multiplier")
           && shown[x30].about && shown[x2].group == CHEAT_GROUP_COMBAT, "each variant is an option with its label; later variants share the line");
    expect(cheats[x10].error[0] && !shown[x10].hide, "a variant that does not parse stays (the game refuses it with its reason)");
    int fairy = find(n, "\xE2\x84\x96" "47. Show all Great Fairy");
    expect(!shown[fairy].hide && shown[fairy + 1].hide == CHEAT_HIDE_DUPLICATE, "the same words twice: the first is kept");
    expect(shown[find(n, "\xE2\x84\x96" "62. Handheld 480p")].hide == CHEAT_HIDE_GRAPHICS, "resolution cheats are left to the Display settings");
    k = find(n, "\xE2\x84\x96" "70. Disable Fog");
    expect(!shown[k].hide && !strcmp(shown[k].name, "No fog") && shown[k].group == CHEAT_GROUP_WORLD && shown[k + 1].hide == CHEAT_HIDE_HELPER,
           "Disable Fog shows, ReEnable Fog (an undo) does not");
    k = find(n, "\xE2\x84\x96" "90. Brand new cheat");
    expect(k >= 0 && !shown[k].hide && shown[k].group == CHEAT_GROUP_OTHER && !strcmp(shown[k].name, "Brand new cheat") && !shown[k].about
           && shown[k].order > CHEAT_CATALOG_N, "a cheat the catalog does not know is kept under Other, cleaned");
    expect(shown[k + 1].hide == CHEAT_HIDE_DUPLICATE, "the same cleaned name twice: the first is kept");
    expect(shown[find(n, "Inf. Health")].hide == CHEAT_HIDE_DUPLICATE, "a second cheat for one catalog entry is a repeat");
    cheat_catalog_apply("082CE09B06E33A12", cheats, n, shown);   /* another build: the catalog's 1.4.3 words do not apply */
    k = find(n, "\xE2\x84\x96 1. Inf. Health");
    expect(!shown[k].hide && shown[k].group == CHEAT_GROUP_OTHER && !strcmp(shown[k].name, "Inf. Health") && shown[x2].choice == -1,
           "the catalog is per build: on another build its cheats are Other, cleaned");
    expect(shown[find(n, "Restore Code (Use after unchecking any cheats below)")].hide == CHEAT_HIDE_HELPER
           && shown[find(n, "\xE2\x84\x96" "62. Handheld 480p")].hide == CHEAT_HIDE_GRAPHICS, "helpers and picture cheats are left out on any build");

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("cheat catalog: all checks passed\n");
    return 0;
}
