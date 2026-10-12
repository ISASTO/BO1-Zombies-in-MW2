"""Verify and execute an isolated exact-native script parameter regression."""
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
        raise ValueError('This probe requires Linux/x86-64')
    data=executable.read_bytes()
    if hashlib.sha256(data).hexdigest()!=EXACT_SHA256:
        raise ValueError('Exact executable SHA256 mismatch')
    pe=pefile.PE(data=data)
    if pe.get_data(0x223970,5)!=bytes.fromhex('48 89 5c 24 08') or pe.get_data(0x21dc30,7)!=bytes.fromhex('ff c9 83 f9 04 73 3b'):
        raise ValueError('Native notification/reference cleanup differs')
    compiler_path=shutil.which(compiler)
    if not compiler_path:raise ValueError('Host C++ compiler unavailable')
    tools=Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='bo1mw2-script-parameters-') as directory:
        root=Path(directory);image=root/'private-mapped-image.bin';program=root/'script-parameters-probe'
        image.write_bytes(pe.get_memory_mapped_image())
        subprocess.run([compiler_path,'-std=c++17','-O2','-I',str(tools.parent/'include'),str(tools/'script_parameters_probe.cpp'),'-o',str(program)],check=True)
        result=subprocess.run([str(program),str(image)],capture_output=True,text=True,timeout=30)
        if result.returncode:raise ValueError(f'Native script probe failed ({result.returncode}): {result.stderr.strip()}')
        report=json.loads(result.stdout);report['executable_sha256']=EXACT_SHA256
        return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();report=run(args.executable)
    rendered=json.dumps(report,indent=2)+'\n';args.output.write_text(rendered);print(rendered,end='')
