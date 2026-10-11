#!/usr/bin/env python3
"""Verify loader-control PE properties without executing Windows code."""
import argparse
import hashlib
import json
from pathlib import Path
import pefile

EXPORTS = {
    'Direct3DCreate9', 'Direct3DCreate9Ex', 'D3DPERF_BeginEvent', 'D3DPERF_EndEvent',
    'D3DPERF_GetStatus', 'D3DPERF_QueryRepeatFrame', 'D3DPERF_SetMarker',
    'D3DPERF_SetOptions', 'D3DPERF_SetRegion', 'DebugSetMute',
}


def inspect(path, imports, exports=None):
    data = path.read_bytes()
    pe = pefile.PE(data=data)
    actual_imports = {entry.dll.decode().lower() for entry in pe.DIRECTORY_ENTRY_IMPORT}
    actual_exports = {entry.name.decode() for entry in pe.DIRECTORY_ENTRY_EXPORT.symbols} if hasattr(pe, 'DIRECTORY_ENTRY_EXPORT') else set()
    checks = {
        'amd64_pe32_plus': pe.FILE_HEADER.Machine == 0x8664 and pe.OPTIONAL_HEADER.Magic == 0x20b,
        'exact_system_imports': actual_imports == imports,
        'no_tls_directory': pe.OPTIONAL_HEADER.DATA_DIRECTORY[9].VirtualAddress == 0,
        'entry_point_present': pe.OPTIONAL_HEADER.AddressOfEntryPoint != 0,
        'aslr_and_nx': (pe.OPTIONAL_HEADER.DllCharacteristics & 0x140) == 0x140,
        'correct_dll_flag': bool(pe.FILE_HEADER.Characteristics & 0x2000) == (exports is not None),
    }
    if exports is not None:
        checks['exact_ten_exports'] = actual_exports == exports
    if not all(checks.values()):
        raise ValueError(f'{path}: failed checks {checks}')
    return {
        'file': path.name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
        'imports': sorted(actual_imports), 'exports': sorted(actual_exports), 'checks': checks,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('startup_dll', type=Path)
    parser.add_argument('probe_exe', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = {
        'version': '0.1.1-startup-test2',
        'compiler': 'MinGW-w64 GCC 13-posix, RelWithDebInfo, freestanding C, no CRT',
        'startup_dll': inspect(args.startup_dll, {'kernel32.dll'}, EXPORTS),
        'probe_exe': inspect(args.probe_exe, {'kernel32.dll', 'shell32.dll'}),
        'runtime_status': 'Retail test 1 launch failed per owner; root cause unknown. Test 2 Windows execution pending.',
        'powershell_status': 'Four scripts parsed in PowerShell 7.6.6 on Linux. Isolated no-log collection and startup install/hash/receipt/uninstall/log-preservation checks passed. Windows event queries and executable loading remain pending.',
    }
    rendered = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(rendered)
    print(rendered, end='')


if __name__ == '__main__':
    main()
