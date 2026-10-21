"""The game data outside the Mac app (build_app.step_store, step_app with a store, prune_data). Synthetic bytes only."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_app


def sha(data):
    return hashlib.sha256(data).hexdigest()


class GameData(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.tree, self.data, self.logs = self.root / 'tree', self.root / 'game-data', self.root / 'logs'
        romfs = self.tree / 'extracted/v143/romfs'
        romfs.mkdir(parents=True)
        self.image = b'romfs image bytes'
        (romfs / 'romfs.bin').write_bytes(self.image)
        (romfs / 'runtime-index.bin').write_bytes(b'index')
        (romfs / 'index.json').write_text(json.dumps(dict(romfs_sha256=sha(self.image))))
        seed = self.tree / 'build/shader-seed/v143'
        seed.mkdir(parents=True)
        files = {'a' * 64 + '-000000000000_b.metal': b'metal source', 'pso_manifest.tsv': b'rows'}
        for name, data in files.items():
            (seed / name).write_bytes(data)
        (seed / 'seed-manifest.json').write_text(json.dumps(dict(version='v143', chain='totk-native-v6-unverified',
                                                                 shaders=8000, files={n: sha(d) for n, d in files.items()})))
        libs = self.tree / 'build/shader-libs'
        libs.mkdir()
        (libs / 'compiler.txt').write_text('metal 1')
        (libs / f'{sha(b"metal source")}.metallib').write_bytes(b'lib')
        (libs / f'{"f" * 64}.metallib').write_bytes(b'a library of an older seed')
        versions = dict(build_app.VERSIONS, v143=dict(build_app.VERSIONS['v143'], romfs=dict(sha256=sha(self.image),
                                                                                             size=len(self.image))))
        patcher = mock.patch.object(build_app, 'VERSIONS', versions)
        patcher.start()
        self.addCleanup(patcher.stop)
        say = mock.patch.object(build_app, 'say')
        say.start()
        self.addCleanup(say.stop)

    def store(self):
        return build_app.step_store(self.tree, self.data, self.logs, version='v143')

    def test_folders_hold_the_data_and_are_found_again(self):
        pointer = self.store()
        romfs, shaders = Path(pointer['romfs']['path']), Path(pointer['shaders']['path'])
        self.assertEqual((romfs / 'romfs.bin').read_bytes(), self.image)
        self.assertEqual(pointer['romfs']['sizes']['romfs.bin'], len(self.image))
        self.assertTrue(romfs.name.startswith('v143-romfs-') and shaders.name.startswith('v143-shaders-'))
        self.assertTrue((shaders / 'shader-seed/seed-manifest.json').is_file())
        self.assertEqual(sorted(p.name for p in (shaders / 'shader-libs').iterdir()), [f'{sha(b"metal source")}.metallib'])
        self.assertFalse((romfs / 'romfs.bin').stat().st_mode & 0o222, 'the game data is read-only')
        with mock.patch.object(build_app, '_store_write', wraps=build_app._store_write) as write, \
             mock.patch.object(build_app, 'clone') as clone:
            self.assertEqual(self.store(), pointer)
        clone.assert_not_called()
        self.assertEqual(write.call_count, 2)

    def test_changed_shaders_get_a_new_folder_and_the_romfs_stays(self):
        first = self.store()
        seed = self.tree / 'build/shader-seed/v143'
        manifest = json.loads((seed / 'seed-manifest.json').read_text())
        manifest['shaders'] = 9000
        (seed / 'seed-manifest.json').write_text(json.dumps(manifest))
        second = self.store()
        self.assertEqual(first['romfs'], second['romfs'])
        self.assertNotEqual(first['shaders']['id'], second['shaders']['id'])

    def test_a_damaged_image_is_made_again(self):
        pointer = self.store()
        image = Path(pointer['romfs']['path']) / 'romfs.bin'
        image.chmod(0o644)
        image.write_bytes(b'romfs image BYTES')   # same size, other bytes: its identity changed, so it is hashed again
        self.assertEqual(self.store(), pointer)
        self.assertEqual(image.read_bytes(), self.image)

    def test_a_seed_file_that_differs_from_its_manifest_stops_the_build(self):
        (self.tree / 'build/shader-seed/v143/pso_manifest.tsv').write_bytes(b'ROWS')
        with self.assertRaisesRegex(build_app.BuildError, 'invalid prepared startup shader seed'):
            self.store()
        self.assertFalse(any(p.name.startswith('v143-shaders-') for p in self.data.iterdir()))

    def test_the_app_carries_a_pointer_instead_of_the_data(self):
        pointer = self.store()
        for rel in ('build/totk-native-v143', 'build/pso_manifest.tsv', 'build/pso_title.tsv', 'build/spirv-cross/spirv-cross',
                    'prebuilt/macos-arm64/totk_xlate', 'build/app-launcher',
                    *[f'rewritten/v143/{n}.patched.bin' for n in ('main', 'sdk', 'subsdk0')]):
            (self.tree / rel).parent.mkdir(parents=True, exist_ok=True)
            (self.tree / rel).write_bytes(b'test')
        out = self.root / 'Game.app'
        with mock.patch.object(build_app, 'sign_app'):
            build_app.step_app(self.tree, out, self.logs, version='v143', store=pointer)
        res = out / 'Contents/Resources/totk'
        self.assertEqual(json.loads((res / 'game-data.json').read_text()), pointer)
        self.assertFalse((res / 'extracted/v143/romfs').exists())
        self.assertFalse((res / 'build/shader-seed').exists())
        self.assertTrue((res / 'build/totk-native').is_file())

    def test_prune_keeps_what_an_app_points_to(self):
        pointer = self.store()
        app = self.root / 'Game.app'
        (app / 'Contents/Resources/totk').mkdir(parents=True)
        (app / 'Contents/Resources/totk/game-data.json').write_text(json.dumps(pointer))
        build_app.register_app(self.data, app, pointer)
        old = self.data / 'v143-shaders-0000000000000000'
        old.mkdir()
        (old / 'store.json').write_text('{}')
        (self.data / 'unrelated').mkdir()
        with mock.patch.object(build_app.subprocess, 'run', return_value=mock.Mock(stdout='')):
            build_app.prune_data(self.data)
        self.assertFalse(old.exists())
        self.assertTrue(Path(pointer['romfs']['path']).is_dir() and Path(pointer['shaders']['path']).is_dir())
        self.assertTrue((self.data / 'unrelated').is_dir(), 'only game-data folders are deleted')


if __name__ == '__main__':
    unittest.main()
