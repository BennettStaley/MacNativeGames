# The shader translator, ready-made

`totk_xlate` is the program the game starts to translate its shaders (`totk_xlate --service`): the Ryujinx shader
translator (`vendor/ryujinx`, MIT) with this project's patches (`games/totk/tools/totk_xlate/patches`) and the service
(`games/totk/tools/totk_xlate/Service.cs`: Maxwell code -> SPIR-V -> SPIRV-Cross -> Metal Shading Language and its
reflection file), compiled ahead of time with .NET NativeAOT for arm64 macOS. It is here so that building the app needs
no .NET and the app needs no .NET runtime and no Python.

- Built with `games/totk/tools/totk_xlate/publish_native.sh` from the sources in this repository (.NET SDK 10.0.301; the early-fragment-tests build was rebuilt here from the merged sources after PR #222 rather than taking the contributor's binary, and the 1.4.3 seed it prepares differs from the previous one only by `[[ early_fragment_tests ]]` in 1,722 fragment shaders).
- `totk_xlate.sha256` is its SHA-256; `build_app.py` refuses to package a different file.
- NativeAOT builds are not byte-for-byte reproducible across folders (paths end up in the binary), so a rebuild gets a
  different hash; what matters is that it translates the same. Check a rebuild by replaying a shader cache through it
  and comparing every MSL, SPIR-V and reflection file with the previous build (the development tree has
  `scripts/diag/retranslate_cache.py --service <program> --compare <cache>` for that). The build before patch 0005 gave
  the same output as the .NET translator with the Python service for all 7,570 shader variants of the development cache.
  Patch 0005 changes only vertex shaders: over the 18,064 startup variants of a 1.4.3 build, 6,679 vertex functions
  gained the initialisation of their outputs and nothing else changed (see docs/how-it-works.md).
  The depth sampling rewrite after PR #104 (Service.cs MatchSwitchDepthSampling) changes 2,650 of the 20,368 seed MSL
  files of a 1.4.3 build, each exactly by the two rewrites; reflection and every other file are unchanged.
  Patch 0006 (from nxnative, MIT) builds texel offsets read from a register with OpCompositeConstruct + Offset, and a
  four-offset gather as four gathers, instead of an OpConstantComposite (invalid SPIR-V; SPIRV-Cross threw `nullptr`).
  Over the 1.4.3 seed (prepare_shader_seed.py, 20,961 variants before) every MSL, reflection and index file is unchanged;
  the two agl_technique_pfx compute shaders that used to fail now translate and compile with `metal -c` (macOS and iOS).
  NOT IN THE CURRENT BINARY: the binary here is upstream's rebuild at f3e3405 (SPIRV-Cross compiled in; chain v8
  unchanged, MSL byte-identical to the 84fe66a binary), built from upstream's sources, which have patches 0001-0005 only. No .NET SDK was available to rebuild it with 0006, so 0006 was moved to
  `patches/pending/` (not applied by publish_native.sh) to keep the sources matching this binary; until a rebuild, those
  two compute shaders fail to translate again, as before 0006. To restore it: move the patch back to `patches/`, run
  publish_native.sh, update `totk_xlate.sha256`, and bump the chain (MSL of those two shaders changes).
  The early fragment tests flag (request field early_fragment_tests, NVN control +0x718) changes 1,722 of the 33,264
  seed MSL files of a 1.4.3 build, each only by `[[ early_fragment_tests ]]` before the fragment entry, all of them
  fragment shaders with the flag; the reflection files differ only by the chain name. Without the field, 1,380 replayed
  variants of a player cache translate to the same MSL and reflection as the previous build.

SPIRV-Cross is compiled into the program (2026-10-23, `games/totk/tools/totk_xlate/spirv_cross_inproc.cpp`): its own
`main.cpp` at the pinned commit (build_app.py `SPIRV_CROSS`) with the same arguments the service passed to the program,
so no process is started per shader (`TOTK_XLATE_SPIRV_CROSS=process` still runs the `--spirv-cross` program). Checked
by preparing the whole 1.4.3 seed with both builds from the same inputs: all 95,227 files (MSL, reflection, variant
indexes) byte-identical; the translation took as long (268 s / 270 s: the preparing script's 8 workers wait on each
reply) for about half the CPU (508 s instead of 961 s, system time 90 s instead of 294 s).
