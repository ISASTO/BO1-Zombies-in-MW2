"""Identify Test23's actor-history and native shooting-buffer faults.
Only derived fault evidence is exported; private memory and game code stay local.
"""
import argparse,hashlib,json,struct,zipfile
from pathlib import Path
from analyze_iterator_crash import Dump

STAMPS=('141752','141842','141922','142002')
def analyze(path):
    results=[]
    with zipfile.ZipFile(path) as archive:
        receipt=json.loads(archive.read('install.json').decode('utf-8-sig'))
        for name in archive.namelist():
            if not name.endswith('.dmp') or not any('20261013-'+s in name for s in STAMPS):continue
            d=Dump(archive.read(name));r=d.registers;rva=r['rip']-d.base
            if d.access!=0 or d.fault!=0xe8 or r['rcx']!=0:raise ValueError('Different fault context')
            item={'dump':name.replace('\\','/').split('/')[-1],
                  'instruction_rva':hex(rva),'access':'read','fault_address':'0xe8',
                  'null_actor_entity':True,'exception_context_source':'exception stream'}
            if rva==0x232e68:
                item.update(path='unrolled frame-history writer',
                            entity_pointer_displacement='0x1560',
                            required_displacement='0x48c0',
                            captured_pointer_load=d.read(d.base+0x232e61,7).hex(),
                            old_actor_cursor_step='0xab00',required_cursor_step='0x24600')
            elif rva==0x1a187e:
                if r['rdi']!=r['rsi']*0x48c0:raise ValueError('Fault does not match expanded traversal')
                item.update(path='native shot rewind',actor_index=r['rsi'],
                            stack_valid_flags=32,actor_capacity=256,
                            read_beyond_valid_flags=r['rsi']>=32,
                            actor_byte_offset=hex(r['rdi']),
                            installed_source_loop_span=hex(256*0x48c0),
                            loop_span_evidence='matching Test23 source; this instruction is outside captured fault bytes',
                            required_loop_span=hex(32*0x48c0))
            else:raise ValueError('Different crash path')
            results.append(item)
    if len(results)!=4:raise ValueError('Expected four matching new dumps')
    return {'archive_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'installed_version':receipt['version'],'installed_dll_sha256':receipt['dll_sha256'],
            'executable_sha256':receipt['executable_sha256'],'crashes':results,
            'diagnosis':'Unconverted unrolled actor displacements in the 32-slot frame history; three shooting rewind/restore scans were widened to 256 without widening their 32-slot stack records.',
            'fix':'Use expanded actor stride for every history writer/validator address and keep native shooting scans bounded to their actual 32-slot records. Live actor capacity remains 256.',
            'friendly_pose_assessment':'History validation and excess restore scans can read/write the wrong poses. This is a concrete cause of pose corruption; corrected visual behavior requires a retail retest.',
            'mp_assets':'All four logs confirm original MP image/sound loaded. Hitmarkers and other weapon toggles were off.'}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('archive',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=analyze(a.archive);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
