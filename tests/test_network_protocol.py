"""Opt-in original-library check of the complete independent framing table."""
import importlib.util
import os
import re
import unittest
from types import SimpleNamespace
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class NativeProtocolTable(unittest.TestCase):
    def test_request_candidates_do_not_promote_dynamic_selectors(self):
        spec = importlib.util.spec_from_file_location("network_protocol", ROOT / "tools/analysis/network_protocol.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        def ins(mnemonic, operands):
            return SimpleNamespace(mnemonic=mnemonic, op_str=operands, address=123)
        read = lambda address, count: b'f|%d|u|%s|\0'
        template = ins('lea', 'eax, [ebx - 0x20]')
        for argument, expected in [('11',11),('eax',None)]:
            rows=module.request_candidates([ins('mov','dword ptr [esp + 8], 4096'),ins('call','0x1'),ins('mov','dword ptr [esp + 8], '+argument),template,ins('call','0x2')],read,0x100)
            self.assertEqual(rows[0].get('function_candidate'),expected)
            self.assertEqual(rows[0]['selector'],'f')
    @unittest.skipUnless(os.getenv("NOVA_REAL_TESTS") == "1" and
                         (ROOT / "local/input/lib/x86/libNOVA3_neon.so").is_file(),
                         "Original library check is opt-in")
    def test_all_wire_sizes_match_both_original_tables(self):
        spec = importlib.util.spec_from_file_location("network_protocol", ROOT / "tools/analysis/network_protocol.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        evidence = module.inventory(ROOT / "local/input/lib/x86/libNOVA3_neon.so")
        source = (ROOT / "server/internal/protocol/game/framing.go").read_text()
        body = source.split("var recordSizes = [...]int{", 1)[1].split("}", 1)[0]
        actual = ["variable" if token == "variableRecord" else int(token)
                  for token in re.findall(r"variableRecord|\d+", body)]
        self.assertEqual(actual, [record["size"] for record in evidence["game_records"]])
        self.assertEqual(len(actual), 165)
        self.assertEqual(len(evidence['lobby_dispatch']),149)
        self.assertEqual(evidence['lobby_dispatch'][46]['handler'],'0xb7315d')
        requests={row['symbol']:row['requests'] for row in evidence['http_request_candidates']}
        register=requests['_ZN14GLXPlayerLogin12sendRegisterEPcS0_S0_S0_bS0_bS0_b'][0]
        self.assertEqual(register['function_candidate'],11)
        self.assertIn('|p|%s|e|%s|',register['template'])


if __name__ == "__main__":
    unittest.main()
