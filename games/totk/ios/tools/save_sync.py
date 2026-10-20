#!/usr/bin/env python3
"""Keep the saves of the Mac app and an iPhone / iPad app in step (docs/save-sync.md).

A profile (Mac: ~/Library/Application Support/MacNativeGames/TOTK/profile, iOS: the app's Documents/save) holds
account.uid, 16 random bytes made on each device (runtime/account.c), and save/<uid hex>/ with the slots, album and storage
(runtime/fs_host.c). The game only opens its own uid's folder, so a sync copies the folder's contents into the other
side's own uid folder; account.uid itself is never copied. The whole folder is the unit: slot_*, album, storage.

The game writes saves in place, so a copy is only taken while no save is in progress (save/.busy, younger than 10 minutes)
and is taken twice: two copies that differ were torn by a write. The game must not be running on the side that receives
a save (Mac: totk-native or a held game slot; iOS: devicectl's process list, or --force when that is not available), and a
save is never sent to a game older than the one that wrote it (save/.version, or the app's version).

A manifest per device (save-sync/<udid>.json) records what both sides held after the last sync. `auto` copies the side
that changed since; when both changed, the newer save (by file time) wins and the other is parked in save-sync/backups/
as save-conflict-<side>-<time>. Before any save is replaced, a copy goes to save-sync/backups/ (the last 10 are kept).

usage: save_sync.py status | pull | push | auto [--device <udid|name>] [--dry-run] [--force]
       save_sync.py install | uninstall     (a LaunchAgent that runs `auto` every 2 minutes)"""
import argparse
import datetime
import fcntl
import hashlib
import json
import os
import plistlib
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from shader_contrib_watch import DATA, devicectl, game_bundle, reachable_devices   # noqa: E402

STORE = DATA / 'save-sync'
MAC_PROFILE = DATA / 'profile'
DEVICE_PROFILE = 'Documents/save'       # TOTK_PROFILE_DIR on iOS (ios/Sources/main.m)
MAC_BUNDLE_ID = 'org.macnativegames.totk'
BUSY_STALE = 10 * 60                    # a .busy older than this was left by a game that stopped in the middle of a save
KEEP_BACKUPS = 10
SYNC_EVERY = 2 * 60                     # the LaunchAgent's interval
LABEL = 'org.macnativegames.totk.save-sync'
AGENT = Path.home() / f'Library/LaunchAgents/{LABEL}.plist'
LOG = Path.home() / 'Library/Logs/MacNativeGames/save-sync.log'
TOOLS = DATA / 'tools/save-sync'        # the LaunchAgent runs copies: it keeps working when the repository moves


class SyncError(RuntimeError):
    pass


# ---------- what a save folder holds ----------
def save_files(folder):
    """Relative paths of the files in a save folder; hidden files (.DS_Store, markers) are not part of a save."""
    folder = Path(folder)
    if not folder.is_dir():
        return []
    return sorted(str(p.relative_to(folder)) for p in folder.rglob('*')
                  if p.is_file() and not any(part.startswith('.') for part in p.relative_to(folder).parts))


def folder_hash(folder):
    """One hash over the names and contents of a save folder; None for a folder without files."""
    files = save_files(folder)
    if not files:
        return None
    h = hashlib.sha256()
    for rel in files:
        h.update(rel.encode() + b'\0' + hashlib.sha256((Path(folder) / rel).read_bytes()).digest())
    return h.hexdigest()


def uid_folder(raw):
    """save/<this> for an account.uid (two little-endian words lo, hi; the folder name is hi then lo, fs_host.c)."""
    if len(raw) != 16:
        return None
    lo, hi = struct.unpack('<QQ', raw)
    return f'{hi:016x}{lo:016x}' if lo or hi else None


def version_key(text):
    try:
        return tuple(int(x) for x in str(text).strip().split('.'))
    except ValueError:
        return None


def game_version(text):
    """A game version (1.0.0, 1.1.2, 1.2.1, 1.4.3), or None. iOS builds before the version was set say 1.0."""
    key = version_key(text)
    return '.'.join(map(str, key)) if key and len(key) == 3 else None


