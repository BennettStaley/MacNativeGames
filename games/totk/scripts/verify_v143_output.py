#!/usr/bin/env python3
"""Execute the production output veneer and the original display-scale path.

Native screenshots/pass dumps still verify actual GPU allocation separately.
No game entry point, graphics or user saves are used by this check.
"""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import (
    UC_ARM64_REG_PC, UC_ARM64_REG_X0, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_X4,
    UC_ARM64_REG_X8, UC_ARM64_REG_X9, UC_ARM64_REG_X10, UC_ARM64_REG_X11, UC_ARM64_REG_X19, UC_ARM64_REG_X23,
)

ROOT = Path(__file__).resolve().parents[1]
RENDER_SITES = {0x585e48, 0x585e50, 0x585e54, 0x585e60}
RENDER_KICK = {0x580920}   # the per-frame update's display-state load, ORed with the data page +48 (a live pick)
# runtime/world_patch.h: the time of day's step (s9) and the two far-plane loads, each a 32-byte stub from +304.
WORLD_SITES = {0x68c15c: (0xbd403e69, 304, 72), 0x628c30: (0xbd413a60, 336, 76), 0xb43420: (0xbd4062c0, 368, 76)}


def clean_env():
    """The fixture runs the production patches with this environment: a size or export switch left in the shell
    would change words the checks below count."""
    return {k: v for k, v in os.environ.items()
            if k not in ('TOTK_RENDER_RES', 'TOTK_RENDER_RESERVE', 'TOTK_EXPORT_IOS', 'TOTK_OUTPUT_RES',
                          'TOTK_TIME_SPEED', 'TOTK_RENDER_DISTANCE')}


