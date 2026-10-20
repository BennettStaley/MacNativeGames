/* 1.4.3 has a new compiler and frame-controller layout. Every adapted game-code
   site below is checked before any write; older versions use game_patches.c. */
#include "aspect_patch.h"
#include "game_patches.h"
#include "game_version.h"
#include "world_survey.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <math.h>
#include <TargetConditionals.h>
#include <sys/sysctl.h>
#if TARGET_OS_IPHONE
#include <os/proc.h>
#endif

typedef struct { uint64_t off; uint32_t original, replacement; } Patch;
enum { FOG_STUB_AT = 400, SHADOW_STUB_AT = 448 };   /* text padding offsets from the stub base (the fog and shadow stubs, to +504) */
static const Patch fps60[] = {
    {0x2db204, 0x6b08015f, 0x7100055f},
    {0x2db208, 0x1a89b10a, 0x1a9fa52a},
    {0x2db22c, 0xb940468a, 0xd503201f},
    {0x2db360, 0x6b0801bf, 0x710005bf},
    {0x2db36c, 0x1a88c1ac, 0x1a9fc5ac},
    {0x2db340, 0xb9400288, 0x52800028},
    {0x2db34c, 0x2a0a03f5, 0x1a9fa555},
};


static _Atomic uint64_t *frame_step_data;
static _Atomic uint32_t *frame_interval_data;
static _Atomic uint32_t pinned_step_us;
static uint64_t last_present_ns;
static double frame_samples[5];
static unsigned frame_sample_count,frame_sample_next;
static void frame_step_store(float seconds){
    float values[3]={seconds*30.0f,seconds,seconds*60.0f};uint64_t bits;uint32_t interval;
    memcpy(&bits,values,8);memcpy(&interval,values+2,4);
    atomic_store_explicit(frame_step_data,bits,memory_order_release);
    atomic_store_explicit(frame_interval_data,interval,memory_order_release);
}
void runtime_frame_step_pin(double ms){
    uint32_t us=ms>0?(uint32_t)(ms*1000.0+0.5):0;atomic_store(&pinned_step_us,us);
    if(frame_step_data&&us)frame_step_store((float)(us/1e6));
    fprintf(stderr,"[totk] 1.4.3 frame step %s (pin %.3f ms)\n",us?"pinned":"real time",us/1000.0);
}
static void render_latch(void);static void output_latch(uint64_t now_ns);
void runtime_frame_step_update(uint64_t now_ns){
    output_latch(now_ns);
    render_latch();   /* the present boundary: a live scene size takes effect between two frames */
    if(!frame_step_data||!now_ns)return;
    if(atomic_load_explicit(&pinned_step_us,memory_order_relaxed)){last_present_ns=now_ns;return;}
    if(!last_present_ns){last_present_ns=now_ns;return;}
    double dt=(double)(now_ns-last_present_ns)*1e-9;last_present_ns=now_ns;
    if(dt>0.25||dt<0.001){frame_sample_count=frame_sample_next=0;return;}
    if(dt<1.0/120.0)dt=1.0/120.0;if(dt>1.0/15.0)dt=1.0/15.0;
    frame_samples[frame_sample_next++%5]=dt;if(frame_sample_count<5)frame_sample_count++;
    double sum=0;for(unsigned i=0;i<frame_sample_count;i++)sum+=frame_samples[i];
    frame_step_store((float)(sum/frame_sample_count));
}
static uint32_t adrp(unsigned reg,uint64_t pc,uint64_t target){
    int64_t pages=(int64_t)(target>>12)-(int64_t)(pc>>12);
    if(pages<-(1<<20)||pages>=(1<<20))runtime_fail("1.4.3 data outside ADRP range");
    uint32_t imm=(uint32_t)pages&0x1fffff;
    return 0x90000000|((imm&3)<<29)|((imm>>2)<<5)|reg;
}
static uint32_t branch(uint64_t pc,uint64_t target){
    int64_t delta=(int64_t)target-(int64_t)pc;
    if((delta&3)||delta<-(1<<27)||delta>=(1<<27))runtime_fail("1.4.3 patch branch outside range");
    return 0x14000000|(((uint32_t)(delta>>2))&0x3ffffff);
}
static void patch_realtime(Module *m){
    const char *enabled=getenv("TOTK_REALTIME");if(enabled&&!strcmp(enabled,"0"))return;
    uint64_t data=(m->file_size+0x3fff)&~0x3fffULL;
    /* Keep executable veneers inside the text segment. iOS embeds x0 as a
     * signed RX segment, so these bytes must be emitted by export-ios rather
     * than written after the app starts. */
    uint64_t stub=(m->text_end+15)&~15ULL;
    if(data+0x4000!=m->size||stub+16>((m->text_end+0x3fff)&~0x3fffULL))runtime_fail("1.4.3 frame-step patch layout");
    const Patch guards[]={
        {0x3e5658,0x1e210808,0},{0x2daa54,0xbd425260,0},{0x2daa58,0x52a84e08,0},
        {0x2daa6c,0x5e21d800,0},{0x2daa70,0x1e201820,0},{0x2daa74,0x1e2c1001,0},{0x2daa78,0x1e210800,0}};
    for(unsigned i=0;i<sizeof guards/sizeof *guards;i++){
#if !TARGET_OS_IPHONE
        uint32_t word;memcpy(&word,m->base+guards[i].off,4);
        if(word!=guards[i].original)runtime_fail("1.4.3 real-time original instruction mismatch");
#endif
    }
#if !TARGET_OS_IPHONE
    for(unsigned i=0;i<16;i++)if(m->base[stub+i])runtime_fail("1.4.3 frame-step veneer padding not empty");
#endif
    /* Replace the nominal 60/FPS interval BEFORE the original producer applies
     * its 0.5/1.0 selector (sys+0x1da bit 0). Both FrameRateMgr call sites use
     * this s8 value. Hooking the callee entry instead would discard that scale
     * and affect any other intentional interval setter. Keep the original fmul
     * and all FrameRateMgr ratio/inverse/seconds/FPS arithmetic untouched.
     * x9 contains live task flags here: use x8, which the next ldxr overwrites.
     * s0 is dead until the call sites copy s8 into it again. */
    uint32_t veneer[]={adrp(8,(uintptr_t)m->base+stub,(uintptr_t)m->base+data),0xbd400900,0x1e210808,branch(stub+12,0x3e565c)};
 #if TARGET_OS_IPHONE
    if(memcmp(m->base+stub,veneer,sizeof veneer))runtime_fail("1.4.3 embedded frame-step veneer mismatch");
 #else
    memcpy(m->base+stub,veneer,sizeof veneer);
 #endif
    uint32_t words[]={branch(0x3e5658,stub),adrp(9,(uintptr_t)m->base+0x2daa54,(uintptr_t)m->base+data),0xbd400120,0xd503201f,0xd503201f,0xd503201f,0xd503201f};
 #if TARGET_OS_IPHONE
    for(unsigned i=0;i<sizeof guards/sizeof *guards;i++){uint32_t got;memcpy(&got,m->base+guards[i].off,4);if(got!=words[i])runtime_fail("1.4.3 embedded real-time patch mismatch");}
 #else
    for(unsigned i=0;i<sizeof guards/sizeof *guards;i++)memcpy(m->base+guards[i].off,words+i,4);
 #endif
    frame_step_data=(_Atomic uint64_t *)(void *)(m->base+data);
    frame_interval_data=(_Atomic uint32_t *)(void *)(m->base+data+8);frame_step_store(1.0f/60.0f);
    fprintf(stderr,"[totk] 1.4.3 real-time frame step: 7 guarded sites, producer veneer +%#llx; original half-step selector and ratio/inverse/seconds/FPS calculations retained\n",(unsigned long long)stub);
}
static void patch_output_res(Module *m);
static void patch_render_res(Module *m);
void runtime_game_patches(Module *m) {
    runtime_survey_init(m);
    if (strcmp(m->name, "main")) runtime_fail("1.4.3 game patches expect main");
    patch_realtime(m);
    patch_output_res(m);
    patch_render_res(m);
    { static const uint64_t sites[] = {0x628c74, 0xb43460, 0x1ff04f4, 0x253fbb0};   /* camera update and setup, viewport x2 */
      aspect_install(m, "1.4.3", sites, 4, ((m->text_end + 15) & ~15ULL) + 48,   /* after the two veneers above */
                     (m->file_size + 0x3fff) & ~0x3fffULL); }
    /* Field of view (aspect_patch.h): the projection writers' fovy loads, 0x628c3c in the camera update (beside 0x628c74)
       and 0xb4342c in the camera setup (beside 0xb43460); each is followed by fmul s8, s0, s1 (the half angle), the fovy
       store at +0xa0 and the sin / cos / tan calls. Stubs after the four aspect ones; always in on the Mac and in the iOS
       export, the multiplier switching them on. */
    { static const uint64_t sites[] = {0x628c3c, 0xb4342c};
      static const uint32_t want[] = {0xbd413e60 /* ldr s0, [x19, #0x13c] */, 0xbd4066c0 /* ldr s0, [x22, #0x64] */};
      fov_install(m, "1.4.3", sites, want, 2, ((m->text_end + 15) & ~15ULL) + 48 + 4 * DPF_REPLACE, (m->file_size + 0x3fff) & ~0x3fffULL); }
    /* Fog density and shadow resolution (aspect_patch.h): text padding from +400 (fog, one 48-byte stub; the shadow's two
       28-byte stubs from +448, to +504), data page +80 (fog) and +84 (shadow). Always in on the Mac and in the iOS export. */
    { static const uint64_t sites[] = {0xd3050};
      static const uint32_t want[] = {0x1e211e49 /* fcsel s9, s18, s1, ne */};
      fog_install(m, "1.4.3", sites, want, 1, ((m->text_end + 15) & ~15ULL) + FOG_STUB_AT, (m->file_size + 0x3fff) & ~0x3fffULL); }
    shadow_install(m, "1.4.3", ((m->text_end + 15) & ~15ULL) + SHADOW_STUB_AT, (m->file_size + 0x3fff) & ~0x3fffULL);
    const char *enabled = getenv("TOTK_FPS60");
    if (enabled && !strcmp(enabled, "0")) {
        fprintf(stderr, "[totk] 1.4.3 shipping frame controller (TOTK_FPS60=0)\n");
        return;
    }
    for (unsigned i = 0; i < sizeof fps60 / sizeof *fps60; i++) {
#if !TARGET_OS_IPHONE
        uint32_t word;
        if (fps60[i].off + 4 > m->text_end) runtime_fail("1.4.3 FPS patch outside text");
        memcpy(&word, m->base + fps60[i].off, 4);
        if (word != fps60[i].original) runtime_fail("1.4.3 FPS patch original instruction mismatch");
#endif
    }
 #if TARGET_OS_IPHONE
    for (unsigned i = 0; i < sizeof fps60 / sizeof *fps60; i++) {
        uint32_t word; memcpy(&word,m->base+fps60[i].off,4);
        if(word!=fps60[i].replacement)runtime_fail("1.4.3 embedded FPS patch mismatch");
    }
 #else
    for (unsigned i = 0; i < sizeof fps60 / sizeof *fps60; i++)
        memcpy(m->base + fps60[i].off, &fps60[i].replacement, 4);
 #endif
    fprintf(stderr, "[totk] 1.4.3 60 FPS: seven verified controller instructions; original game frame-time calculation\n");
}

