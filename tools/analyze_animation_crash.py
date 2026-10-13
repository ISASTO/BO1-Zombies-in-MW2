"""Summarize Test24 retail faults without exporting private dump memory."""
import argparse,hashlib,json,re,zipfile
from pathlib import Path
from analyze_iterator_crash import Dump
def analyze(path):
    reports=[]
    with zipfile.ZipFile(path) as z:
        receipt=json.loads(z.read('install.json').decode('utf-8-sig'))
        for stamp in ('151502','151740','151906'):
            name='game-logs\\crash-20261013-'+stamp+'.dmp';d=Dump(z.read(name));r=d.registers;rva=r['rip']-d.base
            txt=z.read(name[:-4]+'.txt').decode(errors='replace');log=z.read(name[:-4]+'.log').decode(errors='replace').splitlines()
            last_state=next((l for l in reversed(log) if 'AI_STATE' in l),'');m=re.search(r'actor_count=(\d+)',last_state)
            spawned=re.findall(r'operation=SPAWN_END entity=\d+ actors=(\d+)',txt)
            row={'dump':name.split('\\')[-1],'fault_instruction_rva':hex(rva),
                 'last_heartbeat_actors':int(m[1]) if m else None,'latest_spawn_actor_count':int(spawned[-1]) if spawned else None,
                 'exception_context_source':'exception stream'}
            if rva==0x2957be:
                tree=r['rbp'];row.update(path='native DObj creation',animation_tree_pointer=hex(tree),
                    pointer_is_noncanonical=(tree>>48)!= (0xffff if (tree>>47)&1 else 0),
                    faulting_field='tree+8',low_word_entity_number=tree&0xffff,
                    diagnosis='Packed data was supplied as an animation-tree pointer. The unconverted high-slot actor/corpse exchange can produce this exact class of invalid pointer.')
            elif rva in (0x1ef8f0,0x1ef903):
                row.update(path='command removal during engine error/shutdown',
                    corrupt_command_link=True,
                    diagnosis='The command-list traversal encounters noncanonical data. Its triggering engine error and complete earlier corruption chain are not preserved by these exception contexts.')
            else:raise ValueError('Different fault path')
            reports.append(row)
    return {'archive_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'installed_version':receipt['version'],
            'installed_dll_sha256':receipt['dll_sha256'],'executable_sha256':receipt['executable_sha256'],'crashes':reports,
            'screenshots':'All three show severely distorted NPC skeletons; the player weapon and environment appear intact.',
            'confirmed_patch_defects':['Final scratch-tree pointer lookup still reads the original ring cursor; two concurrent leases return the same mutable tree.','Three one-past-end corpse-array markers were incorrectly moved to the temporary-tree arena.','Actor-to-corpse tree exchange still indexes the original 32-slot array, overlapping corpse records at higher actor indices.'],
            'fix_validation':'animation-test25-probe.json reproduces all three native addressing/ownership defects and tests their corrections. It does not evaluate real skeleton assets or reproduce the whole engine error/shutdown crash chain.'}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('archive',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=analyze(a.archive);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
