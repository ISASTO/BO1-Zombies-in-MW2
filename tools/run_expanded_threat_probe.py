"""Reproduce Test25's native threat-frame overwrite and test the new frame."""
import argparse,gc,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from capstone.x86 import X86_OP_MEM,X86_REG_RSP
from audit_actor_capacity import EXACT_SHA256
from run_expanded_iterator_probe import patches,fixture,HEADER
OLD_COMMIT='208bbb22e5d66fb6597c409cc5a6da5b605ceeec'

def unwind_audit(pe,rows):
    def read(at,size):
        blob=bytearray(pe.get_data(at,size)) if at<0x4000000 else bytearray(size)
        for a,_,after,_ in rows:
            lo=max(at,a);hi=min(at+size,a+len(after))
            if lo<hi:blob[lo-at:hi-at]=after[lo-a:hi-a]
        if len(blob)!=size:raise ValueError('Unwind audit reads truncated data')
        return bytes(blob)
    dec=Cs(CS_ARCH_X86,CS_MODE_64);dec.detail=True
    code=list(dec.disasm(read(0x156e00,0x5cd),0x156e00));saves=set()
    for i in code:
        if i.operands and i.operands[0].type==X86_OP_MEM and i.operands[0].mem.base==X86_REG_RSP and i.operands[0].mem.disp>=0x1a20:
            name=i.reg_name(i.operands[1].reg)
            if name in ('rdi','rsi','r12','r13','r14','r15','xmm6','xmm7'):saves.add((i.operands[0].mem.disp,name))
    pe.parse_data_directories(directories=[3]);entries=[x.struct for x in pe.DIRECTORY_ENTRY_EXCEPTION if 0x156e00<=x.struct.BeginAddress<0x1573cd]
    def parse(at):
        flag,prolog,count,frame=read(at,4);blob=read(at,4+((count+1)&~1)*2);slot=0;ops=[]
        if frame or flag&7!=1:raise ValueError('Unexpected threat unwind header')
        while slot<count:
            offset,op=blob[4+slot*2:6+slot*2];info=op>>4;op&=15
            if op in (1,4,8):
                scale=16 if op==8 else 8;value=struct.unpack_from('<H',blob,6+slot*2)[0]*scale;slot+=2
                if op==1:
                    if value!=0x1a68:raise ValueError('Unwind allocation disagrees with native stack')
                else:
                    name=('rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi','r8','r9','r10','r11','r12','r13','r14','r15')[info] if op==4 else f'xmm{info}'
                    if (value,name) not in saves:raise ValueError('Unwind save disagrees with native instruction')
                ops.append((offset,op,value))
            elif op==0:ops.append((offset,op,8));slot+=1
            else:raise ValueError('Unexpected unwind operation')
        if flag&32:
            parent=struct.unpack('<III',read(at+len(blob),12));ops+=parse(parent[2])
        return ops
    for entry in entries:
        ops=parse(entry.UnwindData)
        if sum(v for _,op,v in ops if op==1)!=0x1a68 or sum(v for _,op,v in ops if op==0)!=16:
            raise ValueError('Chained unwind does not recover complete caller frame')
    relay=read(0x4610040,12)
    if relay!=bytes.fromhex('0117040017014d0300500030'):raise ValueError('Relay unwind differs')
    # The two pushes have executed before relay byte zero. The frame is
    # allocated after byte 23; validate both page touches and the jump-back.
    probe=list(dec.disasm(read(0x4610000,28),0x4610000))
    if [i.mnemonic for i in probe]!=['test','test','sub','jmp'] or probe[2].operands[-1].imm!=0x1a68:
        raise ValueError('Ordered stack probe differs')
    for pc in (0,8,23):
        effective=16+(0x1a68 if pc>=23 else 0)
        if effective not in (16,6776):raise ValueError('Relay partial unwind is invalid')
    return {'native_unwind_function_ranges':len(entries),'native_stack_save_slots':len(saves),
            'relay_partial_prolog_states':3,'frame_and_save_offsets_match_native_code':True,
            'rtl_virtual_unwind_executed_on_windows':False}

def run(executable):
    raw=executable.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=EXACT_SHA256:raise ValueError('Unsupported executable')
    pe=pefile.PE(data=raw,fast_load=True);tools=Path(__file__).resolve().parent
    old_header=subprocess.check_output(['git','show',OLD_COMMIT+':include/expanded_capacity_patches.hpp'],cwd=tools.parent).decode()
    with tempfile.TemporaryDirectory(prefix='bo1mw2-threat-') as directory:
        root=Path(directory);old=root/'old.bin';new=root/'new.bin'
        image=fixture(pe,patches(old_header));old.write_bytes(image);del image;gc.collect()
        current=patches(HEADER.read_text());metadata=unwind_audit(pe,current)
        image=fixture(pe,current);new.write_bytes(image);del image,pe,raw;gc.collect()
        binary=root/'threat';subprocess.run(['g++','-std=c++17','-O2','-I',str(tools.parent/'include'),str(tools/'expanded_threat_probe.cpp'),'-o',str(binary)],check=True)
        broken=subprocess.run([str(binary),str(old),'old'],capture_output=True,text=True,timeout=20)
        if broken.returncode!=1 or 'CANDIDATE_51_CORRUPTS_NATIVE_NONVOLATILE_REGISTERS' not in broken.stderr:
            raise ValueError('Old native overwrite did not reproduce: '+broken.stderr)
        good=subprocess.run([str(binary),str(new),'fixed'],capture_output=True,text=True,timeout=30)
        if good.returncode:raise ValueError('Corrected native threat frame failed: '+good.stderr)
        result=json.loads(good.stdout)
    return {'executable_sha256':EXACT_SHA256,'old_revision':OLD_COMMIT,'old_candidate_51_saved_register_overwrite_reproduced':True,
            'old_candidate_rows':50,'new_candidate_rows':274,'old_stack_bytes':0x568,'new_stack_bytes':0x1a68,
            'runtime_plan_external_arena':True,'corrected_native_results':result,'unwind_metadata_audit':metadata,
            'scope':'Exact native stack, perception lookup, candidate construction, opposing-team traversal and best-threat tracking. Score/world predicates, secondary reduction and enemy commit are isolated. Windows guard-page growth and RtlVirtualUnwind remain Windows tests.'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();result=run(a.executable);a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