/* New display configuration object: +0x10 is display 0's canvas size, +0x508
 * its active crop, and +0x528 the floating display scale. The allocation path
 * multiplies the canvas by that scale (8d860..8d894). Publishing only +0x508
 * changes the final crop but leaves every UI/composition target at 1080p.
 * Pending output changes are bit 21 of +8. Keep the original rebuild path. */
static int output_full_at_start;
static unsigned output_w,output_h;
static _Atomic(uint8_t *) output_page;
static uint64_t pack_size(unsigned w,unsigned h){return (uint64_t)h<<32|w;}
uint64_t runtime_output_size_current(void){
    uint8_t *page=atomic_load(&output_page);
    return page?atomic_load((_Atomic uint64_t *)(void *)(page+32)):pack_size(1920,1080);
}
int runtime_output_res(unsigned *w,unsigned *h){
    static int decided,on;static unsigned cw,ch;
    if(!decided){
        const char *e=getenv("TOTK_OUTPUT_RES");
        /* iOS shows 720p only (the user, 2026-10-18): no larger swapchain, heaps or output there, whatever
           TOTK_OUTPUT_RES says. The iOS export runs these patches on the Mac to make the phone's embedded code
           (TOTK_EXPORT_IOS), so it follows the same rule: before, auto put the 2560x1440 swapchain into the phone's code,
           and the phone, which carries the heap overlay in its bundle, reserved 88 MiB more for a picture it never shows. */
        if(TARGET_OS_IPHONE||getenv("TOTK_EXPORT_IOS")){
            on=0;
            if(e&&*e&&strcmp(e,"0")&&strcmp(e,"auto"))fprintf(stderr,"[totk] TOTK_OUTPUT_RES=%s ignored: iOS shows 720p only\n",e);
        }else if(e&&!strcmp(e,"0"))on=0;
        else if(!e||!*e||!strcmp(e,"auto")){
            const char *dir=getenv("TOTK_ROMFS_PATCHES_OUT");char path[4096];
            snprintf(path,sizeof path,"%s/2560x1440/manifest.tsv",dir?dir:"extracted/v143/romfs-patches-out");
            FILE *f=fopen(path,"r");if(f){fclose(f);on=1;cw=2560;ch=1440;}
            /* Headroom (settings): a 4K output on a Mac with 16 GB or more when its overlay was built, with a 2560x1440 scene
               reserve (a 4K scene too would need a 1.5 GiB texture heap, above the 1 GiB a zero-copy GPU buffer may take). */
            uint64_t mem=0;size_t len=sizeof mem;sysctlbyname("hw.memsize",&mem,&len,NULL,0);
            snprintf(path,sizeof path,"%s/3840x2160-scene-2560x1440/manifest.tsv",dir?dir:"extracted/v143/romfs-patches-out");
            if(mem>=(16ULL<<30)&&(f=fopen(path,"r"))){fclose(f);on=1;cw=3840;ch=2160;}
            output_full_at_start=0;
        }else{
            char tail;
            if(sscanf(e,"%ux%u%c",&cw,&ch,&tail)!=2||cw<640||ch<360||cw>7680||ch>4320)runtime_fail("TOTK_OUTPUT_RES must be auto, 0 or widthxheight");
            on=1;output_full_at_start=1;
        }
        decided=1;
    }
    if(on){*w=cw;*h=ch;}return on;
}
/* The settings menu's output size (settings.c display_apply): any 16:9 size up to the swapchain, live through the game's own
   display-config rebuild (as key 3). Asked before the patch installs, it is the size the game starts at. */
