/* Actor spawner: creates any actor (items, weapons, enemies, Zonai devices, materials) next to the player by calling the
   game's own engine::actor::ActorMgr::requestCreateActorAsync, the call the game uses to place actors itself. No code is
   patched (so it works on iOS too): requests wait in a queue and run on the game's frame-end thread, the point where
   world_survey.c already calls game code (runtime_survey_tick, after the game's frame-end GPU wait). That tick is set up
   in every 1.4.3 session (loader.c runtime_frame_controller_return_pc); the survey's own commands stay test-only, and in
   normal play only the menu's Spawn tab queues requests (the input file's "spawn" needs TOTK_SURVEY=1).

   1.4.3 addresses (game_version.h, GAME_SPAWN_*), found from 1.0.0, whose symbols are public (totk-lotuskit
   syms/exking/engine/actor/ActorMgr.py, from dt-12345/totk_syms):
     requestCreateActorAsync 1.0.0 0xa5fdf8 -> 1.4.3 0x599670: the only target that the mapped callers agree on
       (scripts/map_version_sites.py, by callers), 108 direct callers in 1.4.3 against 105 in 1.0.0, the same 12 calls
       inside, identical first seven instructions, and the same CreateArg fields read (+0x48..+0x53 bytes, +0x68..+0x88).
     ActorMgr::sInstance 1.0.0 0x46479e0 -> 1.4.3 0x3ad7450: 87 of the callers load x0 from one GOT slot (1.0.0 0x4557b48,
       1.4.3 0x395e008) whose relocation names exactly these addresses; map_data finds the same through code references.
   Call shape and CreateArg layout (facts, not code, from totk-lotuskit and dt-12345/actor_spawn, both GPL-2.0):
     bool requestCreateActorAsync(ActorMgr *, const SafeString &name, const CreateArg &, CreateWatcherRef *,
                                  CreatePriority (High = 1), PreActor *, ActorFile *, Function *, bool,
                                  Result *, PreActor **out);
   SafeString here is one pointer (the function reads the name through [x1]). CreateArg is 0x98 bytes, below. */
#include "actor_spawn.h"
#include "actor_spawn_queue.h"
#include "game_version.h"
#include "world_survey.h"
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(TOTK_GAME_V143) && !defined(GAME_BOTW)
#define SPAWN_SUPPORTED 1
#endif

_Atomic int runtime_spawn_waiting;
static _Atomic int spawn_commands;   /* the input file's "spawn" lines: test profiles only (world_survey.c) */
static _Atomic(RuntimeSpawnObserver) spawn_observer;
void runtime_spawn_allow_commands(void) { atomic_store(&spawn_commands, 1); }
void runtime_spawn_observe(RuntimeSpawnObserver fn) { atomic_store(&spawn_observer, fn); }
#ifdef SPAWN_SUPPORTED
static pthread_mutex_t spawn_lock = PTHREAD_MUTEX_INITIALIZER;
static SpawnQueue spawn_queue;
static _Atomic int spawn_enabled;

int runtime_romfs_has_file(const char *path);   /* romfs.c */
static int actor_exists(const char *name) {
    char path[SPAWN_NAME_MAX + 32];
    snprintf(path, sizeof path, "Pack/Actor/%s.pack.zs", name);
    return runtime_romfs_has_file(path);
}
#endif

int runtime_spawn_actor(const char *name, int count) {
#ifndef SPAWN_SUPPORTED
    (void)name; (void)count;
    return SPAWN_ERR_UNSUPPORTED;
#else
    if (!atomic_load(&spawn_enabled)) return SPAWN_ERR_DISABLED;
    pthread_mutex_lock(&spawn_lock);
    int r = spawn_queue_push(&spawn_queue, name, count, actor_exists);
    pthread_mutex_unlock(&spawn_lock);
    if (r == SPAWN_OK) {   /* waiting first, then the frame-end wake-up (world_survey.c clears them in the other order) */
        atomic_store(&runtime_spawn_waiting, 1);
        atomic_store(&runtime_survey_pending, 1);
    }
    return r;
#endif
}

const char *runtime_spawn_error(int code) { return spawn_error_text(code); }

int runtime_spawn_available(void) {
#ifndef SPAWN_SUPPORTED
    return SPAWN_ERR_UNSUPPORTED;
#else
    return atomic_load(&spawn_enabled) ? SPAWN_OK : SPAWN_ERR_DISABLED;
#endif
}

