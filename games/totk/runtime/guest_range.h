#pragma once
#include <stdint.h>

/* Overflow-safe range checks shared by the cheat memory guard (cheats.c, services.c runtime_guest_access) and its probe. */

/* [a, a + n) wholly inside [base, base + size). */
static inline int guest_range_in(uint64_t a, uint64_t n, uint64_t base, uint64_t size) {
    return n && a + n > a && a >= base && n <= size && a - base <= size - n;
}
/* Every 4 KiB page of [a, a + n) is marked in pages (one byte per page from start; region of size bytes). */
static inline int guest_pages_mapped(const unsigned char *pages, uint64_t start, uint64_t size, uint64_t a, uint64_t n) {
    if (!guest_range_in(a, n, start, size)) return 0;
    for (uint64_t p = (a - start) / 4096; p <= (a + n - 1 - start) / 4096; p++) if (!pages[p]) return 0;
    return 1;
}
/* The main module, at runtime: 1 when a cheat may read (anywhere before the runtime's key page) or write (only data and
   bss, [data_off, image_end)); 2 when a write falls wholly on code or read-only data ([0, data_off), never writable while
   the game runs: the caller may only compare it with what was applied at launch); 0 otherwise (straddling). */
static inline int guest_main_access(uint64_t base, uint64_t data_off, uint64_t image_end, uint64_t a, uint64_t n, int write) {
    if (!base) return 0;
    if (!write) return guest_range_in(a, n, base, image_end);
    if (guest_range_in(a, n, base + data_off, image_end - data_off)) return 1;
    if (guest_range_in(a, n, base, data_off)) return 2;
    return 0;
}
