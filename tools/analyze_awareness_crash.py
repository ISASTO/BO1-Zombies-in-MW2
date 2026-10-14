"""Public numeric evidence from the returned Test26 high-population crashes."""
import argparse
import hashlib
import json
import re
import struct
import zipfile
from pathlib import Path
from analyze_iterator_crash import Dump


def analyze(archive):
    reports=[]
    with zipfile.ZipFile(archive) as source:
        names={name.replace('\\','/'):name for name in source.namelist()}
        receipt=json.loads(source.read(names['install.json']).decode('utf-8-sig'))
        if receipt['version']!='0.1.25-squad-test26' or receipt['dll_sha256']!='db32a3c55f2563d6efe79e4ae0f519386cbed7f2ba4678e796482b442e860f90':
            raise ValueError('Returned build does not match Test26')
        for stamp in ('20261014-062001','20261014-062156'):
            name=next(n for n in names if n.endswith('crash-'+stamp+'.dmp'))
            dump=Dump(source.read(names[name]))
            text=source.read(names[name[:-4]+'.txt']).decode('utf-8-sig')
            log=source.read(names[name[:-4]+'.log']).decode('utf-8-sig')
            error=re.search(r'native_engine_error_depth=1 message=BG_EvaluateTrajectory: unknown trType: (-?\d+)',text)
            arena=int(re.search(r'AI_CAPACITY_EXPANSION enabled=1 .*?arena=([0-9a-fA-F]+)',log)[1],16)
            if not error:raise ValueError('Different primary error')
            bits=int(error[1])&0xffffffff
            # Both values are the low DWORD of an address inside the relocated
            # sentient pool, rather than a valid trajectory enum or float.
            sentients=arena+0x500000
            index=(bits-(sentients&0xffffffff))//0x88
            pointer=sentients+index*0x88
            if not 0<=index<274 or pointer&0xffffffff!=bits:
                raise ValueError('Corruption does not resolve to a sentient')
            _,context=struct.unpack_from('<II',dump.data,dump.streams[6][0]+160)
            xmm8=struct.unpack_from('<Q',dump.data,context+0x220)[0]
            if stamp.endswith('062156') and xmm8!=pointer:
                raise ValueError('Contingency XMM8 evidence differs')
            totals=re.findall(r'operation=SPAWN_END .*?actors=(\d+)',text)
            if dump.registers['rip']-dump.base!=0x1ef903:
                raise ValueError('Different secondary exception')
            reports.append({'dump':Path(name).name,'dump_sha256':hashlib.sha256(source.read(names[name])).hexdigest(),
                'last_completed_spawn_actor_count':int(totals[-1]),'secondary_exception_rva':'0x1ef903',
                'primary_error':'BG_EvaluateTrajectory: unknown trType','invalid_trajectory_bits':hex(bits),
                'same_bits_resolve_to_expanded_sentient_index':index,
                'xmm8_low_qword_matches_sentient_pointer':xmm8==pointer,
                'xmm8_low_dword_matches_invalid_trajectory_type':xmm8&0xffffffff==bits})
    return {'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),
            'installed_version':receipt['version'],'installed_dll_sha256':receipt['dll_sha256'],
            'executable_sha256':receipt['executable_sha256'],'new_crashes':reports,
            'independent_native_reproduction':'The second 50-row sentient scratch array at native 0x152100 receives expanded iterator output. Candidate 51 aliases saved XMM8. Native qsort can reorder that row; the restored register contains a sentient pointer.',
            'scope':'Contingency preserves that pointer in XMM8 and its low DWORD as the primary trajectory error. The fixture reproduces the definite overwrite; it does not replay the complete Windows mission or final client-state write.'}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    result=analyze(args.archive)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
