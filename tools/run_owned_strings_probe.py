"""Execute exact-retail shared ownership-label lifetime and pool-reuse regression."""
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


def run(executable, compiler='g++'):
    if platform.system()!='Linux' or platform.machine().lower() not in ('amd64','x86_64'):
        raise ValueError('Linux/x86-64 is required for the private MS-ABI fixture')
    data=executable.read_bytes()
    if hashlib.sha256(data).hexdigest()!=EXACT_SHA256:
        raise ValueError('Exact executable SHA256 mismatch')
    pe=pefile.PE(data=data)
    for at,expected in ((0x21ac8b,'ba 01 00 00 00'),(0x21b000,'85 ed'),
                        (0x21b01e,'85 d5 75 03'),(0x21b022,'f0 ff 07'),
                        (0x21b16e,'85 d5 75 03'),(0x21b172,'f0 ff 07'),
                        (0x21ad91,'f0 41 ff 00'),(0x21a8f6,'b8 ff ff ff ff f0 0f c1 07')):
        expected=bytes.fromhex(expected)
        if pe.get_data(at,len(expected))!=expected:
            raise ValueError(f'Native user-mask/reference instruction differs at {at:x}')
    compiler_path=shutil.which(compiler)
    if not compiler_path:raise ValueError('Host C++ compiler unavailable')
    tools=Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='bo1mw2-owned-labels-') as directory:
        root=Path(directory);image=root/'private-image.bin';program=root/'owned-labels-probe'
        image.write_bytes(pe.get_memory_mapped_image())
        subprocess.run([compiler_path,'-std=c++17','-O2','-I',str(tools.parent/'include'),str(tools/'owned_strings_probe.cpp'),'-o',str(program)],check=True)
        result=subprocess.run([str(program),str(image)],capture_output=True,text=True,timeout=30)
        if result.returncode:raise ValueError(f'Native label probe failed ({result.returncode}): {result.stderr.strip()}')
        report=json.loads(result.stdout);report['executable_sha256']=EXACT_SHA256
        return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();report=run(args.executable)
    rendered=json.dumps(report,indent=2)+'\n';args.output.write_text(rendered);print(rendered,end='')
