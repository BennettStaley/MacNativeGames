/* The Spawn tab's list, search, favorites and recents (games/totk/runtime/spawn_list.h) on a synthetic actor list.
   cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined tests/test_spawn_list.c -o /tmp/t && /tmp/t */
#include "../games/totk/runtime/spawn_list.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
static void check(int ok, const char *what) { checks++; if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(1); } }

static const char LIST[] =
    "# actor list for the spawner (v143), scripts/prepare_actor_list.py\n"
    "# total: 11\n"
    "Enemy_Bokoblin_Junior\tcreatures\tworld\tBokoblin\tgame\n"
    "Enemy_Bokoblin_Boss\tcreatures\tworld\tBokoblin Boss\tdeciphered\n"
    "Enemy_Test_X\tcreatures\tworld\tEnemy Test X\tinternal\n"
    "Item_Fruit_A\titems\tpickup\tApple\tgame\n"
    "Item_Fruit_A_Roast\titems\tpickup\tBaked Apple\tgame\n"
    "Weapon_Sword_001\tweapons\tpickup\tTraveler's Sword\tgame\n"
    "SpObj_Cannon_A_01\tzonai\tworld\tCannon\tgame\n"
    "SpObj_Cannon_Capsule_A_01\tzonai\tpickup\tCannon\tgame\n"
    "Npc_AncientAssistant003\tnpcs\tworld\tGrant\xc3\xa9son\tgame\r\n"
    "SoundLocater_X\tsystem\tworld\tSound Locater X\tdeciphered\n"
    "Obj_Rock\tobjects\tworld\tRock\tdeciphered\n"
    "bad name\titems\tpickup\tBad\tgame\n"
    "Item_Short\titems\tpickup\n"
    "Item_Cat\tnosuch\tpickup\tX\tgame\n"
    "Item_Src\titems\tpickup\tX\tnosuch\n"
    "Item_Kind\titems\tsomewhere\tX\tgame";   /* no newline at the end */

static int show(const SpawnList *l, const SpawnPicks *p, int cat, const char *q, int unnamed, int *out) {
    return spawn_list_filter(l, p, cat, q, unnamed, out, 64);
}
static const char *at(const SpawnList *l, const int *idx, int k) { return l->actor[idx[k]].name; }

