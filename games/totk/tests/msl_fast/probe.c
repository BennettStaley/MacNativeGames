// make probe-msl-fast: the TOTK_MSL_FAST text pass (totk_msl_fast.h) on sample MSL snippets. With file arguments it
// rewrites each file into build/tests/msl_fast/<name> (unchanged files are copied) and prints the counts, for a
// `xcrun metal -c` check of real shaders.
#include "totk_msl_fast.h"
#include <stdio.h>

static int fails;
static void expect(int ok, const char *what) { if (!ok) { fprintf(stderr, "FAIL: %s\n", what); fails++; } }
static int has(const char *s, const char *sub) { return s && strstr(s, sub) != NULL; }

#define HEAD "#include <metal_stdlib>\nusing namespace metal;\nfragment main0_out main0(main0_in in [[stage_in]])\n{\n"

int main(int argc, char **argv) {
    if (argc > 2) {   // probe <out dir> <file>...
        int total[3] = {0};
        for (int i = 2; i < argc; i++) {
            FILE *f = fopen(argv[i], "rb"); if (!f) { perror(argv[i]); return 2; }
            fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
            char *src = malloc((size_t)n + 1); fread(src, 1, (size_t)n, f); src[n] = 0; fclose(f);
            mslf_stats st = {0}; char *out = totk_msl_fast_rewrite(src, (size_t)n, MSLF_DIV | MSLF_FUNCS, &st);
            const char *base = strrchr(argv[i], '/'); base = base ? base + 1 : argv[i];
            char path[4096]; snprintf(path, sizeof path, "%s/%s", argv[1], base);
            FILE *o = fopen(path, "wb"); if (!o) { perror(path); return 2; }
            fputs(out ? out : src, o); fclose(o);
            printf("%s: %d divides rewritten, %d kept (integer use), %d calls\n", base, st.divs, st.divs_kept, st.calls);
            total[0] += st.divs; total[1] += st.divs_kept; total[2] += st.calls; free(out); free(src);
        }
        printf("total: %d divides rewritten, %d kept, %d calls\n", total[0], total[1], total[2]);
        return 0;
    }
    // The integer-division idiom stays precise, a plain reciprocal does not.
    const char *a = HEAD
        "    float _10 = 1.0 / _5;\n"
        "    int _12 = as_type<int>(_10) + (-2);\n"
        "    float _20 = 1.0 / in.m_x.y;\n"
        "    float _21 = _20 * 2.0;\n"
        "}\n";
    mslf_stats st = {0}; char *r = totk_msl_fast_rewrite(a, strlen(a), MSLF_DIV | MSLF_FUNCS, &st);
    expect(has(r, "    float _10 = 1.0 / _5;\n"), "idiom reciprocal kept");
    expect(has(r, "    float _20 = fast::divide(1.0, in.m_x.y);\n"), "plain reciprocal rewritten");
    expect(st.divs == 1 && st.divs_kept == 1, "counts 1 rewritten / 1 kept");
    free(r);
    // The sink is reached through a copy and a phi-style reassignment; an int() conversion is a sink too.
    const char *b = HEAD
        "    float _30 = 1.0 / _1;\n"
        "    float _31 = _30;\n"
        "    _40 = _31;\n"
        "    uint _41 = as_type<uint>(_40);\n"
        "    float _50 = _2 / _3;\n"
        "    int _51 = int(_50 * _4);\n"
        "    float _60 = (*spvDescriptorSet0.fp_c9)._m0[12].x / _7;\n"
        "}\n";
    memset(&st, 0, sizeof st); r = totk_msl_fast_rewrite(b, strlen(b), MSLF_DIV, &st);
    expect(has(r, "float _30 = 1.0 / _1;"), "reciprocal reaching as_type<uint> through copies kept");
    expect(has(r, "float _50 = _2 / _3;"), "quotient reaching int() kept");
    expect(has(r, "float _60 = fast::divide((*spvDescriptorSet0.fp_c9)._m0[12].x, _7);"), "uniform operand rewritten");
    free(r);
    // Ambiguous expressions are left alone; vectors and compound expressions do not get fast::divide.
    const char *c = HEAD
        "    float _70 = (_1 + _2) / _3;\n"
        "    float _71 = _1 / _2 / _3;\n"
        "    float2 _72 = _9 / _8;\n"
        "    float _73 = _1 * _2 / _3;\n"
        "    _74 = 1.0 / _5;\n"
        "}\n";
    r = totk_msl_fast_rewrite(c, strlen(c), MSLF_DIV, NULL);
    expect(r == NULL, "nothing ambiguous rewritten");
    free(r);
    // Calls: fast:: prefixed once, not on already-qualified calls, not on sink statements, not in identifiers.
    const char *d = HEAD
        "    float _80 = exp2(log2(_1) * _2);\n"
        "    float3 _81 = sqrt(_3) + rsqrt(_4);\n"
        "    float _82 = precise::sqrt(_5) + my_sqrt(_6);\n"
        "    float _83 = sqrt(_7);\n"
        "    int _84 = as_type<int>(_83);\n"
        "}\n";
    memset(&st, 0, sizeof st); r = totk_msl_fast_rewrite(d, strlen(d), MSLF_FUNCS, &st);
    expect(has(r, "float _80 = fast::exp2(fast::log2(_1) * _2);"), "nested calls prefixed");
    expect(has(r, "float3 _81 = fast::sqrt(_3) + fast::rsqrt(_4);"), "vector calls prefixed, rsqrt not split");
    expect(has(r, "float _82 = precise::sqrt(_5) + my_sqrt(_6);"), "qualified and other names untouched");
    expect(has(r, "float _83 = sqrt(_7);"), "sink call kept");
    expect(st.calls == 4, "4 calls rewritten");
    free(r);
    // Vertex and compute shaders are not touched; a zero mask changes nothing.
    const char *e = "#include <metal_stdlib>\nusing namespace metal;\nvertex main0_out main0()\n{\n    float _1 = 1.0 / _2;\n}\n";
    expect(totk_msl_fast_rewrite(e, strlen(e), MSLF_DIV | MSLF_FUNCS, NULL) == NULL, "vertex shader untouched");
    expect(totk_msl_fast_rewrite(a, strlen(a), 0, NULL) == NULL, "mask 0 untouched");
    if (fails) { fprintf(stderr, "%d failed\n", fails); return 1; }
    printf("msl_fast: all checks passed\n");
    return 0;
}
