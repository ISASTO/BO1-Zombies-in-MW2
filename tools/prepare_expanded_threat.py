"""Exact native threat-frame and biased sentient consumers for Test26.

The 50-row stack array must grow together with its saves, every exit and all
chained AMD64 unwind descriptions. Probes commit stack pages in order without
changing registers. The relay has its own runtime unwind entry.
"""
import struct
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from capstone.x86 import X86_OP_MEM,X86_OP_IMM,X86_REG_RSP,X86_REG_RIP

START=0x156e00;END=0x1573cd
OLD_FRAME=0x568;EXTRA=(274-50)*24;FRAME=OLD_FRAME+EXTRA
PROBE=0x4610000;PROBE_END=PROBE+28;UNWIND=PROBE+0x40
SENTIENTS=0x4500000
HANDLE_SITES=(0x128083,0x157289,0x1572b9,0x19b314,0x1b6631,0x1b693d)

def prepare(pe,image):
    dec=Cs(CS_ARCH_X86,CS_MODE_64);dec.detail=True
    edits=[]
    def put(at,blob,role,data=False):
        image[at:at+len(blob)]=blob
        edits.append({'rva':hex(at),'bytes':len(blob),'role':role,'data':data})
    def ins(at):return next(dec.disasm(pe.get_data(at,16),at))
    def displacement(at,value,role):
        i=ins(at)
        if i.disp_size!=4:raise ValueError(f'Expected DWORD displacement at {at:x}')
        blob=bytearray(i.bytes);struct.pack_into('<i',blob,i.disp_offset,value);put(at,blob,role)
    exits=0;saves=0
    for i in dec.disasm(pe.get_data(START,END-START),START):
        if any(o.type==X86_OP_MEM and o.mem.base==X86_REG_RSP and o.mem.disp>=0x520 for o in i.operands):
            displacement(i.address,i.disp+EXTRA,'expanded threat frame saved register / argument');saves+=1
        if i.mnemonic=='add' and i.operands[0].type!=X86_OP_MEM and i.operands[0].reg==X86_REG_RSP:
            if i.operands[-1].type!=X86_OP_IMM or i.operands[-1].imm!=OLD_FRAME or i.imm_size!=4:
                raise ValueError('Unexpected threat frame exit')
            blob=bytearray(i.bytes);struct.pack_into('<I',blob,i.imm_offset,FRAME)
            put(i.address,blob,'expanded threat frame native exit');exits+=1
    if exits!=3 or saves!=25:raise ValueError(f'Unexpected threat frame inventory: saves={saves}, exits={exits}')
    at=START+3;i=ins(at)
    if bytes(i.bytes)!=bytes.fromhex('4881ec68050000'):raise ValueError('Threat prologue differs')
    # Touch the intervening guard page, then the final page before subtracting
    # 0x1a68. TEST and SUB alter flags, as the original SUB did; GPRs stay intact.
    blob=bytes.fromhex('f6842400f0ffff00')+bytes.fromhex('f68424')+struct.pack('<i',-FRAME)+b'\0'
    blob+=bytes.fromhex('4881ec')+struct.pack('<I',FRAME)
    blob+=b'\xe9'+struct.pack('<i',at+i.size-(PROBE+len(blob)+5))
    if len(blob)!=PROBE_END-PROBE:raise ValueError('Stack probe size differs')
    put(PROBE,blob,'native threat ordered stack probe')
    put(at,b'\xe9'+struct.pack('<i',PROBE-at-5)+b'\x90\x90','native threat stack probe entry')
    # The native RBX/RBP pushes precede the relay. Offset zero represents
    # their already-executed saves; the allocation applies after relay byte 23.
    put(UNWIND,bytes((1,23,4,0,23,1))+struct.pack('<H',FRAME//8)+bytes.fromhex('00500030'),
        'native unwind threat stack probe',True)
    pe.parse_data_directories(directories=[3])
    owners=[x.struct for x in pe.DIRECTORY_ENTRY_EXCEPTION if START<=x.struct.BeginAddress<END]
    rvas=sorted({x.UnwindData for x in owners});updated=0
    for rva in rvas:
        head=pe.get_data(rva,4);count=head[2]
        blob=bytearray(pe.get_data(rva,4+((count+1)&~1)*2));slot=0;changed=False
        while slot<count:
            offset=4+slot*2;op=blob[offset+1]&15;info=blob[offset+1]>>4
            if op in (1,4,8):
                if op==1 and info!=0:raise ValueError('Unexpected far threat allocation')
                units=struct.unpack_from('<H',blob,offset+2)[0];scale=16 if op==8 else 8;value=units*scale
                if (op==1 and value==OLD_FRAME) or (op in (4,8) and value>=0x520):
                    struct.pack_into('<H',blob,offset+2,(value+EXTRA)//scale);changed=True
                slot+=2
            elif op==0:slot+=1
            else:raise ValueError(f'Unexpected threat unwind opcode {op}')
        if changed:
            if any(x.UnwindData==rva and not START<=x.BeginAddress<END for x in (e.struct for e in pe.DIRECTORY_ENTRY_EXCEPTION)):
                raise ValueError('Threat unwind description shared outside function')
            put(rva,blob,'native unwind expanded threat frame',True);updated+=1
    if updated!=8:raise ValueError(f'Expected eight affected unwind descriptions, got {updated}')
    for at in HANDLE_SITES:
        i=ins(at)
        if not any(o.type==X86_OP_MEM and o.mem.base!=X86_REG_RIP and o.mem.disp==0xd9c4f8 for o in i.operands):
            raise ValueError(f'Biased sentient handle differs at {at:x}')
        displacement(at,SENTIENTS-0x88,'relocated image-relative sentient handle resolution')
    for at,old,target in ((0x19fb06,0xd9c640,SENTIENTS+0xc0),
                           (0x19fb0d,0xd9e0d0,SENTIENTS+274*0x88+0xc0)):
        i=ins(at)
        if i.address+i.size+i.disp!=old:raise ValueError('Sentient entity-reference cleanup differs')
        displacement(at,target-at-i.size,'expanded sentient entity-reference cleanup')
    return edits,{'threat_candidate_rows':274,'threat_row_bytes':24,'old_stack_bytes':OLD_FRAME,
                  'expanded_stack_bytes':FRAME,'saved_field_edits':saves,'native_exits':exits,
                  'updated_native_unwind_descriptions':updated,'registered_relay_unwind':True,
                  'biased_sentient_handles':len(HANDLE_SITES),'sentient_entity_reference_cleanup':274}
