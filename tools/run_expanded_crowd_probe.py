"""Reproduce renderer exhaustion and the returned collision fault, then gate Test29."""
import argparse,gc,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from audit_actor_capacity import EXACT_SHA256
from run_expanded_iterator_probe import patches,fixture,HEADER
from prepare_expanded_crowd import RELAY_SITES,UNWINDS,native_unwind,RELAY_RANGES
from prepare_expanded_bridge import hook_specs,READERS,CODE_BASE,UNWIND_BASE,SLOT
from capstone.x86_const import X86_OP_MEM,X86_REG_RIP,X86_REG_RSP

OLD_COMMIT='86a24a95d73cc4f7c5dfc6714f07b9585359c61a'

def audit_unwinds(pe,rows):
    def read(at,n):
        for a,_,after,_ in rows:
            if a<=at and at+n<=a+len(after):return after[at-a:at-a+n]
        raise ValueError(f'Missing crowd relay data at {at:x}')
    dec=Cs(CS_ARCH_X86,CS_MODE_64)
    for site,at in zip(RELAY_SITES,UNWINDS):
        expected=native_unwind(pe,site)
        if read(at,len(expected))!=expected:raise ValueError('Relay unwind differs from native frame')
    # Source code only uses scratch GPRs and the same nonvolatile outputs as
    # the replaced blocks. None of these body relays changes RSP or calls out.
    specs=hook_specs(pe)
    relay_ranges=(*RELAY_RANGES,*((CODE_BASE+n*SLOT,CODE_BASE+(n+1)*SLOT,UNWIND_BASE+n*0x80) for n in range(len(specs))))
    for a,z,_ in relay_ranges:
        code=b''.join(after[max(a,p)-p:min(z,p+len(after))-p] for p,_,after,_ in rows if p<z and a<p+len(after))
        for i in dec.disasm(code,a):
            if i.mnemonic in ('push','pop','call') or ('rsp' in i.op_str and i.mnemonic in ('add','sub','lea','mov') and i.op_str.startswith('rsp,')):
                raise ValueError('Body relay changes native frame')
    host_refs=[f[2] for _,_,_,fixes in rows for f in fixes if f[2]&0x80000000]
    if set(host_refs)!={0x8410fd00,0x8458fb00}:raise ValueError('Renderer host ownership fixups are missing')
    for n,spec in enumerate(specs):
        expected=native_unwind(pe,spec['at'])
        if read(UNWIND_BASE+n*0x80,len(expected))!=expected:raise ValueError('Surface bridge unwind differs from native frame')
    return {'standalone_unwind_entries':7+len(specs),'native_frame_descriptions_match':True,
            'body_relays_keep_rsp':True,'explicit_native_host_target_rvas':['0x410fd00','0x458fb00'],
            'host_globals_are_not_relocated_into_actor_arena':True,'windows_RtlVirtualUnwind_executed':False}

def probe_cases(pe,path):
    dec=Cs(CS_ARCH_X86,CS_MODE_64);dec.detail=True
    regs=['rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi']+[f'r{n}' for n in range(8,16)]
    cases=[]
    for at in READERS:
        i=next(dec.disasm(pe.get_data(at,16),at));m=next(o.mem for o in i.operands if o.type==X86_OP_MEM)
        cases.append((at,at+i.size,regs.index(dec.reg_name(m.base)),regs.index(dec.reg_name(m.index)),-1,0,0x8d5))
    cases += [(0x3c9e,0x3cad,6,5,6,1,0),(0x3e77,0x3e88,14,5,14,1,0),
              (0x64a71,0x64a8a,13,1,7,2,0x8d5),(0x64dd6,0x64de1,13,1,7,1,0),
              (0x65016,0x65027,15,14,6,1,0x8d5),
              (0x419a,0x41b8,7,-1,-1,3,0x8c5),(0x438d,0x43a4,7,-1,-1,4,0x8c5)]
    s=['struct BridgeCase {unsigned at,end;int base,index,destination,form;unsigned flags;};',
       f'constexpr std::array<BridgeCase,{len(cases)}> bridgeCases{{{{']
    s += ['{%s},'%','.join(hex(v) if v>=0 else str(v) for v in row) for row in cases]
    s+=['}};',''];path.write_text('\n'.join(s))

