"""Reproduce Test27 crowd-resource failures and execute guarded replacements."""
import argparse, gc, hashlib, json, struct, subprocess, tempfile
from pathlib import Path
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_RIP
from audit_actor_capacity import EXACT_SHA256
from run_expanded_iterator_probe import patches, fixture, HEADER
from prepare_expanded_resources import CLIENT_BYTES, SERVER_BYTES, POINTERS

OLD_COMMIT='749839366a722513b799f6cafe3e5bf894639852'


def reference_inventory(pe):
    """Find direct references into the old data, excluding following globals."""
    regions=((0x548d00,0x5d8cf0),(0x192da30,0x19ada20))
    contains=lambda at:any(begin<=at<end for begin,end in regions)
    decoder=Cs(CS_ARCH_X86,CS_MODE_64);decoder.detail=True;decoder.skipdata=True
    base=pe.OPTIONAL_HEADER.ImageBase;rips=[];absolute=[]
    for section in pe.sections:
        if not section.Characteristics&0x20000000:continue
        for i in decoder.disasm(section.get_data(),base+section.VirtualAddress):
            if not i.id:continue
            for o in i.operands:
                if o.type==X86_OP_MEM and o.mem.base==X86_REG_RIP and contains(i.address+i.size+o.mem.disp-base):rips.append(i.address-base)
                if o.type==X86_OP_IMM and contains(o.imm-base):absolute.append(i.address-base)
    expected=[0xd4a27,0x230efa,0x230f9d,0x2317d8,0x231911]
    if rips!=expected or absolute:raise ValueError('Additional direct skeleton-array consumer requires review')
    pe.parse_data_directories(directories=[5]);static=[]
    for block in getattr(pe,'DIRECTORY_ENTRY_BASERELOC',[]):
        for entry in block.entries:
            if entry.type==10 and contains(struct.unpack('<Q',pe.get_data(entry.rva,8))[0]-base):static.append(entry.rva)
    if static:raise ValueError('Static relocated pointer still refers to stock skeleton storage')
    return {'old_buffer_direct_rip_sites':[hex(at) for at in rips],
            'old_buffer_absolute_instruction_references':0,'old_buffer_static_dir64_references':0,
            'following_native_pointer_and_generation_globals':'preserved'}


def unwind_audit(pe,rows):
    def read(at,size):
        blob=bytearray(pe.get_data(at,size)) if at<0x4000000 else bytearray(size)
        for a,_,after,_ in rows:
            lo=max(a,at);hi=min(a+len(after),at+size)
            if lo<hi:blob[lo-at:hi-at]=after[lo-a:hi-a]
        if len(blob)!=size:raise ValueError('Truncated resource fragment')
        return bytes(blob)
    decoder=Cs(CS_ARCH_X86,CS_MODE_64);decoder.detail=True
    for begin,end,unwind,frame,saves,pushes in (
        (0x4611000,0x4611300,0x4611300,16,{7:0x30,6:0x28,5:0x20,3:0x18},[15,14]),
        (0x4611400,0x4611500,0x4611500,40,{6:0x38,3:0x30},[7])):
        raw=read(unwind,24 if len(saves)==4 else 16)
        if raw[:4]!=bytes((1,0,10 if len(saves)==4 else 6,0)):raise ValueError('Glass relay unwind header differs')
        slot=0;actual_saves={};actual_pushes=[];allocation=0
        while slot<raw[2]:
            offset,encoded=raw[4+slot*2:6+slot*2];op=encoded&15;info=encoded>>4
            if offset:raise ValueError('Glass relay must describe already-executed prologue')
            if op==4:actual_saves[info]=struct.unpack_from('<H',raw,6+slot*2)[0]*8;slot+=2
            elif op==2:allocation+=info*8+8;slot+=1
            elif op==0:actual_pushes.append(info);slot+=1
            else:raise ValueError('Unexpected glass unwind operation')
        if actual_saves!=saves or actual_pushes!=pushes or allocation+8*len(pushes)!=frame:
            raise ValueError('Glass relay does not describe the native caller frame')
        # Restore actual saved-register slots and stack adjustment. No stack
        # instructions occur in the relay, so every reachable PC has this state.
        rsp=0x100000-frame;stack={rsp+offset:reg+100 for reg,offset in saves.items()}
        for n,reg in enumerate(pushes):stack[rsp+allocation+n*8]=reg+100
        restored={reg:stack[rsp+offset] for reg,offset in actual_saves.items()};rsp+=allocation
        for reg in actual_pushes:restored[reg]=stack[rsp];rsp+=8
        if rsp!=0x100000 or any(value!=reg+100 for reg,value in restored.items()):raise ValueError('Glass unwind simulation failed')
        code=list(decoder.disasm(read(begin,end-begin),begin))
        if sum(i.size for i in code)!=end-begin or any(i.mnemonic in ('push','pop','call') or 'rsp' in i.op_str for i in code):
            raise ValueError('Glass guard changes its native frame or contains undecodable bytes')
    # Actual native prologues, before each relay entry, match these save slots.
    expected={0x100410:'48895c240848896c2410488974241848897c242041564157',
              0x100ed0:'48895c24084889742410574883ec20'}
    for at,hexed in expected.items():
        if pe.get_data(at,len(bytes.fromhex(hexed)))!=bytes.fromhex(hexed):raise ValueError('Native glass prologue differs from registered unwind')
    # All five reset paths must fetch owned buffers through relocatable cells.
    for at,target in ((0xd4a27,POINTERS),(0x230efa,POINTERS+8),(0x230f9d,POINTERS+8),(0x2317d8,POINTERS+8),(0x231911,POINTERS+8)):
        i=next(decoder.disasm(read(at,7),at))
        if i.mnemonic!='mov' or i.operands[1].type!=X86_OP_MEM or i.operands[1].mem.base!=X86_REG_RIP or at+i.size+i.disp!=target:
            raise ValueError('A skeleton reset still selects stock storage')
    for at,size in ((0xd56e3,CLIENT_BYTES),(0xd575d,CLIENT_BYTES),(0x230eed,SERVER_BYTES),(0x230f32,SERVER_BYTES)):
        if next(decoder.disasm(read(at,8),at)).operands[-1].imm!=size-16:raise ValueError('Skeleton buffer/bound mismatch')
    return {'standalone_glass_unwind_entries':2,'native_saved_registers_checked':9,
            'native_prologues_match_unwind':True,'all_skeleton_base_sites':5,'all_skeleton_bound_sites':4,
            'windows_rtl_virtual_unwind_executed':False}


