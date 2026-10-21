"""Save packages (.totksave, games/totk/runtime/save_package.m): the format, the account mapping, the backup and every
refusal, through tests/save_package_tool.m on fake profiles in a temporary folder. The zip and manifest are read back with
Python's own zipfile / json / hashlib, so the format is checked independently of the code that wrote it."""
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / 'games/totk/runtime'
UID_A = struct.pack('<QQ', 0x1111, 0x2222)   # folder 0000000000002222 0000000000001111
UID_B = struct.pack('<QQ', 0xaaaa, 0xbbbb)
SAVE_A = {'slot_00/progress.sav': b'progress-a' * 500, 'slot_00/caption.sav': b'cap-a', 'slot_01/progress.sav': b'p1' * 300,
          'album/000_Photo.jpg': b'\xff\xd8jpg', 'storage/CacheStorageKey.dat': b'key', 'option.sav': b'options'}
SAVE_B = {'slot_00/progress.sav': b'progress-b' * 100, 'option.sav': b'options-b'}


def uid_folder(raw):
    lo, hi = struct.unpack('<QQ', raw)
    return f'{hi:016x}{lo:016x}'


def make_profile(root, uid, files, version='1.4.3'):
    root = Path(root); save = root / 'save' / uid_folder(uid); save.mkdir(parents=True)
    (root / 'account.uid').write_bytes(uid)
    for rel, data in files.items():
        (save / rel).parent.mkdir(parents=True, exist_ok=True); (save / rel).write_bytes(data)
    (save / '.DS_Store').write_bytes(b'finder')                       # hidden: never sent
    (root / 'save/.version').write_text(version + '\n')
    (root / 'cache/0').mkdir(parents=True); (root / 'cache/0/CacheStorageInfo.dat').write_bytes(b'cache')
    (root / 'temp').mkdir(); (root / 'pso_user.tsv').write_text('pso\n')
    return save


def tree(folder):
    folder = Path(folder)
    return {str(p.relative_to(folder)): p.read_bytes() for p in folder.rglob('*') if p.is_file() and not p.name.startswith('.')}


