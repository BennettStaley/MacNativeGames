"""The spawner's actor list (games/totk/scripts/prepare_actor_list.py) on a synthetic RomFS: no game data."""
import importlib.util
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import compression.zstd as zstd

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'games/totk/scripts/prepare_actor_list.py'
spec = importlib.util.spec_from_file_location('prepare_actor_list', SCRIPT)
actor_list = importlib.util.module_from_spec(spec)
spec.loader.exec_module(actor_list)


def make_sarc(files):
    names, entries, data = b'', [], b''
    for name, blob in sorted(files.items()):
        entries.append((len(names) // 4, len(data), len(data) + len(blob)))
        names += name.encode() + b'\0'
        names += b'\0' * (-len(names) % 4)
        data += blob + b'\0' * (-len(blob) % 8)
    sfat = b'SFAT' + struct.pack('<HHI', 12, len(entries), 0x65)
    sfat += b''.join(struct.pack('<IIII', 0, 0x01000000 | off, s, e) for off, s, e in entries)
    sfnt = b'SFNT' + struct.pack('<HH', 8, 0) + names
    head = 0x14 + len(sfat) + len(sfnt)
    head += -head % 8
    body = (sfat + sfnt).ljust(head - 0x14, b'\0')
    return b'SARC' + struct.pack('<H', 0x14) + b'\xff\xfe' + struct.pack('<IIHH', head + len(data), head, 0x100, 0) + body + data


def make_msbt(messages):
    labels = list(messages)
    lbl = struct.pack('<I', 1) + struct.pack('<II', len(labels), 12)
    lbl += b''.join(bytes([len(k)]) + k.encode() + struct.pack('<I', i) for i, k in enumerate(labels))
    texts = [messages[k].encode('utf-16-le') + b'\0\0' for k in labels]
    offs, at = [], 4 + 4 * len(texts)
    for t in texts: offs.append(at); at += len(t)
    txt = struct.pack('<I', len(texts)) + b''.join(struct.pack('<I', o) for o in offs) + b''.join(texts)

    def section(magic, body):
        return magic + struct.pack('<I', len(body)) + b'\0' * 8 + body + b'\xab' * (-len(body) % 16)
    sections = section(b'LBL1', lbl) + section(b'TXT2', txt)
    return b'MsgStdBn\xff\xfe\0\0\0\x03' + struct.pack('<H', 2) + b'\0\0' + struct.pack('<I', 0x20 + len(sections)) + b'\0' * 10 + sections


def make_byml(rows):
    """An array of string-valued hashes (the RSDB tables)."""
    keys = sorted({k for r in rows for k in r})
    strings = sorted({v for r in rows for v in r.values()})

    def table(items):
        body = b''.join(s.encode() + b'\0' for s in items)
        offs, at = [], 4 + 4 * (len(items) + 1)
        for s in items: offs.append(at); at += len(s.encode()) + 1
        offs.append(at)
        out = struct.pack('<I', 0xc2 | len(items) << 8) + b''.join(struct.pack('<I', o) for o in offs) + body
        return out + b'\0' * (-len(out) % 4)
    head = 16
    key_table = table(keys); string_table = table(strings)
    keys_off, strings_off = head, head + len(key_table)
    array_off = strings_off + len(string_table)
    n = len(rows)
    array_size = 4 + ((n + 3) & ~3) + 4 * n
    hashes, at = [], array_off + array_size
    for r in rows:
        hashes.append((at, r)); at += 4 + 8 * len(r)
    array = struct.pack('<I', 0xc0 | n << 8) + bytes([0xc1] * n).ljust((n + 3) & ~3, b'\0')
    array += b''.join(struct.pack('<I', o) for o, _ in hashes)
    blobs = b''
    for _, r in hashes:
        blobs += struct.pack('<I', 0xc1 | len(r) << 8)
        for k in sorted(r):
            blobs += struct.pack('<I', keys.index(k) | 0xa0 << 24) + struct.pack('<I', strings.index(r[k]))
    return b'YB' + struct.pack('<HIII', 7, keys_off, strings_off, array_off) + key_table + string_table + array + blobs


ACTORS = ['Weapon_Sword_001', 'Item_Fruit_A', 'Enemy_Bokoblin_Junior', 'Enemy_Bokoblin_Middle', 'Enemy_Bokoblin_Guard_Junior',
          'SpObj_Cannon_A_01', 'SpObj_Cannon_Capsule_A_01', 'Npc_Test_001', 'Animal_Dog_A', 'Animal_Dog_B', 'Obj_TreeMaple_B_01',
          'Obj_Xyzzyq_A_01', 'SoundLoc_Test', 'Armor_001_Head', 'Armor_001_Upper']


class ActorListTest(unittest.TestCase):
    def build(self, extra_messages=None):
        tmp = Path(self.enterContext(tempfile.TemporaryDirectory()))
        messages = {'Weapon_Sword_001_Name': "Traveler's Sword", 'Item_Fruit_A_Name': 'Apple', 'Enemy_Bokoblin_Junior_Name': 'Bokoblin',
                    'SpObj_Cannon_A_01_Name': 'Cannon', 'Npc_Test_001_Name': 'Test Person', 'Animal_Dog_A_Name': 'Hylian Retriever',
                    'Armor_001_Head_Name': 'Hylian Hood', 'Other_Label': 'A tree of maple wood'}
        messages.update(extra_messages or {})
        files = {f'Pack/Actor/{a}.pack.zs': zstd.compress(b'pack') for a in ACTORS}
        files['Mals/USen.Product.100.sarc.zs'] = zstd.compress(make_sarc({'ActorMsg/PouchContent.msbt': make_msbt(messages)}))
        info = [{'__RowId': 'Weapon_Sword_001', 'Category': 'Weapon'}, {'__RowId': 'Item_Fruit_A', 'Category': 'ObjectPouchContent'},
                {'__RowId': 'Enemy_Bokoblin_Junior', 'Category': 'Enemy'}, {'__RowId': 'SpObj_Cannon_A_01', 'Category': 'ObjectGeneralActive'},
                {'__RowId': 'Npc_Test_001', 'Category': 'NPC'}, {'__RowId': 'Obj_TreeMaple_B_01', 'Category': 'MapStatic'},
                {'__RowId': 'SoundLoc_Test', 'Category': 'SoundLocater'}, {'__RowId': 'Armor_001_Head', 'Category': 'Armor'},
                {'__RowId': 'Armor_001_Upper', 'Category': 'Armor'}]
        pouch = [{'__RowId': 'Weapon_Sword_001', 'PouchCategory': 'Weapon'}, {'__RowId': 'Item_Fruit_A', 'PouchCategory': 'Food'},
                 {'__RowId': 'SpObj_Cannon_Capsule_A_01', 'PouchCategory': 'SpecialParts'},
                 {'__RowId': 'Armor_001_Head', 'PouchCategory': 'Armor'}, {'__RowId': 'Armor_001_Upper', 'PouchCategory': 'Armor'}]
        files['RSDB/ActorInfo.Product.100.rstbl.byml.zs'] = zstd.compress(make_byml(info))
        files['RSDB/PouchActorInfo.Product.100.rstbl.byml.zs'] = zstd.compress(make_byml(pouch))
        folder = tmp / 'extracted/v143/romfs'; folder.mkdir(parents=True)
        index, image = [], b''
        for path, blob in sorted(files.items()):
            index.append({'path': path, 'offset': len(image), 'size': len(blob)}); image += blob
        (folder / 'romfs.bin').write_bytes(image)
        (folder / 'index.json').write_text(json.dumps({'status': 'pass', 'files': index}))
        out = tmp / 'actor_list.tsv'
        run = subprocess.run([sys.executable, SCRIPT, '--root', tmp, '--out', out], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        rows = {line.split('\t')[0]: line.split('\t')[1:] for line in out.read_text().splitlines() if not line.startswith('#')}
        return rows, out.read_text()

    def test_categories_names_and_sources(self):
        rows, text = self.build()
        self.assertEqual(set(rows), set(ACTORS))
        self.assertEqual(rows['Weapon_Sword_001'], ['weapons', 'pickup', "Traveler's Sword", 'game'])
        self.assertEqual(rows['Item_Fruit_A'], ['items', 'pickup', 'Apple', 'game'])
        self.assertEqual(rows['Enemy_Bokoblin_Junior'], ['creatures', 'world', 'Bokoblin', 'game'])
        self.assertEqual(rows['Enemy_Bokoblin_Middle'], ['creatures', 'world', 'Blue Bokoblin', 'deciphered'])
        self.assertEqual(rows['Enemy_Bokoblin_Guard_Junior'], ['creatures', 'world', 'Bokoblin (Guard)', 'deciphered'])
        self.assertEqual(rows['SpObj_Cannon_Capsule_A_01'], ['zonai', 'pickup', 'Cannon Capsule', 'deciphered'])
        self.assertEqual(rows['SpObj_Cannon_A_01'][:2], ['zonai', 'world'])
        self.assertEqual(rows['Npc_Test_001'][0], 'npcs')
        self.assertEqual(rows['Animal_Dog_B'], ['creatures', 'world', 'Hylian Retriever (B)', 'deciphered'])
        self.assertEqual(rows['Armor_001_Upper'], ['armor', 'pickup', 'Hylian Hood set (body)', 'deciphered'])
        self.assertEqual(rows['Obj_TreeMaple_B_01'], ['objects', 'world', 'Tree Maple (B 01)', 'deciphered'])   # words the game uses
        self.assertEqual(rows['Obj_Xyzzyq_A_01'], ['objects', 'world', 'Xyzzyq (A 01)', 'internal'])
        self.assertEqual(rows['SoundLoc_Test'][0], 'system')
        self.assertIn('# total: 15 (game 7, lookup 0, deciphered 6, internal 2)', text)

    def test_text_tags_removed(self):
        rows, _ = self.build({'Item_Fruit_A_Name': 'Ap\x0e\0\0\0ple'})   # a tag: 0x0e, group, type, size 0
        self.assertEqual(rows['Item_Fruit_A'][2], 'Apple')

    def test_readable(self):
        self.assertEqual(actor_list.readable('FldObj_TreeApple_A_01')[0], 'Tree Apple (A 01)')
        self.assertEqual(actor_list.readable('Enemy_Mogurudo_Junior')[0], 'Frox Junior')


if __name__ == '__main__':
    unittest.main()
