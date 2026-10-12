import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audit_actor_capacity import audit,proposal


class CapacityProposalTests(unittest.TestCase):
    def test_256_has_perception_and_snapshot_hazards(self):
        result=proposal(256)
        self.assertEqual(result['requested_sentients'],274)
        self.assertEqual(result['actor_storage_bytes'],0x156000)
        self.assertEqual(result['native_perception_end'],'0xca8')
        self.assertEqual(result['requested_perception_end'],'0x36a8')
        self.assertGreater(int(result['requested_perception_end'],16),0x1560)
        self.assertEqual(result['highest_combined_actor_corpse_index'],319)
        self.assertEqual(result['combined_index_bits_required'],9)
        self.assertEqual(result['native_save_format_counts'],{'actors':32,'sentients':50})

    def test_native_layout_and_512_are_distinct(self):
        native=proposal(32)
        self.assertFalse(native['first_extra_perception_record_overwrites_actor_tail'])
        self.assertEqual(native['expanded_inline_actor_stride'],'0x1560')
        larger=proposal(512)
        self.assertEqual(larger['requested_sentients'],530)
        self.assertEqual(larger['combined_index_bits_required'],10)

    def test_bad_capacity_cannot_propose_mutations(self):
        for value in (-1,0,31,513,1000000):
            with self.assertRaises(ValueError):proposal(value)

    def test_unsupported_binary_fails_before_decode_or_write(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'unsupported.exe';original=b'unrelated executable'
            path.write_bytes(original)
            with self.assertRaisesRegex(ValueError,'SHA256'):audit(path)
            self.assertEqual(path.read_bytes(),original)
            self.assertEqual(list(Path(directory).iterdir()),[path])


if __name__=='__main__':unittest.main()
