/* TOTK_PAR_ENCODE step 1 (2026-10-23): checks the Metal 4 mechanics the parallel G-buffer encode relies on, outside the
   game. One render pass (clear + N draws, no blending, each draw overwrites a band of cells that overlaps the next
   draws, so the result depends on the draw order across chunk borders) is encoded
     (a) serially in one encoder (reference), and
     (b) split into K chunks encoded at the same time on K threads, each into its own MTL4CommandBuffer with its own
         MTL4CommandAllocator, chunk 0 Suspending, middle chunks Resuming | Suspending, the last one Resuming, all
         committed in one commit:count:. The first command buffer also holds work before the pass (a buffer fill) and
         the last one work after it (the texture read back), as the replay would.
   Both readbacks must be identical and match the expected cell values. Prints the encode wall time of both. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

static const char *kSrc =
    "#include <metal_stdlib>\nusing namespace metal;\n"
    "struct V { float4 p [[position]]; uint c [[flat]]; };\n"
    "vertex V vs(uint vid [[vertex_id]], uint iid [[instance_id]]) {\n"
    "  /* draw iid covers cells iid .. iid+3 of a 64-cell row strip (one row of 4-pixel cells per 64 draws) */\n"
    "  uint row = (iid / 64u), c0 = iid % 64u; float2 uv = float2(vid & 1u, vid >> 1u);\n"
    "  float x0 = float(c0) / 64.0, x1 = min(float(c0 + 4u) / 64.0, 1.0), y0 = float(row) / 64.0, y1 = float(row + 1u) / 64.0;\n"
    "  V o; o.p = float4(mix(x0, x1, uv.x) * 2 - 1, 1 - mix(y0, y1, uv.y) * 2, 0, 1); o.c = iid + 1u; return o; }\n"
    "fragment uint4 fs(V i [[stage_in]]) { return uint4(i.c, 0, 0, 1); }\n";

enum { W = 256, H = 256, NDRAW = 2048, KMAX = 8 };
typedef struct { id<MTL4CommandBuffer> cb; MTL4RenderPassDescriptor *rp; id<MTLRenderPipelineState> ps; int first, count, opts; double ms; } chunk_t;

