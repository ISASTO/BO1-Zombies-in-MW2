"""Public numeric evidence for the two returned Test25 high-population dumps.
Reads local owner files. Does not publish dump memory, game assets or paths.
"""
import argparse,hashlib,json,re,struct,zipfile
from pathlib import Path
from analyze_iterator_crash import Dump

def analyze(archive):
    reports=[]
    with zipfile.ZipFile(archive) as z:
        names={name.replace('\\','/'):name for name in z.namelist()}
        receipt=json.loads(z.read(names['install.json']).decode('utf-8-sig'))
        if receipt['version']!='0.1.24-squad-test25':raise ValueError('Different installed build')
        for stamp in ('20261013-154917','20261013-155323'):
            normalized=next(name for name in names if name.endswith('crash-'+stamp+'.dmp'))
            d=Dump(z.read(names[normalized]));txt=z.read(names[normalized[:-4]+'.txt']).decode('utf-8-sig')
            totals=re.findall(r'operation=SPAWN_END .*?actors=(\d+)',txt)
            entry={'dump':normalized.split('/')[-1],'exception':hex(d.code),'fault_instruction_rva':hex(d.registers['rip']-d.base),
                   'last_completed_spawn_actor_count':int(totals[-1])}
            if stamp.endswith('154917'):
                error=re.search(r'native_engine_error_depth=1 message=([^\r\n]+)',txt)
                if not error or error[1]!='BG_EvaluateTrajectory: unknown trType: -1111617280':raise ValueError('Primary trajectory error differs')
                rsp=d.registers['rsp'];available=next(begin+size-rsp for begin,size,_ in d.segments if begin<=rsp<begin+size)
                stack=d.read(rsp,min(available,0x2000));ret=struct.pack('<Q',d.base+0x739b2);position=stack.find(ret)
                if position<8:raise ValueError('Native trajectory caller absent')
                trajectory=struct.unpack_from('<Q',stack,position-8)[0];entity=(trajectory-d.base-0xf0-0x441070)//0x1a8
                if entity!=1202:raise ValueError('Different trajectory record')
                entry.update({'primary_engine_error':error[1],'invalid_trajectory_type_bits':'0xbdbe1100',
                              'same_bits_as_float':struct.unpack('<f',struct.pack('<I',0xbdbe1100))[0],
                              'client_entity_number':entity,'client_trajectory_rva':hex(trajectory-d.base),
                              'fault_is_during_engine_error_cleanup':True})
            else:
                r=d.registers
                if r['rip']-d.base!=0x10047c or r['rcx']-d.base!=0xcdb118 or r['r9']!=r['rbx']*32 or d.fault!=r['rax']+r['r9']:
                    raise ValueError('Different native allocator fault')
                entry.update({'allocator_pool_rva':'0xcdb118','corrupt_free_head':hex(r['rbx']),
                              'same_bits_as_float':struct.unpack('<f',struct.pack('<I',r['rbx']))[0],
                              'allocator_address_arithmetic_matches_fault':True})
            reports.append(entry)
    return {'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'installed_version':receipt['version'],
            'installed_dll_sha256':receipt['dll_sha256'],'executable_sha256':receipt['executable_sha256'],'matching_new_crashes':reports,
            'independently_reproduced_defect':'The expanded opposing-team iterator can feed more than 50 entries to a native 50-row threat stack array. Candidate 51 overwrites saved XMM registers; subsequent rows overlap saved GPRs and caller control data.',
            'causal_scope':'The returned faults show memory corruption. The exact native fixture reproduces this definite overwrite and checks its repair, but does not reproduce either complete Windows crash chain. Mission/renderer stability remains a retail retest.'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('archive',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=analyze(a.archive);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