/* "spawn <name> [count]" from the test input file (input.c), next to world_survey's "survey" commands. */
int runtime_spawn_command(const char *line) {
    if (strncmp(line, "spawn ", 6)) return 0;
    if (!atomic_load(&spawn_commands)) {
        fprintf(stderr, "[spawn] rejected: the input file's spawn needs TOTK_SURVEY=1 on a test profile (players use the Spawn tab)\n");
        return 1;
    }
    char name[SPAWN_NAME_MAX + 1] = {0}; int count = 1, used = 0;
    if (!(sscanf(line, "spawn %96s %d %n", name, &count, &used) == 2 && used && !line[used])) {
        count = 1; used = 0;
        if (!(sscanf(line, "spawn %96s %n", name, &used) == 1 && used && !line[used])) {
            fprintf(stderr, "[spawn] rejected: expected spawn <actor name> [count 1..%d]\n", SPAWN_COUNT_MAX);
            return 1;
        }
    }
    int r = runtime_spawn_actor(name, count);
    if (r == SPAWN_OK) fprintf(stderr, "[spawn] queued name=%s count=%d\n", name, count);
    else fprintf(stderr, "[spawn] refused name=%.96s count=%d: %s\n", spawn_name_valid(name) ? name : "(invalid)", count, spawn_error_text(r));
    return 1;
}

#ifndef SPAWN_SUPPORTED
int runtime_spawn_init(Module *m) { (void)m; return 0; }
void runtime_spawn_tick(uintptr_t player, uint64_t frame) { (void)player; (void)frame; }
#else
typedef struct {
    float position[3];
    float rotation[9];          /* row-major 3x3 */
    float scale[3];
    uint32_t pad_3c;
    const char *name_40;        /* a SafeString, "" */
    uint8_t b48[8];             /* read as bytes by the call: +0x48, +0x4b, +0x4d */
    uint8_t b50[4];             /* +0x50, +0x51 (halfword), +0x53 */
    uint32_t pad_54;
    void *blackboard_info;      /* bb::InitInfo<32> * */
    void *blackboard_60;
    void *parent, *dependent, *creator, *creator_rtti, *instance_heap;
    uint8_t transform_flags;    /* 1 position, 2 rotation, 4 scale */
    uint8_t pad_91[7];
} CreateArg;
_Static_assert(sizeof(CreateArg) == 0x98, "CreateArg is 0x98 bytes");
_Static_assert(__builtin_offsetof(CreateArg, blackboard_info) == 0x58, "blackboard at +0x58");
_Static_assert(__builtin_offsetof(CreateArg, instance_heap) == 0x88, "heap at +0x88");
_Static_assert(__builtin_offsetof(CreateArg, transform_flags) == 0x90, "flags at +0x90");

/* bb::InitInfo<32>: sead::Buffer<Param> {s32 size; Param *ptr}, s32 count, then 32 Params of 0xc8 bytes; empty here. */
typedef struct { int32_t size; uint32_t pad; void *params; int32_t count; uint32_t pad2; uint8_t buffer[32][0xc8]; } BlackboardInit;
_Static_assert(sizeof(BlackboardInit) == 0x1918, "bb::InitInfo<32> is 0x1918 bytes");

typedef _Bool (*RequestCreate)(void *mgr, const char *const *name, const CreateArg *arg, void *watcher, uint32_t priority,
                               void *pre_actor, void *actor_file, void *function, _Bool flag, uint32_t *result, void **out);
static RequestCreate request_create;
static void **actor_mgr_slot;

static const char *result_text(uint32_t r) {
    static const char *const names[] = {"FailedToAcquireWatcher", "Ok", "EmptyActorName", "FailedToCreatePreActor",
        "FailedToCreateBlackboard", "EmptyNameRef", "NoMatchingFactory", "FailedToAllocateRequest", "InvalidWatcher"};
    return r < sizeof names / sizeof *names ? names[r] : "?";
}

