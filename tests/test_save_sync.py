"""Save sync between the Mac and an iPhone / iPad (games/totk/ios/tools/save_sync.py), on folders standing in for both."""
import io
import json
import os
import struct
import sys
import tempfile
import time
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'games/totk/ios/tools'))
import save_sync  # noqa: E402
from save_sync import LocalContainer, Side, decide, folder_hash, sync, uid_folder, version_problem  # noqa: E402

MAC_UID = struct.pack('<QQ', 0x1111, 0x2222)        # folder 0000000000002222 0000000000001111
DEVICE_UID = struct.pack('<QQ', 0xaaaa, 0xbbbb)


def make_profile(root, uid, slots=None, version=None, busy_age=None, when=None):
    """A profile as the game leaves it: account.uid and save/<uid hex>/ with slot files, album and storage."""
    root = Path(root); save = root / 'save' / uid_folder(uid); save.mkdir(parents=True, exist_ok=True)
    (root / 'account.uid').write_bytes(uid)
    for rel, data in (slots or {}).items():
        path = save / rel; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
        if when: os.utime(path, (when, when))
    if version: (root / 'save/.version').write_text(version + '\n')
    if busy_age is not None:
        (root / 'save/.busy').write_text('1 1.4.3\n'); t = time.time() - busy_age; os.utime(root / 'save/.busy', (t, t))
    return save


def edit(save, rel, data, when):
    path = Path(save) / rel; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data); os.utime(path, (when, when))


SAVE_A = {'slot_00/progress.sav': b'a' * 64, 'slot_00/caption.sav': b'cap', 'album/000_Photo.jpg': b'jpg',
          'storage/CacheStorageKey.dat': b'key'}


class Pair(unittest.TestCase):
    """A Mac profile, a device container (Documents/save is the profile) and a sync store in one temporary folder."""
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); root = Path(self.tmp.name)
        self.mac_root, self.device_root, self.store = root / 'mac', root / 'device', root / 'store'
        self.mac_running = False; self.device_running = False
        self.logs = []

    def tearDown(self):
        self.tmp.cleanup()

    def sides(self, mac_version='1.4.3', device_version='1.4.3'):
        mac = Side('Mac', LocalContainer(self.mac_root, lambda: self.mac_running), app_version=mac_version)
        device = Side('device', LocalContainer(self.device_root, lambda: self.device_running), save_sync.DEVICE_PROFILE, device_version)
        return mac, device

    def run_sync(self, action, **kw):
        versions = {k: kw.pop(k) for k in ('mac_version', 'device_version') if k in kw}
        mac, device = self.sides(**versions)
        return sync(action, mac, device, 'UDID-TEST', self.store, log=self.logs.append, **kw)

    def mac_save(self):
        return self.mac_root / 'save' / uid_folder(MAC_UID)

    def device_save(self):
        return self.device_root / 'Documents/save/save' / uid_folder(DEVICE_UID)

    def backups(self):
        folder = self.store / 'backups'
        return sorted(p.name for p in folder.iterdir()) if folder.is_dir() else []


class Policy(unittest.TestCase):
    def test_decisions(self):
        self.assertEqual(decide(None, None, None, 0, 0), (None, False))
        self.assertEqual(decide('x', 'x', 'x', 0, 0), (None, False))
        self.assertEqual(decide(None, 'm', None, 0, 0), ('push', False))        # the device has no save yet
        self.assertEqual(decide(None, None, 'd', 0, 0), ('pull', False))
        self.assertEqual(decide('x', 'm', 'x', 0, 9), ('push', False))          # only the Mac changed, whatever the times
        self.assertEqual(decide('x', 'x', 'd', 9, 0), ('pull', False))
        self.assertEqual(decide('x', 'm', 'd', 5, 9), ('pull', True))           # both changed: the newer wins
        self.assertEqual(decide('x', 'm', 'd', 9, 5), ('push', True))
        self.assertEqual(decide(None, 'm', 'd', 9, 5), ('push', True))          # never synced, both differ

    def test_versions(self):
        self.assertIsNone(version_problem('1.2.1', '1.4.3'))
        self.assertIsNone(version_problem('1.4.3', '1.4.3'))
        self.assertIn('older game', version_problem('1.4.3', '1.2.1'))
        self.assertIn('unknown', version_problem(None, '1.4.3'))
        self.assertIsNone(save_sync.game_version('1.0'))                           # iOS builds before the version was set
        self.assertEqual(save_sync.game_version('1.4.3\n'), '1.4.3')

    def test_uid_folder_matches_the_runtime(self):
        self.assertEqual(uid_folder(MAC_UID), '00000000000022220000000000001111')   # hi then lo, fs_host.c
        self.assertIsNone(uid_folder(b'\0' * 16)); self.assertIsNone(uid_folder(b'short'))

    def test_hash_covers_names_and_contents_not_hidden_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            a = Path(tmp) / 'a'; (a / 'slot_00').mkdir(parents=True); (a / 'slot_00/progress.sav').write_bytes(b'1')
            first = folder_hash(a)
            (a / '.DS_Store').write_bytes(b'x'); self.assertEqual(folder_hash(a), first)
            (a / 'slot_00/progress.sav').write_bytes(b'2'); self.assertNotEqual(folder_hash(a), first)
            (a / 'slot_00/progress.sav').write_bytes(b'1'); (a / 'slot_00/progress.sav').rename(a / 'slot_00/other.sav')
            self.assertNotEqual(folder_hash(a), first)
            self.assertIsNone(folder_hash(Path(tmp) / 'missing'))


