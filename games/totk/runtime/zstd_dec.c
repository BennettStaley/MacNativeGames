/* Zstandard decompression (decoder only) for the mods loader in romfs.c: the ResourceSizeTable it patches and the mod
   files whose size entry needs their contents. The library is zstd 1.5.7's single-file decoder (third_party/zstd, BSD),
   built here so that every build compiling runtime/ (the Makefile's lists, the iOS project) gets it. */
#include "../third_party/zstd/zstddeclib.c"
