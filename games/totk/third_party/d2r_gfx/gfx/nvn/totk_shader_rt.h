// TOTK runtime shader translation (totk_shader_rt.m).
#pragma once
#ifdef __OBJC__
#import <Foundation/Foundation.h>
#include <stdint.h>
NSString *totk_rt_dir(void);
// Returns the variant name whose <dir>/<variant>_b.{metal,json} match the answers, translating when needed.
// stage: NVN shader stage (0 vertex, 1 fragment, 5 compute). answers: {"tex","img","attr","fout"} dictionaries.
NSString *totk_rt_variant(NSString *shader, int stage, const uint8_t *control, const uint8_t *code, NSDictionary *answers, NSArray *local);   // nil on iOS when the variant is not translated yet (request recorded)
NSString *totk_rt_root(void);   // the game folder (the executable lives in its build/)
/* Mac: a variant the shared shader list names (totk_shader_list.m), translated in the background by a translator process
   of its own at utility priority, so the game's own translations never wait for it; only the index update takes the
   translation lock. Never fatal: nil and *why on failure. *fresh: 1 when it added a variant, 0 when the translation
   gave deps a cached variant already has (that variant's name is returned). code_len: the bytes code points to. */
NSString *totk_rt_prepare(NSString *shader, int stage, const uint8_t *control, const uint8_t *code, size_t code_len,
                          NSDictionary *answers, int *fresh, NSString **why);
/* The shared shader list (totk_shader_list.m; Mac only, no-ops on iOS). boot: from the warm-up's start, fetches the list in
   the background (at most once a day, download only); warm_done: from the end of the warm-up, prepares the variants the
   cache lacks, compiling each through compile (0 when it failed). */
void totk_shader_list_boot(void);
void totk_shader_list_warm_done(int (*compile)(NSString *variant_b, int stage));
#endif
#include <stdint.h>
void totk_shader_list_set_enabled(int on);   // the setting Interface > Download new shaders (settings.c, live)
int64_t totk_shader_list_pending(void);       // variants from the list still being prepared (the shader counter's "compiling")
uint64_t totk_rt_last_translation_ns(void);   // CLOCK_UPTIME_RAW time of the game's last own translation (0: none yet)
/* Variants new to the translation cache this run, the variants in the cache (-1 until counted in the background) and
   the CLOCK_UPTIME_RAW time of the last new one (0: none yet). Any pointer may be NULL. */
void totk_rt_counts(uint64_t *fresh, int64_t *total, uint64_t *last_new_ns);