static _Atomic uint64_t output_pending,output_want,output_want_ns;
void runtime_output_max(unsigned *w,unsigned *h){if(!runtime_output_res(w,h)){*w=1920;*h=1080;}}
int runtime_output_request(unsigned w,unsigned h){
    unsigned mw,mh;if(!runtime_output_res(&mw,&mh))return w==1920&&h==1080?0:-1;
    if(w<640||h<360||w>mw||h>mh)return -1;
    uint8_t *page=atomic_load(&output_page);
    if(!page){atomic_store(&output_pending,pack_size(w,h));return 0;}
    /* Each size rebuilds the game's canvas targets, and old and new are both held for a moment: a size clicked through
       on the way to another (several in a row ran past the GPU memory budget, 2026-10-20) is never applied. The present
       thread takes the asked size once it has stayed the same for 0.4 s (output_latch). */
    atomic_store(&output_want,pack_size(w,h));atomic_store(&output_want_ns,0);
    return 0;
}
static void output_latch(uint64_t now_ns){
    uint8_t *page=atomic_load(&output_page);uint64_t want=atomic_load(&output_want);
    if(!page||!want||!now_ns)return;
    _Atomic uint64_t *size=(_Atomic uint64_t *)(void *)(page+32);
    if(atomic_load(size)==want){atomic_store(&output_want,0);return;}
    uint64_t since=atomic_load(&output_want_ns);
    if(!since){atomic_store(&output_want_ns,now_ns);return;}
    if(now_ns-since<400000000ULL)return;
    if(!atomic_compare_exchange_strong(&output_want,&want,0))return;   /* asked again meanwhile: wait for that one */
    atomic_store(size,want);
    uint64_t config=atomic_load((_Atomic uint64_t *)(void *)(page+40));
    if(config){__atomic_fetch_or((uint32_t *)(uintptr_t)(config+8),1u<<21,__ATOMIC_SEQ_CST);runtime_oe_push_message(30);}
    fprintf(stderr,"[totk] 1.4.3 output %ux%u (settings; display config %s)\n",(unsigned)want,(unsigned)(want>>32),config?"ready":"not published yet");
}
/* What the HeapDef overlay adds to DynamicTextureAllocatorMem and Graphics (and runtime/services.c to the application
 * pool): the canvas and the scene each grow the heap by their pixel ratio above the shipped 1920x1080 / 1600x900, the
 * scene reserve with a 1.6 margin (Awis13 f9808deb1c, measured 2026-10-19: a 3440x1440 canvas with a 2580x1080 scene
 * crashed in the allocator at +258 MiB and loaded at +400 MiB). scripts/make_output_res_overlay.py uses the same formula. */
