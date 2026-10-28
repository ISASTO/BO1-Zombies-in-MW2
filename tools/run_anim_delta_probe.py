"""Verify and execute the Test41 exact-native root-motion probe.

Runs the retail x64 XAnimGetRelDelta (0x29cbb0) -> XAnimCalcRelDeltaParts
(0x29b200) on converted BO1 animations (tools/anim_delta_probe.cpp). The
committed report uses synthetic anims covering every root-motion shape the
converter writes, including the BO1 delta flag without a delta part that made
Test40's attacks fault. --bo1-zone adds the owner's local decompressed
common_zombie.ff and samples every BO1 animation the mod uses (never committed).
"""
import argparse
import hashlib
import json
import platform
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
import pefile
from audit_actor_capacity import EXACT_SHA256

ROUTINES = {
    0x29cbb0: '48 89 5c 24 08 57 48 83 ec 60 49 8b',   # XAnimGetRelDelta
    0x29b200: '48 8b c4 53 48 81 ec f0 00 00 00 f6',   # XAnimCalcRelDeltaParts
    0x2a4ac0: '48 89 5c 24 08 48 89 74 24 10 57 48',   # XAnimCalcDelta (per time)
}


def check_native(pe):
    for rva, expected in ROUTINES.items():
        if pe.get_data(rva, 12) != bytes.fromhex(expected):
            raise ValueError(f'Native routine at {rva:#x} differs')
    # The sampler tests only the delta flag (XAnimParts+0x14 bit 1) before
    # reading deltaPart (+0x80): the reason the converter clears the flag.
    if pe.get_data(0x29b20b, 4) != bytes.fromhex('f6 41 14 02') or pe.get_data(0x29b283, 7) != bytes.fromhex('48 8b 83 80 00 00 00'):
        raise ValueError('Delta flag / deltaPart read differs')


def used_anims(root):
    text = (root / 'src' / 'ai.cpp').read_text()
    return sorted(set(re.findall(r'"(ai_zombie_[a-z0-9_]+)"', text)))


def run(executable, zone=None, compiler='g++'):
    if platform.system() != 'Linux' or platform.machine().lower() not in ('amd64', 'x86_64'):
        raise ValueError('This probe requires Linux/x86-64')
    data = executable.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXACT_SHA256:
        raise ValueError('Exact executable SHA256 mismatch')
    pe = pefile.PE(data=data)
    check_native(pe)
    compiler_path = shutil.which(compiler)
    if not compiler_path:
        raise ValueError('Host compiler unavailable')
    tools = Path(__file__).resolve().parent
    root = tools.parent
    with tempfile.TemporaryDirectory(prefix='bo1mw2-anim-delta-') as directory:
        work = Path(directory)
        image = work / 'private-mapped-image.bin'
        program = work / 'anim-delta-probe'
        image.write_bytes(pe.get_memory_mapped_image())
        subprocess.run([compiler_path, '-std=c++17', '-O1', '-I', str(root / 'include'), str(tools / 'anim_delta_probe.cpp'),
                        '-o', str(program)], check=True)
        reports = {}
        for label, args in (('synthetic', ['--synthetic']), ('bo1_zone', [str(zone)] + used_anims(root) if zone else None)):
            if args is None:
                continue
            result = subprocess.run([str(program), str(image)] + args, capture_output=True, text=True, timeout=1800)
            if result.returncode:
                raise ValueError(f'{label} root-motion probe failed ({result.returncode}): {result.stderr.strip() or result.stdout[-400:]}')
            reports[label] = json.loads(result.stdout)
        reports['executable_sha256'] = EXACT_SHA256
        reports['routines'] = sorted(hex(r) for r in ROUTINES)
        return reports


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--bo1-zone', type=Path, help='local decompressed common_zombie zone (optional)')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = run(args.executable, args.bo1_zone)
    rendered = json.dumps(report, indent=2) + '\n'
    args.output.write_text(rendered)
    print(rendered, end='')
