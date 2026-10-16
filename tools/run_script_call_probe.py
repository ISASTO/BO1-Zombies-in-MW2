"""Verify and execute the isolated exact-native builtin-call regression (Test38)."""
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

# First bytes of every native routine the probe executes or the DLL binds.
EXPECTED = {
    0x222140: '40 53 48 83 ec 20 8b d9 e8',      # Scr_AddInt
    0x222250: '48 83 ec 38 0f 29 74 24 20',      # Scr_AddFloat
    0x222300: '40 53 48 83 ec 20 48 8b d9 e8',   # Scr_AddString
    0x2223d0: '40 53 48 83 ec 20 48 8b d9 e8',   # Scr_AddVector
    0x2221a0: '40 53 48 83 ec 20 e8',            # Scr_AddEntityNum
    0x222380: '48 83 ec 28 e8',                  # Scr_AddUndefined
    0x221ff0: '48 83 ec 28 83 3d',               # IncInParam
    0x222ee0: '48 83 ec 28 3b 0d',               # Scr_GetFloat
    0x223180: '8b 05',                           # Scr_GetNumParam
    0x223970: '48 89 5c 24 08',                  # Scr_Notify (zero object cleanup)
    0x189d50: '48 83 ec 38 b9 01 00 00 00',      # min
    0x189d80: '48 83 ec 38 b9 01 00 00 00',      # max
    0x185910: '8b 0d',                           # gettime
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
    with tempfile.TemporaryDirectory(prefix='bo1mw2-script-call-') as directory:
        root=Path(directory);image=root/'private-mapped-image.bin';program=root/'script-call-probe'
        image.write_bytes(pe.get_memory_mapped_image())
        subprocess.run([compiler_path,'-std=c++17','-O2','-I',str(tools.parent/'include'),str(tools/'script_call_probe.cpp'),'-o',str(program)],check=True)
        result=subprocess.run([str(program),str(image)],capture_output=True,text=True,timeout=60)
        if result.returncode:raise ValueError(f'Native builtin-call probe failed ({result.returncode}): {result.stderr.strip()}')
        report=json.loads(result.stdout);report['executable_sha256']=EXACT_SHA256
        return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();report=run(args.executable)
    rendered=json.dumps(report,indent=2)+'\n';args.output.write_text(rendered);print(rendered,end='')
