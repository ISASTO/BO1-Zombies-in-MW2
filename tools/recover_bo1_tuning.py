"""Recover selected BO1 tuning from local version-473 StringTables.

Scoped forensic reader, not a full XAsset loader. It reconstructs virtual-block
XString addresses from unambiguous in-table alias anchors, validates every
inline/cross-table string used, and refuses unresolved or conflicting references.
Only derived tuning/evidence is emitted; original tables stay in local inputs.
"""
import argparse
import collections
import hashlib
import json
import re
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path

INLINE = 0xffffffff
OFFSET_MASK = 0x1fffffff
VIRTUAL_BLOCK = 4


def string_hash(value):
    result = 5381
    for byte in value.lower():
        result = (result * 33 + byte) & 0xffffffff
    return result


@dataclass
class Table:
    name: str
    columns: int
    rows: int
    header_offset: int
    cells: list
    inline: dict
    end_offset: int


def read_table(raw, name_offset, name_end):
    if name_offset < 20:
        raise ValueError('Truncated StringTable header')
    name, columns, rows, values, index = struct.unpack_from('<5I', raw, name_offset - 20)
    count = columns * rows
    if name != INLINE or values != INLINE or index != INLINE or not 1 <= columns <= 32 or not 1 <= count <= 32767:
        raise ValueError('Unsupported StringTable header/pointers')
    cursor = name_end + count * 8
    if cursor > len(raw):
        raise ValueError('Truncated StringTable cells')
    cells = [struct.unpack_from('<II', raw, name_end + i * 8) for i in range(count)]
    inline = {}
    for cell, (pointer, expected_hash) in enumerate(cells):
        if pointer == INLINE:
            end = raw.find(b'\0', cursor, cursor + 32768)
            if end < 0:
                raise ValueError('Unterminated inline XString')
            value = raw[cursor:end]
            if any(c < 32 or c > 126 for c in value) or string_hash(value) != expected_hash:
                raise ValueError('Inline string hash/ASCII mismatch')
            inline[cell] = (value, cursor)
            cursor = end + 1
        elif pointer == 0:
            if expected_hash != 0:
                raise ValueError('Null string has a nonzero hash')
        elif not 0x80000000 <= pointer < 0xfffffff0:
            raise ValueError('Unsupported inserted/packed XString')
    if cursor + count * 2 > len(raw):
        raise ValueError('Truncated cell-index array')
    indices = struct.unpack_from('<' + str(count) + 'h', raw, cursor)
    if set(indices) != set(range(count)):
        raise ValueError('Cell-index array is not a valid permutation')
    return Table(raw[name_offset:name_end-1].decode('ascii'), columns, rows,
                 name_offset-20, cells, inline, cursor + count*2)


def scan_tables(raw):
    tables = []
    for match in re.finditer(rb'[A-Za-z0-9_./-]+\.csv\x00', raw):
        try:
            tables.append(read_table(raw, match.start(), match.end()))
        except (ValueError, struct.error, UnicodeDecodeError):
            # Names also occur outside StringTables; require the complete record.
            pass
    return tables


def alias_base(table):
    by_hash = collections.defaultdict(list)
    for cell, (value, offset) in table.inline.items():
        by_hash[table.cells[cell][1]].append((value, offset))
    bases = collections.defaultdict(set)
    for pointer, expected_hash in table.cells:
        if not 0x80000000 <= pointer < 0xfffffff0:
            continue
        packed = pointer - 1
        block, offset = packed >> 29, packed & OFFSET_MASK
        if block != VIRTUAL_BLOCK:
            continue
        for value, raw_offset in by_hash[expected_hash]:
            bases[raw_offset-offset].add(value)
    if len(bases) != 1:
        raise ValueError('No unique XString alias base for ' + table.name)
    delta, anchors = next(iter(bases.items()))
    return delta, len(anchors)


def build_string_cache(tables):
    cache, mappings = {}, []
    for table in tables:
        try:
            delta, anchors = alias_base(table)
        except ValueError:
            continue
        for value, raw_offset in table.inline.values():
            virtual_offset = raw_offset-delta
            if not 0 <= virtual_offset <= OFFSET_MASK:
                raise ValueError('Reconstructed virtual offset out of range')
            pointer = (VIRTUAL_BLOCK << 29) + virtual_offset + 1
            entry = dict(value=value, raw_offset=raw_offset, source_table=table.name)
            if pointer in cache and cache[pointer] != entry:
                raise ValueError('Conflicting reconstructed XString pointer')
            cache[pointer] = entry
        mappings.append(dict(table=table.name, alias_values=anchors, raw_minus_virtual_delta=hex(delta)))
    return cache, mappings


