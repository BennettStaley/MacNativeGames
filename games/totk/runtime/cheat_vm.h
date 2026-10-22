#pragma once
#include <stddef.h>
#include <stdint.h>

/* Atmosphère cheat programs (the dmnt cheat VM format the Switch scene and Yuzu/Ryujinx use: "[Name]" sections of 32-bit
   hex words, "{Master}" sections that always run), written from Atmosphère's docs/features/cheats.md. Game-independent:
   the runtime glue (cheats.c) supplies the memory region bases, guarded memory access and the held buttons. */
enum { CHEAT_MAX = 1024, CHEAT_MAX_WORDS = 0x400, CHEAT_NAME = 128, CHEAT_ERR = 112 };
typedef struct {
    char name[CHEAT_NAME];        /* as written (group markers repeat a name); control characters become spaces */
    char error[CHEAT_ERR];        /* why this section cannot run (bad word, unknown instruction); empty when fine */
    int master;                   /* {Master code}: runs first every tick, before the enabled cheats */
    unsigned n, line;             /* instruction words; line of the section header (1-based) */
    unsigned dup;                 /* earlier sections of the file with the same name: keys a choice (cheat_choice_name) */
    uint32_t w[CHEAT_MAX_WORDS];
    uint64_t latch[CHEAT_MAX_WORDS / 64];   /* C4 with auto-repeat 0: fired and keys still held (bit = word index) */
} Cheat;

/* Parses a cheat file's text into out (at most max sections). Never fails as a whole: a section with a malformed line or
   instruction keeps its name with error set (and is refused), the others are unaffected. Accepts a UTF-8 BOM, CRLF line
   ends, and "//" or "#" comments. warn (may be NULL) receives file-level notes (words before the first section, sections
   past max), or "". Sections without any instruction words (titles, notes) are kept with n = 0. Returns the count. */
int cheat_parse(const char *text, Cheat *out, int max, char *warn, size_t warnn);
/* The name a choice is saved under: the name, or for a repeated one "name (#2)", "name (#3)"..., so sections that share
   a name are switched on and off separately. */
void cheat_choice_name(const Cheat *c, char *out, size_t n);
/* Instruction length in words starting at w[i] (0: unknown opcode or truncated). */
unsigned cheat_insn_len(const uint32_t *w, unsigned n, unsigned i);
/* Static check of every instruction's fields (widths, memory types, conditions, operations). 1 when runnable. */
int cheat_check(const uint32_t *w, unsigned n, char *err, size_t errn);

/* Memory types: 0 main module, 1 heap, 2 alias region, 3 ASLR region, 4 non-relative (absolute; base[4] is 0). */
typedef struct {
    uint64_t base[5];
    uint64_t keys;   /* held buttons, HID npad bit order (A = bit 0, B, X, Y, ...) */
    unsigned max_steps;   /* instructions one cheat may execute per run (loops); 0: CHEAT_STEPS */
    void *ctx;
    int (*read)(void *ctx, uint64_t addr, void *out, unsigned size);         /* 0: not readable */
    int (*write)(void *ctx, uint64_t addr, const void *in, unsigned size);   /* 0: refused */
} CheatEnv;
enum { CHEAT_STEPS = 1 << 16 };

/* Registers of one tick. Atmosphère runs the master code and the enabled cheats as one program, so registers (and saved
   registers) set by the master code are seen by the cheats after it; they start at 0 every tick (cheat_vm_tick). The
   static registers (C3) keep their values across ticks. */
typedef struct { uint64_t reg[16], saved[16], statics[256]; } CheatVm;
void cheat_vm_tick(CheatVm *vm);
/* Runs one cheat program once on vm. Returns 1, or 0 when it stopped early: a failed read (a pointer not set up yet)
   leaves err empty; a refused write, a malformed program or too many steps set err. */
int cheat_run(Cheat *c, CheatVm *vm, const CheatEnv *env, char *err, size_t errn);