/* Every 1.4.3 session: the call is only made for requests the player (the Spawn tab) or a test queued. */
int runtime_spawn_init(Module *m) {
    static const uint32_t entry[] = {0xa9ba7bfd, 0xa9016ffc, 0x910003fd, 0xa90267fa, 0xa9035ff8, 0xa90457f6, 0xa9054ff4};
    if (strcmp(m->name, "main") || GAME_SPAWN_REQUEST_CREATE + sizeof entry > m->text_end || GAME_SPAWN_ACTOR_MGR + 8u > m->size ||
        memcmp(m->base + GAME_SPAWN_REQUEST_CREATE, entry, sizeof entry)) {
        fprintf(stderr, "[spawn] off: requestCreateActorAsync entry does not match " TOTK_GAME_VERSION "\n");
        return 0;
    }
    request_create = (RequestCreate)(void *)(m->base + GAME_SPAWN_REQUEST_CREATE);
    actor_mgr_slot = (void **)(void *)(m->base + GAME_SPAWN_ACTOR_MGR);
    atomic_store(&spawn_enabled, 1);
    fprintf(stderr, "[spawn] enabled: the Spawn tab's requests run on the game's frame-end thread\n");
    return 1;
}

static void spawn_one(const SpawnRequest *r, const float base[3], float fx, float fz, uint64_t frame) {
    static BlackboardInit blackboard;   /* frame-end thread only; the call copies what it needs */
    void *mgr = *actor_mgr_slot;
    RuntimeSpawnObserver observer = atomic_load(&spawn_observer);
    if (!mgr) {
        fprintf(stderr, "[spawn] name=%s status=no_actor_mgr frame=%llu\n", r->name, (unsigned long long)frame);
        if (observer) observer(r->name, r->count, 0, "the game's actor manager is not up yet");
        return;
    }
    unsigned created = 0; uint32_t last = 0;
    for (int k = 0; k < r->count; ++k) {
        float off[3];
        spawn_offset(k, r->count, fx, fz, 3.0f, off);
        CreateArg arg;
        memset(&arg, 0, sizeof arg);
        for (int c = 0; c < 3; c++) arg.position[c] = base[c] + off[c];
        arg.rotation[0] = arg.rotation[4] = arg.rotation[8] = 1.0f;
        arg.scale[0] = arg.scale[1] = arg.scale[2] = 1.0f;
        arg.name_40 = "";
        memset(&blackboard, 0, sizeof blackboard);
        blackboard.size = 32; blackboard.params = blackboard.buffer;
        arg.blackboard_info = &blackboard;
        arg.transform_flags = 1 | 2 | 4;
        const char *name = r->name;
        uint32_t result = 0; void *pre_actor = NULL;
        _Bool ok = request_create(mgr, &name, &arg, NULL, 1, NULL, NULL, NULL, 0, &result, &pre_actor);
        created += ok; last = result;
        if (!ok) break;   /* the same name fails the same way again */
    }
    fprintf(stderr, "[spawn] name=%s requested=%d created=%u result=%u(%s) at=%.2f,%.2f,%.2f frame=%llu\n", r->name, r->count,
            created, last, result_text(last), base[0], base[1], base[2], (unsigned long long)frame);
    if (observer) {
        char why[96];
        snprintf(why, sizeof why, "the game refused it (%s)", result_text(last));
        observer(r->name, r->count, (int)created, (int)created == r->count ? NULL : why);
    }
}

/* Frame-end thread (world_survey.c runtime_survey_tick). player: the Player actor, or 0 when there is none yet. */
void runtime_spawn_tick(uintptr_t player, uint64_t frame) {
    if (!atomic_load_explicit(&runtime_spawn_waiting, memory_order_acquire)) return;
    pthread_mutex_lock(&spawn_lock);
    SpawnRequest batch[SPAWN_QUEUE_MAX]; unsigned n = 0;
    if (player) while (n < SPAWN_QUEUE_MAX && spawn_queue_pop(&spawn_queue, &batch[n])) n++;
    atomic_store_explicit(&runtime_spawn_waiting, spawn_queue.len != 0, memory_order_release);
    pthread_mutex_unlock(&spawn_lock);
    if (!player) return;   /* loading or a menu: keep the requests for a frame with a player */
    float pos[3], rot[9];
    memcpy(pos, (const void *)(player + GAME_ACTOR_POS), sizeof pos);
    memcpy(rot, (const void *)(player + GAME_ACTOR_ROT), sizeof rot);
    /* the actor's facing is its local +Z: the third column of the row-major rotation, flattened to the ground */
    float fx = rot[2], fz = rot[8], len = sqrtf(fx * fx + fz * fz);
    if (!isfinite(len) || len < 1e-3f) { fx = 0; fz = 1; } else { fx /= len; fz /= len; }
    for (unsigned i = 0; i < n; ++i) spawn_one(&batch[i], pos, fx, fz, frame);
}
#endif
