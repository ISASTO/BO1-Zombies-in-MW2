"""Prepare a private exact-hash image and run the isolated x86-64 capacity probe."""
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


def run(executable,compiler='g++'):
    if platform.machine().lower() not in ('amd64','x86_64') or platform.system()!='Linux':
        raise ValueError('This isolated probe requires Linux/x86-64')
    data=executable.read_bytes()
    if hashlib.sha256(data).hexdigest()!=EXACT_SHA256:raise ValueError('Exact executable SHA256 mismatch')
    pe=pefile.PE(data=data)
    # The only native calls executed by the allocator are memset and the small
    # defaults initializer. Validate both call sites before the harness runs.
    def call(at):
        raw=pe.get_data(at,5)
        if raw[0]!=0xe8:raise ValueError('Native probe call opcode mismatch')
        import struct
        return at+5+struct.unpack_from('<i',raw,1)[0]
    if call(0x1239b0)!=0x2e42d0 or call(0x1239ec)!=0x153590 or call(0x123c3a)!=0x144d30:
        raise ValueError('Native allocator dependencies changed')
    if pe.get_data(0x153590,18)!=bytes.fromhex('c7 41 18 01 00 00 00 c7 81 c0 00 00 00 01 00 00 00 c3'):
        raise ValueError('Native actor defaults initializer changed')
    compiler_path=shutil.which(compiler)
    if not compiler_path:raise ValueError('Host C++ compiler unavailable')
    with tempfile.TemporaryDirectory(prefix='bo1mw2-capacity-') as directory:
        root=Path(directory);image=root/'private-mapped-image.bin';program=root/'capacity-probe'
        image.write_bytes(pe.get_memory_mapped_image())
        subprocess.run([compiler_path,'-std=c++17','-O2',str(Path(__file__).with_name('capacity_probe.cpp')),'-o',str(program)],check=True)
        result=subprocess.run([str(program),str(image)],capture_output=True,text=True,timeout=30)
        if result.returncode:raise ValueError(f'Native probe failed ({result.returncode}): {result.stderr.strip()}')
        report=json.loads(result.stdout);report['executable_sha256']=EXACT_SHA256
        report['substituted_dependencies']=['memset MS-ABI REP STOSB stub','iteration predicate records visits and returns false; native callbacks skipped','perception callback replaced by jump to the original epilogue']
        return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();result=run(args.executable)
    rendered=json.dumps(result,indent=2)+'\n';args.output.write_text(rendered);print(rendered,end='')
