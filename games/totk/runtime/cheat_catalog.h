/* How the Cheats tab shows the community cheat list (cheat_db.h): our own words about the codes, never the codes. Each
   section of the downloaded file gets a clean name, a group (Player, Combat, Movement, Inventory, World, Map, Other), a line
   about it, or a reason to leave it out of the list:
     - titles, notes, group markers and master codes (masters still run with the cheats chosen);
     - helpers that only undo other cheats ("Restore Code", "ReEnable Fog", "Time Resume": switching a code patch off and
       restarting already restores the game's code);
     - resolution, frame rate and picture cheats (FPS, resolution, RRS, contrast, saturation...): the Display settings do
       that, and the cheats would fight them;
     - a repeat of a section shown already (the same words, the same cleaned name, or the same catalog entry).
   Sections the catalog does not know are kept under Other with their numbering ("№13.", "#01.") taken off, so a new
   entry in the database is never lost. Variants of one cheat (Damage x2 / x4 / x10 / x30) become one choice: the first
   variant shown is the row, the others its options.
   The choice a player makes is saved under the cleaned name (cheat_clean_name), which does not move when the database
   renumbers its entries. Plain C, header only (tests/test_cheat_catalog.c). */
#pragma once
#include "cheat_vm.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

enum { CHEAT_GROUP_PLAYER, CHEAT_GROUP_COMBAT, CHEAT_GROUP_MOVEMENT, CHEAT_GROUP_INVENTORY, CHEAT_GROUP_WORLD, CHEAT_GROUP_MAP,
       CHEAT_GROUP_OTHER, CHEAT_GROUPS };
static const char *const CHEAT_GROUP_NAMES[CHEAT_GROUPS] = {"Player", "Combat", "Movement", "Inventory", "World", "Map", "Other"};
enum { CHEAT_SHOW, CHEAT_HIDE_HEADING, CHEAT_HIDE_MASTER, CHEAT_HIDE_HELPER, CHEAT_HIDE_GRAPHICS, CHEAT_HIDE_DUPLICATE };

typedef struct {
    const char *build;    /* the main module's build ID */
    const char *match;    /* the cleaned section name, any case; '*' matches any run of characters */
    int group;            /* CHEAT_GROUP_* */
    const char *name;     /* shown; entries with the same name and an option form one choice */
    const char *option;   /* NULL: a switch; else this variant's label in the choice */
    const char *about;    /* one line under the name */
    int hide;             /* CHEAT_SHOW, or the catalog's reason to leave it out */
} CheatCatalogEntry;

