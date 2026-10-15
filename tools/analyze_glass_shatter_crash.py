"""Recover the Test31 glass shatter state from returned crash dumps.

Both glass dumps unwind to client shatter (0x100870 -> 0x103a80) with the
native found flag clear and the never-initialized piece index still in place.
Only dump-derived values are written; no game memory is included.
"""
import argparse,hashlib,json,struct,zipfile
from pathlib import Path
from analyze_iterator_crash import Dump

FRACTURE_RETURN=0x1009f0;SHATTER_RETURN=0xa2209
# Fault-time distance from RSP to the 0x103a80 body frame, from the native
# return addresses already on each returned stack (0x103e2d / 0x103f1b).
FRACTURE_BODY={0x10e903:0xa0,0x104469:0x440}

def q(dump,address):return struct.unpack('<Q',dump.read(address,8))[0]

def glass(dump):
    raw=dump.read(dump.base+0xcdb118,0xa8);u=lambda o:struct.unpack_from('<I',raw,o)[0]
    count,places=u(0xc),u(0x14);bitmap=dump.read(struct.unpack_from('<Q',raw,0x58)[0],u(0x10)*4)
    states=dump.read(struct.unpack_from('<Q',raw,0x40)[0],count*32)
    active=[n for n in range(count) if (struct.unpack_from('<I',bitmap,(n>>5)*4)[0]>>(31-(n&31)))&1]
    owners={struct.unpack_from('<H',states,n*32+0xc)[0] for n in active}
    return {'pieces':count,'places':places,'active_pieces':len(active),'free_head':u(0x20)},owners

def analyze(dump):
    rip=dump.registers['rip']-dump.base
    if rip not in FRACTURE_BODY:raise ValueError(f'Different crash path {rip:#x}')
    body=dump.registers['rsp']+FRACTURE_BODY[rip];entry=body+0x5620+0x38
    if q(dump,entry)-dump.base!=FRACTURE_RETURN:raise ValueError('Fracture was not called by the shatter lookup')
    lookup=entry+8;outer=lookup+0x50+5*8
    if q(dump,outer)-dump.base!=SHATTER_RETURN:raise ValueError('Shatter lookup was not called by client place state')
    place=q(dump,entry-0x10)&0xffffffff
    found=dump.read(lookup+0x40,1)[0];index=struct.unpack('<I',dump.read(lookup+0x3c,4))[0]
    summary,owners=glass(dump)
    return {'fault_rva':hex(rip),'shatter_place':place,'found_flag':found,'uninitialized_piece_index':hex(index),
            'place_has_active_piece':place in owners,'place_within_loaded_places':place<summary['places'],'client_glass':summary}

def run(archives):
    results={}
    for path in archives:
        with zipfile.ZipFile(path) as archive:
            receipt=json.loads(archive.read('install.json').decode('utf-8-sig'))
            for name in archive.namelist():
                if not name.endswith('.dmp'):continue
                stamp=name.replace('\\','/').split('/')[-1][6:-4]
                if stamp in results:continue
                data=archive.read(name);dump=Dump(data)
                try:result=analyze(dump)
                except (ValueError,TypeError,struct.error):continue
                result.update({'dump_sha256':hashlib.sha256(data).hexdigest(),'installed_version':receipt['version'],'installed_dll_sha256':receipt['dll_sha256']})
                results[stamp]=result
    return {'archives':[{'name':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in archives],'shatter_crashes':results,
            'diagnosis':'Client glass state 2 reached 0x100870 for a place with no active piece. Stock code ignores its found flag and calls native fracture with an uninitialized stack index, so fracture runs on an inactive or foreign piece record.',
            'fix':'Test32 skips native fracture when the lookup found no owning piece and records the place.'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('archives',type=Path,nargs='+');p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=run(a.archives);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
