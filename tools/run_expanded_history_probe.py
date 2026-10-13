"""Reproduce Test23's two crash paths and test native history/shot restoration.
The runtime patch header is applied outside the image, as in the installed DLL.
"""
import argparse,gc,hashlib,json,subprocess,tempfile
from pathlib import Path
import pefile
from audit_actor_capacity import EXACT_SHA256
from run_expanded_iterator_probe import HEADER,patches,fixture
OLD_COMMIT='3801b16dfd94b1873718da2f405f7fcf65df266d'

def run(executable):
    data=executable.read_bytes()
    if hashlib.sha256(data).hexdigest()!=EXACT_SHA256:raise ValueError('Unsupported executable')
    pe=pefile.PE(data=data,fast_load=True);tools=Path(__file__).resolve().parent
    old_text=subprocess.check_output(['git','show',OLD_COMMIT+':include/expanded_capacity_patches.hpp'],cwd=tools.parent).decode()
    with tempfile.TemporaryDirectory(prefix='bo1mw2-history-') as directory:
        root=Path(directory);bad=root/'test23.bin';good=root/'test24.bin'
        old=fixture(pe,patches(old_text));bad.write_bytes(old);del old;gc.collect()
        fixed=fixture(pe,patches(HEADER.read_text()));good.write_bytes(fixed);del fixed,pe,data;gc.collect()
        program=root/'history';subprocess.run(['g++','-std=c++17','-O2','-I',str(tools.parent/'include'),str(tools/'expanded_history_probe.cpp'),'-o',str(program)],check=True)
        failures={}
        for mode,expected in (('old-writer','rva=0x232e68'),('old-shoot','rva=0x1a187e')):
            result=subprocess.run([str(program),str(bad),mode],capture_output=True,text=True,timeout=15)
            if result.returncode!=71 or expected not in result.stderr:raise ValueError(f'{mode} did not reproduce returned fault: {result.stderr}')
            failures[mode]={'reproduced':True,'fault_instruction':expected.split('=')[1]}
        result=subprocess.run([str(program),str(good),'fixed'],capture_output=True,text=True,timeout=20)
        if result.returncode:raise ValueError('Corrected native history regression failed: '+result.stderr)
        passed=json.loads(result.stdout)
    return {'executable_sha256':EXACT_SHA256,'old_revision':OLD_COMMIT,'old_crashes':failures,
            'native_results':passed,'external_runtime_arena':True,
            'substituted_dependencies':['entity relinking recorder (requires a booted world)','MS-ABI memset transport'],
            'old_shoot_isolation':'Only old-shoot uses a correct 32-slot validation callback to isolate the independent excessive loop bound. Fixed tests run the real validator and interpolation.',
            'retail_gameplay_tested':False,
            'capacity_scope':'256 live actors; native lag history and shot scratch remain 32 slots. Higher actors retain current server poses.'}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=run(a.executable);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
