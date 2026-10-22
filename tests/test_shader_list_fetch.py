"""The shared shader list as the Mac app downloads and prepares it (third_party/d2r_gfx/gfx/nvn/totk_shader_list.{h,m},
totk_shader_scan.h): the parser on malformed input, the scanner on synthetic game files, the difference with a cache, the
preparation with the game's side stubbed, and the fetch against a local HTTP server (200, the once-a-day check, 304, a
malformed download kept out, addresses it refuses). Nothing here touches the network or the player's folders."""
import functools
import http.server
import json
import os
import shutil
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

TOTK = Path(__file__).resolve().parents[1] / 'games/totk'
NVN = TOTK / 'third_party/d2r_gfx/gfx/nvn'
HEADER = '# TOTK shader list v1 (test)\n'
KEY = 'ab' * 32
ROW = f'V\t{KEY}\t{{"attr":{{}},"fout":{{"0":"float"}},"img":{{}},"tex":{{"9":"2d"}}}}\n'


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    requests = []

    def log_message(self, *args):
        pass

    def do_GET(self):
        QuietHandler.requests.append((self.path, self.headers.get('If-Modified-Since'), self.headers.get('If-None-Match')))
        super().do_GET()


@unittest.skipUnless(shutil.which('xcrun'), 'needs the Xcode command line tools')
class SharedListTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp())
        cls.probe = cls.tmp / 'probe'
        build = subprocess.run(['xcrun', 'clang', '-fobjc-arc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', f'-I{NVN}',
                                str(TOTK / 'tests/shader_list_fetch/probe.m'), str(NVN / 'totk_shader_list.m'),
                                '-framework', 'Foundation', '-o', str(cls.probe)], capture_output=True, text=True)
        if build.returncode:
            raise unittest.SkipTest(f'clang could not build the probe: {build.stderr[-400:]}')

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_parser_scanner_diff_and_preparation(self):
        run = subprocess.run([str(self.probe), 'units', str(TOTK / 'assets/shader_list.tsv'), str(self.tmp / 'units')],
                             capture_output=True, text=True, timeout=120)
        self.assertEqual(run.returncode, 0, run.stderr[-2000:])
        result = json.loads(run.stdout)
        self.assertEqual(result['status'], 'pass')
        self.assertGreater(result['checked'], 40)

    def fetch(self, directory, url, force=False):
        env = dict(os.environ, TOTK_SHADER_LIST_FORCE='1' if force else '0')
        run = subprocess.run([str(self.probe), 'fetch', str(directory), url], capture_output=True, text=True, env=env, timeout=60)
        self.assertEqual(run.returncode, 0, run.stderr)
        return json.loads(run.stdout), run.stderr

    def test_fetch_once_a_day_with_304_and_rejects(self):
        served = self.tmp / 'www'; served.mkdir(exist_ok=True)
        (served / 'shader_list.tsv').write_text(HEADER + ROW)
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(QuietHandler, directory=str(served)))
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            url = f'http://127.0.0.1:{server.server_address[1]}/shader_list.tsv'
            listdir = self.tmp / 'list'
            first, log = self.fetch(listdir, url)
            self.assertTrue(first['path'] and Path(first['path']).read_text() == HEADER + ROW, log)
            self.assertIn('downloaded the shared list', log)
            self.assertTrue(first['state']['last_modified'])
            QuietHandler.requests.clear()
            again, log = self.fetch(listdir, url)
            self.assertEqual(QuietHandler.requests, [], 'within a day: no request at all')
            self.assertIn('checked 0.0 h ago', log)
            forced, log = self.fetch(listdir, url, force=True)
            self.assertIn('HTTP 304', log)
            self.assertTrue(QuietHandler.requests[-1][1], 'a conditional request (If-Modified-Since)')
            # a malformed list on the server: rejected, the previous one stays
            time.sleep(1.1)
            (served / 'shader_list.tsv').write_text(HEADER + ROW + 'X\tnot a row\n')
            bad, log = self.fetch(listdir, url, force=True)
            self.assertIn('rejected the downloaded list', log)
            self.assertEqual(Path(bad['path']).read_text(), HEADER + ROW)
            self.assertEqual(bad['state']['sha256'], first['state']['sha256'])
            # a good new list replaces it
            time.sleep(1.1)
            (served / 'shader_list.tsv').write_text(HEADER + ROW + ROW.replace(KEY, 'cd' * 32))
            new, log = self.fetch(listdir, url, force=True)
            self.assertIn('2 variants over 2 shaders', log)
            # offline (nothing listening): the previous list is used
            server.shutdown(); server.server_close()
            off, log = self.fetch(listdir, url, force=True)
            self.assertIn('download failed', log)
            self.assertEqual(off['path'], new['path'])
        finally:
            server.server_close()

    def test_addresses(self):
        local = self.tmp / 'local.tsv'; local.write_text(HEADER + ROW)
        d = self.tmp / 'file-list'
        first, log = self.fetch(d, local.as_uri())
        self.assertTrue(first['path'], log)
        same, log = self.fetch(d, local.as_uri(), force=True)
        self.assertIn('not changed', log)
        refused, log = self.fetch(self.tmp / 'refused', 'http://example.com/shader_list.tsv', force=True)
        self.assertIsNone(refused['path'])
        self.assertIn('https only', log)
        self.assertFalse((self.tmp / 'refused' / 'shader_list.tsv').exists())


if __name__ == '__main__':
    unittest.main()
