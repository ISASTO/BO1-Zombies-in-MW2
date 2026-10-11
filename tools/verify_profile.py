"""Validate the exact-build signatures against a user-provided MW2 executable."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
import pefile


def verify(path, header):
    data = path.read_bytes()
    expected = re.search(r'kExeSha256 = "([a-f0-9]+)"', header.read_text())[1]
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected:
        raise ValueError(f'Unsupported executable SHA256: {actual}')
    pe = pefile.PE(data=data)
    if pe.FILE_HEADER.Machine != 0x8664:
        raise ValueError('Expected AMD64 PE')
    section = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
    text = section.get_data()
    result = dict(sha256=actual, architecture='AMD64', signatures={})
    for name, pat in re.findall(r'const char\* (k\w+Pattern) =\s*"([^"]+)"', header.read_text()):
        expr = b''.join(b'.' if t=='??' else re.escape(bytes.fromhex(t)) for t in pat.split())
        matches = list(re.finditer(expr, text, re.S))
        if len(matches) != 1:
            raise ValueError(f'{name}: {len(matches)} matches, expected exactly one')
        at = section.VirtualAddress + matches[0].start()
        record = dict(match_count=1, match_rva=hex(at))
        rip_offsets = {'kSvRunningPattern': (29,54), 'kMapNamePattern': (13,27), 'kEntityPattern': (0,)}.get(name, ())
        record['rip_targets'] = {str(n): hex(at+n+7+struct.unpack_from('<i', text, matches[0].start()+n+3)[0]) for n in rip_offsets}
        if name in ('kSvRunningPattern','kMapNamePattern'):
            offset = rip_offsets[0]
            target = int(record['rip_targets'][str(offset)], 16)
            anchor = pe.get_data(target,64).split(b'\0',1)[0].decode()
            wanted = 'sv_running' if name=='kSvRunningPattern' else 'mapname'
            if anchor != wanted:
                raise ValueError(f'{name}: wrong name anchor {anchor}')
            record['name_anchor'] = anchor
        result['signatures'][name] = record
    if len(result['signatures']) != 5:
        raise ValueError('Expected all five profile signatures')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--header', type=Path, default=Path(__file__).resolve().parents[1]/'include/core.hpp')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps(verify(args.executable,args.header),indent=2)+'\n'
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(result)
    else:
        print(result,end='')