#define V143 "277178B7DBA1B6D4"
static const CheatCatalogEntry CHEAT_CATALOG[] = {
    /* 1.4.3 (sthetix/nx-cheats-db, the same codes as Arch9SK7/MyNXCheats) */
    {V143, "Inf. Health", CHEAT_GROUP_PLAYER, "Infinite health", NULL, "Your hearts never go down.", 0},
    {V143, "Inf. Stamina", CHEAT_GROUP_PLAYER, "Infinite stamina", "On", "The stamina wheel never empties. Pull the Master Sword with its own version.", 0},
    {V143, "Inf. Stamina (Use Only For Master Sword Quest)", CHEAT_GROUP_PLAYER, "Infinite stamina", "Master Sword quest", NULL, 0},
    {V143, "Inf. Food Buff Timer", CHEAT_GROUP_PLAYER, "Endless meal effects", NULL, "Effects of meals and elixirs do not wear off.", 0},
    {V143, "Perfect Thermometer*", CHEAT_GROUP_PLAYER, "Ignore heat and cold", NULL, "No harm from heat, cold or fire, without armor or food.", 0},
    {V143, "Ignore Gloom Effects", CHEAT_GROUP_PLAYER, "Gloom immunity", NULL, "Gloom does not take your hearts away.", 0},
    {V143, "Stealth Mode", CHEAT_GROUP_PLAYER, "Stealth", NULL, "Enemies and animals hardly notice you.", 0},
    {V143, "Inf. Battery", CHEAT_GROUP_PLAYER, "Infinite battery", NULL, "Zonai devices never drain your energy cells.", 0},
    {V143, "Instant Companion Skills*", CHEAT_GROUP_COMBAT, "Sage abilities without cooldown", NULL, "Your companions' abilities are ready again at once.", 0},
    {V143, "Damage Multiplier (2x)", CHEAT_GROUP_COMBAT, "Damage multiplier", "×2", "Multiplies the damage your attacks deal.", 0},
    {V143, "Damage Multiplier (4x)", CHEAT_GROUP_COMBAT, "Damage multiplier", "×4", NULL, 0},
    {V143, "Damage Multiplier (10x)", CHEAT_GROUP_COMBAT, "Damage multiplier", "×10", NULL, 0},
    {V143, "Damage Multiplier (30x)", CHEAT_GROUP_COMBAT, "Damage multiplier", "×30", NULL, 0},
    {V143, "Faster Weapon Charge", CHEAT_GROUP_COMBAT, "Faster charged attacks", NULL, "Charged weapon attacks are ready sooner.", 0},
    {V143, "Faster Bow Charge", CHEAT_GROUP_COMBAT, "Faster bow draw", NULL, "Bows reach full draw sooner.", 0},
    {V143, "Quickfire Bow", CHEAT_GROUP_COMBAT, "Quickfire bow", NULL, "Arrows fire in quick succession.", 0},
    {V143, "Hold (X) Moon Jump", CHEAT_GROUP_MOVEMENT, "Moon jump", NULL, "Hold X to rise into the air; let go to fall.", 0},
    {V143, "Jump Higher", CHEAT_GROUP_MOVEMENT, "Higher jumps", NULL, "Link jumps higher.", 0},
    {V143, "Dive Boost", CHEAT_GROUP_MOVEMENT, "Faster skydiving", NULL, "Dive down faster when falling from the sky.", 0},
    {V143, "Default Movement is Running", CHEAT_GROUP_MOVEMENT, "Default pace", "Running", "How Link moves with the stick when you do not sprint.", 0},
    {V143, "Default Movement is Walking", CHEAT_GROUP_MOVEMENT, "Default pace", "Walking", NULL, 0},
    {V143, "Movement Speed (2x)", CHEAT_GROUP_MOVEMENT, "Movement speed", "×2", "Link walks and runs faster.", 0},
    {V143, "Movement Speed (3x)", CHEAT_GROUP_MOVEMENT, "Movement speed", "×3", NULL, 0},
    {V143, "Movement Speed (10x)", CHEAT_GROUP_MOVEMENT, "Movement speed", "×10", NULL, 0},
    {V143, "Glider Speed (1.5x)*", CHEAT_GROUP_MOVEMENT, "Faster paraglider", "×1.5", "Hold A while gliding to go faster.", 0},
    {V143, "Glider Speed (2.0x)*", CHEAT_GROUP_MOVEMENT, "Faster paraglider", "×2", NULL, 0},
    {V143, "Glider Speed (3.0x)*", CHEAT_GROUP_MOVEMENT, "Faster paraglider", "×3", NULL, 0},
    {V143, "Glider Speed (3.8x)*", CHEAT_GROUP_MOVEMENT, "Faster paraglider", "×3.8", NULL, 0},
    {V143, "Inf. Horse Stamina", CHEAT_GROUP_MOVEMENT, "Infinite horse stamina", NULL, "Your horse never runs out of spurs.", 0},
    {V143, "Items x999", CHEAT_GROUP_INVENTORY, "999 of each item", NULL, "Item counts become 999.", 0},
    {V143, "Free Shopping", CHEAT_GROUP_INVENTORY, "Free shopping", NULL, "Shops do not take your rupees.", 0},
    {V143, "Inf. Equipment Durability", CHEAT_GROUP_INVENTORY, "Unbreakable equipment", NULL, "Weapons, bows and shields never wear out.", 0},
    {V143, "Inf. Arrow*", CHEAT_GROUP_INVENTORY, "Infinite arrows", NULL, "Shooting does not use up arrows.", 0},
    {V143, "Inf. Zonai Devices", CHEAT_GROUP_INVENTORY, "Infinite Zonai devices", NULL, "Using a device capsule does not use it up.", 0},
    {V143, "Inf. Food Items", CHEAT_GROUP_INVENTORY, "Infinite food", NULL, "Eating does not use up the meal.", 0},
    {V143, "Inf. Amiibo Usage", CHEAT_GROUP_INVENTORY, "Unlimited amiibo", NULL, "Scan the same amiibo again without waiting a day.", 0},
    {V143, "Inf. Zonai Device Duration*", CHEAT_GROUP_WORLD, "Zonai devices stay", NULL, "Devices you place never blink out or vanish.", 0},
    {V143, "Remove 21 Zonai Device Spawn Limit", CHEAT_GROUP_WORLD, "No Zonai device limit", NULL, "Build with more than 21 devices at once.", 0},
    {V143, "Disable Fog", CHEAT_GROUP_WORLD, "No fog", NULL, "Clears the haze over the distance.", 0},
    {V143, "ReEnable Fog", CHEAT_GROUP_WORLD, "No fog", NULL, NULL, CHEAT_HIDE_HELPER},
    {V143, "Instant Bloodmoon", CHEAT_GROUP_WORLD, "Blood Moon now", NULL, "Brings the Blood Moon right away.", 0},
    {V143, "Time Stop", CHEAT_GROUP_WORLD, "Stop time", NULL, "The clock stands still.", 0},
    {V143, "Time Resume", CHEAT_GROUP_WORLD, "Stop time", NULL, NULL, CHEAT_HIDE_HELPER},
    {V143, "Time RW*", CHEAT_GROUP_WORLD, "Rewind time", NULL, "Hold ZL and push the right stick left.", 0},
    {V143, "Time FF*", CHEAT_GROUP_WORLD, "Fast-forward time", NULL, "Hold ZL and push the right stick right.", 0},
    {V143, "2x Longer Days", CHEAT_GROUP_WORLD, "Longer days", "×2", "Days and nights last longer.", 0},
    {V143, "3x Longer Days", CHEAT_GROUP_WORLD, "Longer days", "×3", NULL, 0},
    {V143, "4x Longer Days", CHEAT_GROUP_WORLD, "Longer days", "×4", NULL, 0},
    {V143, "Add 60 Seconds To Mission Timer*", CHEAT_GROUP_WORLD, "60 more seconds", NULL, "Press both sticks in a timed challenge.", 0},
    {V143, "Show All Shrine and Cave Locations", CHEAT_GROUP_MAP, "Show all shrines and caves", NULL, "Marks them on your map, found or not.", 0},
    {V143, "Show all All Shrine And Roots", CHEAT_GROUP_MAP, "Show all shrines and Lightroots", NULL, "Marks them on your map, found or not.", 0},
    {V143, "Show All Korok Seed Locations*", CHEAT_GROUP_MAP, "Show all Korok seeds", NULL, "Zoom in on the map to see them.", 0},
    {V143, "Show all All Skyview Towers", CHEAT_GROUP_MAP, "Show all Skyview Towers", NULL, "Marks them on your map, found or not.", 0},
    {V143, "Show all Tears of the Dragon", CHEAT_GROUP_MAP, "Show all Dragon's Tears", NULL, "Marks the geoglyph memories on your map.", 0},
    {V143, "Show all Five Great Temples", CHEAT_GROUP_MAP, "Show all temples", NULL, "Marks the regional temples on your map.", 0},
    {V143, "Show all village and Stable", CHEAT_GROUP_MAP, "Show all villages and stables", NULL, "Marks them on your map, found or not.", 0},
    {V143, "Show all venue not visited", CHEAT_GROUP_MAP, "Show places not visited", NULL, "Marks the places you have not been to yet.", 0},
    {V143, "Show all caves, Wells And Chasm", CHEAT_GROUP_MAP, "Show all caves, wells and chasms", NULL, "Marks them on your map, found or not.", 0},
    {V143, "Show all Great Fairy", CHEAT_GROUP_MAP, "Show all Great Fairy fountains", NULL, "Marks them on your map, found or not.", 0},
    {V143, "Show all Miskos treasure", CHEAT_GROUP_MAP, "Show Misko's treasures", NULL, "Marks the treasure of Misko's journals.", 0},
    {V143, "Show all old map", CHEAT_GROUP_MAP, "Show old map treasures", NULL, "Marks the treasure the old maps point to.", 0},
    {V143, "Show all Devil Statue", CHEAT_GROUP_MAP, "Show the Horned Statues", NULL, "Marks them on your map, found or not.", 0},
    {V143, "Show all All Zonai Device Dispenser", CHEAT_GROUP_MAP, "Show all Device Dispensers", NULL, "Marks them on your map, found or not.", 0},
    {V143, "Show all game field and restaurant", CHEAT_GROUP_MAP, "Show minigames and restaurants", NULL, "Marks them on your map, found or not.", 0},
};
#undef V143
enum { CHEAT_CATALOG_N = sizeof CHEAT_CATALOG / sizeof *CHEAT_CATALOG };

