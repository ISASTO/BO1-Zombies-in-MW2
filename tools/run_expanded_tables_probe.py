"""Reproduce the Test32 compass overrun and anim-info exhaustion; verify Test33."""
import argparse,gc,hashlib,json,subprocess,tempfile
from pathlib import Path
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from audit_actor_capacity import EXACT_SHA256
from run_expanded_iterator_probe import patches,fixture,HEADER
from prepare_expanded_compass import RELAYS,frame_unwind
OLD_COMMIT='52c7856e74a2143a175bb5f31d355de858d3c916'

def audit(pe,rows):
    def read(at,n):
        for a,_,after,_ in rows:
            if a<=at and at+n<=a+len(after):return after[at-a:at-a+n]
        raise ValueError('Missing compass relay fragment')
    decoder=Cs(CS_ARCH_X86,CS_MODE_64)
    for begin,end,unwind,site in RELAYS:
        expected=frame_unwind(pe,site)
        if read(unwind,len(expected))!=expected:raise ValueError('Compass unwind differs from native frame')
        for i in decoder.disasm(read(begin,end-begin),begin):
            if i.mnemonic=='nop':break
            # The slot relay calls the native leaf 0x12c6a0 in place of the
            # original call; nothing else may change the stack.
            if i.mnemonic in ('push','pop') or i.op_str.startswith('rsp,'):raise ValueError('Compass relay changes native frame')
            if i.mnemonic=='call' and not (site==0x92ded and i.op_str=='0x12c6a0'):raise ValueError('Unexpected compass relay call')
    return {'standalone_unwind_entries':len(RELAYS),'native_frame_descriptions_match':True,'windows_RtlVirtualUnwind_executed':False}

def run(executable):
    raw=executable.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=EXACT_SHA256:raise ValueError('Unsupported executable')
    pe=pefile.PE(data=raw,fast_load=True);rows=patches(HEADER.read_text());unwind=audit(pe,rows)
    before=subprocess.check_output(['git','show',OLD_COMMIT+':include/expanded_capacity_patches.hpp'],cwd=HEADER.parents[1],text=True)
    with tempfile.TemporaryDirectory(prefix='bo1mw2-tables-probe-') as directory:
        root=Path(directory);old=root/'test32.bin';fixed=root/'test33.bin';program=root/'tables'
        old.write_bytes(fixture(pe,patches(before)));fixed.write_bytes(fixture(pe,rows));del pe,raw;gc.collect()
        subprocess.run(['g++','-std=c++17','-O2','-I',str(HEADER.parent),str(HEADER.parents[1]/'tools/expanded_tables_probe.cpp'),'-o',str(program)],check=True)
        p=subprocess.run([str(program),str(old),'old'],capture_output=True,text=True,timeout=120)
        if p.returncode:raise ValueError('Test32 failures did not reproduce: '+p.stdout+p.stderr)
        old_result=json.loads(p.stdout)
        p=subprocess.run([str(program),str(fixed),'fixed'],capture_output=True,text=True,timeout=300)
        if p.returncode:raise ValueError(f'Test33 table regression failed ({p.returncode}): '+p.stderr)
    return {'executable_sha256':EXACT_SHA256,'previous_revision':OLD_COMMIT,'test32_reproduction':old_result,
            'corrected_native_results':json.loads(p.stdout),'unwind_audit':unwind,
            'substitutions':['visibility test 0xa9740 returns false','engine error 0x1fb5f0 records the message','dvar registration 0x21b2d0 is a no-op'],
            'scope':'Exact native compass entity update/writer and XAnim anim-info pool init/allocation on Linux. No Windows mission, rendering or real animation trees.',
            'returned_evidence':'Test32 hardware watch: writes at rip 0x92d49..0x92d68 into command fields; engine error "exceeded maximum number of anim info" at ~160 (Estate) and ~100 (Contingency) actors.'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();r=run(a.executable);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
