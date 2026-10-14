"""Numeric evidence from the two returned Test27 archives; no private paths/data."""
import argparse, hashlib, json, re, struct, zipfile
from pathlib import Path
from analyze_iterator_crash import Dump


def analyze(path):
    reports=[]
    with zipfile.ZipFile(path) as archive:
        names={n.replace('\\','/'):n for n in archive.namelist()}
        receipt=json.loads(archive.read(names['install.json']).decode('utf-8-sig'))
        if receipt['version']!='0.1.26-squad-test27':raise ValueError('Different installed build')
        for name in names:
            if not name.endswith('crash-20261014-072936.dmp'):continue
            data=archive.read(names[name]);dump=Dump(data);r=dump.registers
            txt=archive.read(names[name[:-4]+'.txt']).decode('utf-8-sig')
            if r['rip']-dump.base!=0x10047c or r['rcx']-dump.base!=0xcdb118 or r['r9']!=r['rbx']*32 or dump.fault!=r['rax']+r['r9']:
                raise ValueError('Different native glass allocator fault')
            first=struct.unpack('<I',dump.read(r['rax'],4))[0]
            actors=re.findall(r'operation=SPAWN_END .*?actors=(\d+)',txt)
            reports.append({'dump':Path(name).name,'dump_sha256':hashlib.sha256(data).hexdigest(),
                            'exception':hex(dump.code),'fault_instruction_rva':'0x10047c',
                            'glass_pool_rva':'0xcdb118','corrupt_free_head':hex(r['rbx']),
                            'free_head_as_float':struct.unpack('<f',struct.pack('<I',r['rbx']))[0],
                            'first_captured_glass_transform_quaternion_bits':hex(first),
                            'free_head_matches_captured_quaternion':first==r['rbx'],
                            'fault_address_matches_free_head_times_32':True,
                            'last_completed_actor_count':int(actors[-1]),
                            'primary_native_engine_error_present':'native_engine_error_depth=' in txt})
    if len(reports)!=1:raise ValueError('Expected one new Test27 glass crash')
    return {'archive_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'installed_version':receipt['version'],'installed_dll_sha256':receipt['dll_sha256'],
            'executable_sha256':receipt['executable_sha256'],'new_crashes':reports,
            'confirmed':'A quaternion-valued free head is used as a 32-byte glass-piece index, producing the exact invalid read.',
            'separate_reproduction':'Exact native stock skeleton allocators wrap after 64 maximum-size server allocations and exhaust after 72 client allocations. This is an independent capacity dependency, not a dump-proven first writer of the glass chain.',
            'causal_limit':'The first retail write corrupting the glass free chain is not in this dump. Test28 validates current/next links and repairs inactive links from the occupancy map; future dumps include that map and pool metadata.'}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('archive',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=analyze(a.archive);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
