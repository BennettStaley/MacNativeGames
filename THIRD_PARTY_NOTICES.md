# Third-party code

Code in this repository that comes from other projects keeps its license; the license file sits next to the code.

| Code | From | License | File |
|---|---|---|---|
| `games/totk/third_party/d2r` (NSO rewriter, verification tools) | [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile), adapted | MIT | `games/totk/third_party/d2r/LICENSE` |
| `games/totk/third_party/d2r_gfx` (NVN on Metal, texture tools, host headers) | d2r-mobile, extended for this game | MIT | `games/totk/third_party/d2r_gfx/LICENSE` |
| texture layout sizes in `games/totk/third_party/d2r_gfx/gfx/nvn/nvn_tex.c` | deko3d | zlib | `games/totk/third_party/d2r_gfx/LICENSE.deko3d` |
| `games/totk/third_party/d2r_gfx/third_party/bcdec` | [bcdec](https://github.com/iOrange/bcdec) 0.985 | MIT or public domain | `games/totk/third_party/d2r_gfx/third_party/bcdec/LICENSE` |
| `games/totk/tools/totk_xlate` (shader translator front end) | d2r-mobile, adapted | MIT | `games/totk/tools/totk_xlate/LICENSE` |
| `vendor/ryujinx` (Ryujinx.Graphics.Shader, Ryujinx.Common, Spv.Generator) | Ryujinx, as pinned by d2r-mobile; patched by `games/totk/tools/totk_xlate/patches` | MIT | `vendor/ryujinx/LICENSE.txt` |
| `games/totk/prebuilt/macos-arm64/totk_xlate` (the shader translator, a native program) | built from `vendor/ryujinx` + the patches + `games/totk/tools/totk_xlate` with `publish_native.sh`; contains .NET runtime libraries compiled in by NativeAOT | MIT (Ryujinx, d2r-mobile, .NET) | `vendor/ryujinx/LICENSE.txt`, `games/totk/tools/totk_xlate/LICENSE` |
| SMAA 1x in `games/totk/third_party/d2r_gfx/gfx/nvn/totk_post_msl.h` and its lookup tables `smaa_tables.h` (anti-aliasing setting) | [SMAA](https://github.com/iryoku/smaa), ported to Metal | MIT | `games/totk/third_party/licenses/SMAA.txt` |
| contrast adaptive sharpening in `games/totk/third_party/d2r_gfx/gfx/nvn/totk_post_msl.h` (sharpen setting) | [AMD FidelityFX CAS](https://github.com/GPUOpen-Effects/FidelityFX-CAS), ported to Metal | MIT | `games/totk/third_party/licenses/AMD-FidelityFX-CAS.txt` |
| ResourceSizeTable size estimates in `games/totk/runtime/mods.c` (`mods_rstb_estimate`) | ported from [TkSharp](https://github.com/TKMM-Team/TkSharp) (`TkSharp.Merging/ResourceSizeTable`) | MIT | `games/totk/third_party/licenses/TkSharp.txt` |
| `games/totk/third_party/zstd` (decoder only, single file) | [zstd](https://github.com/facebook/zstd) 1.5.7 | BSD-3-Clause (zstd is dual BSD / GPLv2; used under BSD) | `games/totk/third_party/zstd/LICENSE` |
| audio effects in `games/totk/runtime/audio_reverb.c`, `games/totk/runtime/audio_effects.c` (delay, reverb, compressor) and the mixer | ported from Ryujinx.Audio | MIT | `games/totk/third_party/licenses/Ryujinx-Audio.txt` |

Fetched or linked at build time (not in this repository):

| Code | Use | License |
|---|---|---|
| [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) at `aa217ae` | SPIR-V to Metal Shading Language | Apache-2.0 |
| [hactool](https://github.com/SciresM/hactool) at `1d64a83` (with explicit private title-key-file hook), including its bundled mbedTLS | Base + update BKTR merge at build time only | ISC (hactool), Apache-2.0 (mbedTLS); see fetched source LICENSE files |
| libopus (Homebrew), linked statically | Opus audio | BSD-3-Clause |
| libvpx (Homebrew), linked statically | VP9 movies | BSD-3-Clause |
