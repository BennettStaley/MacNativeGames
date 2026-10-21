#pragma once
#include "cheat_vm.h"

/* Cheats that write instructions into the main module's code. Here the code is never writable while guest threads run it,
   so those words are written once at launch, before the modules are sealed r-x; the rest of such a cheat (its data
   writes, button conditions) runs on the cheat thread like any other cheat, where a write of a code word it already
   applied is a no-op compare. This file decides which words those are and whether they are safe to write, against the
   ground truth of what the port changed:
   - changed: words the Apple-silicon rewrite (patch_nso.py) changed, from the original main text vs the rewritten
     image; moved: instruction windows the rewrite runs from its patch area (their words in place are dead). Both are
     generated at build time (scripts/prepare_runtime.py) into the module table.
   - image_word: the verified rewritten image, so any word that differs in memory was patched by the runtime itself
     (game_patches*.c, its veneers in the padding after the text). */
typedef struct {
    const uint8_t *mem;                         /* main as loaded now */
    uint64_t text_end, rx_end, image_end;       /* end of code; end of the executable padding page; start of the key page */
    const uint32_t *changed; unsigned nchanged; /* sorted, disjoint [start, end) offset pairs */
    const uint32_t *moved; unsigned nmoved;     /* sorted, disjoint [start, end) offset pairs */
    int (*image_word)(void *ctx, uint64_t off, uint32_t *out);
    void *ctx;
} CheatMain;
typedef struct { uint64_t off; uint32_t value; int cheat; } CheatWord;
enum { CHEAT_CODE_WORDS = 256 };
typedef struct {
    CheatWord w[CHEAT_CODE_WORDS];
    unsigned n;
    int runtime;   /* the cheat also has work for the cheat thread (memory reads or data writes, button conditions) */
    int conflict;  /* a word an earlier cheat (prior[].cheat) wrote with a different value, or -1 */
} CheatCode;

/* Finds the main-module code words cheat c writes and checks each. Registers are followed through the master codes
   (which run first, on the same registers) and c's straight-line code, so "0TMR..." with a register offset counts when
   the register's value is known. Returns 1 with out filled (out->n may be 0), or 0 with err when a code write:
   is under a condition or in a loop (it cannot be split from the condition), has a register-dependent address that is
   not known, is not whole aligned instructions, overlaps an instruction the rewrite changed or moved, overlaps the
   runtime's own patches, writes an instruction the rewrite would have had to change (svc, the thread registers), or
   writes a branch (B, BL, B.cond, CBZ/CBNZ, TBZ/TBNZ) or PC-relative reference (ADR, ADRP, LDR literal) whose target
   leaves what it can reach (the code, or for data references the module image). prior: code words written at launch by
   cheats applied before this one. */
int cheat_code_scan(const CheatMain *m, const Cheat *const *masters, int nmasters, const Cheat *c,
                    const CheatWord *prior, unsigned nprior, CheatCode *out, char *err, size_t errn);

