// Write-protect dirty-page tracking for guest memory (nvn_dirty.c)
#pragma once
#include <stddef.h>
#include <stdint.h>

void nvn_dirty_add_range(const void *mem, size_t size);           // Register a memory pool as a tracked range (the first call registers the page-fault hook and the host pre-write hook)
void nvn_dirty_stat(int *live, int *peak, int *cap);                // number of registered memory pools, peak, tracking table capacity (for the memory readout line)
void nvn_dirty_remove_range(const void *mem);                     // Pool destruction: restore protected pages to writable and mark invalid
int nvn_dirty_tracked(const void *addr, size_t len);
void nvn_dirty_protect(const void *addr, size_t len);              // Set the covered pages read-only
uint64_t nvn_dirty_now(void);                                      // Current generation: taken before upload, as the stamp of that content
int nvn_dirty_since(const void *addr, size_t len, uint64_t stamp);
int nvn_dirty_check(const void *addr, size_t len, uint64_t stamp);  // TOTK: -1 untracked, else nvn_dirty_since (one range lookup, chunk-level skip)
void *nvn_dirty_gpu_write_begin(void *addr, size_t len);   // write guest memory on behalf of the GPU: not counted as a CPU write
void nvn_dirty_gpu_write_end(void *tok);
typedef struct { uintptr_t page; uint64_t pc, lr; uint32_t src; } nvn_dirty_rec;   // src 0: guest write (fault), 1: host write
int nvn_dirty_recent(uintptr_t lo, uintptr_t hi, nvn_dirty_rec *out, int max);   // latest unprotections in [lo, hi) (DM_NVN_TILELOG)
void nvn_dirty_stats(uint64_t *faults, uint64_t *preps);           // Cumulative: number of page faults triggered by game writes, number of pages unprotected before host writes
void nvn_dirty_mp_stats(uint64_t *calls, uint64_t *pages, uint64_t *ns);   // Cumulative: this module's mprotect call count, pages covered, time spent
void nvn_dirty_slot_stats(int *live, int *used_slots, uint64_t *adds_total); // Currently live slots, slot table high-water mark, total number of registrations
