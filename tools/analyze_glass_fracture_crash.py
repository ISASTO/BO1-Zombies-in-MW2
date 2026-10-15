"""Verify numeric evidence for the returned Test30 glass stack overwrite."""
import argparse,hashlib,json,struct,zipfile
from pathlib import Path
from analyze_iterator_crash import Dump
EXPECTED_DLL='c1853d110773377a96d3aab81d76ab13abd1f87224b4542a9639557be1bbbe37'

def analyze(path):
    with zipfile.ZipFile(path) as archive:
        names={n.replace('\\','/'):n for n in archive.namelist()}
        receipt=json.loads(archive.read(names['install.json']).decode('utf-8-sig'))
        if receipt['version']!='0.1.29-squad-test30' or receipt['dll_sha256']!=EXPECTED_DLL:raise ValueError('Different installed build')
        name='game-logs/crash-20261014-161533.dmp';raw=archive.read(names[name]);d=Dump(raw);r=d.registers
        if r['rip']-d.base!=0x1095e5 or d.code!=0xc0000005:raise ValueError('Different native fault')
        # RET has popped the three saved registers, but not its return address.
        # The piece context is the original body RSP+0x20, body RSP=RET RSP-0x808.
        context=r['rsp']-0x808+0x20
        returned=d.read(r['rsp'],8);pattern=bytes((255,8,3));span=d.read(r['rsp'],0x400)
        repeated=next((i for i,b in enumerate(span) if b!=pattern[i%3]),len(span))
        if returned!=bytes.fromhex('ff0803ff0803ff08') or repeated<512:raise ValueError('Repeated fragment overwrite is absent')
        geometry=struct.unpack('<I',d.read(context+0x430,4))[0]
        fragments=struct.unpack('<I',d.read(context+0x42c,4))[0]
        if geometry!=416 or fragments!=393:raise ValueError('Captured geometry extent differs')
        state=d.read(d.base+0x57e0000,0x400)
        gpu,cpu,surface,bad=struct.unpack_from('<IIII',state,0x80)
        handles,mapping=struct.unpack_from('<II',state,0xa4)
        physics=struct.unpack_from('<II',state,0x200)
        text=archive.read(names[name[:-4]+'.txt']).decode(errors='replace')
        if 'actors=97' not in text:raise ValueError('Expected final population is absent')
        saved={reg:hex(r[reg]) for reg in ('rbx','rbp','rsi','rdi','r14','r15')}
    return {'archive_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'installed_version':receipt['version'],'installed_dll_sha256':receipt['dll_sha256'],
            'executable_sha256':receipt['executable_sha256'],'dump':Path(name).name,
            'dump_sha256':hashlib.sha256(raw).hexdigest(),'fault_instruction_rva':'0x1095e5',
            'fault_instruction':'ret','final_total_actors':97,'corrupted_return_bytes':returned.hex(' '),
            'saved_register_fragment_patterns':saved,'consecutive_repeated_fragment_bytes_at_return':repeated,
            'piece_geometry_dwords':geometry,'native_piece_geometry_limit_dwords':231,
            'captured_generated_fragment_dwords':fragments,'return_slot_bytes_after_geometry_bound':24,
            'render_failures':{'gpu':gpu,'cpu':cpu,'surface':surface,'bad_frames':bad,'handle_exhaustion':handles,'invalid_mapping':mapping},
            'prior_physics_guards':{'nonfinite_queries':physics[0],'invalid_features':physics[1]},
            'diagnosis':'The native glass-fragment builder emitted repeated two-vertex records beyond its 231-DWORD local piece geometry buffer. Saved nonvolatile registers and the return address contain those records; the eventual RET raises a noncanonical-address exception. The captured geometry count is 416 DWORDs.',
            'correction':'Bound every native polygon output write; reject results with fewer than three vertices; include terminator/alignment in capacity checks; skip native piece allocation/publication on rejection.',
            'causal_limit':'The dump proves the output overrun, but does not capture the first geometry update that produced the malformed edge topology. Zero renderer failures support separation from this glass crash; absence of flickering is the owner observation.'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('archive',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();r=analyze(a.archive);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
