/* The actor spawner's request queue and name check (actor_spawn.c), kept free of game code so tests/test_actor_spawn.c
   runs them on any Mac. Callers hold their own lock; nothing here allocates. */
#pragma once
#include <string.h>

enum {
    SPAWN_NAME_MAX = 96,     /* the longest actor name in 1.4.3 is 69 characters */
    SPAWN_COUNT_MAX = 20,    /* one request; the game creates them over the next frames */
    SPAWN_QUEUE_MAX = 16,    /* requests waiting for the next frame end */
};

/* What runtime_spawn_actor returns: 0 queued, else one of these (negative). */
enum {
    SPAWN_OK = 0,
    SPAWN_ERR_NAME = -1,         /* empty, too long or not [A-Za-z0-9_] */
    SPAWN_ERR_COUNT = -2,        /* outside 1..SPAWN_COUNT_MAX */
    SPAWN_ERR_UNKNOWN = -3,      /* no Pack/Actor/<name>.pack.zs in the RomFS (or a mod) */
    SPAWN_ERR_FULL = -4,         /* SPAWN_QUEUE_MAX requests already waiting */
    SPAWN_ERR_DISABLED = -5,     /* the spawner is not on in this session */
    SPAWN_ERR_UNSUPPORTED = -6,  /* this game version has no verified spawn call */
};

static inline const char *spawn_error_text(int code) {
    switch (code) {
    case SPAWN_OK: return "queued";
    case SPAWN_ERR_NAME: return "invalid actor name";
    case SPAWN_ERR_COUNT: return "count must be 1..20";
    case SPAWN_ERR_UNKNOWN: return "no such actor in this game";
    case SPAWN_ERR_FULL: return "too many spawns waiting";
    case SPAWN_ERR_DISABLED: return "spawner not enabled";
    case SPAWN_ERR_UNSUPPORTED: return "not supported on this game version";
    default: return "unknown error";
    }
}

/* Actor names are ASCII identifiers (Weapon_Sword_001, Enemy_Bokoblin_Junior); anything else is refused before it can
   reach the game or a RomFS path. */
static inline int spawn_name_valid(const char *name) {
    if (!name || !*name) return 0;
    size_t n = 0;
    for (const char *p = name; *p; ++p, ++n) {
        char c = *p;
        if (n >= SPAWN_NAME_MAX - 1) return 0;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return 0;
    }
    return 1;
}

/* Where a spawn goes (the Spawn tab's Place popup, kept in spawn-picks.txt as "place <word>"). */
enum { SPAWN_PLACE_FRONT, SPAWN_PLACE_ABOVE, SPAWN_PLACE_FAR, SPAWN_PLACE_FEET, SPAWN_PLACE_COUNT };
static const char *const SPAWN_PLACE_NAMES[SPAWN_PLACE_COUNT] = {"In front", "Above", "Far", "At Link's feet"};
static const char *const SPAWN_PLACE_WORDS[SPAWN_PLACE_COUNT] = {"front", "above", "far", "feet"};   /* file and test command */
/* The placement for a word ("front", "above", "far", "feet"), or -1. */
static inline int spawn_place_parse(const char *word) {
    for (int i = 0; word && i < SPAWN_PLACE_COUNT; i++) if (!strcmp(word, SPAWN_PLACE_WORDS[i])) return i;
    return -1;
}

typedef struct { char name[SPAWN_NAME_MAX]; int count, place; } SpawnRequest;
typedef struct { SpawnRequest item[SPAWN_QUEUE_MAX]; unsigned head, len; } SpawnQueue;

/* Validated push: SPAWN_OK or an error. exists (may be NULL) is asked only for a well-formed name. */
static inline int spawn_queue_push_at(SpawnQueue *q, const char *name, int count, int place, int (*exists)(const char *name)) {
    if (!spawn_name_valid(name)) return SPAWN_ERR_NAME;
    if (count < 1 || count > SPAWN_COUNT_MAX) return SPAWN_ERR_COUNT;
    if (exists && !exists(name)) return SPAWN_ERR_UNKNOWN;
    if (q->len >= SPAWN_QUEUE_MAX) return SPAWN_ERR_FULL;
    SpawnRequest *r = &q->item[(q->head + q->len) % SPAWN_QUEUE_MAX];
    strcpy(r->name, name);   /* spawn_name_valid bounded it */
    r->count = count;
    r->place = place >= 0 && place < SPAWN_PLACE_COUNT ? place : SPAWN_PLACE_FRONT;
    q->len++;
    return SPAWN_OK;
}
static inline int spawn_queue_push(SpawnQueue *q, const char *name, int count, int (*exists)(const char *name)) {
    return spawn_queue_push_at(q, name, count, SPAWN_PLACE_FRONT, exists);
}

static inline int spawn_queue_pop(SpawnQueue *q, SpawnRequest *out) {
    if (!q->len) return 0;
    *out = q->item[q->head];
    q->head = (q->head + 1) % SPAWN_QUEUE_MAX;
    q->len--;
    return 1;
}

/* Where the k-th of count actors goes, relative to the player: dist metres ahead along the facing (fx, fz, unit length in
   the ground plane), the group spread sideways 1.2 m apart and in rows of five, up metres up (spawn_offset: 1 m, so it
   settles onto the ground). */
static inline void spawn_offset_up(int k, int count, float fx, float fz, float dist, float up, float out[3]) {
    int per_row = count < 5 ? count : 5, row = k / 5, col = k % 5;
    int in_row = (row == (count - 1) / 5) ? count - row * 5 : per_row;
    float side = ((float)col - (float)(in_row - 1) * 0.5f) * 1.2f, ahead = dist + (float)row * 1.2f;
    out[0] = fx * ahead - fz * side;   /* the side vector is the facing turned 90 degrees in the ground plane */
    out[1] = up;
    out[2] = fz * ahead + fx * side;
}
static inline void spawn_offset(int k, int count, float fx, float fz, float dist, float out[3]) {
    spawn_offset_up(k, count, fx, fz, dist, 1.0f, out);
}
/* The same group for a placement: in front 3 m ahead and 1 m up; above 1 m ahead and 8 m up, so it drops in; far 15 m
   ahead and 2 m up (the ground there may be higher); at Link's feet on his spot, 0.3 m up. */
static inline void spawn_place_offset(int k, int count, float fx, float fz, int place, float out[3]) {
    switch (place) {
    case SPAWN_PLACE_ABOVE: spawn_offset_up(k, count, fx, fz, 1.0f, 8.0f, out); break;
    case SPAWN_PLACE_FAR: spawn_offset_up(k, count, fx, fz, 15.0f, 2.0f, out); break;
    case SPAWN_PLACE_FEET: spawn_offset_up(k, count, fx, fz, 0.0f, 0.3f, out); break;
    default: spawn_offset_up(k, count, fx, fz, 3.0f, 1.0f, out); break;
    }
}