class Sync(Pair):
    def test_first_push_writes_under_the_device_uid_and_never_copies_account_uid(self):
        make_profile(self.mac_root, MAC_UID, SAVE_A, version='1.4.3')
        make_profile(self.device_root / 'Documents/save', DEVICE_UID)
        self.assertEqual(self.run_sync('auto'), 'push')
        self.assertEqual(folder_hash(self.device_save()), folder_hash(self.mac_save()))
        self.assertEqual((self.device_root / 'Documents/save/account.uid').read_bytes(), DEVICE_UID)
        self.assertFalse((self.device_root / 'Documents/save/save' / uid_folder(MAC_UID)).exists())
        self.assertEqual((self.device_root / 'Documents/save/save/.version').read_text(), '1.4.3\n')
        manifest = json.loads((self.store / 'UDID-TEST.json').read_text())
        self.assertEqual(manifest['hash'], folder_hash(self.mac_save())); self.assertEqual(manifest['direction'], 'push')
        self.assertEqual(self.backups(), [], 'an empty save is not backed up')
        self.assertIsNone(self.run_sync('auto'))
        self.assertIn('neither save changed', self.logs[-1])

    def test_only_one_side_changed_copies_that_side_with_a_backup(self):
        make_profile(self.mac_root, MAC_UID, SAVE_A, version='1.4.3')
        make_profile(self.device_root / 'Documents/save', DEVICE_UID)
        self.run_sync('auto')
        edit(self.device_save(), 'slot_00/progress.sav', b'device progress', time.time() - 3600)   # older time: still the only change
        self.assertEqual(self.run_sync('auto'), 'pull')
        self.assertEqual((self.mac_save() / 'slot_00/progress.sav').read_bytes(), b'device progress')
        self.assertEqual((self.mac_root / 'account.uid').read_bytes(), MAC_UID)
        [backup] = self.backups()
        self.assertEqual((self.store / 'backups' / backup / 'slot_00/progress.sav').read_bytes(), b'a' * 64)
        self.assertIsNone(self.run_sync('auto'))

    def test_both_changed_keeps_the_newer_and_parks_the_other(self):
        make_profile(self.mac_root, MAC_UID, SAVE_A, version='1.4.3', when=time.time() - 7200)
        make_profile(self.device_root / 'Documents/save', DEVICE_UID)
        self.run_sync('auto')
        now = time.time()
        edit(self.mac_save(), 'slot_01/progress.sav', b'mac', now - 600)
        edit(self.device_save(), 'slot_00/progress.sav', b'device newer', now - 60)
        self.assertEqual(self.run_sync('auto'), 'pull')
        self.assertEqual(folder_hash(self.mac_save()), folder_hash(self.device_save()))
        self.assertFalse((self.mac_save() / 'slot_01').exists())
        [parked] = self.backups()
        self.assertTrue(parked.startswith('save-conflict-Mac-'))
        self.assertEqual((self.store / 'backups' / parked / 'slot_01/progress.sav').read_bytes(), b'mac')
        self.assertTrue(any('CONFLICT' in line for line in self.logs))

    def test_dry_run_changes_nothing(self):
        make_profile(self.mac_root, MAC_UID, SAVE_A, version='1.4.3')
        make_profile(self.device_root / 'Documents/save', DEVICE_UID, {'slot_00/progress.sav': b'old'}, when=time.time() - 9999)
        before = folder_hash(self.device_save())
        self.assertEqual(self.run_sync('auto', dry_run=True), 'push')
        self.assertEqual(folder_hash(self.device_save()), before)
        self.assertFalse((self.store / 'UDID-TEST.json').exists()); self.assertEqual(self.backups(), [])
        self.assertIn('dry run', self.logs[-1])

    def test_busy_source_fresh_blocks_stale_does_not(self):
        make_profile(self.mac_root, MAC_UID, SAVE_A, version='1.4.3', busy_age=30)
        make_profile(self.device_root / 'Documents/save', DEVICE_UID)
        self.assertIsNone(self.run_sync('push'))
        self.assertIn('saving right now', self.logs[-1])
        t = time.time() - 3600; os.utime(self.mac_root / 'save/.busy', (t, t))
        self.assertEqual(self.run_sync('push'), 'push')

    def test_running_games_and_versions_block_a_copy(self):
        make_profile(self.mac_root, MAC_UID, SAVE_A, version='1.4.3')
        make_profile(self.device_root / 'Documents/save', DEVICE_UID, {'slot_00/progress.sav': b'd'})
        self.device_running = True
        self.assertIsNone(self.run_sync('push')); self.assertIn('running on the device', self.logs[-1])
        self.device_running = None   # devicectl could not list processes
        self.assertIsNone(self.run_sync('push')); self.assertIn('--force', self.logs[-1])
        self.device_running = False; self.mac_running = True
        self.assertIsNone(self.run_sync('pull')); self.assertIn('running on the Mac', self.logs[-1])
        self.mac_running = False
        self.assertIsNone(self.run_sync('push', device_version='1.2.1')); self.assertIn('older game', self.logs[-1])
        self.assertIsNone(self.run_sync('push', device_version=None, force=False)); self.assertIn('unknown', self.logs[-1])
        self.assertEqual(self.run_sync('push', device_version=None, force=True), 'push')
        self.assertEqual(self.backups()[0][15:], '-device-UDID-TES')   # <time>-<target>-<udid>

    def test_pull_while_device_game_runs_is_allowed(self):
        make_profile(self.mac_root, MAC_UID, {'slot_00/progress.sav': b'm'}, version='1.4.3')
        make_profile(self.device_root / 'Documents/save', DEVICE_UID, SAVE_A, version='1.4.3')
        self.device_running = True
        self.assertEqual(self.run_sync('pull'), 'pull')

    def test_no_device_account_or_empty_source_refused(self):
        make_profile(self.mac_root, MAC_UID, SAVE_A, version='1.4.3')
        (self.device_root / 'Documents/save').mkdir(parents=True)
        self.assertIsNone(self.run_sync('push')); self.assertIn('no account yet', self.logs[-1])
        make_profile(self.device_root / 'Documents/save', DEVICE_UID)
        self.assertIsNone(self.run_sync('pull')); self.assertIn('no save to copy', self.logs[-1])

    def test_torn_copy_is_retried_until_two_copies_agree(self):
        make_profile(self.mac_root, MAC_UID, SAVE_A, version='1.4.3')
        make_profile(self.device_root / 'Documents/save', DEVICE_UID)
        container = LocalContainer(self.mac_root); calls = []
        real_pull = container.pull
        def pull(rel, dst):
            ok = real_pull(rel, dst)
            if rel.startswith('save/') and len(calls) < 1 and Path(dst).is_dir():
                calls.append(rel); (Path(dst) / 'slot_00/progress.sav').write_bytes(b'half written')
            return ok
        container.pull = pull
        side = Side('Mac', container, app_version='1.4.3').read(Path(self.tmp.name) / 'w')
        save_sync.time.sleep, sleep = (lambda s: None), save_sync.time.sleep
        try:
            self.assertEqual(side.snapshot(Path(self.tmp.name) / 'snap'), folder_hash(self.mac_save()))
        finally:
            save_sync.time.sleep = sleep
        self.assertEqual(len(calls), 1)

    def test_backups_keep_the_last_ten_and_every_conflict(self):
        backups = self.store / 'backups'
        for i in range(14): (backups / f'20260101-0000{i:02d}-device-x').mkdir(parents=True)
        (backups / 'save-conflict-Mac-20250101-000000').mkdir()
        save_sync.prune_backups(self.store)
        names = self.backups()
        self.assertEqual(len(names), 11); self.assertIn('save-conflict-Mac-20250101-000000', names)
        self.assertNotIn('20260101-000000-device-x', names); self.assertIn('20260101-000013-device-x', names)

    def test_status_reports_both_sides_and_changes_nothing(self):
        make_profile(self.mac_root, MAC_UID, SAVE_A, version='1.4.3', busy_age=30)
        make_profile(self.device_root / 'Documents/save', DEVICE_UID)
        self.assertIsNone(self.run_sync('status'))
        text = '\n'.join(self.logs)
        self.assertIn(f'uid {uid_folder(MAC_UID)}', text); self.assertIn('slots slot_00', text)
        self.assertIn('busy saving now', text); self.assertIn('auto would copy the Mac save to the device', text)
        self.assertFalse(self.store.exists())

    def test_command_line_dry_run_with_a_stand_in_device(self):
        make_profile(self.mac_root, MAC_UID, SAVE_A, version='1.4.3')
        make_profile(self.device_root / 'Documents/save', DEVICE_UID)
        out = io.StringIO()
        with redirect_stdout(out):
            save_sync.main(['auto', '--dry-run', '--mac-profile', str(self.mac_root), '--device-dir', str(self.device_root),
                            '--store', str(self.store), '--mac-version', '1.4.3', '--device-version', '1.4.3'])
        self.assertIn('dry run: would copy the Mac save', out.getvalue())
        self.assertEqual(save_sync.save_files(self.device_save()), [])

    def test_agent_runs_auto(self):
        plist = save_sync.agent_plist('/usr/bin/python3', Path('/x/save_sync.py'), '/Dev')
        self.assertEqual(plist['ProgramArguments'][2:], ['auto', '--notify'])
        self.assertEqual(plist['StartInterval'], 120)


if __name__ == '__main__':
    unittest.main()