int main(void) {
    SpawnList l; int idx[64], n;
    check(spawn_list_parse(&l, LIST, sizeof LIST - 1) == 11, "eleven good lines, comments and bad lines skipped");
    check(!strcmp(l.actor[0].display, "Bokoblin") && l.actor[0].category == SPAWN_CAT_CREATURES && !l.actor[0].pickup, "columns");
    check(l.actor[3].pickup && l.actor[3].source == SPAWN_SRC_GAME, "pickup and source");
    check(!strcmp(l.actor[8].display, "Grant\xc3\xa9son"), "CRLF trimmed, UTF-8 kept");
    check(spawn_list_find(&l, "Obj_Rock") == 10 && spawn_list_find(&l, "Nope") < 0, "find");
    SpawnList empty; check(spawn_list_parse(&empty, NULL, 0) == 0, "no file"); spawn_list_free(&empty);

    /* categories */
    SpawnPicks p; memset(&p, 0, sizeof p);
    n = show(&l, &p, SPAWN_CAT_CREATURES, "", 0, idx);
    check(n == 2 && !strcmp(at(&l, idx, 0), "Enemy_Bokoblin_Junior"), "unnamed hidden by default");
    check(show(&l, &p, SPAWN_CAT_CREATURES, NULL, 1, idx) == 3, "Show unnamed");
    check(show(&l, &p, SPAWN_CAT_SYSTEM, "", 1, idx) == 0, "system never shown");
    check(show(&l, &p, SPAWN_CAT_ZONAI, "  ", 0, idx) == 2, "a blank query is no query");
    check(show(&l, &p, SPAWN_CAT_FAVORITES, "", 0, idx) == 0 && show(&l, &p, SPAWN_CAT_RECENTS, "", 0, idx) == 0, "nothing picked yet");

    /* search: every category, best first */
    n = show(&l, &p, SPAWN_CAT_WEAPONS, "apple", 0, idx);
    check(n == 2 && !strcmp(at(&l, idx, 0), "Item_Fruit_A") && !strcmp(at(&l, idx, 1), "Item_Fruit_A_Roast"),
          "a query leaves the category; the display name starting with it first");
    n = show(&l, &p, SPAWN_CAT_ITEMS, "BOKO", 0, idx);
    check(n == 2, "case-insensitive");
    check(show(&l, &p, SPAWN_CAT_ITEMS, "sword 001", 0, idx) == 1, "words match the display or the internal name");
    check(show(&l, &p, SPAWN_CAT_ITEMS, "capsule", 0, idx) == 1, "the internal name is searched");
    check(show(&l, &p, SPAWN_CAT_ITEMS, "test", 0, idx) == 0 && show(&l, &p, SPAWN_CAT_ITEMS, "test", 1, idx) == 1, "unnamed only with the toggle");
    check(show(&l, &p, SPAWN_CAT_ITEMS, "locater", 1, idx) == 0, "search leaves System out");
    n = show(&l, &p, SPAWN_CAT_ITEMS, "boss", 0, idx);
    check(n == 1 && !strcmp(at(&l, idx, 0), "Enemy_Bokoblin_Boss"), "a later word of the name");
    check(spawn_list_filter(&l, &p, SPAWN_CAT_ITEMS, "a", 1, idx, 3) == 3, "at most max");

    /* favorites */
    check(spawn_picks_toggle_favorite(&p, "SpObj_Cannon_A_01") == 1 && spawn_picks_toggle_favorite(&p, "Item_Fruit_A") == 1, "marked");
    check(spawn_picks_toggle_favorite(&p, "../x") == -1, "bad name refused");
    n = show(&l, &p, SPAWN_CAT_FAVORITES, "", 0, idx);
    check(n == 2 && !strcmp(at(&l, idx, 0), "Item_Fruit_A"), "favorites in the list's order");
    check(spawn_picks_toggle_favorite(&p, "Item_Fruit_A") == 0 && !spawn_picks_is_favorite(&p, "Item_Fruit_A") && p.nfav == 1, "unmarked");
    spawn_picks_toggle_favorite(&p, "Enemy_Test_X");
    check(show(&l, &p, SPAWN_CAT_FAVORITES, "", 0, idx) == 2, "a favorite shows whatever its name");
    spawn_picks_toggle_favorite(&p, "Gone_Actor");
    check(show(&l, &p, SPAWN_CAT_FAVORITES, "", 0, idx) == 2, "a favorite the game lacks is left out");

    /* recents */
    spawn_picks_add_recent(&p, "Weapon_Sword_001"); spawn_picks_add_recent(&p, "Item_Fruit_A"); spawn_picks_add_recent(&p, "Obj_Rock");
    spawn_picks_add_recent(&p, "Item_Fruit_A");
    check(p.nrecent == 3 && !strcmp(p.recent[0], "Item_Fruit_A") && !strcmp(p.recent[1], "Obj_Rock") && !strcmp(p.recent[2], "Weapon_Sword_001"),
          "a repeat moves to the front once");
    n = show(&l, &p, SPAWN_CAT_RECENTS, "", 0, idx);
    check(n == 3 && !strcmp(at(&l, idx, 0), "Item_Fruit_A"), "recents newest first");
    for (int i = 0; i < SPAWN_RECENT_MAX + 5; i++) { char name[32]; snprintf(name, sizeof name, "Actor_%d", i); spawn_picks_add_recent(&p, name); }
    check(p.nrecent == SPAWN_RECENT_MAX && !strcmp(p.recent[0], "Actor_24") && !strcmp(p.recent[SPAWN_RECENT_MAX - 1], "Actor_5"), "the oldest fall off");
    spawn_picks_add_recent(&p, "bad name"); check(strcmp(p.recent[0], "bad name") != 0, "bad name not recorded");

    /* the file */
    char text[8192];
    int len = spawn_picks_format(&p, text, sizeof text);
    check(len > 0 && len < (int)sizeof text, "the file fits");
    SpawnPicks again; spawn_picks_parse(&again, text);
    check(again.nfav == p.nfav && again.nrecent == p.nrecent && !memcmp(again.fav, p.fav, sizeof p.fav[0] * (size_t)p.nfav) &&
          !strcmp(again.recent[0], p.recent[0]) && !strcmp(again.recent[SPAWN_RECENT_MAX - 1], p.recent[SPAWN_RECENT_MAX - 1]),
          "write then read gives the same picks");
    check(spawn_picks_format(&p, text, 10) > 10, "a short buffer reports the full length");
    spawn_picks_parse(&again, "# c\nfavorite Item_Fruit_A\r\nfavorite Item_Fruit_A\nfavorite ../etc\nrecent Obj_Rock\nrecent Obj_Rock\nbogus line\nrecent\n");
    check(again.nfav == 1 && again.nrecent == 1 && !strcmp(again.fav[0], "Item_Fruit_A"), "bad, repeated and CRLF lines");
    spawn_picks_parse(&again, NULL); check(!again.nfav && !again.nrecent, "no file: nothing picked");

    spawn_list_free(&l);
    printf("spawn list: %u checks passed\n", checks);
    return 0;
}
