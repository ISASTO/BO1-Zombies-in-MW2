"""Carve validated, inline compressed GSC/CSC RawFiles from local T5 v473 zones.

This is a scoped research extractor, not an XAsset loader or complete fastfile parser.
It writes copyrighted reference inputs ONLY under an ignored local-data directory.
"""
import argparse
import hashlib
import json
import re
import struct
import zlib
from pathlib import Path, PurePosixPath

MAX_ZONE = 512*1024*1024
MAX_SCRIPT = 16*1024*1024
NAMES = re.compile(rb'[a-zA-Z0-9_/.]+\.(?:gsc|csc)\x00')


def bounded_inflate(data, maximum):
    stream = zlib.decompressobj()
    result = stream.decompress(data, maximum+1)
    if len(result)>maximum or stream.unconsumed_tail or not stream.eof:
        raise ValueError('Truncated zlib stream or output exceeds bound')
    return result


def carve(raw):
    found = []
    for match in NAMES.finditer(raw):
        name = match[0][:-1].decode('ascii')
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts:
            continue
        at = match.end()
        if at+10 > len(raw):
            continue
        out_size, in_size = struct.unpack_from('<II',raw,at)
        if not (0 < out_size <= MAX_SCRIPT and 0 < in_size <= MAX_SCRIPT) or at+8+in_size > len(raw):
            continue
        compressed = raw[at+8:at+8+in_size]
        try:
            decoded = bounded_inflate(compressed, MAX_SCRIPT)
            stream = zlib.decompressobj(); stream.decompress(compressed)
            if stream.unused_data or len(decoded) != out_size or not decoded.endswith(b'\0'):
                continue
        except (ValueError,zlib.error):
            continue
        found.append((name, decoded[:-1], at, out_size, in_size))
    return found


def extract(paths, output):
    report = []
    for path in paths:
        data = path.read_bytes()
        if data[:8] != b'IWffu100' or len(data)<14 or struct.unpack_from('<I',data,8)[0] != 473:
            raise ValueError(f'{path.name}: expected unsigned T5 fastfile version 473')
        raw = bounded_inflate(data[12:],MAX_ZONE)
        scripts = []
        for name, text, at, out_size, in_size in carve(raw):
            destination = output/path.name/name
            destination.parent.mkdir(parents=True,exist_ok=True)
            destination.write_bytes(text)
            scripts.append(dict(name=name,sha256=hashlib.sha256(text).hexdigest(),size=len(text),
                                raw_buffer_offset=hex(at),declared_output_size=out_size,compressed_size=in_size))
        report.append(dict(zone=path.name,sha256=hashlib.sha256(data).hexdigest(),
                           decompressed_size=len(raw),validated_script_count=len(scripts),scripts=scripts))
    output.mkdir(parents=True,exist_ok=True)
    (output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fastfiles',nargs='+',type=Path)
    parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'local-data/bo1')
    args = parser.parse_args()
    for zone in extract(args.fastfiles,args.output):
        print(f"{zone['zone']}: {zone['validated_script_count']} validated scripts, manifest written")
