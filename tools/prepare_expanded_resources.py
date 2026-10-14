"""Coordinate skeleton storage budgets and guard the returned glass fault.

The two skeleton buffers are independently allocated with inaccessible end
pages. Native frame resets load their pointers through the checked near arena.
Glass occupancy bits, geometry limits, transforms and native allocation/free
code remain authoritative. The guard repairs only inactive free-list links.
"""
import struct, subprocess, tempfile
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

POINTERS=0x47e0000
CLIENT_BYTES=0x900000
SERVER_BYTES=0x800000
ALLOC_GUARD=0x4611000
FREE_GUARD=0x4611400


def prepare(pe,image):
    decoder=Cs(CS_ARCH_X86,CS_MODE_64); decoder.detail=True
    edits=[]
    def put(at,blob,role,data=False):
        image[at:at+len(blob)]=blob
        edits.append({'rva':hex(at),'bytes':len(blob),'role':role,'data':data})
    def ins(at):return next(decoder.disasm(pe.get_data(at,16),at))
    def immediate(at,old,new):
        i=ins(at)
        if i.mnemonic!='cmp' or i.imm_size!=4 or i.operands[-1].imm!=old:
            raise ValueError(f'Native skeleton allocation bound differs at {at:x}')
        b=bytearray(i.bytes);struct.pack_into('<I',b,i.imm_offset,new)
        put(at,b,'expanded skeleton storage and matching allocation bound')
    # Keep native generation/reset/allocation semantics. Replace the static
    # array addresses, never just the bounds on the old arrays.
    for at,register,target in ((0xd4a27,'rax',POINTERS),
                              (0x230efa,'r8',POINTERS+8),
                              (0x230f9d,'rax',POINTERS+8),
                              (0x2317d8,'rax',POINTERS+8),
                              (0x231911,'rax',POINTERS+8)):
        i=ins(at)
        if i.mnemonic!='lea' or i.reg_name(i.operands[0].reg)!=register or i.size!=7:
            raise ValueError(f'Native skeleton frame base differs at {at:x}')
        b=bytearray(i.bytes);b[1]=0x8b
        struct.pack_into('<i',b,i.disp_offset,target-at-i.size)
        put(at,b,'owned guarded skeleton frame storage pointer')
    for at in (0xd56e3,0xd575d):immediate(at,0x8fff0,CLIENT_BYTES-16)
    for at in (0x230eed,0x230f32):immediate(at,0x7fff0,SERVER_BYTES-16)
    with tempfile.TemporaryDirectory(prefix='bo1mw2-resource-guards-') as name:
        root=Path(name);obj=root/'guards.o';binary=root/'guards.bin'
        subprocess.run(['as','--64',str(Path(__file__).with_name('expanded_glass_guard.S')),'-o',str(obj)],check=True,capture_output=True)
        subprocess.run(['ld','-Ttext',hex(ALLOC_GUARD),'--oformat=binary','-o',str(binary),str(obj)],check=True,capture_output=True)
        blob=binary.read_bytes()
    if len(blob)!=0x510:raise ValueError('Glass guard layout differs')
    for at,before,target in ((0x100439,bytes.fromhex('81fbffff00000f8480000000'),ALLOC_GUARD),
                             (0x100edf,bytes.fromhex('8b4120488bd9'),FREE_GUARD)):
        if pe.get_data(at,len(before))!=before:raise ValueError(f'Glass guard entry differs at {at:x}')
        put(at,b'\xe9'+struct.pack('<i',target-at-5)+b'\x90'*(len(before)-5),'validated native glass occupancy/free-list guard entry')
    put(ALLOC_GUARD,blob[:0x300],'native glass allocation guard and bounded inactive-link rebuild')
    put(ALLOC_GUARD+0x300,blob[0x300:0x318],'native unwind glass allocation guard',True)
    put(FREE_GUARD,blob[0x400:0x500],'native glass duplicate/out-of-range free guard')
    put(FREE_GUARD+0x100,blob[0x500:],'native unwind glass free guard',True)
    return edits,{'client_skeleton_bytes':CLIENT_BYTES,'server_skeleton_bytes':SERVER_BYTES,
                  'native_skeleton_base_sites':5,'native_skeleton_bound_sites':4,
                  'glass_allocation_guard':'validate current/next inactive indices; rebuild from occupancy map on corruption',
                  'glass_free_guard':'reject out-of-range or already inactive pieces',
                  'glass_relay_unwind_entries':2,
                  'causal_limit':'The returned crash proves a corrupt glass free head. It does not capture the first write that damaged the chain.'}
