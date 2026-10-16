import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
import prepare_script_vm as vm

class ScriptVmLayoutTests(unittest.TestCase):
    def test_pools_keep_stock_positions_and_16_bit_ids(self):
        self.assertEqual(vm.OLD_CHILD_BASE,0x9002)
        self.assertEqual(vm.OLD_ENTRIES,vm.OLD_CHILD_BASE+2*vm.POOL_STRIDE)
        self.assertLessEqual(vm.PARENT_END,0xffff)            # parent ids are 16-bit, 0xffff never used
        self.assertGreater(vm.CHILD_BASE,vm.PARENT_END)
        self.assertGreaterEqual(vm.MAP_BYTES,vm.PARENT_END*2)
        self.assertLess(vm.POOL_STRIDE,0x10000)              # relative child ids are 16-bit
        self.assertEqual(vm.POOLS&(vm.POOLS-1),0)            # selectors use and reg,POOLS-1
        self.assertEqual(vm.ENTRIES,vm.CHILD_BASE+vm.POOLS*vm.POOL_STRIDE)

    def test_region_layout_is_disjoint_and_guarded(self):
        spans=[(vm.REGION,vm.REGION+vm.CODE_BYTES),(vm.UNWIND,vm.UNWIND+0x28*len(vm.RELAYS)),
               (vm.COUNTS,vm.COUNTS+4*vm.POOLS),(vm.SAVE_CELLS,vm.SAVE_CELLS+16),(vm.MT_CELLS,vm.MT_CELLS+40),
               (vm.MAPS[0],vm.MAPS[0]+vm.MAP_BYTES),(vm.MAPS[1],vm.MAPS[1]+vm.MAP_BYTES),
               (vm.LIST,vm.LIST+vm.LIST_BYTES),(vm.LIST_GUARD,vm.LIST_GUARD+0x1000),
               (vm.DOBJ,vm.DOBJ+vm.DOBJ_RECORDS*vm.DOBJ_STRIDE),(vm.DOBJ_GUARD,vm.DOBJ_GUARD+0x1000),
               (vm.ENTH,vm.ENTH+vm.ENTH_RECORDS*vm.ENTH_STRIDE),(vm.ENTH_GUARD,vm.ENTH_GUARD+0x1000)]
        spans.sort()
        for (a,b),(c,d) in zip(spans,spans[1:]):self.assertLessEqual(b,c,f'{a:x}-{b:x} overlaps {c:x}')
        self.assertLessEqual(vm.UNWIND+0x28*len(vm.RELAYS),vm.REGION+0x1000)   # unwind stays in the RX page
        self.assertEqual(vm.COUNTS&0xfff,0)                                    # data starts on its own RW page
        self.assertEqual(vm.LIST_GUARD&0xfff,0);self.assertEqual(vm.DOBJ_GUARD&0xfff,0)
        self.assertEqual(vm.ENTH_GUARD&0xfff,0)
        self.assertEqual(vm.REGION+vm.REGION_BYTES,vm.ENTH_GUARD+0x1000)
        self.assertLess(vm.REGION_BYTES,0x10000000)

    def test_relay_blob_assembles_to_its_layout(self):
        blob=vm.build_relays()
        self.assertEqual(len(blob),vm.CODE_BYTES)
        for begin,end,_ in vm.RELAYS:self.assertNotEqual(blob[begin-vm.REGION],0xcc)

    def test_object_key_and_int_key_names_are_disjoint(self):
        # Object keys are 0x10000+id for every parent id; int keys are
        # (index+0x800000) mod 2^24 for every index the VM accepts.
        highest_id=vm.PARENT_END-2
        self.assertGreater(vm.OBJECT_NAME_END,0x10000+highest_id)
        self.assertEqual(vm.OLD_OBJECT_NAME_END,0x19000)
        lowest_index=-(0x800000-vm.OBJECT_NAME_END)
        self.assertEqual((lowest_index+0x800000)&0xffffff,vm.OBJECT_NAME_END)
        self.assertEqual((0x7fffff+0x800000)&0xffffff,0xffffff)
        self.assertEqual(len(vm.OBJECT_NAME_SITES),6);self.assertEqual(len(vm.ARRAY_INDEX_SITES),4)

    def test_saved_object_numbers_are_unsigned(self):
        at,before,after=vm.SAVE_ID_READ
        self.assertEqual(before[:4],'0fbf');self.assertEqual(after[:4],'0fb7');self.assertEqual(before[4:],after[4:])

    def test_entity_handle_indices_stay_16_bit(self):
        self.assertLessEqual(vm.ENTH_RECORDS,0x10000)
        self.assertGreaterEqual(vm.ENTH_RECORDS,8*vm.ENTH_STOCK)

    def test_capacity_raises(self):
        self.assertGreaterEqual(vm.SAVE_BYTES,32*vm.SAVE_STOCK)
        self.assertGreaterEqual(vm.DOBJ_RECORDS,8*vm.DOBJ_STOCK)

if __name__=='__main__':unittest.main()