# ---------- where a profile lives ----------
class LocalContainer:
    """A folder on this Mac: the Mac profile, or a stand-in for a device's app container in tests and --device-dir."""
    def __init__(self, root, running=lambda: False):
        self.root, self._running = Path(root), running

    def listing(self, rel):
        """(relative path, size, mtime, is directory) of everything under rel, or None when it is not there."""
        base = self.root / rel
        if not base.is_dir():
            return None
        return [(str(p.relative_to(base)), p.stat().st_size, p.stat().st_mtime, p.is_dir()) for p in sorted(base.rglob('*'))]

    def pull(self, rel, dst):
        src = self.root / rel
        if src.is_dir():
            shutil.copytree(src, dst, dirs_exist_ok=True)
        elif src.is_file():
            Path(dst).parent.mkdir(parents=True, exist_ok=True); shutil.copy2(src, dst)
        else:
            return False
        return True

    def push_folder(self, src, rel):
        """Replaces the folder rel with src: the new copy is complete before the old one moves aside."""
        dst = self.root / rel
        new, old = dst.with_name(f'.{dst.name}.sync-new'), dst.with_name(f'.{dst.name}.sync-old')
        shutil.rmtree(new, ignore_errors=True); shutil.rmtree(old, ignore_errors=True)
        shutil.copytree(src, new)
        if dst.exists():
            dst.rename(old)
        new.rename(dst)
        shutil.rmtree(old, ignore_errors=True)

    def push_file(self, src, rel):
        dst = self.root / rel
        tmp = dst.with_name(dst.name + '.sync-new'); shutil.copy2(src, tmp); tmp.rename(dst)

    def running(self):
        return self._running()


class DeviceContainer:
    """The game's app data container on an iPhone / iPad, through devicectl (the Mac needs Xcode)."""
    def __init__(self, ident, bundle, app_url=None):
        self.ident, self.bundle, self.app_url = ident, bundle, app_url

    def _domain(self):
        return ['--device', self.ident, '--domain-type', 'appDataContainer', '--domain-identifier', self.bundle]

    def listing(self, rel):
        out = devicectl('device', 'info', 'files', *self._domain(), '--subdirectory', rel)
        if out is None:
            return None
        rows = []
        for f in out.get('result', {}).get('files', []):
            meta, res = f.get('metadata', {}), f.get('resources', {})
            when = meta.get('lastModDate', '').replace('Z', '+00:00')
            mtime = datetime.datetime.fromisoformat(when).timestamp() if when else 0.0
            rows.append((f.get('relativePath', f.get('name', '')), meta.get('size', 0), mtime, bool(res.get('isDirectory'))))
        return rows

    def pull(self, rel, dst):
        return devicectl('device', 'copy', 'from', *self._domain(), '--source', rel, '--destination', str(dst)) is not None

    def push_folder(self, src, rel):
        if devicectl('device', 'copy', 'to', *self._domain(), '--source', str(src), '--destination', rel,
                     '--remove-existing-content', 'true') is None:
            raise SyncError(f'devicectl could not copy the save to the device ({rel})')

    def push_file(self, src, rel):
        if devicectl('device', 'copy', 'to', *self._domain(), '--source', str(src), '--destination', rel) is None:
            raise SyncError(f'devicectl could not copy {rel} to the device')

    def running(self):
        """True / False from the device's process list; None when devicectl cannot tell."""
        out = devicectl('device', 'info', 'processes', '--device', self.ident)
        if out is None or not self.app_url:
            return None
        app = self.app_url.rstrip('/') + '/'
        return any(p.get('executable', '').startswith(app) for p in out.get('result', {}).get('runningProcesses', []))


def mac_game_running(slot=DATA / 'game-slot'):
    """The Mac game runs: a totk-native process, or the game slot's lock is held (runtime/game_slot.c)."""
    if subprocess.run(['pgrep', '-q', 'totk-native'], capture_output=True).returncode == 0:
        return True
    try:
        fd = os.open(slot / 'lock', os.O_RDONLY)
    except OSError:
        return False
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return False
    except OSError:
        return True
    finally:
        os.close(fd)


def mac_app_version(app=None):
    """CFBundleShortVersionString of the Mac app (build_app.py sets the game version there), or None."""
    candidates = [Path(app)] if app else []
    if not app:
        found = subprocess.run(['mdfind', f'kMDItemCFBundleIdentifier == "{MAC_BUNDLE_ID}"'], capture_output=True, text=True).stdout
        candidates += [Path('/Applications/Tears of the Kingdom.app'), Path.home() / 'Applications/Tears of the Kingdom.app']
        candidates += [Path(line) for line in found.splitlines() if line.endswith('.app')]
    for path in candidates:
        try:
            return game_version(plistlib.loads((path / 'Contents/Info.plist').read_bytes()).get('CFBundleShortVersionString'))
        except (OSError, plistlib.InvalidFileException):
            continue
    return None