def render_res_checks(out, raw, data, layout):
    """Scene render size (TOTK_RENDER_RES, PR #19; live since runtime_render_request): the four 1.4.3 sites always
    carry the patch (TOTK_RENDER_RES=0 leaves them alone) and the size sits on the data page +16: unset, the game's own
    docked 1600x900, which the patched sequence stores exactly as the original does; set, that size with the output
    width kept; the iOS export embeds the four words always; a live request is latched by the next present and divided
    by the canvas scale (2560 wide: 1600x900 asked is written as 1200x675); a malformed value stops the start."""
    def image(extra):
        path = out / 'main-render-res.bin'
        env = clean_env() | {'TOTK_REALTIME': '1', 'TOTK_FPS60': '1', 'TOTK_OUTPUT_RES': '0',
                             'TOTK_ROMFS_PATCHES_OUT': str(out / 'no-overlays')} | extra
        run = subprocess.run(['build/v143-output-fixture', 'rewritten/v143/main.patched.bin', str(path),
                              str(layout['TEXT_END'])], capture_output=True, text=True, env=env)
        got = path.read_bytes() if run.returncode == 0 else None
        path.unlink(missing_ok=True)
        return run.returncode, got

    def sites_changed(got):
        return {p for p in RENDER_SITES if raw[p:p+4] != got[p:p+4]}

    def stores(img):
        mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        mu.mem_map(0, (len(img) + 0x3fff) & ~0x3fff)
        mu.mem_write(0, img)
        renderer, source = 0x10000000, 0x10004000
        mu.mem_map(renderer, 0x8000)
        mu.mem_write(source + 0x40, struct.pack('<I', 0x1234))
        mu.mem_write(renderer + 0xc38, struct.pack('<Q', 0x5678))
        mu.mem_write(renderer + 0x9f0, struct.pack('<Q', 0x9abc))
        mu.reg_write(UC_ARM64_REG_X19, renderer)
        mu.reg_write(UC_ARM64_REG_X8, source)
        mu.reg_write(UC_ARM64_REG_X9, 1920)
        mu.reg_write(UC_ARM64_REG_X10, 900)
        mu.reg_write(UC_ARM64_REG_X11, 1600)
        mu.emu_start(0x585e48, 0x585e6c, count=16)
        assert mu.reg_read(UC_ARM64_REG_PC) == 0x585e6c
        assert (mu.reg_read(UC_ARM64_REG_X2), mu.reg_read(UC_ARM64_REG_X9)) == (0x1234, 1920)
        assert (mu.reg_read(UC_ARM64_REG_X8), mu.reg_read(UC_ARM64_REG_X0)) == (0x5678, 0x9abc)
        size = struct.unpack('<ff', mu.mem_read(renderer + 0xcc4, 8))
        stores.renderer = struct.unpack('<Q', mu.mem_read(data + 24, 8))[0] if len(img) > data + 24 else None
        output = struct.unpack('<f', mu.mem_read(renderer + 0xcc0, 4))[0]
        return size, output

    code, off = image({'TOTK_RENDER_RES': '0'})
    assert code == 0 and sites_changed(off) == set(), 'TOTK_RENDER_RES=0 changed the render-size sites'
    code, unset = image({})
    assert code == 0 and sites_changed(unset) == RENDER_SITES and raw[0x580920:0x580924] != unset[0x580920:0x580924], 'TOTK_RENDER_RES unset did not patch the four sites'
    assert struct.unpack('<ff', unset[data + 16:data + 24]) == (1600.0, 900.0), 'unset is not the game\'s own docked size'
    code, set_ = image({'TOTK_RENDER_RES': '960x540'})
    assert code == 0 and sites_changed(set_) == RENDER_SITES, 'TOTK_RENDER_RES=960x540 did not patch the four sites'
    veneer = (layout['TEXT_END'] + 15 & ~15) + 272
    others = {p for p in range(0, data, 4) if off[p:p+4] != set_[p:p+4]}
    assert others == RENDER_SITES | RENDER_KICK | {veneer + 4 * i for i in range(8)}, [hex(p) for p in sorted(others ^ RENDER_SITES)]
    assert struct.unpack('<ff', set_[data + 16:data + 24]) == (960.0, 540.0)
    assert stores(raw) == ((1600.0, 900.0), 1920.0), 'the original sequence no longer stores the game size'
    assert stores(unset) == stores(raw), 'unset, the patched sequence does not store what the original does'
    assert stores(set_) == ((960.0, 540.0), 1920.0), 'the patched sequence does not store 960x540 with the output kept'
    assert stores.renderer == 0x10000000, 'the veneer did not keep the renderer on the data page +24'
    def kick(img, flag):
        """The per-frame update's display-state load (main+0x580920) through the second veneer: the flags it tests."""
        img = bytearray(img); img[data + 48:data + 52] = struct.pack('<I', flag)
        mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        mu.mem_map(0, (len(img) + 0x3fff) & ~0x3fff); mu.mem_write(0, bytes(img))
        mu.mem_map(0x20000000, 0x4000); mu.mem_write(0x20000008, struct.pack('<H', 0x43))
        mu.reg_write(UC_ARM64_REG_X23, 0x20000000); mu.reg_write(UC_ARM64_REG_X9, 0x280)
        mu.emu_start(0x580920, 0x580924, count=8)
        assert mu.reg_read(UC_ARM64_REG_PC) == 0x580924
        return mu.reg_read(UC_ARM64_REG_X8)
    assert kick(raw + bytes(data + 0x4000 - len(raw)), 0) == 0x43 and kick(unset, 0) == 0x43, 'the display-state load changed'
    assert kick(unset, 0x80) == 0xc3, 'the data page +48 does not reach the mode-change test'
    code, live = image({'TOTK_OUTPUT_RES': '2560x1440', 'FIXTURE_RENDER_REQUEST': '1600x900'})
    assert code == 0 and struct.unpack('<ff', live[data + 16:data + 24]) == (1200.0, 675.0), 'a live request on a 2560 canvas'
    code, _ = image({'FIXTURE_RENDER_REQUEST': '2560x1440'})
    assert code == 3, 'a live request above the boot reserve (no HeapDef overlay here) was accepted'
    export = {'TOTK_EXPORT_IOS': str(out / 'unused-export')}
    code, export_unset = image(export)
    assert code == 0 and sites_changed(export_unset) == RENDER_SITES, 'the iOS export did not embed the render-size patch'
    code, _ = image({'TOTK_RENDER_RES': '960x540x'})
    assert code != 0, 'a malformed TOTK_RENDER_RES was accepted'
    return dict(changed_words=len(RENDER_SITES), unset_size=[1600, 900], patched_size=[960, 540], kept_output_width=1920,
                live_request_on_2560_canvas=[1200, 675], pick_flag_reaches_mode_test=True, above_reserve_refused=True, ios_export_unset_changed=len(RENDER_SITES),
                malformed_rejected=True)