static double now_ms(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1e6; }
static void encode_draws(id<MTL4RenderCommandEncoder> e, id<MTLRenderPipelineState> ps, int first, int count) {
    [e setRenderPipelineState:ps];
    for (int i = first; i < first + count; i++)   /* one draw per call, as the replay issues them */
        [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:1 baseInstance:(NSUInteger)i];
}
static void *chunk_main(void *arg) {
    chunk_t *c = arg; double t0 = now_ms();
    @autoreleasepool {
        id<MTL4RenderCommandEncoder> e = [c->cb renderCommandEncoderWithDescriptor:c->rp options:(MTL4RenderEncoderOptions)c->opts];
        if (!e) { fprintf(stderr, "renderCommandEncoderWithDescriptor:options: failed\n"); exit(1); }
        encode_draws(e, c->ps, c->first, c->count); [e endEncoding];
    }
    c->ms = now_ms() - t0; return NULL;
}
static uint32_t expected(int x, int y) {   /* last draw covering the cell wins; 0 = cleared */
    int cell = x / 4, row = y / 4; if (row >= NDRAW / 64) return 0;
    for (int i = row * 64 + 63; i >= row * 64; i--) if (cell >= i % 64 && cell < (int)(i % 64) + 4) return (uint32_t)i + 1;
    return 0;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        int K = argc > 1 ? atoi(argv[1]) : 4; if (K < 2 || K > KMAX) K = 4;
        int mode = argc > 2 ? atoi(argv[2]) : 1;   /* bit 0: work before the pass in the first command buffer; bit 1: the readback in the last one (else in its own command buffer after them); bit 2: no barrier before the readback */
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); NSError *err = nil;
        if (![dev supportsFamily:MTLGPUFamilyMetal4]) { printf("{\"skip\": \"no Metal 4\"}\n"); return 0; }
        id<MTL4CommandQueue> q = [dev newMTL4CommandQueue];
        id<MTLLibrary> lib = [dev newLibraryWithSource:@(kSrc) options:nil error:&err]; if (!lib) { printf("%s\n", err.localizedDescription.UTF8String); return 1; }
        MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [lib newFunctionWithName:@"vs"]; pd.fragmentFunction = [lib newFunctionWithName:@"fs"]; pd.colorAttachments[0].pixelFormat = MTLPixelFormatR32Uint;
        id<MTLRenderPipelineState> ps = [dev newRenderPipelineStateWithDescriptor:pd error:&err]; if (!ps) { printf("%s\n", err.localizedDescription.UTF8String); return 1; }
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Uint width:W height:H mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; td.storageMode = MTLStorageModePrivate;
        id<MTLTexture> tex[2] = {[dev newTextureWithDescriptor:td], [dev newTextureWithDescriptor:td]};
        id<MTLBuffer> rb[2] = {[dev newBufferWithLength:W * H * 4 options:MTLResourceStorageModeShared], [dev newBufferWithLength:W * H * 4 options:MTLResourceStorageModeShared]};
        id<MTLBuffer> pre = [dev newBufferWithLength:4096 options:MTLResourceStorageModeShared];
        MTLResidencySetDescriptor *rsd = [MTLResidencySetDescriptor new]; id<MTLResidencySet> rs = [dev newResidencySetWithDescriptor:rsd error:&err];
        [rs addAllocation:tex[0]]; [rs addAllocation:tex[1]]; [rs addAllocation:rb[0]]; [rs addAllocation:rb[1]]; [rs addAllocation:pre]; [rs commit];
        [q addResidencySet:rs];
        id<MTLSharedEvent> ev = [dev newSharedEvent];
        MTL4RenderPassDescriptor *rp[2];
        for (int i = 0; i < 2; i++) { rp[i] = [MTL4RenderPassDescriptor new]; rp[i].colorAttachments[0].texture = tex[i];
            rp[i].colorAttachments[0].loadAction = MTLLoadActionClear; rp[i].colorAttachments[0].storeAction = MTLStoreActionStore; rp[i].colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0); }
        id<MTL4CommandAllocator> al[KMAX + 1]; for (int i = 0; i <= KMAX; i++) al[i] = [dev newCommandAllocator];

        /* (a) serial reference */
        double ta = now_ms();
        id<MTL4CommandBuffer> cs = [dev newCommandBuffer]; [cs beginCommandBufferWithAllocator:al[KMAX]]; [cs useResidencySet:rs];
        { id<MTL4RenderCommandEncoder> e = [cs renderCommandEncoderWithDescriptor:rp[0]]; encode_draws(e, ps, 0, NDRAW); [e endEncoding]; }
        { id<MTL4ComputeCommandEncoder> b = [cs computeCommandEncoder]; [b barrierAfterQueueStages:MTLStageFragment beforeStages:MTLStageBlit visibilityOptions:MTL4VisibilityOptionDevice];
          [b copyFromTexture:tex[0] sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(W, H, 1) toBuffer:rb[0] destinationOffset:0 destinationBytesPerRow:W * 4 destinationBytesPerImage:W * H * 4]; [b endEncoding]; }
        [cs endCommandBuffer]; double serial_ms = now_ms() - ta;
        { id<MTL4CommandBuffer> a[] = {cs}; [q commit:a count:1]; } [q signalEvent:ev value:1];
        if (![ev waitUntilSignaledValue:1 timeoutMS:5000]) { printf("{\"error\": \"serial timeout\"}\n"); return 1; }

        /* (b) K chunks on K threads, suspend / resume, one commit */
        double tb = now_ms();
        chunk_t ch[KMAX]; pthread_t th[KMAX]; int per = NDRAW / K;
        for (int k = 0; k < K; k++) {
            ch[k].cb = [dev newCommandBuffer]; [ch[k].cb beginCommandBufferWithAllocator:al[k]]; [ch[k].cb useResidencySet:rs];
            ch[k].rp = rp[1]; ch[k].ps = ps; ch[k].first = k * per; ch[k].count = k == K - 1 ? NDRAW - k * per : per;
            ch[k].opts = (k < K - 1 ? MTL4RenderEncoderOptionSuspending : 0) | (k > 0 ? MTL4RenderEncoderOptionResuming : 0);
        }
        if (mode & 1) { id<MTL4ComputeCommandEncoder> b = [ch[0].cb computeCommandEncoder]; [b fillBuffer:pre range:NSMakeRange(0, 4096) value:0xab]; [b endEncoding]; }   /* work before the pass in the first command buffer */
        for (int k = 1; k < K; k++) pthread_create(&th[k], NULL, chunk_main, &ch[k]);
        chunk_main(&ch[0]);
        for (int k = 1; k < K; k++) pthread_join(th[k], NULL);
        id<MTL4CommandBuffer> post = ch[K - 1].cb;
        if (!(mode & 2)) { post = [dev newCommandBuffer]; [post beginCommandBufferWithAllocator:al[KMAX]]; [post useResidencySet:rs]; }
        if (!(mode & 1)) { id<MTL4ComputeCommandEncoder> b = [post computeCommandEncoder]; [b fillBuffer:pre range:NSMakeRange(0, 4096) value:0xab]; [b endEncoding]; }
        { id<MTL4ComputeCommandEncoder> b = [post computeCommandEncoder]; if (!(mode & 4)) [b barrierAfterQueueStages:MTLStageFragment beforeStages:MTLStageBlit visibilityOptions:MTL4VisibilityOptionDevice];   /* work after the pass in the last one */
          [b copyFromTexture:tex[1] sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(W, H, 1) toBuffer:rb[1] destinationOffset:0 destinationBytesPerRow:W * 4 destinationBytesPerImage:W * H * 4]; [b endEncoding]; }
        id<MTL4CommandBuffer> all[KMAX + 1]; int nall = 0; for (int k = 0; k < K; k++) { [ch[k].cb endCommandBuffer]; all[nall++] = ch[k].cb; }
        if (post != ch[K - 1].cb) { [post endCommandBuffer]; all[nall++] = post; }
        double par_ms = now_ms() - tb;
        MTL4CommitOptions *co = [MTL4CommitOptions new];
        [co addFeedbackHandler:^(id<MTL4CommitFeedback> f) { if (f.error) fprintf(stderr, "parallel commit error: %s\n", f.error.description.UTF8String); }];
        [q commit:all count:(NSUInteger)nall options:co]; [q signalEvent:ev value:2];
        if (![ev waitUntilSignaledValue:2 timeoutMS:5000]) { printf("{\"error\": \"parallel timeout\"}\n"); return 1; }

        const uint32_t *a = rb[0].contents, *b = rb[1].contents; int bad_ref = 0, diff = 0;
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { uint32_t ex = expected(x, y); bad_ref += a[y * W + x] != ex; diff += a[y * W + x] != b[y * W + x]; }
        if (diff) { int shown = 0; for (int i = 0; i < W * H && shown < 4; i++) if (a[i] != b[i]) { fprintf(stderr, "first differences: pixel %d serial %u parallel %u\n", i, a[i], b[i]); shown++; } }
        int pre_ok = ((const uint8_t *)pre.contents)[4095] == 0xab;
        printf("{\"mode\": %d, \"chunks\": %d, \"draws\": %d, \"serial_ref_mismatch\": %d, \"parallel_vs_serial_mismatch\": %d, \"pre_pass_work\": %s, \"serial_encode_ms\": %.3f, \"parallel_encode_ms\": %.3f, \"chunk_ms\": [",
               mode, K, NDRAW, bad_ref, diff, pre_ok ? "true" : "false", serial_ms, par_ms);
        for (int k = 0; k < K; k++) printf("%s%.3f", k ? ", " : "", ch[k].ms);
        printf("], \"result\": \"%s\"}\n", !bad_ref && !diff && pre_ok ? "pass" : "FAIL");
        return !bad_ref && !diff && pre_ok ? 0 : 1;
    }
}
