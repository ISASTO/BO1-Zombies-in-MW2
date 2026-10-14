"""Expand the second native 50-row sentient scratch array as one frame.

The nearby-awareness producer uses 16-byte rows, independently of the threat
producer's 24-byte rows. Moving a pool/iterator alone does not grow this stack.
"""
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_RSP

START=0x152100
END=0x152437
OLD_FRAME=0x3a0
EXTRA=(274-50)*16
FRAME=OLD_FRAME+EXTRA
PROBE=0x4610100
PROBE_END=PROBE+28
UNWIND=PROBE+0x40


def prepare(pe,image):
    decoder=Cs(CS_ARCH_X86,CS_MODE_64)
    decoder.detail=True
    edits=[]

    def put(at,blob,role,data=False):
        image[at:at+len(blob)]=blob
        edits.append({'rva':hex(at),'bytes':len(blob),'role':role,'data':data})

    saves=exits=0
    for ins in decoder.disasm(pe.get_data(START,END-START),START):
        # R11 still names entry RSP, so its saved-register references move
        # with the larger allocation automatically. Only RSP-relative upper
        # fields, including the borrowed caller shadow slot, need edits.
        if any(o.type==X86_OP_MEM and o.mem.base==X86_REG_RSP
               and o.mem.disp>=0x360 for o in ins.operands):
            if ins.disp_size!=4:raise ValueError('Unexpected awareness frame field')
            blob=bytearray(ins.bytes)
            struct.pack_into('<i',blob,ins.disp_offset,ins.disp+EXTRA)
            put(ins.address,blob,'expanded awareness frame saved register / temporary')
            saves+=1
        if ins.mnemonic=='add' and ins.operands[0].type!=X86_OP_MEM and ins.operands[0].reg==X86_REG_RSP:
            if ins.imm_size!=4 or ins.operands[-1].type!=X86_OP_IMM or ins.operands[-1].imm!=OLD_FRAME:
                raise ValueError('Unexpected awareness frame exit')
            blob=bytearray(ins.bytes)
            struct.pack_into('<I',blob,ins.imm_offset,FRAME)
            put(ins.address,blob,'expanded awareness frame native exit')
            exits+=1
    if saves!=13 or exits!=1:
        raise ValueError(f'Awareness frame inventory differs: fields={saves}, exits={exits}')

    at=START+6
    if pe.get_data(at,7)!=bytes.fromhex('4881eca0030000'):
        raise ValueError('Awareness prologue differs')
    blob=bytes.fromhex('f6842400f0ffff00f68424')+struct.pack('<i',-FRAME)+b'\0'
    blob+=bytes.fromhex('4881ec')+struct.pack('<I',FRAME)
    blob+=b'\xe9'+struct.pack('<i',at+7-(PROBE+len(blob)+5))
    if len(blob)!=PROBE_END-PROBE:raise ValueError('Awareness page probe size differs')
    put(PROBE,blob,'native awareness ordered stack probe')
    put(at,b'\xe9'+struct.pack('<i',PROBE-at-5)+b'\x90\x90','native awareness stack probe entry')
    # RBP, RSI and RDI have already been pushed before relay byte zero.
    unwind=bytes((1,23,5,0,23,1))+struct.pack('<H',FRAME//8)+bytes.fromhex('0070006000500000')
    put(UNWIND,unwind,'native unwind awareness stack probe',True)

    pe.parse_data_directories(directories=[3])
    entries=[e.struct for e in pe.DIRECTORY_ENTRY_EXCEPTION]
    owners=[e for e in entries if START<=e.BeginAddress<END]
    changed=0
    for rva in sorted({e.UnwindData for e in owners}):
        count=pe.get_data(rva,4)[2]
        blob=bytearray(pe.get_data(rva,4+((count+1)&~1)*2))
        slot=0
        touched=False
        while slot<count:
            offset=4+slot*2
            op=blob[offset+1]&15
            info=blob[offset+1]>>4
            if op in (1,4,8):
                if op==1 and info:raise ValueError('Unexpected far awareness allocation')
                units=struct.unpack_from('<H',blob,offset+2)[0]
                scale=16 if op==8 else 8
                value=units*scale
                if (op==1 and value==OLD_FRAME) or (op in (4,8) and value>=0x360):
                    struct.pack_into('<H',blob,offset+2,(value+EXTRA)//scale)
                    touched=True
                slot+=2
            elif op==0:slot+=1
            else:raise ValueError(f'Unexpected awareness unwind opcode {op}')
        if touched:
            if any(e.UnwindData==rva and not START<=e.BeginAddress<END for e in entries):
                raise ValueError('Awareness unwind shared outside routine')
            put(rva,blob,'native unwind expanded awareness frame',True)
            changed+=1
    if len(owners)!=7 or changed!=5:raise ValueError('Awareness unwind inventory differs')
    return edits,{'candidate_rows':274,'row_bytes':16,'old_stack_bytes':OLD_FRAME,
                  'expanded_stack_bytes':FRAME,'rsp_field_edits':saves,'native_exits':exits,
                  'native_unwind_ranges':len(owners),'updated_unwind_descriptions':changed,
                  'relay_unwind_registered_before_publication':True}