def world_checks(out, raw, data, layout):
    """Time of day speed and render distance (runtime/world_patch.h): the three 1.4.3 sites hold their original words,
    the padding they use is empty, and the production code always installs them (Mac and the iOS export). Each stub
    is executed: with the data page factor 0 it leaves the game's value, with a factor it multiplies it, and paused
    (-0.0) gives a zero step; x0 (the time site's live return value), x10 and sp come through, and it returns right
    after the site."""
    from unicorn.arm64_const import UC_ARM64_REG_S0, UC_ARM64_REG_S9, UC_ARM64_REG_X22, UC_ARM64_REG_SP
    base = (layout['TEXT_END'] + 15) & ~15
    for site, (word, off, _) in WORLD_SITES.items():
        assert struct.unpack('<I', raw[site:site + 4])[0] == word, f'world site {site:#x} is not the verified 1.4.3 word'
        assert raw[base + off:base + off + 32] == bytes(32), f'world stub padding +{off} not empty'
    assert base + 400 <= layout['RO_OFF'], 'world stubs would overlap read-only data'

    def image(extra):
        path = out / 'main-world.bin'
        env = clean_env() | {'TOTK_REALTIME': '1', 'TOTK_FPS60': '1', 'TOTK_OUTPUT_RES': '0',
                             'TOTK_ROMFS_PATCHES_OUT': str(out / 'no-overlays')} | extra
        run = subprocess.run(['build/v143-output-fixture', 'rewritten/v143/main.patched.bin', str(path),
                              str(layout['TEXT_END'])], capture_output=True, text=True, env=env)
        got = path.read_bytes() if run.returncode == 0 else None
        path.unlink(missing_ok=True)
        return run.returncode, got

    def run_site(img, site, factor_bits):
        word, off, slot = WORLD_SITES[site]
        img = bytearray(img); img[data + slot:data + slot + 4] = struct.pack('<I', factor_bits)
        mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        mu.mem_map(0, (len(img) + 0x3fff) & ~0x3fff); mu.mem_write(0, bytes(img))
        obj, stack = 0x30000000, 0x31000000
        mu.mem_map(obj, 0x4000); mu.mem_map(stack, 0x4000)
        mu.mem_write(obj + 0x3c, struct.pack('<f', 2.0))      # the clock's step
        mu.mem_write(obj + 0x138, struct.pack('<f', 1000.0))  # the camera's far plane (update)
        mu.mem_write(obj + 0x60, struct.pack('<f', 1000.0))   # the camera's far plane (setup)
        for r in (UC_ARM64_REG_X19, UC_ARM64_REG_X22): mu.reg_write(r, obj)
        mu.reg_write(UC_ARM64_REG_X0, 0x1111); mu.reg_write(UC_ARM64_REG_X10, 0x2222); mu.reg_write(UC_ARM64_REG_SP, stack + 0x2000)
        mu.emu_start(site, site + 4, count=12)
        assert mu.reg_read(UC_ARM64_REG_PC) == site + 4
        assert (mu.reg_read(UC_ARM64_REG_X0), mu.reg_read(UC_ARM64_REG_X10)) == (0x1111, 0x2222), 'a live register changed'
        assert mu.reg_read(UC_ARM64_REG_SP) == stack + 0x2000
        reg = UC_ARM64_REG_S9 if site == 0x68c15c else UC_ARM64_REG_S0
        return struct.unpack('<f', struct.pack('<I', mu.reg_read(reg) & 0xffffffff))[0]

    code, img = image({})
    assert code == 0
    changed = {p for p in WORLD_SITES if raw[p:p + 4] != img[p:p + 4]}
    assert changed == set(WORLD_SITES), 'the world sites are not all patched'
    assert img[data + 72:data + 80] == bytes(8), 'the game\'s own time and distance are not 0 on the data page'
    bits = lambda f: struct.unpack('<I', struct.pack('<f', f))[0]
    results = {}
    for site in WORLD_SITES:
        own = run_site(img, site, 0)
        want_own = run_site(raw + bytes(data + 0x4000 - len(raw)), site, 0)
        assert own == want_own, f'{site:#x}: factor 0 is not the game\'s own value'
        scaled = run_site(img, site, bits(2.5))
        assert scaled == own * 2.5, (hex(site), own, scaled)
        results[hex(site)] = [own, scaled]
    paused = run_site(img, 0x68c15c, 0x80000000)
    assert paused == 0.0, 'paused (-0.0) did not give a zero step'
    code, start = image({'TOTK_TIME_SPEED': '0', 'TOTK_RENDER_DISTANCE': '250'})
    assert code == 0 and struct.unpack('<If', start[data + 72:data + 80]) == (0x80000000, 2.5), 'the start values are not on the data page'
    code, start = image({'TOTK_TIME_SPEED': '800', 'TOTK_RENDER_DISTANCE': '100'})
    assert code == 0 and struct.unpack('<fI', start[data + 72:data + 80]) == (8.0, 0)
    code, _ = image({'TOTK_TIME_SPEED': '300'})
    assert code != 0, 'a time speed outside the choices was accepted'
    code, _ = image({'TOTK_RENDER_DISTANCE': '20'})
    assert code != 0, 'a render distance below 50 was accepted'
    code, export = image({'TOTK_EXPORT_IOS': str(out / 'unused-export')})
    assert code == 0 and export[:data] == img[:data], 'the iOS export does not embed the same world stubs'
    return dict(sites={hex(p): hex(w) for p, (w, _, _) in WORLD_SITES.items()}, stubs=[hex(base + 304), hex(base + 400)],
                game_own_and_x2_5=results, paused_step=paused, ios_export_embedded=True)


