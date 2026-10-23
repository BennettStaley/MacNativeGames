// Tegra block linear <-> linear. Level / layer offsets and per-level block height and depth always come from nvn/nvn_tex.c (nvn_tex_level_offset / nvn_tex_layout / nvn_tex_level_tiles).
// Tegra block-linear ↔ linear. Level / layer offsets and per-level block height and block depth always come from nvn/nvn_tex.c (nvn_tex_level_offset / nvn_tex_layout / nvn_tex_level_tiles).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../nvn/nvn_tex.h"

// Deswizzles level `level`, layer `layer` (array / cube layer; 0 for non-layered textures) from the whole texture storage src_storage into linear dst_linear.
// dst is laid out in block rows: dst_pitch bytes per row (>= level width in blocks x bytes per block); for compressed formats one row = one row of blocks.
// 3D textures: all depth slices of the level are deswizzled at once; slice k is at dst_linear + k x dst_pitch x the level's block rows.
// Linear / linear render targets are copied by pitch; multisample and BUFFER are unsupported and return -1. Returns 0 on success.
// Decode level number level, layer number layer (layer of an array / cube; 0 for non-layered textures) from the whole-texture storage src_storage into linear dst_linear.
// dst is laid out by block rows: dst_pitch bytes per row (≥ width in blocks of this level × bytes per block); for compressed formats one row = one row of blocks.
// 3D textures: all depth slices of this level are decoded at once; slice k is at dst_linear + k × dst_pitch × block row count of this level.
// Linear / linear-render-target storage is copied by pitch. Multisampling and BUFFER return -1; success returns 0.
int nvn_deswizzle(const nvn_texdesc *d, unsigned level, unsigned layer, const void *src_storage, void *dst_linear, size_t dst_pitch);
// Reverse (for unit-test round trips): linear src_linear -> written into the matching level / layer of the whole texture storage dst_storage
// Reverse (for unit test round trips): linear src_linear → written into the corresponding level / layer of the whole-texture storage dst_storage
int nvn_swizzle(const nvn_texdesc *d, unsigned level, unsigned layer, const void *src_linear, size_t src_pitch, void *dst_storage);
// Single-point address: within this level and layer, linear (x bytes, y block rows, z slices) -> block-linear byte offset from the level/layer start (unit tests check it against hand computation)
// Single-point address: within this level and layer, linear (x byte, y block row, z slice) → block-linear byte offset relative to the start of this level and layer (unit tests verify against hand calculation)
uint64_t nvn_bl_offset(uint32_t x, uint32_t y, uint32_t z, uint32_t row_bytes, uint32_t rows, unsigned tileH, unsigned tileD);

int nvn_deswizzle_geom(const nvn_texdesc *d, unsigned level, unsigned layer,
                       uint32_t *row_bytes, uint32_t *rows, uint32_t *slices,
                       uint64_t *base, unsigned *tileH, unsigned *tileD, uint32_t *bpb);
