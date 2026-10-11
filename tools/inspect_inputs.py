"""Inventory local PE/fastfile inputs. Reports metadata, never copies game bytes."""
import argparse
import datetime
import hashlib
import json
import struct
from pathlib import Path


def inspect(path):
    data = path.read_bytes()
    record = dict(name=path.name, size=len(data), sha256=hashlib.sha256(data).hexdigest())
    if data[:2] == b'MZ':
        off = struct.unpack_from('<I', data, 60)[0]
        if data[off:off+4] != b'PE\0\0':
            raise ValueError('Invalid PE signature')
        machine, count, timestamp, _, _, option_size, _ = struct.unpack_from('<HHIIIHH', data, off+4)
        magic = struct.unpack_from('<H', data, off+24)[0]
        base = struct.unpack_from('<Q' if magic == 0x20b else '<I', data, off+(48 if magic == 0x20b else 52))[0]
        sections = []
        for n in range(count):
            at = off+24+option_size+n*40
            name = data[at:at+8].rstrip(b'\0').decode('ascii', errors='replace')
            virtual_size, rva, raw_size, raw_offset = struct.unpack_from('<IIII', data, at+8)
            sections.append(dict(name=name, rva=hex(rva), virtual_size=virtual_size, raw_size=raw_size, raw_offset=raw_offset))
        record['pe'] = dict(machine=hex(machine), architecture='AMD64' if machine==0x8664 else 'I386' if machine==0x14c else 'unknown',
                            timestamp_utc=datetime.datetime.fromtimestamp(timestamp, datetime.timezone.utc).isoformat(),
                            image_base=hex(base), entry_rva=hex(struct.unpack_from('<I', data, off+40)[0]),
                            image_size=hex(struct.unpack_from('<I', data, off+80)[0]), sections=sections)
    elif data[:8] == b'IWffu100':
        record['fastfile'] = dict(magic='IWffu100', version=struct.unpack_from('<I', data, 8)[0])
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths', nargs='+', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps([inspect(p) for p in args.paths], indent=2)+'\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result)
    else:
        print(result, end='')
