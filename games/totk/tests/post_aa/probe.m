/* GPU check of the picture-settings shaders (totk_post_msl.h, smaa_tables.h) on a synthetic picture, no game data:
   an aliased scene (a rotated bright square and a thin slanted bar on a dark ramp, one sample per pixel) against its
   16x16-supersampled reference. Copy must be exact; FXAA and SMAA must bring the edge pixels closer to the reference
   and leave flat areas alone; SMAA must not touch pixels outside the region it was given; CAS must raise the contrast
   of a soft edge, keep flat areas and stay in 0..1. Run through the game slot (make probe-post-aa). */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "totk_post_msl.h"
#include "smaa_tables.h"

enum { W = 256, H = 192 };
static id<MTLDevice> dev; static id<MTLCommandQueue> q; static id<MTLLibrary> lib;
static void check(int ok, const char *what) { if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(1); } }
static float scene(float x, float y) {   // pixel coordinates, continuous
    float cx = 120, cy = 96, a = 0.35f, u = cosf(a) * (x - cx) + sinf(a) * (y - cy), v = -sinf(a) * (x - cx) + cosf(a) * (y - cy);
    if (fabsf(u) < 48 && fabsf(v) < 48) return 0.9f;
    float d = fabsf((x - 20) * 0.28f - (y - 10)) / sqrtf(1 + 0.28f * 0.28f);   // a thin bar at a shallow slope
    if (d < 1.2f && x > 20 && x < 236) return 0.75f;
    return 0.05f + 0.1f * y / H;
}
static id<MTLTexture> tex(MTLPixelFormat pf, int w, int h, MTLTextureUsage u) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pf width:w height:h mipmapped:NO];
    d.usage = u; d.storageMode = MTLStorageModeShared; return [dev newTextureWithDescriptor:d];
}
static id<MTLTexture> upload(const float *g) {   // grey values -> RGBA32Float
    id<MTLTexture> t = tex(MTLPixelFormatRGBA32Float, W, H, MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget);
    float *p = malloc(W * H * 16); for (int i = 0; i < W * H; i++) p[i * 4] = p[i * 4 + 1] = p[i * 4 + 2] = g[i], p[i * 4 + 3] = 1;
    [t replaceRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0 withBytes:p bytesPerRow:W * 16]; free(p); return t;
}
static void download(id<MTLTexture> t, float *g) {
    float *p = malloc(W * H * 16); [t getBytes:p bytesPerRow:W * 16 fromRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0];
    for (int i = 0; i < W * H; i++) g[i] = p[i * 4 + 1]; free(p);
}
static id<MTLTexture> table(const char *hex, int w, int h, MTLPixelFormat pf, int bpp) {
    uint8_t *b = malloc(w * h * bpp); for (int i = 0; i < w * h * bpp; i++) { unsigned v; sscanf(hex + 2 * i, "%2x", &v); b[i] = v; }
    id<MTLTexture> t = tex(pf, w, h, MTLTextureUsageShaderRead); [t replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:b bytesPerRow:w * bpp]; free(b); return t;
}
static void pass(const char *fn, id<MTLTexture> dst, int clear, NSArray<id<MTLTexture>> *in, const float k[12], MTLScissorRect r) {
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [lib newFunctionWithName:@"pp_vs"]; pd.fragmentFunction = [lib newFunctionWithName:@(fn)]; pd.colorAttachments[0].pixelFormat = dst.pixelFormat;
    NSError *e = nil; id<MTLRenderPipelineState> ps = [dev newRenderPipelineStateWithDescriptor:pd error:&e]; check(ps != nil, fn);
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor new]; rp.colorAttachments[0].texture = dst;
    rp.colorAttachments[0].loadAction = clear ? MTLLoadActionClear : MTLLoadActionLoad; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLRenderCommandEncoder> en = [cb renderCommandEncoderWithDescriptor:rp];
    [en setRenderPipelineState:ps]; [en setScissorRect:r];
    for (NSUInteger i = 0; i < in.count; i++) [en setFragmentTexture:in[i] atIndex:i];
    [en setFragmentBytes:k length:48 atIndex:0]; [en drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; [en endEncoding];
    [cb commit]; [cb waitUntilCompleted]; check(cb.status == MTLCommandBufferStatusCompleted, "command buffer");
}
static float *run_aa(int mode, const float *alias, MTLScissorRect r) {
    static id<MTLTexture> area, search; if (!area) { area = table(k_smaa_area_hex, SMAA_AREA_W, SMAA_AREA_H, MTLPixelFormatRG8Unorm, 2); search = table(k_smaa_search_hex, SMAA_SEARCH_W, SMAA_SEARCH_H, MTLPixelFormatR8Unorm, 1); }
    id<MTLTexture> src = upload(alias), dst = upload(alias);
    float k[12] = {1.f / W, 1.f / H, W, H, r.x, r.y, r.x + r.width, r.y + r.height, 0, 0, 0, 0};
    if (mode == 0) pass("pp_copy", dst, 0, @[src], k, r);
    else if (mode == 2) pass("pp_fxaa", dst, 0, @[src], k, r);
    else {
        MTLTextureUsage u = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        id<MTLTexture> edges = tex(MTLPixelFormatRG8Unorm, W, H, u), blend = tex(MTLPixelFormatRGBA8Unorm, W, H, u);
        pass("smaa_edge", edges, 1, @[src], k, r); pass("smaa_weight", blend, 1, @[edges, area, search], k, r); pass("smaa_blend", dst, 0, @[src, blend], k, r);
    }
    float *g = malloc(W * H * 4); download(dst, g); return g;
}
int main(void) { @autoreleasepool {
    dev = MTLCreateSystemDefaultDevice(); check(dev != nil, "Metal device"); q = [dev newCommandQueue];
    NSError *e = nil; lib = [dev newLibraryWithSource:@(k_post_msl) options:nil error:&e];
    if (!lib) fprintf(stderr, "%s\n", e.localizedDescription.UTF8String);
    check(lib != nil, "post shaders compile");
    float *alias = malloc(W * H * 4), *ref = malloc(W * H * 4);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        alias[y * W + x] = scene(x + 0.5f, y + 0.5f);
        double s = 0; for (int j = 0; j < 16; j++) for (int i = 0; i < 16; i++) s += scene(x + (i + 0.5f) / 16, y + (j + 0.5f) / 16);
        ref[y * W + x] = (float)(s / 256);
    }
    MTLScissorRect all = {0, 0, W, H};
    float *c = run_aa(0, alias, all);
    for (int i = 0; i < W * H; i++) check(fabsf(c[i] - alias[i]) < 1e-6f, "copy is exact");
    double e0 = 0; int ne = 0;   // error on edge pixels (where the aliased value is off by more than 0.05)
    for (int i = 0; i < W * H; i++) if (fabsf(alias[i] - ref[i]) > 0.05f) { e0 += fabsf(alias[i] - ref[i]); ne++; }
    const char *name[4] = {"copy", "", "FXAA", "SMAA"};
    for (int m = 2; m <= 3; m++) {
        float *o = run_aa(m, alias, all); double em = 0, flat = 0;
        for (int i = 0; i < W * H; i++) { if (fabsf(alias[i] - ref[i]) > 0.05f) em += fabsf(o[i] - ref[i]); else if (alias[i] == ref[i]) flat = fmax(flat, fabsf(o[i] - alias[i])); }
        printf("%s: edge error %.4f -> %.4f (%d edge pixels), largest change on exact pixels %.4f\n", name[m], e0 / ne, em / ne, ne, flat);
        check(em < e0 * (m == 3 ? 0.75 : 0.85), "anti-aliasing brings edges closer to the supersampled reference");
        check(flat < 0.35, "anti-aliasing leaves exact pixels mostly alone");
        free(o);
    }
    MTLScissorRect part = {32, 24, 160, 120};   // a smaller region (dynamic resolution): nothing outside it changes
    float *o = run_aa(3, alias, part);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (x < 32 || y < 24 || x >= 192 || y >= 144) check(o[y * W + x] == alias[y * W + x], "SMAA stays in its region");
    free(o);
    // CAS: a soft vertical edge (0.2, 0.35, 0.65, 0.8: an edge blurred over two pixels) gets steeper; flat columns stay; output within 0..1
    float *soft = malloc(W * H * 4);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) soft[y * W + x] = x < 127 ? 0.2f : x == 127 ? 0.35f : x == 128 ? 0.65f : 0.8f;
    id<MTLTexture> s = upload(soft), d = upload(soft);
    float k[12] = {1.f / W, 1.f / H, W, H, 0, 0, W, H, -1.f / (8.f + (5.f - 8.f) * 0.8f), 0, 0, 0};
    pass("pp_cas", d, 0, @[s], k, all);
    float *g = malloc(W * H * 4); download(d, g);
    int y = H / 2; float before = soft[y * W + 128] - soft[y * W + 127], after = g[y * W + 128] - g[y * W + 127];
    printf("CAS: slope across the edge %.4f -> %.4f, flat %.4f -> %.4f\n", before, after, soft[y * W + 40], g[y * W + 40]);
    check(after > before * 1.1f, "CAS sharpens");
    check(fabsf(g[y * W + 40] - soft[y * W + 40]) < 1e-3f && fabsf(g[y * W + 220] - soft[y * W + 220]) < 1e-3f, "CAS keeps flat areas");
    for (int i = 0; i < W * H; i++) check(g[i] >= 0 && g[i] <= 1, "CAS stays in 0..1");
    printf("post-aa probe: ok\n");
    return 0;
} }
