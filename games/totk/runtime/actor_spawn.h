/* Actor spawner (actor_spawn.c): queue an actor by name; it is created a few metres in front of the player at the next
   frame end. Return values and limits: actor_spawn_queue.h. */
#pragma once
#include "runtime.h"
#include <stdatomic.h>
#include <stdint.h>

/* 0 queued, or a negative SPAWN_ERR_* (runtime_spawn_error names it). Any thread. */
int runtime_spawn_actor(const char *name, int count);
const char *runtime_spawn_error(int code);
/* "spawn <name> [count]" from the test input file: 1 when the line was a spawn command (handled or refused). */
int runtime_spawn_command(const char *line);
void runtime_spawn_init(Module *main_module);
/* Nonzero while requests wait for the frame-end thread. */
extern _Atomic int runtime_spawn_waiting;
void runtime_spawn_tick(uintptr_t player_actor, uint64_t frame);
