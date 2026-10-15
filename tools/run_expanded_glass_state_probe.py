"""Reproduce Test31 glass shatter/damage failures and verify Test32 guards."""
import argparse,gc,hashlib,json,subprocess,tempfile
from pathlib import Path
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from audit_actor_capacity import EXACT_SHA256
from run_expanded_iterator_probe import patches,fixture,HEADER
from prepare_expanded_glass_state import RELAYS,EMPTY_UNWIND
from prepare_expanded_crowd import native_unwind
OLD_COMMIT='ddeecf116454286f21775104e1e70ca5e1d1a5ec'

def audit(pe,rows):
    def read(at,n):
        for a,_,after,_ in rows:
            if a<=at and at+n<=a+len(after):return after[at-a:at-a+n]
        raise ValueError('Missing glass state relay fragment')
    decoder=Cs(CS_ARCH_X86,CS_MODE_64)
    for begin,end,unwind,site in RELAYS:
        expected=native_unwind(pe,site) if site else EMPTY_UNWIND
        if read(unwind,len(expected))!=expected:raise ValueError('Glass state unwind differs from native frame')
        for i in decoder.disasm(read(begin,end-begin),begin):
            if i.mnemonic=='nop':break
            if i.mnemonic in ('push','pop','call') or i.op_str.startswith('rsp,'):raise ValueError('Glass state relay changes native frame')
    return {'standalone_unwind_entries':len(RELAYS),'native_frame_descriptions_match':True,
            'relays_keep_rsp':True,'windows_RtlVirtualUnwind_executed':False}

def run(executable):
    raw=executable.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=EXACT_SHA256:raise ValueError('Unsupported executable')
    pe=pefile.PE(data=raw,fast_load=True);rows=patches(HEADER.read_text());unwind=audit(pe,rows)
    before=subprocess.check_output(['git','show',OLD_COMMIT+':include/expanded_capacity_patches.hpp'],cwd=HEADER.parents[1],text=True)
    with tempfile.TemporaryDirectory(prefix='bo1mw2-glass-state-probe-') as directory:
        root=Path(directory);old=root/'test31.bin';fixed=root/'test32.bin';program=root/'glass-state'
        old.write_bytes(fixture(pe,patches(before)));fixed.write_bytes(fixture(pe,rows));del pe,raw;gc.collect()
        subprocess.run(['g++','-std=c++17','-O2','-I',str(HEADER.parent),str(HEADER.parents[1]/'tools/expanded_glass_state_probe.cpp'),'-o',str(program)],check=True)
        p=subprocess.run([str(program),str(old),'old-missing'],capture_output=True,text=True,timeout=20)
        if p.returncode:raise ValueError('Stock missing-piece fracture did not reproduce: '+p.stderr)
        old_missing=json.loads(p.stdout)
        p=subprocess.run([str(program),str(old),'old-damage'],capture_output=True,text=True,timeout=20)
        if p.returncode!=71 or 'rva=0x16a0a0' not in p.stderr:raise ValueError('Stock out-of-range place access did not reproduce: '+p.stderr)
        old_damage={'guard_fault_rva':'0x16a0a0','native_access':'unbounded place-record read; the read-modify-write store follows at 0x16a0e8'}
        p=subprocess.run([str(program),str(fixed),'fixed'],capture_output=True,text=True,timeout=60)
        if p.returncode:raise ValueError(f'Native glass state regression failed ({p.returncode}): '+p.stderr)
    return {'executable_sha256':EXACT_SHA256,'previous_revision':OLD_COMMIT,
            'returned_crashes':{'20261015-055729':{'fault_rva':'0x10e903','place':10,'found_flag':0,'uninitialized_index':'0x7ff7'},
                                '20261015-060313':{'fault_rva':'0x104469','place':27,'found_flag':0,'uninitialized_index':'0x0','estate_places':27}},
            'old_missing_piece_fracture':old_missing,'old_out_of_range_damage':old_damage,
            'corrected_native_results':json.loads(p.stdout),'unwind_audit':unwind,
            'substitutions':['fracture 0x103a80, effects 0x1013c0, state publication 0x16a380 and release tail 0x10b4f0 are MS-ABI recorders',
                             'client and server glass records are fixture allocations with a guard page after the 12-byte server place records'],
            'scope':'Exact native shatter lookup, server place damage and guarded release code run on Linux. No Windows mission, physics, rendering or real glass topology is exercised.',
            'causal_limit':'This proves the stock lookup fractures an uninitialized index when no piece owns the place, matching both returned dumps. It does not establish why the place had no active piece; Test32 records ignored releases and rejected damage to capture that upstream cause.'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();r=run(a.executable);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
