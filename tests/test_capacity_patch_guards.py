import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
from prepare_expanded_runtime import validate_control_entry
from capstone import Cs,CS_ARCH_X86,CS_MODE_64

class CapacityGuardTests(unittest.TestCase):
    def test_rex_prefix_changes_compared_register(self):
        decoder=Cs(CS_ARCH_X86,CS_MODE_64)
        code=bytes.fromhex('49 83 f8 20 7c dc')
        full=next(decoder.disasm(code,0x1286fe))
        partial=next(decoder.disasm(code[1:],0x1286ff))
        self.assertEqual(full.op_str,'r8, 0x20')
        self.assertEqual(partial.op_str,'eax, 0x20')
        with self.assertRaisesRegex(ValueError,'inside instruction'):
            validate_control_entry('complete lifecycle capacity guard',partial.address,full.address)

    def test_complete_guard_retains_prefix(self):
        validate_control_entry('complete lifecycle capacity guard',0x1286fe,0x1286fe)
        validate_control_entry('expanded capacity bound',0x1286fe,0x1286fe)
        with self.assertRaises(ValueError):
            validate_control_entry('expanded capacity bound',0x1286ff,0x1286fe)

    def test_scalar_field_edits_can_be_inside_an_instruction(self):
        validate_control_entry('full actor traversal stride',0x1286fb,0x1286f8)

if __name__=='__main__':unittest.main()