def main():
    os.chdir(ROOT)
    out = ROOT / 'analysis/v143-port'
    raw = (ROOT / 'rewritten/v143/main.patched.bin').read_bytes()
    expected = json.loads((ROOT / 'assets/expected-v1.4.3.json').read_text())
    assert hashlib.sha256(raw).hexdigest() == expected['rewritten_sha256']['main.patched.bin']
    layout = json.loads((out / 'runtime-layout.json').read_text())[0]['layout']
    subprocess.run([
        'xcrun', 'clang', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
        '-Iruntime', 'scripts/v143_timing_fixture.c', 'runtime/game_patches_v143.c',
        '-o', 'build/v143-output-fixture',
    ], check=True)
    target = out / 'main-output-fixture.bin'
    run = subprocess.run([
        'build/v143-output-fixture', 'rewritten/v143/main.patched.bin', str(target),
        str(layout['TEXT_END']),
    ], capture_output=True, text=True, check=True,
        env=clean_env() | {'TOTK_REALTIME': '1', 'TOTK_FPS60': '1', 'TOTK_OUTPUT_RES': '2560x1440'})
    (out / 'output-fixture.log').write_text(run.stdout + run.stderr)
    patched = target.read_bytes()
    data = (len(raw) + 0x3fff) & ~0x3fff
    # Same placement as game_patches_v143.c: both veneers sit in the zero
    # padding after the text segment, the frame-step one (4 words) first and
    # this one (7 words) right after it. They must end before read-only data.
    frame_step = (layout['TEXT_END'] + 15) & ~15
    stub = frame_step + 16
    assert stub + 28 <= layout['RO_OFF'], 'output veneer would overlap read-only data'
    assert raw[frame_step:stub + 28] == bytes(stub + 28 - frame_step), \
        'veneer padding not empty in the rewritten image'
    changes = {p for p in range(0, len(raw), 4) if raw[p:p+4] != patched[p:p+4]}
    want = {
        0xbdb578, 0xbdb584, 0x3dcb28,
        0x2db204, 0x2db208, 0x2db22c, 0x2db360, 0x2db36c, 0x2db340, 0x2db34c,
        0x3e5658, 0x2daa54, 0x2daa58, 0x2daa6c, 0x2daa70, 0x2daa74, 0x2daa78,
    } | RENDER_SITES | RENDER_KICK | set(WORLD_SITES) | {frame_step + 304 + 4 * i for i in range(24)} | {frame_step + 272 + 4 * i for i in range(8)} | {frame_step + 4 * i for i in range(4)} | {stub + 4 * i for i in range(7)}
    # The 21:9 view (runtime/aspect_patch.h, always installed since c446bce): four sites and its stubs after the two veneers.
    # The field of view (aspect_patch.h fov_install): two camera sites and their 48-byte stubs after the aspect stubs.
    aspect = {0x628c74, 0xb43460, 0x1ff04f4, 0x253fbb0, 0x628c3c, 0xb4342c} | {p for p in changes if frame_step + 48 <= p < frame_step + 272}
    assert changes - aspect == want, [hex(p) for p in sorted((changes - aspect) ^ want)]

    def execute(w, h, scale, apply):
        mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        mu.mem_map(0, len(patched))
        mu.mem_write(0, patched if apply else raw)
        config = 0x10000000
        mu.mem_map(config, 0x4000)
        mu.mem_write(config, bytes([0xa5]) * 0x600)
        mu.mem_write(config + 0x10, struct.pack('<II', 1920, 1080))
        mu.mem_write(config + 0x0c, struct.pack('<I', 0))
        mu.mem_write(config + 0x520, struct.pack('<ff', *scale))
        mu.mem_write(data + 32, struct.pack('<II', w, h))
        mu.mem_write(data + 40, bytes(8))
        mu.reg_write(UC_ARM64_REG_X19, config)
        mu.reg_write(UC_ARM64_REG_X8, 0)
        mu.reg_write(UC_ARM64_REG_X9, 1 << 21)
        mu.reg_write(UC_ARM64_REG_X3, (1080 << 32) | 1920)
        mu.emu_start(0x3dca48, 0x3dcb30, count=100)
        assert mu.reg_read(UC_ARM64_REG_PC) == 0x3dcb30
        assert mu.reg_read(UC_ARM64_REG_X19) == config
        canvas = struct.unpack('<II', mu.mem_read(config + 0x10, 8))
        crop = struct.unpack('<II', mu.mem_read(config + 0x508, 8))
        assert struct.unpack('<ff', mu.mem_read(config + 0x528, 8)) == scale
        assert bytes(mu.mem_read(config + 0x518, 8)) == bytes([0xa5]) * 8
        if apply:
            assert canvas == crop == (w, h), (canvas, crop)
            assert mu.reg_read(UC_ARM64_REG_X4) == h
            assert struct.unpack('<Q', mu.mem_read(data + 40, 8))[0] == config
        return dict(canvas=canvas, crop=crop, scale=scale)

    cases = []
    for w, h in [(1920, 1080), (2560, 1440), (1920, 1080)]:
        for scale in [(1.0, 1.0), (0.5, 0.75)]:
            cases.append(execute(w, h, scale, True))
    negative = execute(2560, 1440, (1.0, 1.0), False)
    assert negative['canvas'] == negative['crop'] == (1920, 1080)

    # Auto (no TOTK_OUTPUT_RES) on the Mac takes 2560x1440 when the heap overlay exists. iOS shows 720p only: the iOS
    # export (TOTK_EXPORT_IOS) must leave every output word alone, in auto and with an explicit size (2026-10-18).
    overlay = out / 'auto-overlay'
    (overlay / '2560x1440').mkdir(parents=True, exist_ok=True)
    (overlay / '2560x1440/manifest.tsv').write_text('# presence is what auto looks for\n')
    output_sites = {0xbdb578, 0xbdb584, 0x3dcb28}

    def output_words(extra):
        image = out / 'main-output-auto.bin'
        env = clean_env()
        env |= {'TOTK_REALTIME': '1', 'TOTK_FPS60': '1', 'TOTK_ROMFS_PATCHES_OUT': str(overlay)} | extra
        subprocess.run(['build/v143-output-fixture', 'rewritten/v143/main.patched.bin', str(image),
                        str(layout['TEXT_END'])], capture_output=True, text=True, check=True, env=env)
        got = image.read_bytes(); image.unlink()
        return {p for p in output_sites if raw[p:p+4] != got[p:p+4]}

    export = {'TOTK_EXPORT_IOS': str(out / 'unused-export')}
    auto_mac = output_words({})
    auto_export = output_words(export)
    explicit_export = output_words(export | {'TOTK_OUTPUT_RES': '2560x1440'})
    assert auto_mac == output_sites, ('auto on the Mac did not take 2560x1440', auto_mac)
    assert auto_export == set(), ('the iOS export took the 2560x1440 swapchain in auto', auto_export)
    assert explicit_export == set(), ('the iOS export took an explicit output size', explicit_export)
    render = render_res_checks(out, raw, data, layout)
    world = world_checks(out, raw, data, layout)
    report = dict(status='pass', version='v143', cases=cases,
                  negative_control=negative, veneer_offset=hex(stub),
                  auto={'mac_changed_output_words': len(auto_mac), 'ios_export_auto_changed': len(auto_export), 'ios_export_explicit_changed': len(explicit_export)},
                  render_res=render,
                  world=world,
                  changed_original_words=len(changes),
                  scope='production C patch, original scale selection, canvas/crop publication; GPU allocations tested separately')
    (out / 'output-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
