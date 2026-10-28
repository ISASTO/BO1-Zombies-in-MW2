"""Verify and execute the isolated exact-native Test44 asset-pool probe.

Runs the retail x64 DB_Init on a private mapped image after the loaded_sound
pool was enlarged as src/asset_pools.cpp does it, then the retail pool
allocator and free routine on it (see tools/asset_pool_probe.cpp).
"""
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

# Routines the probe runs unmodified...
EXECUTED = {
    0xeff10: '48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57',  # DB_Init
    0xeec00: '4c 63 d2 4c 8d 41 08 49 ff ca 4c 89 01',           # loaded_sound pool init
    0xeee20: '48 8b 01 48 85 c0 74 07 48 8b 10 48 89 11 c3',     # pool alloc
    0xee6c0: '48 8b 01 48 89 11 48 89 02 c3',                    # pool free
}
# ...and the one it replaces.
REPLACED = {0x203bb0: None}   # database thread start


def run(executable, compiler='g++'):
    if platform.system() != 'Linux' or platform.machine().lower() not in ('amd64', 'x86_64'):
        raise ValueError('This probe requires Linux/x86-64')
    data = executable.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXACT_SHA256:
        raise ValueError('Exact executable SHA256 mismatch')
    pe = pefile.PE(data=data)
    for rva, expected in EXECUTED.items():
        want = bytes.fromhex(expected)
        if pe.get_data(rva, len(want)) != want:
            raise ValueError(f'Native routine at {rva:#x} differs')
    # DB_Init calls the database thread start once, at 0xeff9f.
    if pe.get_data(0xeff9f, 1) != b'\xe8' or 0xeff9f + 5 + int.from_bytes(pe.get_data(0xeffa0, 4), 'little', signed=True) != 0x203bb0:
        raise ValueError('DB_Init database thread call differs')
    compiler_path = shutil.which(compiler)
    if not compiler_path:
        raise ValueError('Host compiler unavailable')
    tools = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='bo1mw2-asset-pool-') as directory:
        root = Path(directory)
        image = root / 'private-mapped-image.bin'
        program = root / 'asset-pool-probe'
        image.write_bytes(pe.get_memory_mapped_image())
        subprocess.run([compiler_path, '-std=c++17', '-O2', '-I', str(tools.parent / 'include'), str(tools / 'asset_pool_probe.cpp'),
                        '-o', str(program)], check=True)
        result = subprocess.run([str(program), str(image)], capture_output=True, text=True, timeout=300)
        if result.returncode:
            raise ValueError(f'Native asset-pool probe failed ({result.returncode}): {result.stderr.strip()}')
        report = json.loads(result.stdout)
        report['executable_sha256'] = EXACT_SHA256
        report['executed_routines'] = sorted(hex(r) for r in EXECUTED)
        report['replaced_routines'] = sorted(hex(r) for r in REPLACED)
        return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = run(args.executable)
    rendered = json.dumps(report, indent=2) + '\n'
    args.output.write_text(rendered)
    print(rendered, end='')