# ---------- one side of a sync ----------
class Side:
    def __init__(self, name, container, prefix='', app_version=None):
        self.name, self.container, self.prefix, self.app_version = name, container, prefix, app_version

    def rel(self, *parts):
        return '/'.join(p for p in (self.prefix, *parts) if p)

    def read(self, work):
        """What the side holds, from a listing and two small files; nothing is changed."""
        work = Path(work) / self.name; work.mkdir(parents=True, exist_ok=True)
        self.uid = self.save_version = self.hash = None
        if self.container.pull(self.rel('account.uid'), work / 'account.uid') and (work / 'account.uid').is_file():
            self.uid = uid_folder((work / 'account.uid').read_bytes())
        if self.container.pull(self.rel('save/.version'), work / 'version') and (work / 'version').is_file():
            self.save_version = game_version((work / 'version').read_text(errors='replace'))
        rows = self.container.listing(self.rel('save')) or []
        busy = [m for rel, _, m, d in rows if rel == '.busy' and not d]
        self.busy_age = time.time() - busy[0] if busy else None
        mine = [(rel.split('/', 1)[1], size, m) for rel, size, m, d in rows
                if self.uid and rel.startswith(self.uid + '/') and not d and not any(p.startswith('.') for p in rel.split('/'))]
        self.fingerprint = hashlib.sha256(json.dumps(sorted([r, s, round(m, 3)] for r, s, m in mine)).encode()).hexdigest() if mine else None
        self.newest = max((m for _, _, m in mine), default=None)
        self.slots = sorted({r.split('/')[0] for r, _, _ in mine if r.startswith('slot_') and '/' in r})
        self.running = self.container.running()
        return self

    def busy(self):
        return self.busy_age is not None and self.busy_age < BUSY_STALE

    def version(self):
        """The version of the game this save goes with: the version that last saved it, else the installed app's."""
        return self.save_version or self.app_version

    def snapshot(self, dst, tries=3):
        """Copies the save folder twice; returns its hash once two copies agree (a save written meanwhile tears one)."""
        if not self.uid:
            return None
        for _ in range(tries):
            a, b = Path(f'{dst}.a'), Path(f'{dst}.b')
            for p in (a, b):
                shutil.rmtree(p, ignore_errors=True)
                if not self.container.pull(self.rel('save', self.uid), p):
                    p.mkdir(parents=True, exist_ok=True)   # no save folder yet
            ha, hb = folder_hash(a), folder_hash(b)
            if ha == hb:
                shutil.rmtree(b); shutil.rmtree(dst, ignore_errors=True); a.rename(dst)
                self.hash = ha
                return ha
            time.sleep(2)
        raise SyncError(f'the save on the {self.name} kept changing while it was copied; try again when the game is idle')

    def describe(self):
        hash_ = self.hash or self.fingerprint
        when = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(self.newest)) if self.newest else '-'
        busy = 'saving now' if self.busy() else ('stale marker ignored' if self.busy_age is not None else 'no')
        running = {True: 'yes', False: 'no', None: 'unknown'}[self.running]
        return (f'{self.name}: uid {self.uid or "- (start the game once)"}; slots {", ".join(self.slots) or "-"}; newest {when}; '
                f'{"hash" if self.hash else "files"} {hash_[:12] if hash_ else "-"}; save version '
                f'{self.save_version or "?"}; app version {self.app_version or "?"}; busy {busy}; game running {running}')


# ---------- policy ----------
def decide(last, mac, device, mac_time, device_time):
    """('push' | 'pull' | None, conflict) from the folder hashes now and at the last sync (None: never synced)."""
    if mac == device or (mac is None and device is None):
        return None, False
    if device is None:
        return 'push', False
    if mac is None:
        return 'pull', False
    mac_changed, device_changed = mac != last, device != last
    if mac_changed and not device_changed:
        return 'push', False
    if device_changed and not mac_changed:
        return 'pull', False
    return ('push' if (mac_time or 0) >= (device_time or 0) else 'pull'), True


