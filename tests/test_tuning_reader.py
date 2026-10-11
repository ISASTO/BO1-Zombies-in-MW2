"""Synthetic zone records only; no game source/assets are used in fixtures."""
import importlib.util
import struct
import sys
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('tuning_reader', Path(__file__).resolve().parents[1]/'tools/recover_bo1_tuning.py')
reader = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = reader
spec.loader.exec_module(reader)


def fixture(cells):
    name = b'test/table.csv\0'
    values, strings = [], bytearray()
    for cell in cells:
        if isinstance(cell, bytes):
            values.append((reader.INLINE, reader.string_hash(cell)))
            strings += cell+b'\0'
        else:
            values.append(cell)
    raw = (struct.pack('<5I',reader.INLINE,len(cells),1,reader.INLINE,reader.INLINE)+name
           +b''.join(struct.pack('<II',*c) for c in values)+strings
           +struct.pack('<'+'h'*len(cells),*range(len(cells))))
    return raw,20,20+len(name)


class TuningReaderTests(unittest.TestCase):
    def test_inline_and_exact_packed_reference(self):
        pointer=0x80001235
        raw,start,end=fixture([b'90',(pointer,reader.string_hash(b'90'))])
        table=reader.read_table(raw,start,end)
        cache,mappings=reader.build_string_cache([table])
        self.assertEqual(reader.resolve_cell(table,0,cache)[0],'90')
        self.assertEqual(reader.resolve_cell(table,1,cache)[0],'90')
        self.assertEqual(cache[pointer]['raw_offset'],table.inline[0][1])
        self.assertEqual(mappings[0]['alias_values'],1)

    def test_bad_inline_hash_is_rejected(self):
        raw,start,end=fixture([b'90'])
        bad=bytearray(raw);struct.pack_into('<I',bad,end+4,0)
        with self.assertRaisesRegex(ValueError,'hash'):
            reader.read_table(bad,start,end)

    def test_unresolved_pointer_is_not_guessed_by_hash(self):
        raw,start,end=fixture([b'90',(0x80001235,reader.string_hash(b'90'))])
        table=reader.read_table(raw,start,end);cache,_=reader.build_string_cache([table])
        table.cells[1]=(0x80009999,reader.string_hash(b'90'))
        with self.assertRaisesRegex(ValueError,'Unresolved'):
            reader.resolve_cell(table,1,cache)
        table.cells[1]=(0x80001235,reader.string_hash(b'91'))
        with self.assertRaisesRegex(ValueError,'hash'):
            reader.resolve_cell(table,1,cache)

    def test_ambiguous_alias_bases_and_conflicts_reject(self):
        raw,start,end=fixture([b'alpha',b'beta',(0x80001001,reader.string_hash(b'alpha')),
                              (0x80007001,reader.string_hash(b'beta'))])
        table=reader.read_table(raw,start,end)
        with self.assertRaisesRegex(ValueError,'unique'):
            reader.alias_base(table)
        a,st,en=fixture([b'alpha',(0x80001001,reader.string_hash(b'alpha'))])
        b,st2,en2=fixture([b'beta',(0x80001001,reader.string_hash(b'beta'))])
        with self.assertRaisesRegex(ValueError,'Conflicting'):
            reader.build_string_cache([reader.read_table(a,st,en),reader.read_table(b,st2,en2)])

    def test_truncation_and_bad_index_permutation_reject(self):
        raw,start,end=fixture([b'alpha',b'beta'])
        for truncated in (raw[:end+2],raw[:end+16+2],raw[:-1]):
            with self.subTest(size=len(truncated)),self.assertRaises(ValueError):
                reader.read_table(truncated,start,end)
        bad=raw[:-4]+struct.pack('<hh',0,0)
        with self.assertRaisesRegex(ValueError,'permutation'):
            reader.read_table(bad,start,end)


if __name__=='__main__':
    unittest.main()
