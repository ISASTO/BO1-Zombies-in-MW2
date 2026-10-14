"""Reproduce the Test22 retail iterator crash and test corrected runtime bytes."""
import argparse,gc,hashlib,json,re,struct,subprocess,tempfile
from pathlib import Path
import pefile
from audit_actor_capacity import EXACT_SHA256
OLD_COMMIT='15f40d33cb63b1a37b2877161c241641f701b512'
HEADER=Path(__file__).parents[1]/'include/expanded_capacity_patches.hpp'

def patches(text):
    rows=[]
    for rva,before,after,fixups in re.findall(r'^\s+\{0x([0-9a-f]+),"([0-9a-f]*)","([0-9a-f]*)",\{(.*?)\}\},$',text,re.M):
        fixes=[]
        for off,width,target,end,relative in re.findall(r'\{(\d+),(\d+),0x([0-9a-f]+),0x([0-9a-f]+),(true|false)\}',fixups):
            fixes.append((int(off),int(width),int(target,16),int(end,16),relative=='true'))
        rows.append((int(rva,16),bytes.fromhex(before),bytes.fromhex(after),fixes))
    if len(rows)<209 or len({row[0] for row in rows})!=len(rows):raise ValueError('Incomplete or duplicate runtime patch inventory')
    return rows

def fixture(pe,rows):
    image=bytearray(pe.get_memory_mapped_image());image.extend(bytes(0x5800000-len(image)))
    def address(rva):
        if rva&0x80000000:return rva&0x7fffffff
        return rva+0x1000000 if 0x4000000<=rva<0x4800000 else rva
    for rva,before,after,fixups in rows:
        at=address(rva);blob=bytearray(after)
        if before and bytes(image[at:at+len(before)])!=before:raise ValueError(f'Original bytes changed at {rva:x}')
        for off,width,target,end,relative in fixups:
            value=address(target)-(address(end) if relative else end)
            blob[off:off+width]=value.to_bytes(width,'little',signed=True)
        image[at:at+len(blob)]=blob
    return image

def run(executable):
    data=executable.read_bytes()
    if hashlib.sha256(data).hexdigest()!=EXACT_SHA256:raise ValueError('Unsupported executable')
    pe=pefile.PE(data=data,fast_load=True)
    old_text=subprocess.check_output(['git','show',OLD_COMMIT+':include/expanded_capacity_patches.hpp'],cwd=HEADER.parents[1]).decode()
    old=fixture(pe,patches(old_text));fixed=fixture(pe,patches(HEADER.read_text()))
    tools=Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='bo1mw2-iterator-') as directory:
        root=Path(directory);bad=root/'test22.bin';good=root/'test23.bin';bad.write_bytes(old);good.write_bytes(fixed)
        del old,fixed,pe,data;gc.collect()
        program=root/'iterator';subprocess.run(['g++','-std=c++17','-O2','-I',str(tools.parent/'include'),str(tools/'expanded_iterator_probe.cpp'),'-o',str(program)],check=True)
        broken_negative=subprocess.run([str(program),str(bad),'negative'],capture_output=True,text=True,timeout=10)
        if broken_negative.returncode!=71 or 'rva=0x1286e0 actor_index=256' not in broken_negative.stderr:raise ValueError('Original Test22 did not reproduce the guarded native overrun: '+broken_negative.stderr)
        broken_positive=subprocess.run([str(program),str(bad),'positive'],capture_output=True,text=True,timeout=10)
        if broken_positive.returncode!=1 or 'Sparse team iterator lost an expanded actor' not in broken_positive.stderr:raise ValueError('Original positive-pointer failure not reproduced')
        passes=[]
        for sign in ('negative','positive'):
            process=subprocess.run([str(program),str(good),sign],capture_output=True,text=True,timeout=10)
            if process.returncode:raise ValueError('Corrected iterator failed: '+process.stderr)
            passes.append(json.loads(process.stdout))
    return {'executable_sha256':EXACT_SHA256,'old_revision':OLD_COMMIT,'old_negative_pointer_overrun_reproduced':True,'old_overrun_instruction_rva':'0x1286e0','old_guard_page_actor_index':256,'old_positive_pointer_skips_later_actors_reproduced':True,'corrected_guard_instruction_rva':'0x1286fe','old_guard_started_at':'0x1286ff','corrected_native_results':passes,'total_corrected_cases':sum(p['guarded_iterator_cases'] for p in passes),'runtime_plan_external_arena':True,'substituted_engine_callbacks':[],'windows_mission_load_retest':'pending'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();result=run(a.executable);a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
