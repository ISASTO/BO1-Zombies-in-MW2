"""Verify numeric evidence in the exact user-supplied T5 executable; scan local zones for tuning.

This does not run the game, parse complete XAssets, or publish original inputs.
"""
import argparse
import hashlib
import json
import re
import struct
import zlib
from pathlib import Path
import pefile

EXPECTED_SHA256 = '2ba4a04e81de2b60582da0e4489efde21e6cb07904a223bae633b6ccaebbf759'


def unique_pattern(text, section_rva, pattern):
    expr = b''.join(b'.' if t == '??' else re.escape(bytes.fromhex(t)) for t in pattern.split())
    matches = list(re.finditer(expr, text, re.S))
    if len(matches) != 1:
        raise ValueError(f'Expected one numeric-evidence match, found {len(matches)}')
    return section_rva + matches[0].start()


def verify(executable, zones=()):
    data = executable.read_bytes()
    actual = hashlib.sha256(data).hexdigest()
    if actual != EXPECTED_SHA256:
        raise ValueError(f'Unsupported T5 executable SHA256: {actual}')
    pe = pefile.PE(data=data)
    if pe.FILE_HEADER.Machine != 0x14c:
        raise ValueError('Expected the supplied I386 PE')
    section = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
    text = section.get_data()
    divide_rva = unique_pattern(text, section.VirtualAddress,
        'C7 47 04 05 00 00 00 8B 36 85 F6 74 ?? F3 0F 2A 07 F3 0F 2A CE F3 0F 5E C1 F3 0F 11 07 5F 5E 5D C3')
    truncate_rva = unique_pattern(text, section.VirtualAddress, 'DD 1C 24 F2 0F 2C 04 24 C9 C3')
    # Identify both VM int builtins by their actual table name/function pointers.
    names = list(re.finditer(b'\x00int\x00', data))
    if len(names) != 1:
        raise ValueError('Expected a unique native int builtin name')
    name_va = pe.OPTIONAL_HEADER.ImageBase + pe.get_rva_from_offset(names[0].start() + 1)
    tables = []
    for sec in pe.sections:
        if sec.Name.rstrip(b'\0') != b'.data':
            continue
        raw = sec.get_data()
        for match in re.finditer(re.escape(struct.pack('<I', name_va)), raw):
            at = match.start()
            if at + 12 > len(raw):
                continue
            function_va, developer = struct.unpack_from('<II', raw, at + 4)
            rva = function_va - pe.OPTIONAL_HEADER.ImageBase
            if not section.VirtualAddress <= rva < section.VirtualAddress + len(text) or developer != 0:
                continue
            # The float case gets the VM float, then calls the verified truncation
            # helper before adding an integer result. Decode that relative call.
            body = pe.get_data(rva, 128)
            calls = []
            for i in range(len(body) - 4):
                if body[i] == 0xe8:
                    target = rva + i + 5 + struct.unpack_from('<i', body, i + 1)[0]
                    if target == truncate_rva - 0x12:
                        calls.append(hex(rva + i))
            if len(calls) != 1:
                raise ValueError('Int builtin must call the independently matched truncation helper')
            tables.append(dict(table_entry_rva=hex(sec.VirtualAddress+at), function_rva=hex(rva), float_conversion_call=calls[0]))
    if len(tables) != 2:
        raise ValueError(f'Expected two VM int builtin entries, found {len(tables)}')
    scans = []
    for path in zones:
        zone = path.read_bytes()
        if zone[:8] != b'IWffu100' or struct.unpack_from('<I', zone, 8)[0] != 473:
            raise ValueError(f'Unsupported T5 zone header: {path.name}')
        inflate = zlib.decompressobj()
        raw = inflate.decompress(zone[12:]) + inflate.flush()
        if not inflate.eof:
            raise ValueError(f'Incomplete T5 zone: {path.name}')
        scans.append(dict(file=path.name, sha256=hashlib.sha256(zone).hexdigest(),
                          raw_size=len(raw), tuning_name_present=b'zombiemode.csv' in raw))
    return dict(executable_sha256=actual, architecture='I386',
                integer_division=dict(integer_case_rva=hex(divide_rva), result_type=5,
                                      type_semantics='float', float_width_bytes=4,
                                      evidence='set type 5, CVTSI2SS both operands, DIVSS, MOVSS result'),
                float_to_integer=dict(truncation_sequence_rva=hex(truncate_rva),
                                      evidence='FSTP qword followed by CVTTSD2SI; truncates toward zero',
                                      builtin_entries=tables),
                zone_scans=scans, scan_limit='Name search in decompressed streams; not a full StringTable loader',
                retail_final_parameters_verified=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--zone', action='append', type=Path, default=[])
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps(verify(args.executable, args.zone), indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result)
    else:
        print(result, end='')