def version_problem(source_version, target_version):
    """Why a save of source_version must not go to a game of target_version, or None."""
    if not source_version or not target_version:
        return (f'the game version is unknown on the {"source" if not source_version else "target"} side '
                f'(build the app again to record it, or give --force)')
    if version_key(source_version) > version_key(target_version):
        return f'the save is from {source_version} and the game it would go to is {target_version}: an older game cannot read it'
    return None


def load_manifest(store, udid):
    try:
        return json.loads((Path(store) / f'{udid}.json').read_text())
    except (OSError, ValueError):
        return {}


def save_manifest(store, udid, data):
    path = Path(store) / f'{udid}.json'; path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix('.tmp'); tmp.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n'); tmp.rename(path)


def keep_backup(store, src, name):
    backups = Path(store) / 'backups'; backups.mkdir(parents=True, exist_ok=True)
    dst = backups / name
    shutil.copytree(src, dst)
    return dst


def prune_backups(store, keep=KEEP_BACKUPS):
    """Keeps the newest `keep` routine backups; parked conflicts stay until the player deletes them."""
    backups = Path(store) / 'backups'
    if not backups.is_dir():
        return
    routine = sorted((p for p in backups.iterdir() if p.is_dir() and not p.name.startswith('save-conflict-')), key=lambda p: p.name)
    for old in routine[:-keep] if keep else routine:
        shutil.rmtree(old)


def sync(action, mac, device, udid, store=STORE, dry_run=False, force=False, notify=None, log=print):
    """status / pull / push / auto between two Sides; returns what was done ('push', 'pull' or None)."""
    stamp = time.strftime('%Y%m%d-%H%M%S')
    with tempfile.TemporaryDirectory(prefix='totk-save-sync-') as tmp:
        tmp = Path(tmp)
        for side in (mac, device):
            side.read(tmp)
        manifest = load_manifest(store, udid)
        if (action == 'auto' and manifest and mac.fingerprint == manifest.get('mac_fingerprint')
                and device.fingerprint == manifest.get('device_fingerprint')):
            log(f'in step: neither save changed since the last sync ({manifest.get("time")})')
            return None
        for side in (mac, device):
            if side.busy() and action != 'status':
                log(f'the game on the {side.name} is saving right now; not copying'); return None
        snaps = {side.name: tmp / f'{side.name}-save' for side in (mac, device)}
        for side in (mac, device):
            try:
                side.snapshot(snaps[side.name])
            except SyncError as e:
                if action != 'status': raise
                log(str(e))
        conflict = False
        if action in ('status', 'auto'):
            direction, conflict = decide(manifest.get('hash'), mac.hash, device.hash, mac.newest, device.newest)
        else:
            direction = action
        if action == 'status':
            log(mac.describe()); log(device.describe())
            log(f'last sync: {manifest.get("time", "never")}' + (f' ({manifest.get("direction")})' if manifest else ''))
            log({None: 'auto would do nothing: the saves are the same' if mac.hash == device.hash else 'auto would do nothing',
                 'push': 'auto would copy the Mac save to the device', 'pull': 'auto would copy the device save to the Mac'}[direction]
                + (' (both changed: the newer save wins, the other is kept as a conflict copy)' if conflict else ''))
            return None
        if direction is None:
            log('in step: the saves are the same' if mac.hash == device.hash else 'nothing to copy')
            if mac.hash == device.hash and not dry_run and mac.hash:
                save_manifest(store, udid, dict(manifest, hash=mac.hash, mac_fingerprint=mac.fingerprint,
                                                device_fingerprint=device.fingerprint, time=manifest.get('time') or stamp))
            return None
        source, target = (mac, device) if direction == 'push' else (device, mac)

        problems = []
        if source.hash is None:
            problems.append(f'the {source.name} has no save to copy')
        if not target.uid:
            problems.append(f'the {target.name} has no account yet: start the game there once')
        if mac.running:
            problems.append('the game is running on the Mac: close it first')
        if target is device and device.running:
            problems.append('the game is running on the device: close it first (swipe it away in the app switcher)')
        if target is device and device.running is None:
            if force: log('WARNING: cannot tell whether the game runs on the device; --force: copying anyway')
            else: problems.append('cannot tell whether the game runs on the device: close it there and give --force')
        version = version_problem(source.version(), target.app_version or target.save_version)
        if version and not (force and 'unknown' in version):
            problems.append(version)
        elif version:
            log(f'WARNING: {version}; --force: copying anyway')
        if problems:
            log(f'not copying the {source.name} save to the {target.name}: ' + '; '.join(problems))
            return None

        what = f'copy the {source.name} save ({source.hash[:12]}) to the {target.name}'
        if manifest.get('hash') and target.hash and target.hash not in (manifest['hash'], source.hash) and not conflict:
            what += f'; the {target.name} save changed since the last sync too: it is kept in the backups'
        if conflict:
            what += (f'; both saves changed since the last sync: the {source.name} save is newer, '
                     f'the {target.name} save is kept as save-conflict-{target.name}-{stamp}')
        if dry_run:
            log(f'dry run: would {what}'); return direction

        backup = None
        if target.hash:
            name = f'save-conflict-{target.name}-{stamp}' if conflict else f'{stamp}-{target.name}-{udid[:8]}'
            backup = keep_backup(store, snaps[target.name], name)
        target.container.push_folder(snaps[source.name], target.rel('save', target.uid))
        if source.version():
            (tmp / 'version').write_text(source.version() + '\n')
            target.container.push_file(tmp / 'version', target.rel('save/.version'))
        check = tmp / 'check'
        if target.snapshot(check) != source.hash:
            raise SyncError(f'the save on the {target.name} differs from the copy sent; the previous save is in {backup}')
        synced = source.hash; mac.read(tmp / 'after'); device.read(tmp / 'after')   # fingerprints of what both hold now
        mac.hash = device.hash = synced
        save_manifest(store, udid, dict(hash=synced, direction=direction, time=stamp, mac_uid=mac.uid, device_uid=device.uid,
                                        mac_version=mac.version(), device_version=device.version(),
                                        mac_fingerprint=mac.fingerprint, device_fingerprint=device.fingerprint))
        prune_backups(store)
        log(f'done: {what}' + (f'; the previous {target.name} save is in {backup}' if backup and not conflict else ''))
        if conflict:
            log(f'CONFLICT: the save on the {target.name} had changed too; it is in {backup}. To use it instead, copy its '
                f'contents back over the save folder.')
            if notify: notify(f'Both saves changed. The {source.name} save (newer) is now on both; the other is in {backup}.')
        return direction


