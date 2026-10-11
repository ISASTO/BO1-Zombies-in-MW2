import sys
import struct
import unittest
import zlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from extract_bo1_scripts import carve,bounded_inflate


class ExtractorTests(unittest.TestCase):
    def fixture(self,name='maps/test.gsc',text=b'main() { return; }\0'):
        compressed=zlib.compress(text)
        return name.encode()+b'\0'+struct.pack('<II',len(text),len(compressed))+compressed

    def test_validated_inline_source(self):
        result=carve(b'\x00'*12+self.fixture())
        self.assertEqual(len(result),1)
        self.assertEqual(result[0][:2],('maps/test.gsc',b'main() { return; }'))

    def test_truncated_and_invalid_records_do_not_extract(self):
        self.assertEqual(carve(self.fixture()[:-5]),[])
        raw=bytearray(self.fixture());struct.pack_into('<I',raw,len('maps/test.gsc')+1,12345)
        self.assertEqual(carve(raw),[])

    def test_path_traversal_is_rejected(self):
        self.assertEqual(carve(self.fixture('../escape.gsc')),[])

    def test_expansion_limit(self):
        with self.assertRaises(ValueError):bounded_inflate(zlib.compress(b'A'*1000),32)


if __name__=='__main__':unittest.main()
