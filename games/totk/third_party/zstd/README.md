# zstd 1.5.7, decoder only

`zstddeclib.c` was generated with `build/single_file_libs/create_single_file_decoder.sh` from the zstd 1.5.7 release
(https://github.com/facebook/zstd/releases/tag/v1.5.7); `zstd.h` and `zstd_errors.h` are that release's `lib/` headers.
None of them is changed. zstd is dual-licensed BSD / GPLv2; it is used here under the BSD license (`LICENSE`).
`runtime/zstd_dec.c` compiles it into the game.