# ---------- devices and the command line ----------
def find_devices(wanted=None):
    """(devicectl id, udid, name) of reachable devices, all of them or the one whose id, udid or name is `wanted`."""
    devices = reachable_devices(devicectl('list', 'devices') or {})
    if wanted:
        devices = [d for d in devices if wanted in d]
    return devices


def device_side(ident):
    apps = (devicectl('device', 'info', 'apps', '--device', ident) or {}).get('result', {}).get('apps', [])
    bundle = game_bundle({'result': {'apps': apps}})
    if not bundle:
        return None
    app = next(a for a in apps if a.get('bundleIdentifier') == bundle)
    return Side('device', DeviceContainer(ident, bundle, app.get('url')), DEVICE_PROFILE, game_version(app.get('version')))


def notify(text):
    quote = text.replace('\\', '\\\\').replace('"', '\\"')
    subprocess.run(['osascript', '-e', f'display notification "{quote}" with title "Tears of the Kingdom saves"'], capture_output=True)


def agent_plist(python, script, developer_dir):
    return {'Label': LABEL, 'ProgramArguments': [python, str(script), 'auto', '--notify'], 'StartInterval': SYNC_EVERY,
            'RunAtLoad': False, 'ProcessType': 'Background', 'EnvironmentVariables': {'DEVELOPER_DIR': developer_dir},
            'StandardOutPath': str(LOG), 'StandardErrorPath': str(LOG)}