@unittest.skipUnless(sys.platform == 'darwin', 'Foundation requires macOS')
class SavePackage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('clang')
        if not compiler:
            raise unittest.SkipTest('clang is required')
        cls.build = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.build.name) / 'save-package-tool'
        r = subprocess.run([compiler, '-fobjc-arc', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                            '-I' + str(RUNTIME), str(ROOT / 'tests/save_package_tool.m'), str(RUNTIME / 'save_package.m'),
                            '-framework', 'Foundation', '-lz', '-o', str(cls.binary)], capture_output=True, text=True, timeout=120)
        if r.returncode:
            raise AssertionError(r.stdout + r.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix='totk-save-package-'))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.a, self.b = self.tmp / 'mac/profile', self.tmp / 'ios/save'
        self.save_a = make_profile(self.a, UID_A, SAVE_A)
        self.save_b = make_profile(self.b, UID_B, SAVE_B)
        self.backups = self.tmp / 'ios/save-backups'

    def tool(self, *args, refused=False):
        r = subprocess.run([str(self.binary), *map(str, args)], capture_output=True, text=True, timeout=60)
        self.assertEqual(r.returncode, 2 if refused else 0, r.stdout + r.stderr)
        return r.stdout.strip()

    def export(self, profile=None):
        return Path(self.tool('export', profile or self.a, self.tmp / 'out', '1.4.3', 'mac', "Bennett's Mac"))

    def test_package_format(self):
        package = self.export()
        self.assertEqual(package.suffix, '.totksave')
        self.assertTrue(package.name.startswith("TOTK saves - Bennett's Mac - "))
        with zipfile.ZipFile(package) as z:
            self.assertIsNone(z.testzip())
            names = z.namelist()
            self.assertEqual(names[0], 'manifest.json')
            self.assertEqual(sorted(names[1:]), sorted('save/' + rel for rel in SAVE_A))   # no hidden files, cache, temp, pso, account
            m = json.loads(z.read('manifest.json'))
            for f in m['files']:
                data = z.read('save/' + f['path'])
                self.assertEqual((len(data), hashlib.sha256(data).hexdigest()), (f['size'], f['sha256']))
                self.assertEqual(data, SAVE_A[f['path']])
            self.assertEqual(z.getinfo('save/slot_00/progress.sav').compress_type, zipfile.ZIP_DEFLATED)
        self.assertEqual((m['format'], m['title_id'], m['game_version'], m['save_version']), (1, '0100F2C0115B6000', '1.4.3', '1.4.3'))
        self.assertEqual((m['from_platform'], m['from_device'], m['from_model']), ('mac', "Bennett's Mac", 'TestModel1,1'))
        self.assertEqual([s['name'] for s in m['slots']], ['slot_00', 'slot_01'])
        self.assertEqual([s['files'] for s in m['slots']], [2, 1])
        self.assertTrue(m['created'].endswith('Z'))
        # a second export replaces the first one in the same folder
        time.sleep(0.01); second = self.export()
        self.assertEqual(sorted(p.name for p in (self.tmp / 'out').iterdir()), [second.name])

    def test_import_maps_the_account_and_backs_up(self):
        old = time.mktime((2026, 1, 2, 12, 30, 10, 0, 0, -1))
        os.utime(self.save_a / 'slot_01/progress.sav', (old, old))
        package = self.export()
        info = json.loads(self.tool('inspect', package, '1.4.3'))
        self.assertEqual(info['slots'], ['slot_00', 'slot_01'])
        self.assertIn("Bennett's Mac (TestModel1,1, Mac)", info['from'])
        self.assertEqual(tree(self.save_b), SAVE_B)   # inspecting changes nothing
        done = json.loads(self.tool('import', package, self.b, self.backups, '1.4.3'))
        self.assertEqual(tree(self.save_b), SAVE_A)                                  # into B's own account folder
        self.assertEqual((self.b / 'account.uid').read_bytes(), UID_B)               # the account is never sent
        self.assertEqual(sorted(p.name for p in (self.b / 'save').iterdir()), ['.version', uid_folder(UID_B)])   # no A folder, no leftovers
        self.assertEqual((self.b / 'save/.version').read_text(), '1.4.3\n')
        backup = Path(done['backup'])
        self.assertEqual(backup.parent, self.backups)
        self.assertTrue(backup.name.endswith('-before-import'))
        self.assertEqual(tree(backup), SAVE_B)                                       # the replaced save, restorable by hand
        self.assertEqual(done['last_backup'], str(backup))
        self.assertAlmostEqual(done['last_backup_time'], time.time(), delta=5)
        self.assertTrue(done['summary'].startswith('2 slots, last saved'))
        self.assertAlmostEqual((self.save_b / 'slot_01/progress.sav').stat().st_mtime, old, delta=2)   # files keep their times
        self.assertEqual(tree(self.save_a), SAVE_A)                                  # the sender is untouched

    def test_import_into_a_profile_that_never_started(self):
        package = self.export(); fresh = self.tmp / 'fresh'
        done = json.loads(self.tool('import', package, fresh, self.backups, '1.4.3'))
        uid = (fresh / 'account.uid').read_bytes()
        self.assertEqual(len(uid), 16); self.assertNotEqual(uid, UID_A)
        self.assertEqual(tree(fresh / 'save' / uid_folder(uid)), SAVE_A)
        self.assertEqual(done['backup'], '')                                         # nothing to back up
        self.assertFalse(self.backups.exists())

    def refused(self, package, version='1.4.3', why=''):
        out = self.tool('import', package, self.b, self.backups, version, refused=True)
        self.assertIn(why, out)
        self.assertEqual(tree(self.save_b), SAVE_B)                                  # nothing changed
        self.assertFalse(self.backups.exists())
        self.assertEqual(sorted(p.name for p in (self.b / 'save').iterdir() if p.name != '.busy'), ['.version', uid_folder(UID_B)])
        return out

    def rewrite(self, package, change):
        """A copy of the package with change(name, data) -> data (None drops the entry) and extra entries."""
        out = package.with_name('changed.totksave')
        with zipfile.ZipFile(package) as src, zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as dst:
            for name in src.namelist():
                data = change(name, src.read(name))
                if data is not None:
                    dst.writestr(name, data)
            for name, data in getattr(change, 'extra', {}).items():
                dst.writestr(name, data)
        return out

    def test_refuses_another_game_version(self):
        package = self.export()
        out = self.refused(package, '1.2.1', 'game version 1.4.3; this app runs version 1.2.1')
        self.assertIn('same game version', out)
        self.tool('inspect', package, '1.2.1', refused=True)

    def test_refuses_a_changed_file(self):
        package = self.rewrite(self.export(), lambda n, d: b'tampered' + d if n == 'save/slot_00/progress.sav' else d)
        self.refused(package, why='slot_00/progress.sav in the save package is not what was sent (SHA-256 differs)')

    def test_refuses_a_missing_or_unlisted_file(self):
        self.refused(self.rewrite(self.export(), lambda n, d: None if n == 'save/option.sav' else d), why='lacks option.sav')
        keep = lambda n, d: d   # noqa: E731
        keep.extra = {'save/slot_02/progress.sav': b'extra'}
        self.refused(self.rewrite(self.export(), keep), why='which its manifest does not list')

    def test_refuses_paths_outside_the_save_folder(self):
        for name in ('save/../account.uid', '../escape', 'save/.busy', 'other/file', 'save//x'):
            with self.subTest(name=name):
                keep = lambda n, d: d   # noqa: E731
                keep.extra = {name: b'x'}
                self.refused(self.rewrite(self.export(), keep), why='holds a file it should not')
        def manifest(n, d):
            if n != 'manifest.json': return d
            m = json.loads(d); m['files'][0]['path'] = '../account.uid'; return json.dumps(m).encode()
        self.refused(self.rewrite(self.export(), manifest), why="manifest is damaged")

    def test_refuses_other_titles_formats_and_non_packages(self):
        def field(key, value):
            def change(n, d):
                if n != 'manifest.json': return d
                m = json.loads(d); m[key] = value; return json.dumps(m).encode()
            return change
        self.refused(self.rewrite(self.export(), field('title_id', '01007EF00011E000')), why='not for Tears of the Kingdom')
        self.refused(self.rewrite(self.export(), field('format', 2)), why='newer version of the app')
        junk = self.tmp / 'junk.totksave'; junk.write_bytes(b'not a zip at all' * 10)
        self.refused(junk, why='not a save package')
        cut = self.tmp / 'cut.totksave'; cut.write_bytes(self.export().read_bytes()[:-30])
        self.refused(cut)

    def test_busy_marker(self):
        package = self.export()
        busy = self.b / 'save/.busy'; busy.write_text('123 1.4.3\n')
        self.refused(package, why='saving right now')
        old = time.time() - 11 * 60; os.utime(busy, (old, old))                   # left by a crash: ignored
        self.tool('import', package, self.b, self.backups, '1.4.3')
        (self.a / 'save/.busy').write_text('1 1.4.3\n')
        self.assertIn('saving right now', self.tool('export', self.a, self.tmp / 'out2', '1.4.3', 'mac', 'x', refused=True))

    def test_export_without_saves(self):
        empty = self.tmp / 'empty'; (empty / 'save' / uid_folder(UID_A)).mkdir(parents=True); (empty / 'account.uid').write_bytes(UID_A)
        self.assertIn('no saves', self.tool('export', empty, self.tmp / 'out', '1.4.3', 'ios', 'x', refused=True))
        self.assertIn('no saves', self.tool('export', self.tmp / 'nothing', self.tmp / 'out', '1.4.3', 'ios', 'x', refused=True))

    def test_keeps_the_last_ten_backups(self):
        package = self.export()
        for _ in range(12):
            self.tool('import', package, self.b, self.backups, '1.4.3')
        names = sorted(p.name for p in self.backups.iterdir())
        self.assertEqual(len(names), 10)
        self.assertEqual(len(set(names)), 10)
        self.assertEqual(tree(self.save_b), SAVE_A)


if __name__ == '__main__':
    unittest.main()
