import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
import prepare_script_vm as vm

class ScriptVmLayoutTests(unittest.TestCase):
    def test_pools_keep_stock_positions_and_16_bit_ids(self):
        self.assertEqual(vm.CHILD_BASE,0x9002)
        self.assertEqual(vm.OLD_ENTRIES,vm.CHILD_BASE+2*vm.POOL_STRIDE)
        self.assertLess(vm.POOL_STRIDE,0x10000)              # relative child ids are 16-bit
        self.assertEqual(vm.POOLS&(vm.POOLS-1),0)            # selectors use and reg,POOLS-1
        self.assertEqual(vm.ENTRIES,vm.CHILD_BASE+vm.POOLS*vm.POOL_STRIDE)

    def test_region_layout_is_disjoint_and_guarded(self):
        spans=[(vm.REGION,vm.REGION+vm.CODE_BYTES),(vm.UNWIND,vm.UNWIND+0x28*len(vm.RELAYS)),
               (vm.COUNTS,vm.COUNTS+4*vm.POOLS),(vm.SAVE_CELLS,vm.SAVE_CELLS+16),(vm.MT_CELLS,vm.MT_CELLS+40),
               (vm.LIST,vm.LIST+vm.LIST_BYTES),(vm.LIST_GUARD,vm.LIST_GUARD+0x1000),
               (vm.DOBJ,vm.DOBJ+vm.DOBJ_RECORDS*vm.DOBJ_STRIDE),(vm.DOBJ_GUARD,vm.DOBJ_GUARD+0x1000)]
        spans.sort()
        for (a,b),(c,d) in zip(spans,spans[1:]):self.assertLessEqual(b,c,f'{a:x}-{b:x} overlaps {c:x}')
        self.assertLessEqual(vm.UNWIND+0x28*len(vm.RELAYS),vm.REGION+0x1000)   # unwind stays in the RX page
        self.assertEqual(vm.COUNTS&0xfff,0)                                    # data starts on its own RW page
        self.assertEqual(vm.LIST_GUARD&0xfff,0);self.assertEqual(vm.DOBJ_GUARD&0xfff,0)
        self.assertEqual(vm.REGION+vm.REGION_BYTES,vm.DOBJ_GUARD+0x1000)
        self.assertLess(vm.REGION_BYTES,0x10000000)

    def test_relay_blob_assembles_to_its_layout(self):
        blob=vm.build_relays()
        self.assertEqual(len(blob),vm.CODE_BYTES)
        for begin,end,_ in vm.RELAYS:self.assertNotEqual(blob[begin-vm.REGION],0xcc)

    def test_capacity_raises(self):
        self.assertGreaterEqual(vm.SAVE_BYTES,32*vm.SAVE_STOCK)
        self.assertGreaterEqual(vm.DOBJ_RECORDS,8*vm.DOBJ_STOCK)

if __name__=='__main__':unittest.main()