def install(dry_run=False):
    from xcode_dir import developer_dir   # devicectl comes with the Xcode that built the iOS app
    python = '/usr/bin/python3' if Path('/usr/bin/python3').exists() else sys.executable   # not a work folder's venv, which may go
    plist = plistlib.dumps(agent_plist(python, TOOLS / 'save_sync.py', developer_dir()))
    if dry_run:
        return print(f'dry run: would copy save_sync.py and shader_contrib_watch.py to {TOOLS} and write {AGENT}:\n{plist.decode()}')
    TOOLS.mkdir(parents=True, exist_ok=True); LOG.parent.mkdir(parents=True, exist_ok=True); AGENT.parent.mkdir(parents=True, exist_ok=True)
    here = Path(__file__).resolve().parent
    for name in ('save_sync.py', 'shader_contrib_watch.py'):   # a copy of its own: the shader reminder's uninstall removes that one's
        shutil.copy2(here / name, TOOLS / name)
    AGENT.write_bytes(plist)
    domain = f'gui/{os.getuid()}'
    subprocess.run(['launchctl', 'bootout', f'{domain}/{LABEL}'], capture_output=True)
    subprocess.run(['launchctl', 'bootstrap', domain, str(AGENT)], check=True, capture_output=True)
    print(f'installed {AGENT}: syncs saves with connected iPhones / iPads every {SYNC_EVERY // 60} minutes (log: {LOG})')


def uninstall(dry_run=False):
    if dry_run:
        return print(f'dry run: would stop and remove {AGENT} and {TOOLS}')
    subprocess.run(['launchctl', 'bootout', f'gui/{os.getuid()}/{LABEL}'], capture_output=True)
    AGENT.unlink(missing_ok=True)
    shutil.rmtree(TOOLS, ignore_errors=True)
    print(f'removed {AGENT}')


def main(argv=None):
    ap = argparse.ArgumentParser(description='Sync Tears of the Kingdom saves between this Mac and an iPhone / iPad.')
    ap.add_argument('command', choices=['status', 'pull', 'push', 'auto', 'install', 'uninstall'])
    ap.add_argument('--device', help='device id, udid or name (default: every connected device with the game)')
    ap.add_argument('--dry-run', action='store_true', help='say what would be copied, change nothing')
    ap.add_argument('--force', action='store_true', help='copy even when the game version or whether the device game runs is unknown')
    ap.add_argument('--mac-profile', type=Path, default=MAC_PROFILE, help=argparse.SUPPRESS)
    ap.add_argument('--mac-app', type=Path, help='the Mac app, for its game version (default: found by Spotlight)')
    ap.add_argument('--store', type=Path, default=STORE, help=argparse.SUPPRESS)
    ap.add_argument('--device-dir', type=Path, help=argparse.SUPPRESS)   # tests: a folder standing in for a device's app container
    ap.add_argument('--mac-version', help=argparse.SUPPRESS)      # tests: the Mac app's game version
    ap.add_argument('--device-version', help=argparse.SUPPRESS)   # tests: the device app's game version
    ap.add_argument('--notify', action='store_true', help='show a notification on a conflict (the LaunchAgent)')
    args = ap.parse_args(argv)
    if args.command in ('install', 'uninstall'):
        return {'install': install, 'uninstall': uninstall}[args.command](args.dry_run)
    if args.device_dir:
        mac = Side('Mac', LocalContainer(args.mac_profile), app_version=game_version(args.mac_version))
        targets = [('fake', Side('device', LocalContainer(args.device_dir), DEVICE_PROFILE, game_version(args.device_version)))]
    else:
        mac = Side('Mac', LocalContainer(args.mac_profile, mac_game_running),
                   app_version=game_version(args.mac_version) or mac_app_version(args.mac_app))
        targets = []
        for ident, udid, name in find_devices(args.device):
            side = device_side(ident)
            if side: targets.append((udid, side)); side.label = name
        if not targets:
            msg = f'no connected iPhone / iPad with the game{f" matching {args.device}" if args.device else ""}'
            if args.command == 'auto': return print(msg) if args.device else None
            raise SyncError(msg)
        if args.command in ('pull', 'push') and len(targets) > 1:
            raise SyncError('several devices are connected: choose one with --device')
    args.store.mkdir(parents=True, exist_ok=True)
    with open(args.store / '.lock', 'w') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            raise SyncError('another save sync is running') from None
        for udid, side in targets:
            prefix = f'{time.strftime("%Y-%m-%d %H:%M:%S")} {getattr(side, "label", udid)}: '
            sync(args.command, mac, side, udid, args.store, args.dry_run, args.force,
                 notify if args.notify else None, log=lambda text: print(prefix + text, flush=True))


if __name__ == '__main__':
    try:
        main()
    except (SyncError, OSError, subprocess.CalledProcessError) as e:
        print(f'ERROR: {e}', file=sys.stderr); sys.exit(1)