def resolve_cell(table, cell, cache):
    pointer, expected_hash = table.cells[cell]
    if cell in table.inline:
        value, offset = table.inline[cell]
        source = dict(kind='inline', raw_offset=hex(offset), source_table=table.name)
    elif pointer in cache:
        entry = cache[pointer]
        value = entry['value']
        source = dict(kind='reconstructed_shared_xstring', pointer=hex(pointer),
                      raw_offset=hex(entry['raw_offset']), source_table=entry['source_table'])
    elif pointer == 0:
        value, source = b'', dict(kind='null')
    else:
        raise ValueError(f'Unresolved XString {pointer:#x} in {table.name}, cell {cell}')
    if pointer != 0 and string_hash(value) != expected_hash:
        raise ValueError('Resolved string hash mismatch')
    return value.decode('ascii'), source


def recover(raw):
    tables = scan_tables(raw)
    selected = [t for t in tables if t.name == 'mp/zombiemode.csv']
    if len(selected) != 1:
        raise ValueError('Expected exactly one complete mp/zombiemode.csv record')
    table = selected[0]
    cache, mappings = build_string_cache(tables)
    column = 2  # Patched init_vars: difficulty=1; column=int(difficulty)+1.
    if table.columns <= column:
        raise ValueError('Missing normal difficulty column')
    label, _ = resolve_cell(table, column, cache)
    if label != 'MEDIUM':
        raise ValueError('Unexpected normal tuning column label')
    # Unneeded weapon-name references can lie outside the scoped table cache.
    # Use hashes to find keys, then verify their actual strings and exact refs.
    key_hashes = [table.cells[row*table.columns][1] for row in range(1,table.rows)]
    parameters = {}
    for key, floating in {
        'zombie_health_start': False, 'zombie_health_increase': False,
        'zombie_health_increase_multiplier': True, 'zombie_spawn_delay': True,
        'zombie_max_ai': False, 'zombie_ai_per_player': False,
        'zombie_new_runner_interval': False, 'zombie_move_speed_multiplier': False,
    }.items():
        matching = [row for row in range(1,table.rows)
                    if table.cells[row*table.columns][1] == string_hash(key.encode())]
        if len(matching) != 1:
            raise ValueError('Missing/ambiguous required tuning row: '+key)
        row = matching[0]
        actual_key, _ = resolve_cell(table,row*table.columns,cache)
        if actual_key.lower() != key:
            raise ValueError('Tuning key hash collision: '+key)
        text, source = resolve_cell(table, row*table.columns+column, cache)
        if not re.fullmatch(r'[0-9]+(?:\.[0-9]+)?', text) or (not floating and '.' in text):
            raise ValueError('Unexpected numeric tuning string: '+key)
        parameters[key] = dict(value=float(text) if floating else int(text),
                               row=row, column=column, cell_hash=hex(string_hash(text.encode())), **source)
    if string_hash(b'zombie_between_round_time') in key_hashes:
        raise ValueError('Inter-round CSV row found; update fallback evidence instead of assuming it')
    return dict(asset=table.name, header_raw_offset=hex(table.header_offset), columns=table.columns,
                rows=table.rows, normal_column=column, column_label=label,
                parameters=parameters,
                between_round_time=dict(value=10, source='Patched GSC fallback; CSV key absent'),
                concurrent_limit=dict(value=24, source='Patched GSC init level.zombie_ai_limit and SetAILimit'),
                structurally_valid_tables=len(tables), mapped_xstrings=len(cache), mappings=mappings,
                limits='Scoped ASCII/inline/virtual-block CSV reader. Some unneeded tables/references remain unresolved. Not a full zone pointer loader or final runtime override audit.')


def inspect(path):
    data = path.read_bytes()
    if data[:8] != b'IWffu100' or len(data) < 12 or struct.unpack_from('<I', data, 8)[0] != 473:
        raise ValueError('Expected unsigned T5 zone version 473')
    inflater = zlib.decompressobj()
    raw = inflater.decompress(data[12:]) + inflater.flush()
    if not inflater.eof:
        raise ValueError('Incomplete zone stream')
    return dict(file=path.name, size=len(data), sha256=hashlib.sha256(data).hexdigest(),
                raw_size=len(raw), **recover(raw))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('zone', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    text = json.dumps(inspect(args.zone), indent=2)+'\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    else:
        print(text, end='')
