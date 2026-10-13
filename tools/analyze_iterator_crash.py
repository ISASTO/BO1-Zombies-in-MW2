"""Summarize AMD64 fault contexts for the Test22 actor-iterator crashes.
Uses the exception-stream context, not thread contexts captured in the handler.
No dump memory or game/private paths are included in the public evidence.
"""
import argparse,hashlib,json,struct,zipfile,subprocess
from run_expanded_iterator_probe import patches,OLD_COMMIT
from pathlib import Path
from capstone import Cs,CS_ARCH_X86,CS_MODE_64

class Dump:
    def __init__(self,data):
        self.data=data
        if data[:4]!=b'MDMP':raise ValueError('Not a minidump')
        count,at=struct.unpack_from('<II',data,8)
        self.streams={}
        for n in range(count):
            kind,size,rva=struct.unpack_from('<III',data,at+n*12);self.streams[kind]=(rva,size)
        at,size=self.streams[6]
        self.thread=struct.unpack_from('<I',data,at)[0]
        self.code=struct.unpack_from('<I',data,at+8)[0]
        self.exception_address=struct.unpack_from('<Q',data,at+24)[0]
        self.access,self.fault=struct.unpack_from('<QQ',data,at+40)
        length,context=struct.unpack_from('<II',data,at+160)
        if length<0x100:raise ValueError('Truncated AMD64 context')
        names=('rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi','r8','r9','r10','r11','r12','r13','r14','r15','rip')
        self.registers={name:struct.unpack_from('<Q',data,context+0x78+i*8)[0] for i,name in enumerate(names)}
        at,size=self.streams[4];self.base=struct.unpack_from('<Q',data,at+4)[0]
        self.segments=[]
        if 5 in self.streams:
            at,size=self.streams[5];count=struct.unpack_from('<I',data,at)[0]
            for n in range(count):
                begin,length,rva=struct.unpack_from('<QII',data,at+4+n*16);self.segments.append((begin,length,rva))
        if 9 in self.streams:
            at,size=self.streams[9];count,rva=struct.unpack_from('<QQ',data,at)
            for n in range(count):
                begin,length=struct.unpack_from('<QQ',data,at+16+n*16);self.segments.append((begin,length,rva));rva+=length
    def read(self,address,length):
        for begin,size,offset in self.segments:
            if begin<=address and address+length<=begin+size:return self.data[offset+address-begin:offset+address-begin+length]
        raise ValueError(f'Address not captured: {address:x}')

def analyze(path):
    results=[];decoder=Cs(CS_ARCH_X86,CS_MODE_64)
    repo=Path(__file__).resolve().parents[1]
    old_header=subprocess.check_output(['git','show',OLD_COMMIT+':include/expanded_capacity_patches.hpp'],cwd=repo).decode()
    old_patches=patches(old_header)
    with zipfile.ZipFile(path) as archive:
        receipt=json.loads(archive.read('install.json').decode('utf-8-sig'))
        for name in archive.namelist():
            if not name.endswith('.dmp') or not any(stamp in name for stamp in ('20261013-133945','20261013-134024','20261013-134043')):continue
            dump=Dump(archive.read(name));regs=dump.registers
            if regs['rip']-dump.base!=0x1286e0:raise ValueError('Different crash path')
            loop=dump.read(dump.base+0x1286fe,6)
            relay=dump.base+0x1286ff+5+struct.unpack_from('<i',loop,2)[0]
            try:
                code=dump.read(relay,16);compare_evidence='captured dump memory'
            except ValueError:
                target=relay-dump.base-0x1000000
                row=next(p for p in old_patches if p[0]<=target<p[0]+len(p[2]))
                code=row[2][target-row[0]:target-row[0]+16];compare_evidence='matching Test22 source tree; private relay memory absent from dump'
            compare=next(decoder.disasm(code,relay))
            index=regs['r8'];pool=regs['r9'];actor_offset=regs['rax']-pool
            if actor_offset!=index*0x48c0+8 or dump.fault!=regs['rax']+0x14f7:raise ValueError('Fault address disagrees with actor traversal')
            results.append({'dump':name.replace('\\','/').split('/')[-1],'exception':hex(dump.code),'fault_instruction_rva':hex(regs['rip']-dump.base),'read_actor_index':index,'supported_actor_capacity':256,'actor_stride':'0x48c0','actor_pointer_relative_to_pool':hex(actor_offset),'fault_address_relative_to_pool':hex(dump.fault-pool),'allocated_arena_bytes':'0x800000','fault_beyond_arena':dump.fault>=pool+0x800000,'captured_loop_patch':loop.hex(' '),'relay_compare':compare.mnemonic+' '+compare.op_str,'relay_compare_evidence':compare_evidence,'compared_register':'eax','required_register':'r8','team_mask':hex(regs['r10'])})
    return {'archive_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'installed_version':receipt['version'],'installed_dll_sha256':receipt['dll_sha256'],'executable_sha256':receipt['executable_sha256'],'new_matching_crashes':len(results),'crashes':results,'diagnosis':'Test22 starts the iterator loop guard at 0x1286ff inside the REX-prefixed CMP R8 instruction at 0x1286fe, so the relay compares EAX (the actor pointer low DWORD). A negative low DWORD permits traversal past 255; every returned fault reaches actor index 451, beyond the entire 8 MiB arena.','fix':'Patch the complete instruction at 0x1286fe; preserve CMP R8, 256; reject capacity control patches that begin inside an original instruction.','multiplayer_asset_load':'Returned logs confirm the original MP texture/sound initialization succeeded; all feedback toggles were off during these crashes.'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('archive',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();r=analyze(a.archive);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
