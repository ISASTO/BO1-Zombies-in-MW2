"""Invented table/image fixtures, with no original executable or asset data."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import verify_ai_bindings as verifier


class Fixture:
    def __init__(self):
        self.base, self.size = 0x700000000, 0x1000
        self.bytes = bytearray(self.size)
        self.next = 0x700

    def string(self, text):
        at = self.next
        data = text.encode('ascii') + b'\0'
        self.bytes[at:at+len(data)] = data
        self.next += len(data)
        return self.base + at

    def read(self, rva, size):
        return bytes(self.bytes[rva:rva+size])

    def sections(self):
        return [(0, bytes(self.bytes))]

    def entry(self, at, name, function):
        struct.pack_into('<QQQ', self.bytes, at, self.string(name), self.base + function, 0)

    def teams(self, at, names=verifier.TEAM_NAMES):
        struct.pack_into('<6Q', self.bytes, at, *(self.string(name) for name in names))

    def fields(self, at, classname=0x13e):
        for delta, name, offset, kind in ((0, 'classname', classname, 4), (32, 'origin', 0xe8, 6), (96, 'spawnflags', 0x14c, 0)):
            struct.pack_into('<QIIQQ', self.bytes, at + delta, self.string(name), offset, kind, 0, 0)


class AiBindingsTests(unittest.TestCase):
    def test_method_requires_both_native_name_and_function(self):
        f = Fixture(); f.entry(0x80, 'dospawn', 0x450); f.entry(0xa0, 'other', 0x450)
        self.assertEqual(verifier.method_entry(f.sections(), f.read, f.base, f.size, 'dospawn', 0x450), 0x80)
        with self.assertRaises(ValueError):
            verifier.method_entry(f.sections(), f.read, f.base, f.size, 'delete', 0x450)
        struct.pack_into('<Q', f.bytes, 0x80, f.base - 8)
        with self.assertRaises(ValueError):
            verifier.method_entry(f.sections(), f.read, f.base, f.size, 'dospawn', 0x450)

    def test_duplicate_binding_rejects_even_if_each_entry_looks_valid(self):
        f = Fixture(); f.entry(0x80, 'dospawn', 0x450); f.entry(0xa0, 'dospawn', 0x450)
        with self.assertRaisesRegex(ValueError, 'found 2'):
            verifier.method_entry(f.sections(), f.read, f.base, f.size, 'dospawn', 0x450)

    def test_team_order_cannot_be_inferred_from_shared_engine_ancestry(self):
        f = Fixture(); f.teams(0x100)
        self.assertEqual(verifier.team_table(f.sections(), f.read, f.base, f.size), 0x100)
        axis, allies = struct.unpack_from('<QQ', f.bytes, 0x108)
        struct.pack_into('<QQ', f.bytes, 0x108, allies, axis)
        with self.assertRaises(ValueError):
            verifier.team_table(f.sections(), f.read, f.base, f.size)

    def test_script_classname_is_distinct_from_internal_name(self):
        f = Fixture(); f.fields(0x200)
        self.assertEqual(verifier.entity_field_table(f.sections(), f.read, f.base, f.size, 0x13e, 0xe8, 0x14c), 0x200)
        with self.assertRaises(ValueError):
            verifier.entity_field_table(f.sections(), f.read, f.base, f.size, 0x13c, 0xe8, 0x14c)
        struct.pack_into('<I', f.bytes, 0x22c, 0)  # Origin descriptor type ceases to be a vector.
        with self.assertRaises(ValueError):
            verifier.entity_field_table(f.sections(), f.read, f.base, f.size, 0x13e, 0xe8, 0x14c)

    def test_independent_caller_reader_and_writer_graph(self):
        pairs = [('callback_slot_value', 'actor_think'), ('spawn_call_target', 'spawn_core'),
                 ('spawn_enabled_consumer', 'spawn_enabled_registration'), ('spawn_disabled_consumer', 'spawn_disabled_registration'),
                 ('entity_pool_reader', 'entity_pool_free_reader'), ('level_time', 'spawn_time_read'),
                 ('level_time', 'spawn_time_write'), ('level_time', 'actor_init_time'), ('think_ai_gate', 'world_ai_gate')]
        graph = {name: 0x100 for pair in pairs for name in pair}
        verifier.validate_graph(graph)
        for _, target in pairs:
            bad = dict(graph); bad[target] += 8
            with self.subTest(target=target), self.assertRaisesRegex(ValueError, 'disagrees'):
                verifier.validate_graph(bad)


if __name__ == '__main__':
    unittest.main()