static int render_reserve_heap(unsigned *w,unsigned *h);
uint64_t runtime_output_res_extra_bytes(void){
    unsigned w,h,sw,sh;double grow=0;
    if(runtime_output_res(&w,&h)&&(double)w*h>1920.0*1080.0)grow+=(double)w*h/(1920.0*1080.0)-1;
    int scene=render_reserve_heap(&sw,&sh);if(scene)grow=(grow+(double)sw*sh/(1600.0*900.0)-1)*1.6;
    if(grow<=0)return 0;
    uint64_t kib=(uint64_t)ceil(113664.0*(1+grow)/1024.0)*1024-113664;return kib<<10;
}
static void patch_output_res(Module *m){
    unsigned w,h;if(!runtime_output_res(&w,&h))return;
    uint64_t data=(m->file_size+0x3fff)&~0x3fffULL,stub=((m->text_end+15)&~15ULL)+16;
    if(data+0x4000!=m->size||stub+28>((m->text_end+0x3fff)&~0x3fffULL))runtime_fail("1.4.3 output patch layout");
    const Patch guards[]={
        {0xbdb578,0xd280f008,0},{0xbdb584,0xf2c08708,0},
        {0x3dcb28,0xd360fc64,0},{0x3dcb2c,0xf9028663,0},{0x3dcb30,0xd001ac08,0}};
    for(unsigned i=0;i<sizeof guards/sizeof *guards;i++){
#if !TARGET_OS_IPHONE
        uint32_t word;memcpy(&word,m->base+guards[i].off,4);
        if(word!=guards[i].original)runtime_fail("1.4.3 output original instruction mismatch");
#endif
    }
#if !TARGET_OS_IPHONE
    for(unsigned i=0;i<28;i++)if(m->base[stub+i])runtime_fail("1.4.3 output veneer padding not empty");
#endif
    /* x9 is dead at this publication point; the next original use initializes
     * it from the active display selector. x19 remains the display config. */
    uint32_t veneer[]={adrp(9,(uintptr_t)m->base+stub,(uintptr_t)m->base+data),
        0xf9001533, /* str x19,[x9,#40]: live display config */
        0xf9401123, /* ldr x3,[x9,#32]: requested output */
        0xf9000a63, /* str x3,[x19,#0x10]: display 0 canvas */
        0xd360fc64, /* lsr x4,x3,#32: crop height */
        0xf9028663, /* str x3,[x19,#0x508]: original active output write */
        branch(stub+24,0x3dcb30)};
 #if TARGET_OS_IPHONE
    if(memcmp(m->base+stub,veneer,sizeof veneer))runtime_fail("1.4.3 embedded output veneer mismatch");
 #else
    memcpy(m->base+stub,veneer,sizeof veneer);
 #endif
    uint32_t words[]={0xd2800008|(w<<5),0xf2c00008|(h<<5),branch(0x3dcb28,stub)};
 #if TARGET_OS_IPHONE
    for(unsigned i=0;i<3;i++){uint32_t got;memcpy(&got,m->base+guards[i].off,4);if(got!=words[i])runtime_fail("1.4.3 embedded output patch mismatch");}
 #else
    for(unsigned i=0;i<3;i++)memcpy(m->base+guards[i].off,words+i,4);
 #endif
    uint8_t *page=m->base+data;output_w=w;output_h=h;
    uint64_t pending=atomic_load(&output_pending);
    atomic_store((_Atomic uint64_t *)(void *)(page+32),pending?pending:output_full_at_start?pack_size(w,h):pack_size(1920,1080));
    atomic_store((_Atomic uint64_t *)(void *)(page+40),0);
    atomic_store(&output_page,page);   /* Publish initialized state to the HUD on the main thread. */
    fprintf(stderr,"[totk] 1.4.3 output: %ux%u swapchain; starts %s, key 3 cycles MetalFX / native / off; display-config veneer +%#llx\n",w,h,output_full_at_start?"at requested resolution":"at 1920x1080",(unsigned long long)stub);
}
/* Scene render resolution - the 1.4.3 port of game_patches.c's TOTK_RENDER_RES patch, live since the settings menu.
   1.4.3 keeps the same graphics-parameter entry (+0x30 of the mode entry: handheld 1280x720, docked 1600x900) and the
   same renderer fields (+0xcc4 width, +0xcc8 height), but converts both through s0 and keeps the output size in w9:
     main+0x585e48  scvtf s0, w11             -> b veneer: adrp x3, <data page>; str x19, [x3, #24]; b main+0x585e4c
     main+0x585e50  str   s0, [x19, #0xcc4]   -> ldp  s0, s1, [x3, #16]
     main+0x585e54  scvtf s0, w10             -> str  s0, [x19, #0xcc4]
     main+0x585e60  str   s0, [x19, #0xcc8]   -> str  s1, [x19, #0xcc8]
   Every path that picks the render size converges through main+0x585e48 (the branch at 0x585f50 only reloads w11/w10
   and jumps back), so the four sites cover all of them. x3 is scratch and untouched by this function; x9 must NOT be
   used here because w9 still holds the output size for main+0x585e64. The size comes from the loader's data page +16
   (the page patch_realtime reserves), so it can change while the game runs.
   The four words are written always (Mac) and embedded always (iOS export, TOTK_EXPORT_IOS: the phone cannot write
   code), with the game's own size on the page until something asks for another: handheld 1280x720 (the phone, and
   TOTK_OPERATION_MODE=handheld), docked 1600x900. TOTK_RENDER_RES=<w>x<h> asks at boot; runtime_render_request asks
   live and the present thread latches it (render_latch, from runtime_frame_step_update; after
   ly05010419/MacNativeGames#42). The game reads the size only when it picks one (main+0x585da0), which it does when
   its mode index changes: rewriting the page alone changes nothing mid-game, and neither does nn::oe message 30 with
   the display config's pending bit, nor a mode index no mode has in the renderer's cached one (+0xcd0) (all tested
   2026-10-19, the scene stayed 1600x896). The per-frame graphics update (main+0x5808f0..) takes its mode-change path,
   main+0x580b28 -> main+0x585bd0(force 1), when the display state's flags at +8 have 0x280; a store there from the
   host is overwritten before it is read. So a second veneer at main+0x580920 ORs the data page +48 into the flags it
   loads (ldrh w8,[x23,#8]; x10 is dead there, every path calls before reading it). The latch writes the size, sets +48
   to 0x80 and the cached index to -1, and clears +48 once the cached index shows the pick ran (normally the next
   frame; given up after 600 frames). The pick reallocates the scene targets the game's own way, as on a mode change.
   The first veneer also keeps the renderer (x19) at +24 for that. Code from an older export without the patch keeps
   the game's own size and refuses live requests. TOTK_RENDER_RES=0 leaves main alone (Mac; live requests refused).
   The game scales its scene with the canvas (Awis13/MacNativeGames b899aa841c): it renders the patched size times the
   canvas width over 1920 on both axes (measured there: 2560x1440 asked on a 2560x1440 canvas rendered 3408x1920). An
   asked size is therefore written divided by that factor, and written again when the canvas changes (key 3); the
   game's own size is written as it is, so the game scales it exactly as it does unpatched.
   The scene's render targets come from DynamicTextureAllocatorMem, sized once at boot from the HeapDef (113664 KiB:
   a 1080p canvas plus the 1600x900 scene). The boot reserve (runtime_render_reserve) is the biggest scene the heaps
   hold: above 1600x900 it needs the HeapDef overlay <canvas>-scene-<w>x<h> (scripts/make_output_res_overlay.py
   --scene; runtime/romfs.c loads it) and grows the application pool by the same amount (runtime_output_res_extra_bytes,
   Awis13 f9808deb1c's formula). Live requests above the reserve are refused (-1: applies after a restart). */
