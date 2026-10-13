"""Run the exact-retail held-shot gate without booting or distributing the game."""
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


def run(executable):
    if platform.system() != 'Linux' or platform.machine().lower() not in ('amd64', 'x86_64'):
        raise ValueError('Linux/x86-64 required')
    data = executable.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXACT_SHA256:
        raise ValueError('Exact executable SHA256 mismatch')
    pe = pefile.PE(data=data)
    code = pe.get_data(0x83f90, 0x73)
    if code[:7] != bytes.fromhex('44 8b 4a 64 4c 8b d1') or code[-1] != 0xc3:
        raise ValueError('Native mode gate changed')
    tools = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='bo1mw2-fire-mode-') as directory:
        root = Path(directory)
        fixture, program = root/'native-gate.bin', root/'probe'
        fixture.write_bytes(code)
        compiler = shutil.which('g++')
        if not compiler:
            raise ValueError('Host C++ compiler unavailable')
        subprocess.run([compiler, '-std=c++17', '-O2', '-I', str(tools.parent/'include'),
                        str(tools/'fire_mode_probe.cpp'), '-o', str(program)], check=True)
        result = subprocess.run([str(program), str(fixture)], capture_output=True, text=True, timeout=10)
        if result.returncode:
            raise ValueError(f'Native firing probe failed ({result.returncode}): {result.stderr.strip()}')
        report = json.loads(result.stdout)
    report['executable_sha256'] = EXACT_SHA256
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = run(args.executable)
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
