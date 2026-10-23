# The shader translator, ready-made

`totk_xlate` is the program the game starts to translate its shaders (`totk_xlate --service`): the Ryujinx shader
translator (`vendor/ryujinx`, MIT) with this project's patches (`games/totk/tools/totk_xlate/patches`) and the service
(`games/totk/tools/totk_xlate/Service.cs`: Maxwell code -> SPIR-V -> SPIRV-Cross -> Metal Shading Language and its
reflection file), compiled ahead of time with .NET NativeAOT for arm64 macOS. It is here so that building the app needs
no .NET and the app needs no .NET runtime and no Python.

- Built with `games/totk/tools/totk_xlate/publish_native.sh` from the sources in this repository (.NET SDK 10.0.301; rebuilt with 10.0.402 for early fragment tests).
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
  NOT IN THE CURRENT BINARY: the binary here is upstream PR #222's (ae8f4f4, chain v8), built from upstream's sources,
  which have patches 0001-0005 only. No .NET SDK was available to rebuild it with 0006, so 0006 was moved to
  `patches/pending/` (not applied by publish_native.sh) to keep the sources matching this binary; until a rebuild, those
  two compute shaders fail to translate again, as before 0006. To restore it: move the patch back to `patches/`, run
  publish_native.sh, update `totk_xlate.sha256`, and bump the chain (MSL of those two shaders changes).
  The early fragment tests flag (request field early_fragment_tests, NVN control +0x718) changes 1,722 of the 33,264
  seed MSL files of a 1.4.3 build, each only by `[[ early_fragment_tests ]]` before the fragment entry, all of them
  fragment shaders with the flag; the reflection files differ only by the chain name. Without the field, 1,380 replayed
  variants of a player cache translate to the same MSL and reflection as the previous build.