static uint8_t *render_page;                 /* the data page once the four sites carry the patch */
static unsigned render_game_w,render_game_h; /* the game's own size for this mode */
static _Atomic uint64_t render_request;      /* pack_size of the asked size; 0 = the game's own */
static uint64_t render_latched;static unsigned render_canvas;   /* present thread only */
#define RENDER_VENEER 272                    /* 32 bytes after the frame-step (16), output (28), aspect (4 x 32 at +48) and field-of-view (2 x 48 at +176) stubs */
#define RENDER_STOCK_PIXELS (1600u*900u)     /* the scene the shipped heaps hold next to a 1080p canvas */
static int render_size_parse(const char *e,unsigned *w,unsigned *h){
    char rest;return sscanf(e,"%ux%u%c",w,h,&rest)==2&&*w>=64&&*h>=64&&*w<=4096&&*h<=4096;
}
static int render_res(unsigned *w,unsigned *h){
    const char *e=getenv("TOTK_RENDER_RES");
    if(!e||!*e||!strcmp(e,"0"))return 0;
    if(!render_size_parse(e,w,h))runtime_fail("TOTK_RENDER_RES must be <width>x<height>");
    return 1;
}
/* The HeapDef overlay a scene reserve above the stock heaps needs: <canvas>-scene-<w>x<h> in TOTK_ROMFS_PATCHES_OUT. */
static void render_overlay_name(char *out,size_t n,unsigned sw,unsigned sh){
    unsigned w=1920,h=1080;runtime_output_res(&w,&h);snprintf(out,n,"%ux%u-scene-%ux%u",w,h,sw,sh);
}
static int render_overlay_exists(unsigned sw,unsigned sh){
    if((uint64_t)sw*sh<=RENDER_STOCK_PIXELS)return 1;
    char name[64],path[4096];render_overlay_name(name,sizeof name,sw,sh);const char *dir=getenv("TOTK_ROMFS_PATCHES_OUT");
    snprintf(path,sizeof path,"%s/%s/manifest.tsv",dir?dir:"extracted/v143/romfs-patches-out",name);
    FILE *f=fopen(path,"r");if(f)fclose(f);return f!=NULL;
}
void runtime_render_reserve(unsigned *w,unsigned *h){
    static int decided;static unsigned rw=1600,rh=900;
    if(!decided){
        const char *e=getenv("TOTK_RENDER_RESERVE");unsigned bw,bh;int boot=render_res(&bw,&bh);
        if(getenv("TOTK_EXPORT_IOS"))boot=0;   /* the export builds code only; the phone decides at its own launch */
        else if(e&&*e){
            if(!render_size_parse(e,&rw,&rh))runtime_fail("TOTK_RENDER_RESERVE must be <width>x<height>");
            if(!render_overlay_exists(rw,rh)){char name[64];render_overlay_name(name,sizeof name,rw,rh);
                fprintf(stderr,"[totk] TOTK_RENDER_RESERVE=%s needs the HeapDef overlay %s: scripts/make_output_res_overlay.py <canvas> --scene %ux%u --version v143\n",e,name,rw,rh);
                runtime_fail("scene reserve HeapDef overlay missing");}
        }else{
            /* By what the app may use at launch (iOS: its limit is far below the RAM) or the Mac's memory: the biggest
               candidate at or below that whose overlay was built (1600x900 and below need none). */
            static const unsigned cand[][2]={{2560,1440},{1920,1080},{1600,900},{1280,720}};
#if TARGET_OS_IPHONE
            uint64_t avail=os_proc_available_memory(),gib=1ULL<<30;
            unsigned first=avail>=10*gib?1:avail>=5*gib?2:3;
            fprintf(stderr,"[totk] render reserve: %.2f GiB available at launch\n",avail/(double)gib);
#else
            uint64_t mem=0;size_t len=sizeof mem;sysctlbyname("hw.memsize",&mem,&len,NULL,0);
            unsigned first=mem>=(16ULL<<30)?0:1;
#endif
            for(unsigned i=first;i<4;i++)if(render_overlay_exists(cand[i][0],cand[i][1])){rw=cand[i][0];rh=cand[i][1];break;}
        }
        if(boot&&(uint64_t)bw*bh>(uint64_t)rw*rh){
            if(render_overlay_exists(bw,bh)){rw=bw;rh=bh;}
            else fprintf(stderr,"[totk] TOTK_RENDER_RES=%ux%u is above the %ux%u reserve and has no HeapDef overlay: heaps sized for %ux%u\n",bw,bh,rw,rh,rw,rh);
        }
        decided=1;
        fprintf(stderr,"[totk] render reserve %ux%u: live scene sizes up to %u pixels (%s)\n",rw,rh,rw*rh,
                (uint64_t)rw*rh>RENDER_STOCK_PIXELS?"HeapDef overlay grows the texture heap":"the game's own heaps");
    }
    *w=rw;*h=rh;
}
static int render_reserve_heap(unsigned *w,unsigned *h){runtime_render_reserve(w,h);return (uint64_t)*w**h>RENDER_STOCK_PIXELS;}
int runtime_heap_overlay(char *out,size_t n){
    unsigned w=1920,h=1080,sw,sh;if(!runtime_output_res_extra_bytes())return 0;
    if(render_reserve_heap(&sw,&sh))render_overlay_name(out,n,sw,sh);else{runtime_output_res(&w,&h);snprintf(out,n,"%ux%u",w,h);}
    return 1;
}
static void render_write(unsigned canvas){
    uint64_t r=atomic_load(&render_request);float v[2];
    if(!r){v[0]=(float)render_game_w;v[1]=(float)render_game_h;}
    else{float k=canvas>1920?canvas/1920.0f:1.0f;v[0]=(float)(uint32_t)r/k;v[1]=(float)(uint32_t)(r>>32)/k;}
    uint64_t bits;memcpy(&bits,v,8);atomic_store((_Atomic uint64_t *)(void *)(render_page+16),bits);
    render_latched=r;render_canvas=canvas;
    fprintf(stderr,"[totk] 1.4.3 render resolution: scene %ux%u on a %u-wide canvas (data page %.1fx%.1f)%s\n",
            r?(unsigned)r:render_game_w,r?(unsigned)(r>>32):render_game_h,canvas,v[0],v[1],r?"":", the game's own");
}
static uint64_t render_forced;static unsigned render_waited;   /* the renderer whose pick was asked for, until it has picked (logged) */
static void render_latch(void){
    if(!render_page)return;
    if(render_forced&&atomic_load((_Atomic int8_t *)(uintptr_t)(render_forced+0xcd0))!=-1){
        float v[2];memcpy(v,(void *)(uintptr_t)(render_forced+0xcc4),8);render_forced=0;
        fprintf(stderr,"[totk] 1.4.3 render resolution: the game picked again, renderer size %.0fx%.0f\n",v[0],v[1]);
        atomic_store((_Atomic uint32_t *)(void *)(render_page+48),0);
    }else if(render_forced&&++render_waited==600){   /* ten seconds without a pick (paused, loading): stop asking every frame */
        atomic_store((_Atomic uint32_t *)(void *)(render_page+48),0);render_forced=0;
        fprintf(stderr,"[totk] 1.4.3 render resolution: no pick after 600 frames; the size applies at the game's next own pick\n");
    }
    uint64_t r=atomic_load_explicit(&render_request,memory_order_acquire);unsigned canvas=(unsigned)runtime_output_size_current();
    if(r==render_latched&&canvas==render_canvas)return;
    render_write(canvas);
    /* One forced pick through the per-frame update's mode-change path (the second veneer); the cached index set to -1
       tells when it ran (the pick stores the real one). */
    uint64_t renderer=atomic_load((_Atomic uint64_t *)(void *)(render_page+24));
    if(!renderer){fprintf(stderr,"[totk] 1.4.3 render resolution: the game has not picked a size yet; it takes the new one at its first\n");return;}
    atomic_store((_Atomic int8_t *)(uintptr_t)(renderer+0xcd0),(int8_t)-1);render_forced=renderer;render_waited=0;
    atomic_store((_Atomic uint32_t *)(void *)(render_page+48),0x80);   /* the per-frame update takes its mode-change path */
}
int runtime_render_request(unsigned w,unsigned h){
    if(!w&&!h){if(atomic_exchange(&render_request,0))fprintf(stderr,"[totk] render request: the game's own size\n");return 0;}
    unsigned rw,rh;runtime_render_reserve(&rw,&rh);
    int range=w>=64&&h>=64&&w<=4096&&h<=4096;
    if(!range||(uint64_t)w*h>(uint64_t)rw*rh){
        fprintf(stderr,"[totk] render request %ux%u refused: %s\n",w,h,!range?"out of range":"above the boot reserve, applies after a restart");
        return -1;
    }
    if(atomic_load(&render_request)==pack_size(w,h))return 0;
    atomic_store_explicit(&render_request,pack_size(w,h),memory_order_release);
    fprintf(stderr,"[totk] render request %ux%u (reserve %ux%u): latched at the next present\n",w,h,rw,rh);return 0;
}
static void patch_render_res(Module *m){
    int handheld=runtime_handheld_mode();render_game_w=handheld?1280:1600;render_game_h=handheld?720:900;
    unsigned w,h;int set=render_res(&w,&h);
#if !TARGET_OS_IPHONE
    const char *e=getenv("TOTK_RENDER_RES");
    if(e&&!strcmp(e,"0")){fprintf(stderr,"[totk] 1.4.3 render resolution: the game's own, not patched (TOTK_RENDER_RES=0)\n");return;}
#endif
    uint64_t data=(m->file_size+0x3fff)&~0x3fffULL,stub=((m->text_end+15)&~15ULL)+RENDER_VENEER;
    if(data+0x4000!=m->size||stub+32>((m->text_end+0x3fff)&~0x3fffULL))runtime_fail("1.4.3 render-resolution patch layout");
    const Patch sites[]={
        {0x585e48,0x1e220160,0},
        {0x585e50,0xbd0cc660,0x2d420460},
        {0x585e54,0x1e220140,0xbd0cc660},
        {0x585e60,0xbd0cca60,0xbd0cca61},
    };
    /* The veneer also keeps the renderer (x19) at data page +24, for render_latch to ask for a new pick. */
    uint32_t veneer[]={adrp(3,(uintptr_t)m->base+stub,(uintptr_t)m->base+data),
        0xf9000c73, /* str x19,[x3,#24] */
        branch(stub+8,0x585e4c)};   /* stub+12: veneer2 below */
    uint32_t want[4]={branch(sites[0].off,stub),sites[1].replacement,sites[2].replacement,sites[3].replacement};
    /* The per-frame graphics update takes its mode-change path (main+0x580b28: re-pick, forced) when the display state's
       flags at +8 have 0x280; the second veneer ORs the data page +48 into what it loads (x10 is dead there: every path
       calls before reading it), so render_latch can ask for one pick. */
    const Patch kick={0x580920,0x794012e8,0};
    uint32_t veneer2[]={kick.original,adrp(10,(uintptr_t)m->base+stub+16,(uintptr_t)m->base+data),
        0xb940314a, /* ldr w10,[x10,#48] */
        0x2a0a0108, /* orr w8,w8,w10 */
        branch(stub+28,0x580924)},kick_word=branch(kick.off,stub+12);
#if TARGET_OS_IPHONE
    uint32_t first;memcpy(&first,m->base+sites[0].off,4);
    if(first==sites[0].original){fprintf(stderr,"[totk] 1.4.3 render resolution: the game's own (this export did not embed the patch; no live sizes)\n");return;}
    if(memcmp(m->base+stub,veneer,sizeof veneer)||memcmp(m->base+stub+12,veneer2,sizeof veneer2)||memcmp(m->base+kick.off,&kick_word,4))
        runtime_fail("1.4.3 embedded render-resolution veneer mismatch");
    for(unsigned i=0;i<sizeof sites/sizeof *sites;i++){
        uint32_t got;memcpy(&got,m->base+sites[i].off,4);
        if(got!=want[i])runtime_fail("1.4.3 embedded render-resolution patch mismatch");
    }
#else
    for(unsigned i=0;i<sizeof sites/sizeof *sites;i++){
        uint32_t word;memcpy(&word,m->base+sites[i].off,4);
        if(word!=sites[i].original){
            fprintf(stderr,"[totk] render resolution patch main+%#llx: found %08x, expected %08x\n",
                    (unsigned long long)sites[i].off,word,sites[i].original);
            runtime_fail("render resolution patch: main does not match 1.4.3");
        }
    }
    {uint32_t word;memcpy(&word,m->base+kick.off,4);if(word!=kick.original)runtime_fail("render resolution patch: main does not match 1.4.3");}
    for(unsigned i=0;i<sizeof veneer+sizeof veneer2;i++)if(m->base[stub+i])runtime_fail("1.4.3 render-resolution veneer padding not empty");
    memcpy(m->base+stub,veneer,sizeof veneer);memcpy(m->base+stub+12,veneer2,sizeof veneer2);memcpy(m->base+kick.off,&kick_word,4);
    for(unsigned i=0;i<sizeof sites/sizeof *sites;i++)memcpy(m->base+sites[i].off,&want[i],4);
#endif
    atomic_store((_Atomic uint64_t *)(void *)(m->base+data+24),0);atomic_store((_Atomic uint32_t *)(void *)(m->base+data+48),0);
    if(set)atomic_store(&render_request,pack_size(w,h));
    render_page=m->base+data;render_write((unsigned)runtime_output_size_current());
    fprintf(stderr,"[totk] 1.4.3 render resolution patch: 4 instructions at main+0x585e48, veneer +%#llx, size on the data page (live: runtime_render_request)\n",(unsigned long long)stub);
}
int runtime_output_res_toggle(void){
    if(!output_page)return 0;
    _Atomic uint64_t *size=(_Atomic uint64_t *)(void *)(output_page+32);
    int full=atomic_load(size)!=pack_size(output_w,output_h);
    atomic_store(size,full?pack_size(output_w,output_h):pack_size(1920,1080));
    uint64_t config=atomic_load((_Atomic uint64_t *)(void *)(output_page+40));
    if(config){__atomic_fetch_or((uint32_t *)(uintptr_t)(config+8),1u<<21,__ATOMIC_SEQ_CST);runtime_oe_push_message(30);}
    fprintf(stderr,"[totk] 1.4.3 output requested %ux%u (display config %s)\n",full?output_w:1920,full?output_h:1080,config?"ready":"not published yet");
    return 1;
}
int runtime_output_mode_cycle(void){
    if(!output_page)return 0;
    extern int mfxup_output_enabled(void);extern void dm_gfx_set_flag(const char *,int);
    int full=atomic_load((_Atomic uint64_t *)(void *)(output_page+32))==pack_size(output_w,output_h);
    int native=0,mfx=0;const char *name;
    if(full)name="关闭 2K";
    else if(mfxup_output_enabled()){native=1;name="原生 2K";}
    else{mfx=1;name="MetalFX 2K";}
    if(full!=native)runtime_output_res_toggle();
    dm_gfx_set_flag("mfx_output",mfx);dm_gfx_set_flag("mfx_upscale",mfx);
    fprintf(stderr,"[totk] key 3: %s\n",name);return 1;
}