/* '*' matches any run of characters; letters in any case. */
static inline int cheat_glob(const char *pat, const char *s) {
    if (!*pat) return !*s;
    if (*pat == '*') { for (;; s++) { if (cheat_glob(pat + 1, s)) return 1; if (!*s) return 0; } }
    return *s && tolower((unsigned char)*pat) == tolower((unsigned char)*s) && cheat_glob(pat + 1, s + 1);
}
/* Case-insensitive substring that does not start inside a word ("60FPS" has "fps", "Arrows" has no "rrs"). */
static inline int cheat_has(const char *s, const char *word) {
    size_t n = strlen(word);
    for (const char *p = s; *p; p++)
        if ((p == s || !isalpha((unsigned char)p[-1])) && !strncasecmp(p, word, n)) return 1;
    return 0;
}

/* The name without the database's numbering ("№13. ", "№ 1a ", "#01. ", "1. ", the "â„–" of a file saved in the wrong
   encoding), with the Switch font's button glyphs written out (U+E0A0.. A, B, X, Y, L, R, ZL, ZR) and other private-use
   characters dropped, spaces collapsed. Never empty when the name has a letter or digit. */
static inline void cheat_clean_name(const char *in, char *out, size_t cap) {
    static const char *const buttons[] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR"};
    char tmp[CHEAT_NAME * 2]; size_t n = 0;
    const unsigned char *p = (const unsigned char *)in;
    while (*p && n + 4 < sizeof tmp) {   /* glyphs and spaces */
        if (p[0] == 0xEE && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {   /* U+E000..U+EFFF: the private use area */
            unsigned cp = (unsigned)(p[0] & 0x0F) << 12 | (unsigned)(p[1] & 0x3F) << 6 | (p[2] & 0x3F);
            if (cp >= 0xE0A0 && cp <= 0xE0A7) { const char *b = buttons[cp - 0xE0A0]; tmp[n++] = ' '; while (*b) tmp[n++] = *b++; tmp[n++] = ' '; }
            p += 3; continue;
        }
        tmp[n++] = isspace(*p) ? ' ' : (char)*p;
        p++;
    }
    tmp[n] = 0;
    const char *s = tmp;
    while (*s == ' ') s++;
    int marked = 0;   /* a numbering sign: "№" (E2 84 96), its mis-decoded "â„–" (C3 A2 E2 80 9E E2 80 93), "#", "No.", "Nº" */
    if (!strncmp(s, "\xE2\x84\x96", 3)) { s += 3; marked = 1; }
    else if (!strncmp(s, "\xC3\xA2\xE2\x80\x9E\xE2\x80\x93", 8)) { s += 8; marked = 1; }
    else if (*s == '#') { s++; marked = 1; }
    else if (!strncmp(s, "No.", 3) || !strncmp(s, "N\xC2\xBA", 3)) { s += 3; marked = 1; }
    while (*s == ' ') s++;
    const char *d = s; while (isdigit((unsigned char)*d) && d - s < 4) d++;
    if (d > s) {
        const char *e = d;
        if (islower((unsigned char)*e) && (e[1] == ' ' || e[1] == '.')) e++;   /* "1a " */
        if (*e == '.' || *e == ')' || *e == ':') { e++; if (*e == ' ' || !*e) s = e; }
        else if (marked && *e == ' ') s = e;
    }
    while (*s == ' ') s++;
    size_t o = 0;
    for (; *s && o + 1 < cap; s++) {
        if (*s == ' ' && (o == 0 || out[o - 1] == ' ')) continue;
        out[o++] = *s;
    }
    while (o && out[o - 1] == ' ') o--;
    if (cap) out[o] = 0;
    if (!o && cap) snprintf(out, cap, "%s", in);   /* nothing left: keep what was written */
}

/* Resolution, frame rate and picture cheats: the Display settings do that (and the two would fight). */
static inline int cheat_is_graphics(const char *name) {
    static const char *const words[] = {"fps", "frame rate", "framerate", "resolution", "rrs", "handheld", "docked", "contrast",
                                        "saturation", "sharpen", "anti-alias", "antialias", "aspect ratio", "ultrawide", "21:9",
                                        "dynamic res", "vsync", "shadow res", "lod "};
    for (size_t i = 0; i < sizeof words / sizeof *words; i++) if (cheat_has(name, words[i])) return 1;
    for (const char *s = name; *s; s++) {   /* "720p", "1080p" */
        if (!isdigit((unsigned char)*s) || (s > name && isalnum((unsigned char)s[-1]))) continue;
        const char *e = s; while (isdigit((unsigned char)*e)) e++;
        if (e - s >= 3 && (*e == 'p' || *e == 'P') && !isalnum((unsigned char)e[1])) return 1;
    }
    return 0;
}
/* Sections that only undo other cheats. */
static inline int cheat_is_helper(const char *name) {
    return cheat_has(name, "restore code") || cheat_glob("restore*", name) || cheat_glob("off enable*", name) || cheat_glob("*re-enable*", name)
        || cheat_glob("re enable*", name) || cheat_glob("reenable*", name) || cheat_glob("*(restore)*", name);
}
/* The catalog entry for a cleaned name on a build, or -1. */
static inline int cheat_catalog_find(const char *build_id, const char *clean) {
    for (int i = 0; i < CHEAT_CATALOG_N; i++)
        if (build_id && !strcasecmp(CHEAT_CATALOG[i].build, build_id) && cheat_glob(CHEAT_CATALOG[i].match, clean)) return i;
    return -1;
}

typedef struct {
    char name[CHEAT_NAME];   /* shown: the catalog's name, or the cleaned section name */
    char key[CHEAT_NAME];    /* the cleaned section name: what the player's choice is saved under */
    const char *about;       /* NULL or one line */
    const char *option;      /* in a choice: this variant's label */
    int group;               /* CHEAT_GROUP_* */
    int hide;                /* CHEAT_SHOW, or why it is left out */
    int choice;              /* -1, or the first section of its choice (the row the options belong to) */
    int order;               /* within its group: the catalog's order, else 10000 + section */
    int entry;               /* the catalog entry, or -1 */
} CheatShown;

static inline int cheat_is_group_marker(const Cheat *c) { return !c->master && c->n == 1 && (c->w[0] == 0x20000000 || c->w[0] == 0x20000001); }

/* How the n sections of a downloaded list (in file order) show for build_id. */
static inline void cheat_catalog_apply(const char *build_id, const Cheat *c, int n, CheatShown *out) {
    for (int k = 0; k < n; k++) {
        CheatShown *s = &out[k];
        memset(s, 0, sizeof *s);
        s->choice = -1; s->entry = -1; s->group = CHEAT_GROUP_OTHER; s->order = 10000 + k;
        cheat_clean_name(c[k].name, s->key, sizeof s->key);
        snprintf(s->name, sizeof s->name, "%s", s->key);
        if (c[k].master) { s->hide = CHEAT_HIDE_MASTER; continue; }
        if (!c[k].n && !c[k].error[0]) { s->hide = CHEAT_HIDE_HEADING; continue; }
        if (cheat_is_group_marker(&c[k])) { s->hide = CHEAT_HIDE_HEADING; continue; }
        int e = cheat_catalog_find(build_id, s->key);
        if (e >= 0) {
            const CheatCatalogEntry *ce = &CHEAT_CATALOG[e];
            s->entry = e; s->group = ce->group; s->order = e; s->about = ce->about; s->option = ce->option; s->hide = ce->hide;
            snprintf(s->name, sizeof s->name, "%s", ce->name);
            if (!s->about && ce->option)   /* a choice's later variants: the line of its first */
                for (int i = 0; i < e; i++) if (!strcmp(CHEAT_CATALOG[i].name, ce->name) && CHEAT_CATALOG[i].about) { s->about = CHEAT_CATALOG[i].about; break; }
        } else if (cheat_is_helper(s->key)) s->hide = CHEAT_HIDE_HELPER;
        else if (cheat_is_graphics(s->key)) s->hide = CHEAT_HIDE_GRAPHICS;
        if (s->hide) continue;
        for (int j = 0; j < k && !s->hide; j++) {   /* shown already? */
            const CheatShown *t = &out[j];
            if (t->hide) continue;
            if ((c[j].n == c[k].n && !memcmp(c[j].w, c[k].w, c[k].n * sizeof *c[k].w) && !c[k].error[0] && !c[j].error[0])
                || !strcasecmp(t->key, s->key) || (e >= 0 && t->entry == e)) s->hide = CHEAT_HIDE_DUPLICATE;
        }
    }
    for (int k = 0; k < n; k++) {   /* choices: the first variant shown is the row */
        CheatShown *s = &out[k];
        if (s->hide || !s->option) continue;
        for (int j = 0; j <= k; j++)
            if (!out[j].hide && out[j].option && out[j].group == s->group && !strcmp(out[j].name, s->name)) { s->choice = j; break; }
    }
}
