"""Keep expanded actors out of the native 32-actor compass table.

Returned Test32 hardware-watch hits show 0x92d49..0x92d68 (native compass
entity update) writing actor positions into static command entries. The
table at 0x3fdf50 has 42 records of 0x2c bytes: 32 actor slots, 2 player
slots and 8 other slots. Actor slots come from the client entity->actor byte
map (0..255), so expanded actors overran into command entries (slots 51..63)
and the client glass place state table (slots 93..209).
"""
from pathlib import Path
import struct, subprocess, tempfile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_IMM
from prepare_expanded_crowd import native_unwind

BASE=0x4616200
SITES=((0x92c37,0x92c40,BASE,'8b4c2440e9b1000000'),
       (0x92ded,0x92df2,BASE+0x100,'e8ae980900'),
       (0x925af,0x925b4,BASE+0x200,'448b4c2430'))
RELAYS=((BASE,BASE+0x100,0x4616780,0x92c37),
        (BASE+0x100,BASE+0x200,0x46167c0,0x92ded),
        (BASE+0x200,BASE+0x300,0x4616800,0x925af))
TABLE=0x3fdf50;RECORD=0x2c;SLOTS=42;ACTOR_SLOTS=32


def _callers(pe,targets):
    cs=Cs(CS_ARCH_X86,CS_MODE_64);cs.detail=True;cs.skipdata=True;found={t:[] for t in targets}
    for s in pe.sections:
        if not s.Characteristics&0x20000000:continue
        for i in cs.disasm(s.get_data(),s.VirtualAddress):
            if i.id and i.mnemonic in ('call','jmp') and i.operands and i.operands[0].type==X86_OP_IMM and i.operands[0].imm in found:
                found[i.operands[0].imm].append(i.address)
    return found


# 0x92dc0 saves RBX only on a later path (prologue offset 0x62); at the hooked
# call (offset 0x2d) only its 0x28-byte allocation has executed.
SLOT_FRAME_UNWIND=bytes((1,0,1,0, 0,0x42,0,0))


def frame_unwind(pe,site):
    if site!=0x92ded:return native_unwind(pe,site)
    pe.parse_data_directories(directories=[3])
    entry=next(x.struct for x in pe.DIRECTORY_ENTRY_EXCEPTION if x.struct.BeginAddress<=site<x.struct.EndAddress)
    raw=pe.get_data(entry.UnwindData,12)
    if entry.BeginAddress!=0x92dc0 or raw[:8]!=bytes.fromhex('0162030062340400') or raw[8:10]!=bytes.fromhex('0442'):
        raise ValueError('Compass slot lookup frame differs')
    if pe.get_data(0x92dc0,4)!=bytes.fromhex('4883ec28'):raise ValueError('Compass slot lookup prologue differs')
    return SLOT_FRAME_UNWIND


def validate(pe):
    for begin,end,target,before in SITES:
        if pe.get_data(begin,end-begin)!=bytes.fromhex(before):raise ValueError(f'Compass site differs at {begin:x}')
    for at,want in ((0x92df2,'b0014883c428c3'),(0x92db5,'4883c4205f5e5dc3'),(0x9260d,'488b5c24384883c4205fc3'),
                    (0x92cff,'488d054ab23600'),(0x92546,'488d0d03ba3600'),(0x92553,'e8781d2500')):
        if pe.get_data(at,len(want)//2)!=bytes.fromhex(want):raise ValueError(f'Compass native code differs at {at:x}')
    callers=_callers(pe,(0x12c6a0,0x92dc0,0xaa560))
    if sorted(callers[0x12c6a0])!=[0x92c32,0x92ded]:raise ValueError('Additional actor-slot consumer requires review')
    if callers[0x92dc0]!=[0x925aa]:raise ValueError('Additional compass slot caller requires review')
    if callers[0xaa560]!=[0x92db0]:raise ValueError('Additional compass record writer requires review')
    return callers


def prepare(pe,image):
    callers=validate(pe);edits=[]
    def put(at,blob,role,data=False):
        image[at:at+len(blob)]=blob
        edits.append({'rva':hex(at),'bytes':len(blob),'role':role,'data':data})
    with tempfile.TemporaryDirectory(prefix='bo1mw2-compass-') as directory:
        root=Path(directory);obj=root/'guard.o';binary=root/'guard.bin'
        subprocess.run(['as','--64',str(Path(__file__).with_name('expanded_compass_guard.S')),'-o',str(obj)],check=True,capture_output=True)
        subprocess.run(['ld','-Ttext',hex(BASE),'--oformat=binary','-o',str(binary),str(obj)],check=True,capture_output=True)
        blob=binary.read_bytes()
    if len(blob)!=0x300:raise ValueError('Compass relay layout differs')
    for begin,end,unwind,site in RELAYS:
        put(begin,blob[begin-BASE:end-BASE],'native compass actor-slot bound')
        raw=frame_unwind(pe,site)
        if len(raw)>0x40:raise ValueError('Compass unwind slot exceeded')
        put(unwind,raw,'standalone native compass unwind',True)
    for begin,end,target,before in SITES:
        put(begin,b'\xe9'+struct.pack('<i',target-begin-5)+b'\x90'*(end-begin-5),'complete native compass guard entry')
    return edits,{'table_rva':hex(TABLE),'record_bytes':RECORD,'native_slots':SLOTS,'actor_slots':ACTOR_SLOTS,
                  'overrun_targets':{'static_command_entries':'actor slots 51..63','client_glass_place_states':'actor slots 93..209'},
                  'behavior':'actor slots >= 32 are not tracked on the compass; stock unfound-slot writes are skipped',
                  'actor_slot_callers':[hex(a) for a in callers[0x12c6a0]],'standalone_unwind_entries':len(RELAYS),
                  'returned_evidence':'Test32 static watch hits rip 0x92d49/0x92d50/0x92d57/0x92d68 on command fields 0x3fe938/0x3fea00/0x3fea28; 43,892 missing-place shatters for places up to 495 on Estate'}
