"""Never fracture a glass piece the native place lookup did not find.

Test31 returned two Estate crashes through the same stock path. Client glass
state 2 for a place reaches 0x100870, whose active-piece scan finds nothing;
the routine still calls native fracture with an uninitialized stack index.
That fracture runs on an inactive or foreign piece record, which is the
first point the returned dumps show glass topology being corrupted.

The server damage entry indexes place records from a trace hit id with no
bound. It is bounded here and records any rejected caller, so a future dump
can show whether an out-of-range id is the upstream source.
"""
from pathlib import Path
import struct, subprocess, tempfile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_IMM
from prepare_expanded_crowd import native_unwind

BASE=0x4616000
LOOKUP=(0x1009d6,0x1009de,BASE)
DAMAGE=(0x16a060,0x16a06a,BASE+0x100)
RELAYS=((BASE,BASE+0x100,BASE+0x700,0x1009d6),
        (BASE+0x100,BASE+0x200,BASE+0x740,None))
EMPTY_UNWIND=bytes((1,0,0,0))


def _callers(pe,targets):
    decoder=Cs(CS_ARCH_X86,CS_MODE_64);decoder.detail=True;decoder.skipdata=True
    found={t:[] for t in targets}
    for section in pe.sections:
        if not section.Characteristics&0x20000000:continue
        for i in decoder.disasm(section.get_data(),section.VirtualAddress):
            if i.id and i.mnemonic in ('call','jmp') and i.operands and i.operands[0].type==X86_OP_IMM and i.operands[0].imm in found:
                found[i.operands[0].imm].append(i.address)
    return found


def validate(pe):
    decoder=Cs(CS_ARCH_X86,CS_MODE_64)
    def text(at,n):return [f'{i.mnemonic} {i.op_str}'.strip() for i in decoder.disasm(pe.get_data(at,n),at)]
    expected={
        0x100890:['mov edi, dword ptr [rip + 0xbda892]','xor r11d, r11d','mov r10d, dword ptr [rsp + 0x3c]','mov byte ptr [rsp + 0x40], 0'],
        0x10099e:['mov r10d, edx','mov byte ptr [rsp + 0x40], 1'],
        0x1009d6:['mov r9, r15','mov qword ptr [rsp + 0x20], r14','mov r8, r12','lea rcx, [rip + 0xbda730]','mov edx, r10d','call 0x103a80','add rsp, 0x50'],
        0x16a060:['mov qword ptr [rsp + 8], rbx','mov qword ptr [rsp + 0x10], rbp','mov qword ptr [rsp + 0x18], rsi','push rdi','sub rsp, 0x20','mov rax, qword ptr [rip + 0xb8d52d]'],
        0x16aa4d:['mov rcx, qword ptr [rip + 0xb8cb54]','mov esi, ebx','mov ebp, ebx','mov eax, ebx','mov r8d, dword ptr [rcx + 8]'],
    }
    for at,want in expected.items():
        got=text(at,64)[:len(want)]
        if got!=want:raise ValueError(f'Native glass state code differs at {at:x}: {got}')
    if pe.get_data(LOOKUP[0],8)!=bytes.fromhex('4d8bcf4c89742420'):raise ValueError('Glass place lookup entry differs')
    if pe.get_data(DAMAGE[0],10)!=bytes.fromhex('48895c240848896c2410'):raise ValueError('Glass damage entry differs')
    callers=_callers(pe,(0x100870,0x16a060))
    if callers[0x100870]!=[0xa2204]:raise ValueError('Additional glass shatter caller requires review')
    if sorted(callers[0x16a060])!=[0x1596b9,0x15996a,0x1a276c]:raise ValueError('Additional glass damage caller requires review')
    return callers


def prepare(pe,image):
    callers=validate(pe);edits=[]
    def put(at,blob,role,data=False):
        image[at:at+len(blob)]=blob
        edits.append({'rva':hex(at),'bytes':len(blob),'role':role,'data':data})
    with tempfile.TemporaryDirectory(prefix='bo1mw2-glass-state-') as directory:
        root=Path(directory);obj=root/'guard.o';binary=root/'guard.bin'
        subprocess.run(['as','--64',str(Path(__file__).with_name('expanded_glass_state_guard.S')),'-o',str(obj)],check=True,capture_output=True)
        subprocess.run(['ld','-Ttext',hex(BASE),'--oformat=binary','-o',str(binary),str(obj)],check=True,capture_output=True)
        blob=binary.read_bytes()
    if len(blob)!=0x200:raise ValueError('Glass state relay layout differs')
    for begin,end,unwind,site in RELAYS:
        put(begin,blob[begin-BASE:end-BASE],'native glass place/damage state guard')
        raw=native_unwind(pe,site) if site else EMPTY_UNWIND
        if len(raw)>0x40:raise ValueError('Glass state unwind slot exceeded')
        put(unwind,raw,'standalone native glass state unwind',True)
    for begin,end,target in (LOOKUP,DAMAGE):
        put(begin,b'\xe9'+struct.pack('<i',target-begin-5)+b'\x90'*(end-begin-5),'complete native glass state guard entry')
    return edits,{'shatter_lookup_site':hex(LOOKUP[0]),'shatter_caller':[hex(a) for a in callers[0x100870]],
                  'missing_piece_behavior':'skip native fracture; caller still records the applied place state',
                  'damage_entry':hex(DAMAGE[0]),'damage_callers':[hex(a) for a in callers[0x16a060]],
                  'damage_bound':'place < loaded server place count, else rejected with caller recorded',
                  'standalone_unwind_entries':len(RELAYS),
                  'returned_evidence':'Test31 dumps 20261015-055729 and 20261015-060313: found flag 0, uninitialized index 0x7ff7 and 0, places 10 and 27 (Estate loads 27 places).',
                  'causal_limit':'Why a shatter state reached a place with no active piece is not yet captured; the free-caller ring and rejected-damage caller are diagnostics for that upstream cause.'}
