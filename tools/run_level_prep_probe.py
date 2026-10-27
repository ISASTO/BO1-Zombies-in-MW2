"""Verify and execute the isolated exact-native Test39 level-preparation probe."""
import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import tempfile
from pathlib import Path
import pefile
from audit_actor_capacity import EXACT_SHA256

# First bytes of every native routine the probe executes.
EXPECTED = {
    0xef500: '48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18',   # DB_FindXAssetEntry
    0xdb6d0: '48 63 01 48 8d 15',                               # DB_GetXAssetName
    0x272610: '4c 8b d9 4c 8b c2 4c 2b da',                     # I_stricmp
    0x2f39c4: '48 83 ec 28 83 3d',                              # tolower
    0x29c920: '48 8b c1 8b ca 48 ff c1',                        # XAnimGetLength
    0x29d110: '8b c2 48 03 c0 66 83 7c c1 08 00',               # XAnimIsLeafNode
    0x20b0d0: '8b c1 48 8d 0d',                                 # animtree by index
    0x20b0e0: '48 63 05 5d 88 23 01',                           # animtree index
    0x222070: '40 53 48 83 ec 20 48 8b d9 e8',                  # Scr_AddAnim
    0x222840: '48 89 5c 24 10 55 56 41 56',                     # Scr_GetAnim
    0x2968e0: '48 8b 01 c3',                                    # XAnimTree anims
    0x221ff0: '48 83 ec 28 83 3d',                              # IncInParam
    0x223970: '48 89 5c 24 08',                                 # Scr_Notify (zero object cleanup)
}


def run(executable, compiler='g++'):
    if platform.system()!='Linux' or platform.machine().lower() not in ('amd64','x86_64'):
        raise ValueError('This probe requires Linux/x86-64')
    data=executable.read_bytes()
    if hashlib.sha256(data).hexdigest()!=EXACT_SHA256:
        raise ValueError('Exact executable SHA256 mismatch')
    pe=pefile.PE(data=data)
    for rva,expected in EXPECTED.items():
        want=bytes.fromhex(expected)
        if pe.get_data(rva,len(want))!=want:
            raise ValueError(f'Native routine at {rva:#x} differs')
    compiler_path=shutil.which(compiler)
    if not compiler_path:raise ValueError('Host C++ compiler unavailable')
    tools=Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='bo1mw2-level-prep-') as directory:
        root=Path(directory);image=root/'private-mapped-image.bin';program=root/'level-prep-probe'
        image.write_bytes(pe.get_memory_mapped_image())
        subprocess.run([compiler_path,'-std=c++17','-O2','-I',str(tools.parent/'include'),str(tools/'level_prep_probe.cpp'),'-o',str(program)],check=True)
        result=subprocess.run([str(program),str(image)],capture_output=True,text=True,timeout=60)
        if result.returncode:raise ValueError(f'Native level-preparation probe failed ({result.returncode}): {result.stderr.strip()}')
        report=json.loads(result.stdout);report['executable_sha256']=EXACT_SHA256
        return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();report=run(args.executable)
    rendered=json.dumps(report,indent=2)+'\n';args.output.write_text(rendered);print(rendered,end='')
