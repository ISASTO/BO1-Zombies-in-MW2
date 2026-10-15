"""Reproduce Test30 native glass output overruns and verify bounded Test31 paths."""
import argparse,gc,hashlib,json,subprocess,tempfile
from pathlib import Path
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from audit_actor_capacity import EXACT_SHA256
from run_expanded_iterator_probe import patches,fixture,HEADER
from prepare_expanded_fracture import RANGES
from prepare_expanded_crowd import native_unwind
OLD_COMMIT='202b64b0f1dc7f2efcf764c7cb10b3eedbecca9d'

def audit(pe,rows):
    def read(at,n):
        for a,_,after,_ in rows:
            if a<=at and at+n<=a+len(after):return after[at-a:at-a+n]
        raise ValueError('Missing glass-fragment relay fragment')
    decoder=Cs(CS_ARCH_X86,CS_MODE_64)
    for begin,end,unwind,site in RANGES:
        expected=native_unwind(pe,site)
        if read(unwind,len(expected))!=expected:raise ValueError('Glass-fragment unwind differs from native frame')
        for i in decoder.disasm(read(begin,end-begin),begin):
            if i.mnemonic in ('push','pop','call') or i.op_str.startswith('rsp,'):raise ValueError('Glass-fragment relay changes native frame')
    return {'standalone_unwind_entries':len(RANGES),'native_frame_descriptions_match':True,
            'body_relays_keep_rsp':True,'windows_RtlVirtualUnwind_executed':False}

def run(executable):
    raw=executable.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=EXACT_SHA256:raise ValueError('Unsupported executable')
    pe=pefile.PE(data=raw,fast_load=True);rows=patches(HEADER.read_text());unwind=audit(pe,rows)
    before=subprocess.check_output(['git','show',OLD_COMMIT+':include/expanded_capacity_patches.hpp'],cwd=HEADER.parents[1],text=True)
    with tempfile.TemporaryDirectory(prefix='bo1mw2-fracture-probe-') as directory:
        root=Path(directory);old=root/'test30.bin';fixed=root/'test31.bin';program=root/'fracture'
        old.write_bytes(fixture(pe,patches(before)));fixed.write_bytes(fixture(pe,rows));del pe,raw;gc.collect()
        subprocess.run(['g++','-std=c++17','-O2','-I',str(HEADER.parent),str(HEADER.parents[1]/'tools/expanded_fracture_probe.cpp'),'-o',str(program)],check=True)
        failures={}
        for mode in ('old-degenerate','old-full'):
            p=subprocess.run([str(program),str(old),mode],capture_output=True,text=True,timeout=20)
            if p.returncode!=71 or not any('rva='+at in p.stderr for at in ('0x109e9c','0x10ab15')):raise ValueError('Native output overrun did not reproduce: '+p.stderr)
            failures[mode]={'guard_fault_rva':p.stderr.split('rva=')[1].split()[0],'exact_native_writer':True}
        p=subprocess.run([str(program),str(fixed),'fixed',str(old)],capture_output=True,text=True,timeout=60)
        if p.returncode:raise ValueError(f'Native fracture regression failed ({p.returncode}): '+p.stderr)
    return {'executable_sha256':EXACT_SHA256,'previous_revision':OLD_COMMIT,
            'returned_crash_rva':'0x1095e5','returned_crash_is_corrupted_ret':True,
            'old_native_overruns':failures,'corrected_native_results':json.loads(p.stdout),'unwind_audit':unwind,
            'substitutions':['CRT memset/memmove use MS-ABI wrappers','stack probe is a fixture return; native frame sizes stay unchanged',
                             'native producer boundary helper supplies controlled geometry','native publication callback records valid pieces; no world/asset allocation'],
            'scope':'Exact native polygon generation, edge bookkeeping and producer publication decision run on Linux with a guard at the original 924-byte output boundary. Valid polygons compare byte-for-byte with Test30. No Windows mission or actual glass physics/rendering is exercised.',
            'causal_limit':'The fixture reproduces the two-vertex output overrun mechanism at its native writer. The returned crash occurs later at RET after stack corruption; the first malformed topology update is not captured.'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();r=run(a.executable);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
