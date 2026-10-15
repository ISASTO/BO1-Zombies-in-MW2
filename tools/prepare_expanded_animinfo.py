"""Relocate the native XAnim anim-info pool from 4,096 to 16,384 records.

Test32 Estate (160 actors) and Contingency (~100 actors) both stopped with the
native engine error "exceeded maximum number of anim info". The pool is a
static 64-byte-record array at 0x1bc0a30 (record 0 is the list head; indices
are WORDs). Its free list is built as a circular list with modulo-4096
arithmetic at 0x29ce80. Every reference is retargeted to an owned arena block
of 16,384 records with an inaccessible page after it, and the init modulus and
end bound are changed to match. Indices stay 16-bit; record semantics and all
native allocation/free code are unchanged.
"""
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_RIP

OLD=0x1bc0a30; OLD_COUNT=0x1000; RECORD=64
NEW=0x4640000; NEW_COUNT=0x4000
NEW_BYTES=NEW_COUNT*RECORD
OLD_END=OLD+OLD_COUNT*RECORD  # 0x1c00a30, first byte of the next native object
# The only reference to OLD_END+0xa is the init loop bound; every other
# reference at or after OLD_END belongs to the following native array.
INIT_END_SITE=0x29ce90
INIT_IMMEDIATES=((0x29cea0,0xffe,NEW_COUNT-2),
                 (0x29cea6,0x80000fff,0x80000000|(NEW_COUNT-1)),(0x29ceaf,0xfffff000,(-NEW_COUNT)&0xffffffff),
                 (0x29cebc,0x80000fff,0x80000000|(NEW_COUNT-1)),(0x29cec5,0xfffff000,(-NEW_COUNT)&0xffffffff))
EXPECTED_RIP=58; EXPECTED_IMAGE_RELATIVE=10
# Per-snapshot server->client anim transfer records (0x1c bytes), reset each
# snapshot and rebuilt for every transferred tree; a checkpoint reload
# transfers every actor at once. Sized to the expanded anim-info pool.
TRANSFER_OLD=0x1c01230; TRANSFER_RECORD=0x1c; TRANSFER_OLD_COUNT=0x800
TRANSFER_NEW=0x4741000; TRANSFER_NEW_COUNT=NEW_COUNT; TRANSFER_BYTES=TRANSFER_NEW_COUNT*TRANSFER_RECORD
TRANSFER_SITES=(0x29ec1d,0x29ed36); TRANSFER_BOUND=0x29ebd7


def inventory(pe):
    base=pe.OPTIONAL_HEADER.ImageBase
    cs=Cs(CS_ARCH_X86,CS_MODE_64);cs.detail=True;cs.skipdata=True
    rows=[]
    for s in pe.sections:
        if not s.Characteristics&0x20000000:continue
        for i in cs.disasm(s.get_data(),base+s.VirtualAddress):
            if not i.id:continue
            at=i.address-base
            for o in i.operands:
                if o.type!=X86_OP_MEM:continue
                if o.mem.base==X86_REG_RIP:
                    target=at+i.size+o.mem.disp
                    if OLD<=target<OLD_END or (at==INIT_END_SITE and target==OLD_END+0xa):
                        rows.append(('rip',at,i.size,i.disp_offset,target,f'{i.mnemonic} {i.op_str}'))
                elif OLD<=o.mem.disp<OLD_END:
                    rows.append(('image',at,i.size,i.disp_offset,o.mem.disp,f'{i.mnemonic} {i.op_str}'))
    return rows


def inventory_range(pe,lo,hi):
    base=pe.OPTIONAL_HEADER.ImageBase
    cs=Cs(CS_ARCH_X86,CS_MODE_64);cs.detail=True;cs.skipdata=True;rows=[]
    for s in pe.sections:
        if not s.Characteristics&0x20000000:continue
        for i in cs.disasm(s.get_data(),base+s.VirtualAddress):
            if not i.id:continue
            at=i.address-base
            for o in i.operands:
                if o.type!=X86_OP_MEM:continue
                if o.mem.base==X86_REG_RIP:
                    target=at+i.size+o.mem.disp
                    if lo<=target<hi:rows.append(('rip',at,i.size,i.disp_offset,target,f'{i.mnemonic} {i.op_str}'))
                elif lo<=o.mem.disp<hi:rows.append(('image',at,i.size,i.disp_offset,o.mem.disp,f'{i.mnemonic} {i.op_str}'))
    return rows


