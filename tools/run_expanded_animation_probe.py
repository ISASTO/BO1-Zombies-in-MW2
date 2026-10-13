"""Reproduce Test24 animation relocation faults and test corrected ownership.
Native animation assets/renderer remain owner tests; no game files are shipped.
"""
import argparse,gc,hashlib,json,subprocess,tempfile
from pathlib import Path
import pefile
from audit_actor_capacity import EXACT_SHA256
from run_expanded_iterator_probe import HEADER,patches,fixture
OLD_COMMIT='0ec3c88ad34d62c7516183ff5684906adc42a333'
def run(executable):
    raw=executable.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=EXACT_SHA256:raise ValueError('Unsupported executable')
    pe=pefile.PE(data=raw,fast_load=True);tools=Path(__file__).resolve().parent
    previous=subprocess.check_output(['git','show',OLD_COMMIT+':include/expanded_capacity_patches.hpp'],cwd=tools.parent).decode()
    with tempfile.TemporaryDirectory(prefix='bo1mw2-animation-') as temp:
        root=Path(temp);old=root/'old.bin';new=root/'new.bin'
        image=fixture(pe,patches(previous));old.write_bytes(image);del image;gc.collect()
        image=fixture(pe,patches(HEADER.read_text()));new.write_bytes(image);del image,pe,raw;gc.collect()
        binary=root/'animation';subprocess.run(['g++','-std=c++17','-O2','-I',str(tools.parent/'include'),str(tools/'expanded_animation_probe.cpp'),'-o',str(binary)],check=True)
        failures={}
        for mode,expected in (('old-leases','TWO_LIVE_LEASES_SHARE_ONE_TREE'),('old-end','CORPSE_SEARCH_ACCEPTED_ONE_PAST_END'),('old-swap','ACTOR_59_REUSED_PACKED_CORPSE_DATA_AS_TREE')):
            p=subprocess.run([str(binary),str(old),mode],capture_output=True,text=True,timeout=15)
            if p.returncode!=1 or expected not in p.stderr:raise ValueError('Old fault did not reproduce: '+p.stderr)
            failures[mode]=expected
        p=subprocess.run([str(binary),str(new),'fixed'],capture_output=True,text=True,timeout=20)
        if p.returncode:raise ValueError('Corrected animation ownership failed: '+p.stderr)
        passed=json.loads(p.stdout)
    return {'executable_sha256':EXACT_SHA256,'old_revision':OLD_COMMIT,'old_faults_reproduced':failures,
            'corrected_native_results':passed,'runtime_plan_external_arena':True,
            'substituted_dependencies':['XAnim tree-node reset (real assets needed); allocator/release native loops run intact','corpse search stops before booted-world pose callbacks','tree exchange stops before script notifications'],
            'scope':'Exact native addressing, allocation ownership and bounded lookup, using distinct tree tokens. Real skeleton evaluation/rendering and whole crash-chain reproduction remain retail tests.'}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=run(a.executable);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