def run(executable):
    raw=executable.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=EXACT_SHA256:raise ValueError('Exact executable hash differs')
    pe=pefile.PE(data=raw,fast_load=True)
    old_header=subprocess.check_output(['git','show',OLD_COMMIT+':include/expanded_capacity_patches.hpp'],cwd=HEADER.parents[1]).decode()
    current=patches(HEADER.read_text());unwind=unwind_audit(pe,current);references=reference_inventory(pe)
    with tempfile.TemporaryDirectory(prefix='bo1mw2-resources-') as name:
        root=Path(name);old=root/'test27.bin';new=root/'test28.bin'
        image=fixture(pe,patches(old_header));old.write_bytes(image);del image;gc.collect()
        image=fixture(pe,current);new.write_bytes(image);del image,pe,raw;gc.collect()
        binary=root/'resources'
        subprocess.run(['g++','-std=c++17','-O2','-I',str(HEADER.parent),str(HEADER.parents[1]/'tools/expanded_resources_probe.cpp'),'-o',str(binary)],check=True)
        bad=subprocess.run([str(binary),str(old),'old-head'],text=True,capture_output=True,timeout=20)
        if bad.returncode!=71 or 'rva=0x10047c' not in bad.stderr:raise ValueError('Returned native glass fault did not reproduce: '+bad.stderr)
        duplicate=subprocess.run([str(binary),str(old),'old-double-free'],text=True,capture_output=True,timeout=20)
        if duplicate.returncode!=1 or 'DOUBLE_FREE_REISSUES_ONE_LIVE_PIECE' not in duplicate.stderr:raise ValueError('Native double-free corruption did not reproduce: '+duplicate.stderr)
        baselines={}
        for mode in ('old-client','old-server'):
            p=subprocess.run([str(binary),str(old),mode],text=True,capture_output=True,timeout=20)
            if p.returncode:raise ValueError('Stock skeleton budget reproduction failed: '+p.stderr)
            baselines[mode]=json.loads(p.stdout)
        p=subprocess.run([str(binary),str(new),'fixed'],text=True,capture_output=True,timeout=60)
        if p.returncode:raise ValueError(f'Expanded native resource probe failed ({p.returncode}): '+p.stderr)
        native=json.loads(p.stdout)
    return {'executable_sha256':EXACT_SHA256,'old_revision':OLD_COMMIT,
            'returned_glass_free_head':'0x3f33787f','returned_native_fault_reproduced':'0x10047c',
            'native_double_free_reproduces_live_piece_alias':True,'stock_skeleton_baselines':baselines,
            'runtime_plan_external_arena':True,'corrected_native_results':native,'unwind_audit':unwind,'skeleton_reference_inventory':references,
            'substituted_dependencies':['released-piece physics/render broadphase callback; allocation/free bodies and occupancy bookkeeping execute natively'],
            'scope':'Actual client/server skeleton allocations, generation wrap, bounds, glass allocation/free and free-list repair in Linux MS-ABI fixtures. No Windows missions or real skeleton assets/rendering. The first retail glass-chain corruption is not captured by the Test27 dump.'}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();result=run(a.executable);a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