def prepare(pe,image):
    decoder=Cs(CS_ARCH_X86,CS_MODE_64);decoder.detail=True
    rows=inventory(pe)
    rip=[r for r in rows if r[0]=='rip'];relative=[r for r in rows if r[0]=='image']
    if len(rip)!=EXPECTED_RIP or len(relative)!=EXPECTED_IMAGE_RELATIVE:
        raise ValueError(f'Anim-info pool reference inventory differs: {len(rip)} rip, {len(relative)} image-relative')
    # Image-relative accesses must use the image base register (r14 in the
    # only routine), so the displacement is the pool RVA plus a field offset.
    for kind,at,size,off,target,text in relative:
        if 'r14' not in text:raise ValueError(f'Unexpected image-relative anim-info base at {at:x}: {text}')
    edits=[]
    def put(at,blob,role,data=False):
        image[at:at+len(blob)]=blob
        edits.append({'rva':hex(at),'bytes':len(blob),'role':role,'data':data})
    def relocate(target):
        return NEW+NEW_BYTES+0xa if target==OLD_END+0xa else NEW+target-OLD
    for kind,at,size,off,target,text in rows:
        blob=bytearray(pe.get_data(at,size));moved=relocate(target)
        value=moved-(at+size) if kind=='rip' else moved
        struct.pack_into('<i',blob,off,value)
        put(at,bytes(blob),'relocated native anim-info pool reference')
    for at,old,new in INIT_IMMEDIATES:
        i=next(decoder.disasm(pe.get_data(at,16),at));blob=bytearray(i.bytes)
        if i.mnemonic=='lea':
            if i.disp!=old or i.disp_size!=4:raise ValueError(f'Anim-info init modulus differs at {at:x}')
            struct.pack_into('<i',blob,i.disp_offset,new)
        else:
            immediate=[o.imm&0xffffffff for o in i.operands if o.type==X86_OP_IMM]
            if immediate!=[old] or i.imm_size!=4:raise ValueError(f'Anim-info init mask differs at {at:x}')
            struct.pack_into('<I',blob,i.imm_offset,new)
        put(at,bytes(blob),'expanded native anim-info circular free-list modulus')
    transfer=[]
    for kind,at,size,off,target,text in inventory_range(pe,TRANSFER_OLD,TRANSFER_OLD+TRANSFER_OLD_COUNT*TRANSFER_RECORD):
        transfer.append(at)
        if kind!='rip' or target!=TRANSFER_OLD:raise ValueError(f'Unexpected anim-transfer reference at {at:x}')
        blob=bytearray(pe.get_data(at,size));struct.pack_into('<i',blob,off,TRANSFER_NEW-(at+size))
        put(at,bytes(blob),'relocated native anim-transfer record array')
    if tuple(transfer)!=TRANSFER_SITES:raise ValueError(f'Anim-transfer reference inventory differs: {[hex(a) for a in transfer]}')
    i=next(decoder.disasm(pe.get_data(TRANSFER_BOUND,16),TRANSFER_BOUND))
    if i.mnemonic!='cmp' or i.imm_size!=4 or [o.imm for o in i.operands if o.type==X86_OP_IMM]!=[TRANSFER_OLD_COUNT]:
        raise ValueError('Anim-transfer bound differs')
    blob=bytearray(i.bytes);struct.pack_into('<I',blob,i.imm_offset,TRANSFER_NEW_COUNT)
    put(TRANSFER_BOUND,bytes(blob),'expanded native anim-transfer bound')
    return edits,{'anim_transfer_records':TRANSFER_NEW_COUNT,'native_anim_transfer_records':TRANSFER_OLD_COUNT,
                  'anim_transfer_arena_rva':hex(TRANSFER_NEW),'native_pool_rva':hex(OLD),'native_records':OLD_COUNT,'expanded_pool_arena_rva':hex(NEW),
                  'expanded_records':NEW_COUNT,'record_bytes':RECORD,'index_width_bits':16,
                  'rip_references':len(rip),'image_relative_references':len(relative),'init_constants':len(INIT_IMMEDIATES),
                  'returned_error':'exceeded maximum number of anim info (Test32 Estate ~160 actors, Contingency ~100 actors)',
                  'scope':'Native XAnim allocation/free/lookup code is unchanged; only storage location, count and init modulus change.'}
