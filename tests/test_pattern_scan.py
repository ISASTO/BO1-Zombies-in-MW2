import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from pattern_scan import pattern_matches


class PatternTests(unittest.TestCase):
    def test_overlapping_matches(self):
        self.assertEqual([m.start() for m in pattern_matches(b'\xaa'*4,'AA AA')],[0,1,2])

    def test_wildcard_includes_newline_and_region_end(self):
        self.assertEqual([m.start() for m in pattern_matches(b'\x48\x0a\x48','48 ??')],[0])
        self.assertEqual([m.start() for m in pattern_matches(b'\x48\x0a\x48','?? 48')],[1])
        self.assertEqual(pattern_matches(b'\x48','48 48'),[])
        self.assertEqual(pattern_matches(b'\x48',''),[])

    def test_catalog_memset_overlap_regression(self):
        # Four adjacent equal-layout memset calls: the old two-call pattern
        # overlaps three times; adding the real function prologue makes one.
        block=bytes.fromhex('33 D2 48 8D 0D 00 00 00 00 41 B8 40 06 00 00 E8 00 00 00 00')
        old='33 D2 48 8D 0D ?? ?? ?? ?? 41 B8 40 06 00 00 E8 ?? ?? ?? ?? '
        old+=old+'33 D2'
        data=bytes.fromhex('48 83 EC 28')+block*4+bytes.fromhex('33 D2')
        self.assertEqual([m.start() for m in pattern_matches(data,old)],[4,24,44])
        self.assertEqual([m.start() for m in pattern_matches(data,'48 83 EC 28 '+old)],[0])


if __name__=='__main__': unittest.main()
