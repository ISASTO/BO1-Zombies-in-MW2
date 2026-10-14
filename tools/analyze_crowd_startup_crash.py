"""Verify the returned Test29 renderer startup regression using numeric evidence.

Keep private paths, raw dump memory and heap addresses out of published output.
"""
import argparse,hashlib,json,struct,zipfile
from pathlib import Path
from analyze_iterator_crash import Dump

EXPECTED_DLL='0b921eda6158832c2cff0c07d1301c0ea344a1c1efa88b7a3dba2181d334518d'
FRAME=0x458fb00
STRIDE=0x13d700
POOL=0x42980

def analyze(path):
    reports=[]
    with zipfile.ZipFile(path) as archive:
        names={n.replace('\\','/'):n for n in archive.namelist()}
        receipt=json.loads(archive.read(names['install.json']).decode('utf-8-sig'))
        if receipt['version']!='0.1.28-squad-test29' or receipt['dll_sha256']!=EXPECTED_DLL:
            raise ValueError('Different installed build')
        for stamp in ('153218','153243'):
            name='game-logs/crash-20261014-'+stamp+'.dmp'
            raw=archive.read(names[name]);dump=Dump(raw);r=dump.registers
            if r['rip']-dump.base!=0x645bd or dump.fault!=0x10 or r['rbx']!=0:
                raise ValueError('Different native CPU renderer fault')
            state=dump.read(dump.base+0x57e0000,0x400)
            match=None
            for frame in range(2):
                old_origin=dump.base+FRAME+frame*STRIDE+POOL
                delta=r['rsi']-old_origin
                if delta>=0 and delta%4==0 and 0x8000<=delta//4<=0xffff:
                    match=(frame,delta//4);break
            if match is None:raise ValueError('Faulting pointer is not a packed animated ID applied to the stock pool')
            frame,record_id=match
            surfaces,used,peak,reverse,table=struct.unpack_from('<QIIQQ',state,0x40+frame*32)
            count=struct.unpack_from('<I',state,0x9c+frame*4)[0]
            if record_id-0x8000>=count:raise ValueError('The handle was not published')
            mapped=struct.unpack('<Q',dump.read(table+record_id*8,8))[0]
            if not surfaces<=mapped<surfaces+used:raise ValueError('The correct mapping escapes its active record storage')
            marker=struct.unpack('<i',dump.read(mapped,4))[0]
            mesh=struct.unpack('<Q',dump.read(mapped+0x20,8))[0]
            reverse_id=struct.unpack('<H',dump.read(reverse+(mapped-surfaces)//2,2))[0]
            if mesh==0 or reverse_id!=record_id-0x8000:raise ValueError('The captured animated mapping is not coherent')
            log=archive.read(names[name[:-4]+'.log']).decode(errors='replace')
            txt=archive.read(names[name[:-4]+'.txt']).decode(errors='replace')
            reports.append({'dump':Path(name).name,'dump_sha256':hashlib.sha256(raw).hexdigest(),
                            'fault_instruction_rva':'0x645bd','read_address':'0x10','null_mesh_pointer':True,
                            'faulting_record_id':hex(record_id),'native_frame':frame,
                            'old_pointer_offset_from_native_pool':record_id*4,
                            'owned_record_offset':mapped-surfaces,'owned_frame_used_bytes':used,
                            'published_handles':count,'correct_record_marker':marker,
                            'correct_record_has_mesh_pointer':True,'reverse_and_forward_mapping_agree':True,
                            'mod_spawn_completed_before_crash':'AI_SPAWN_ACCEPT' in log or 'operation=SPAWN_END' in txt,
                            'native_engine_error_present':'native_engine_error_depth=' in txt})
    return {'archive_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'installed_version':receipt['version'],'installed_dll_sha256':receipt['dll_sha256'],
            'executable_sha256':receipt['executable_sha256'],'crashes':reports,
            'diagnosis':'The CPU renderer adds the stock surface origin as 0x10a60 quarter units before multiplying by four. Test29 did not bridge this representation. It applies animated handles 0x8000/0x8001 to the old pool, obtains a null mesh, and faults reading mesh+0x10. The captured forward/reverse mappings already contain the valid owned records.',
            'expanded_inventory':'Six quarter-unit consumers occur at 0x5dfa2, 0x5e2da, 0x5e3a9, 0x5e5a5, 0x643d3 and 0x648cf. Test30 bridges their initial/continuing record reads and retains native field outputs and ADD flags.',
            'scope':'Both returned faults are the same Test29 startup regression. This evidence does not establish that the prior crowded-renderer flicker or upstream invalid-glass physics is fully resolved.'}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();result=analyze(args.archive)
    args.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