def bridge_coverage(pe):
    dec=Cs(CS_ARCH_X86,CS_MODE_64);dec.detail=True;dec.skipdata=True;base=pe.OPTIONAL_HEADER.ImageBase
    expected=set(READERS)|{0x3c9e,0x3e77,0x41a7,0x4393,0x12cfb,0x17330,0x1735a,0x532c5,0x64a58,0x64dd6,0x65016}
    found=set()
    for section in pe.sections:
        if not section.Characteristics&0x20000000:continue
        for i in dec.disasm(section.get_data(),base+section.VirtualAddress):
            if not i.id:continue
            if any(o.type==X86_OP_MEM and o.mem.base!=X86_REG_RIP and 0x42980<=o.mem.disp<=0x429fc for o in i.operands) or any(o.type==2 and o.imm in (0x2980,0x42980) for o in i.operands):found.add(i.address-base)
    if found!=expected:raise ValueError('Unreviewed surface representation reference: '+str([hex(v) for v in found^expected]))
    # The lookup scratch is the native outgoing home area. No original locals
    # in these active fragments occupy it; no relay changes RSP or calls out.
    for spec in hook_specs(pe):
        if spec['at']==0x532e2:continue
        e=next(e.struct for e in pe.DIRECTORY_ENTRY_EXCEPTION if e.struct.BeginAddress<=spec['at']<e.struct.EndAddress)
        prolog=pe.get_data(e.UnwindData,4)[1]
        for i in dec.disasm(pe.get_data(e.BeginAddress,e.EndAddress-e.BeginAddress),base+e.BeginAddress):
            if i.address-base<e.BeginAddress+prolog:continue
            if i.id and any(o.type==X86_OP_MEM and o.mem.base==X86_REG_RSP and 0<=o.mem.disp<32 for o in i.operands):raise ValueError('Native surface fragment uses the planned outgoing scratch area')
    return {'native_direct_surface_representation_sites':len(found),'all_declared_or_bridged':True,
            'original_home_area_free_in_reader_fragments':True,'packed_draw_format_bits':16,
            'native_rigid_ids':32768,'animated_handles':32768}

def run(executable):
    raw=executable.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=EXACT_SHA256:raise ValueError('Unsupported executable')
    pe=pefile.PE(data=raw,fast_load=True);rows=patches(HEADER.read_text());unwinds=audit_unwinds(pe,rows);coverage=bridge_coverage(pe)
    old_header=subprocess.check_output(['git','show',OLD_COMMIT+':include/expanded_capacity_patches.hpp'],cwd=HEADER.parents[1],text=True)
    with tempfile.TemporaryDirectory(prefix='bo1mw2-crowd-probe-') as name:
        root=Path(name);old=root/'test28.bin';new=root/'test29.bin'
        old.write_bytes(fixture(pe,patches(old_header)));new.write_bytes(fixture(pe,rows));probe_cases(pe,root/'crowd_probe_cases.hpp');del pe,raw;gc.collect()
        program=root/'crowd'
        subprocess.run(['g++','-std=c++17','-O2','-pthread','-I',str(HEADER.parent),'-I',str(root),str(HEADER.parents[1]/'tools/expanded_crowd_probe.cpp'),str(HEADER.parents[1]/'tools/crowd_probe_shims.S'),'-o',str(program)],check=True)
        crash=subprocess.run([str(program),str(old),'old-nan'],capture_output=True,text=True,timeout=20)
        if crash.returncode!=71 or 'rva=0x1dd323' not in crash.stderr:
            raise ValueError(f'Returned collision read did not reproduce at the same instruction ({crash.returncode}): '+crash.stderr)
        baseline=subprocess.run([str(program),str(old),'old-render'],capture_output=True,text=True,timeout=20)
        if baseline.returncode:raise ValueError('Stock renderer reproduction failed: '+baseline.stderr)
        good=subprocess.run([str(program),str(new),'fixed',str(old)],capture_output=True,text=True,timeout=60)
        if good.returncode:raise ValueError(f'Crowd guard fixture failed ({good.returncode}): '+good.stderr)
    return {'executable_sha256':EXACT_SHA256,'previous_revision':OLD_COMMIT,
            'returned_nonfinite_glass_fault_reproduced_at':'0x1dd323',
            'stock_renderer_results':json.loads(baseline.stdout),'corrected_native_results':json.loads(good.stdout),
            'unwind_audit':unwinds,'surface_representation_audit':coverage,
            'substitutions':['D3D CreateVertexBuffer records length/flags; no GPU calls',
                             'native CPU reserve uses guarded mmap in the fixture',
                             'allocation continuation wrappers isolate native reservations from asset/world callbacks',
                             'finite-descriptor pass-through gate stops before world contacts'],
            'scope':'Exact Windows-ABI native renderer allocation blocks, renderer initialization requests and glass narrowphase failure execute on Linux with an externally relocated patch arena. Real Windows rendering, mission gameplay, physics integration and RtlVirtualUnwind remain owner tests.'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=run(a.executable);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
