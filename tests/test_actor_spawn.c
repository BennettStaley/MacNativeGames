/* The actor spawner's request queue and name check (games/totk/runtime/actor_spawn_queue.h): no game needed.
   cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined tests/test_actor_spawn.c -o /tmp/t && /tmp/t */
#include "../games/totk/runtime/actor_spawn_queue.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
static void check(int ok, const char *what) { checks++; if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(1); } }
static int asked;
static int known(const char *name) { asked++; return !strcmp(name, "Weapon_Sword_001") || !strcmp(name, "Item_Fruit_A"); }

int main(void) {
    /* names */
    check(spawn_name_valid("Enemy_Bokoblin_Junior"), "plain actor name");
    check(spawn_name_valid("a"), "one letter");
    check(!spawn_name_valid(""), "empty refused");
    check(!spawn_name_valid(NULL), "NULL refused");
    check(!spawn_name_valid("../Pack/Actor/x"), "path refused");
    check(!spawn_name_valid("Weapon Sword"), "space refused");
    check(!spawn_name_valid("Item_Fruit_A\n"), "newline refused");
    check(!spawn_name_valid("Itém"), "non-ASCII refused");
    char longest[SPAWN_NAME_MAX]; memset(longest, 'A', sizeof longest - 1); longest[sizeof longest - 1] = 0;
    check(spawn_name_valid(longest), "95 characters fit");
    char too_long[SPAWN_NAME_MAX + 1]; memset(too_long, 'A', sizeof too_long - 1); too_long[sizeof too_long - 1] = 0;
    check(!spawn_name_valid(too_long), "96 characters refused");

    /* queue */
    SpawnQueue q = {0}; SpawnRequest r;
    check(!spawn_queue_pop(&q, &r), "empty queue pops nothing");
    check(spawn_queue_push(&q, "bad name", 1, known) == SPAWN_ERR_NAME && asked == 0, "bad name refused before the lookup");
    check(spawn_queue_push(&q, "Weapon_Sword_001", 0, known) == SPAWN_ERR_COUNT, "count 0 refused");
    check(spawn_queue_push(&q, "Weapon_Sword_001", SPAWN_COUNT_MAX + 1, known) == SPAWN_ERR_COUNT, "count above the limit refused");
    check(spawn_queue_push(&q, "Weapon_Sword_999", 1, known) == SPAWN_ERR_UNKNOWN && asked == 1, "unknown actor refused");
    check(q.len == 0, "refusals queue nothing");
    check(spawn_queue_push(&q, "Weapon_Sword_001", 3, known) == SPAWN_OK, "known actor queued");
    check(spawn_queue_push(&q, "Item_Fruit_A", SPAWN_COUNT_MAX, NULL) == SPAWN_OK, "no lookup: any well-formed name");
    check(spawn_queue_pop(&q, &r) && !strcmp(r.name, "Weapon_Sword_001") && r.count == 3, "first in, first out");
    check(spawn_queue_pop(&q, &r) && !strcmp(r.name, "Item_Fruit_A") && r.count == SPAWN_COUNT_MAX, "second");
    for (int i = 0; i < SPAWN_QUEUE_MAX; i++) check(spawn_queue_push(&q, "Item_Fruit_A", 1, known) == SPAWN_OK, "fill");
    check(spawn_queue_push(&q, "Item_Fruit_A", 1, known) == SPAWN_ERR_FULL, "full queue refuses");
    for (int i = 0; i < SPAWN_QUEUE_MAX; i++) check(spawn_queue_pop(&q, &r), "drain (wraps around)");
    check(!spawn_queue_pop(&q, &r) && q.len == 0, "drained");

    /* errors have text */
    for (int code = SPAWN_ERR_UNSUPPORTED; code <= SPAWN_OK; code++) check(strcmp(spawn_error_text(code), "unknown error"), "error text");

    /* placement: ahead along the facing, one metre up, spread sideways, never on top of each other */
    float o[3];
    spawn_offset(0, 1, 0, 1, 3, o);
    check(fabsf(o[0]) < 1e-6f && o[1] == 1 && fabsf(o[2] - 3) < 1e-6f, "single actor straight ahead");
    spawn_offset(0, 1, 1, 0, 3, o);
    check(fabsf(o[0] - 3) < 1e-6f && fabsf(o[2]) < 1e-6f, "follows the facing");
    float seen[SPAWN_COUNT_MAX][3];
    for (int k = 0; k < SPAWN_COUNT_MAX; k++) {
        spawn_offset(k, SPAWN_COUNT_MAX, 0.6f, 0.8f, 3, seen[k]);
        float ahead = seen[k][0] * 0.6f + seen[k][2] * 0.8f;
        check(ahead >= 3 - 1e-4f && ahead <= 3 + 1.2f * 3 + 1e-4f, "within the rows ahead");
        for (int j = 0; j < k; j++) {
            float dx = seen[k][0] - seen[j][0], dz = seen[k][2] - seen[j][2];
            check(dx * dx + dz * dz > 1.0f, "at least a metre apart");
        }
    }
    printf("actor spawn queue: %u checks passed\n", checks);
    return 0;
}
