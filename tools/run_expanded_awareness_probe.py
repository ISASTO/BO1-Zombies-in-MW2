"""Reproduce Test26's second 50-row overwrite and execute its corrected paths."""
import argparse
import gc
import hashlib
import json
import struct
import subprocess
import tempfile
from pathlib import Path
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RSP, X86_REG_R11
from audit_actor_capacity import EXACT_SHA256
from run_expanded_iterator_probe import patches, fixture, HEADER
from prepare_expanded_awareness import START, END, FRAME, PROBE, UNWIND

OLD_COMMIT='83de84bf972f7b501c7ad4b29de1334decdebad6'


def unwind_audit(pe,rows):
    def read(at,size):
        blob=bytearray(pe.get_data(at,size)) if at<0x4000000 else bytearray(size)
        for a,_,after,_ in rows:
            lo=max(a,at)
            hi=min(a+len(after),at+size)
            if lo<hi:blob[lo-at:hi-at]=after[lo-a:hi-a]
        if len(blob)!=size:raise ValueError('Truncated unwind data')
        return bytes(blob)

    decoder=Cs(CS_ARCH_X86,CS_MODE_64)
    decoder.detail=True
    saves=set()
    for ins in decoder.disasm(read(START,END-START),START):
        if len(ins.operands)<2 or ins.operands[0].type!=X86_OP_MEM:continue
        mem=ins.operands[0].mem
        if mem.base not in (X86_REG_RSP,X86_REG_R11):continue
        name=ins.reg_name(ins.operands[1].reg)
        if name not in ('rbx','r12','r13','r14','r15','xmm6','xmm7','xmm8'):continue
        offset=mem.disp+(FRAME+24 if mem.base==X86_REG_R11 else 0)
        if offset<0x1160:raise ValueError('Native save still aliases candidate storage')
        saves.add((offset,name))
    if len(saves)!=8:raise ValueError('Awareness register-save inventory differs')
    pe.parse_data_directories(directories=[3])
    entries=[e.struct for e in pe.DIRECTORY_ENTRY_EXCEPTION if START<=e.struct.BeginAddress<END]

    def parse(at):
        flag,_,count,frame=read(at,4)
        if frame or flag&7!=1:raise ValueError('Unexpected native unwind header')
        blob=read(at,4+((count+1)&~1)*2)
        slot=0
        allocation=pushes=0
        while slot<count:
            encoded=blob[5+slot*2]
            op=encoded&15
            info=encoded>>4
            if op in (1,4,8):
                value=struct.unpack_from('<H',blob,6+slot*2)[0]*(16 if op==8 else 8)
                if op==1:
                    if info or value!=FRAME:raise ValueError('Native unwind frame disagrees')
                    allocation+=value
                else:
                    name=('rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi','r8','r9','r10','r11','r12','r13','r14','r15')[info] if op==4 else f'xmm{info}'
                    if (value,name) not in saves:raise ValueError('Unwind save does not match native instruction')
                slot+=2
            elif op==0:pushes+=8;slot+=1
            else:raise ValueError('Unexpected native unwind opcode')
        if flag&32:
            parent=struct.unpack('<III',read(at+len(blob),12))
            a,p=parse(parent[2])
            allocation+=a;pushes+=p
        return allocation,pushes

    for entry in entries:
        if parse(entry.UnwindData)!=(FRAME,24):raise ValueError('Incomplete awareness unwind chain')
    if read(UNWIND,16)!=bytes.fromhex('01170500170134020070006000500000'):
        raise ValueError('Awareness relay unwind differs')
    code=list(decoder.disasm(read(PROBE,28),PROBE))
    if [i.mnemonic for i in code]!=['test','test','sub','jmp'] or [i.disp for i in code[:2]]!=[-4096,-FRAME]:
        raise ValueError('Stack pages are not touched in order')
    if code[2].operands[-1].imm!=FRAME or code[3].operands[-1].imm!=START+13:
        raise ValueError('Stack allocation/jump-back differs')
    # Simulate the three reachable relay prolog states. Push descriptions at
    # offset zero account for native saves completed before this relay starts.
    original_rsp=0x100000
    stack={original_rsp-8:0x55,original_rsp-16:0x66,original_rsp-24:0x77}
    unwind=read(UNWIND,16)
    for pc in (0,8,23):
        rsp=original_rsp-24-(FRAME if pc>=23 else 0)
        restored={};slot=0
        while slot<unwind[2]:
            offset,encoded=unwind[4+slot*2:6+slot*2]
            op,info=encoded&15,encoded>>4
            if op==1:
                if offset<=pc:rsp+=struct.unpack_from('<H',unwind,6+slot*2)[0]*8
                slot+=2
            elif op==0:
                if offset<=pc:restored[info]=stack[rsp];rsp+=8
                slot+=1
            else:raise ValueError('Unexpected relay unwind opcode')
        if rsp!=original_rsp or restored!={7:0x77,6:0x66,5:0x55}:
            raise ValueError('Partial relay prolog fails to restore native pushes')
    return {'native_unwind_ranges':len(entries),'saved_register_slots':len(saves),
            'relay_partial_prolog_states':3,'frame_and_saves_match_native_instructions':True,
            'windows_rtl_virtual_unwind_executed':False}


def run(executable):
    raw=executable.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=EXACT_SHA256:raise ValueError('Exact executable hash differs')
    pe=pefile.PE(data=raw,fast_load=True)
    old_text=subprocess.check_output(['git','show',OLD_COMMIT+':include/expanded_capacity_patches.hpp'],cwd=HEADER.parents[1]).decode()
    current=patches(HEADER.read_text())
    metadata=unwind_audit(pe,current)
    with tempfile.TemporaryDirectory(prefix='bo1mw2-awareness-') as directory:
        root=Path(directory)
        old=root/'old.bin'
        new=root/'fixed.bin'
        image=fixture(pe,patches(old_text));old.write_bytes(image);del image;gc.collect()
        image=fixture(pe,current);new.write_bytes(image);del image,pe,raw;gc.collect()
        program=root/'awareness'
        subprocess.run(['g++','-std=c++17','-O2','-I',str(HEADER.parent),str(HEADER.parents[1]/'tools/expanded_awareness_probe.cpp'),'-o',str(program)],check=True)
        broken=subprocess.run([str(program),str(old),'old'],capture_output=True,text=True,timeout=30)
        if broken.returncode!=1 or 'NEARBY_CANDIDATE_51_RESTORES_SENTIENT_POINTER_IN_XMM8' not in broken.stderr:
            raise ValueError('Test26 overwrite did not reproduce: '+broken.stderr)
        good=subprocess.run([str(program),str(new),'fixed'],capture_output=True,text=True,timeout=60)
        if good.returncode:raise ValueError(f'Corrected awareness probe failed ({good.returncode}): '+good.stderr)
        native=json.loads(good.stdout)
    return {'executable_sha256':EXACT_SHA256,'old_revision':OLD_COMMIT,
            'old_candidate_51_sentient_pointer_in_xmm8_reproduced':True,
            'old_candidate_rows':50,'new_candidate_rows':274,'row_bytes':16,
            'old_frame_bytes':0x3a0,'new_frame_bytes':FRAME,
            'checked_runtime_plan_external_arena':True,'native_results':native,'unwind_audit':metadata,
            'scope':'Actual sentient traversal, distance/weight calculation, candidate construction, native qsort/comparator, record refresh and nonvolatile preservation. World visibility callbacks are isolated. Windows mission/renderer stability and RtlVirtualUnwind remain owner tests.'}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    result=run(args.executable)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
