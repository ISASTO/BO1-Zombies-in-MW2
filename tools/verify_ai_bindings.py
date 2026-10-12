"""Cross-check the retail SP actor bindings independently of signature uniqueness.

Only derived addresses/field evidence leave this tool. No game byte stream is
written to its result. Table fixtures use invented names/addresses, not assets.
"""
import re
import struct
from pattern_scan import pattern_matches

TEAM_NAMES = ('invalid', 'axis', 'allies', 'neutral', 'team3', 'dead')


def cstring(read, base, image_size, pointer):
    if not base <= pointer < base + image_size:
        return None
    raw = read(pointer - base, min(96, base + image_size - pointer))
    if b'\0' not in raw:
        return None
    try:
        return raw.split(b'\0', 1)[0].decode('ascii')
    except UnicodeDecodeError:
        return None


def method_entry(sections, read, base, image_size, name, function):
    hits = []
    for rva, data in sections:
        for n in range(8, len(data) - 7, 8):
            if struct.unpack_from('<Q', data, n)[0] != base + function:
                continue
            pointer = struct.unpack_from('<Q', data, n - 8)[0]
            if cstring(read, base, image_size, pointer) == name:
                hits.append(rva + n - 8)
    if len(hits) != 1:
        raise ValueError(f'{name}: expected one native name/function entry, found {len(hits)}')
    return hits[0]


def team_table(sections, read, base, image_size):
    first = set()
    for rva, data in sections:
        at = 0
        while (at := data.find(b'invalid\0', at)) >= 0:
            first.add(base + rva + at)
            at += 8
    hits = []
    for rva, data in sections:
        for n in range(0, len(data) - 47, 8):
            if struct.unpack_from('<Q', data, n)[0] not in first:
                continue
            pointers = struct.unpack_from('<6Q', data, n)
            if tuple(cstring(read, base, image_size, p) for p in pointers) == TEAM_NAMES:
                hits.append(rva + n)
    if len(hits) != 1:
        raise ValueError(f'Native team-name mapping is absent/ambiguous: {len(hits)}')
    return hits[0]


def entity_field_table(sections, read, base, image_size, classname, origin, spawnflags):
    """One compiled 32-byte descriptor cluster, distinct from the spawn-key table."""
    hits = []
    for rva, data in sections:
        for n in range(0, len(data) - 127, 8):
            if struct.unpack_from('<II', data, n + 8) != (classname, 4):
                continue
            same = True
            for delta, name, member, kind in (
                (0, 'classname', classname, 4),
                (32, 'origin', origin, 6),
                (96, 'spawnflags', spawnflags, 0),
            ):
                pointer, offset, field_type = struct.unpack_from('<QII', data, n + delta)
                same &= (offset, field_type) == (member, kind) and cstring(read, base, image_size, pointer) == name
            if same:
                hits.append(rva + n)
    if len(hits) != 1:
        raise ValueError(f'Native classname/origin/spawnflags descriptors disagree: {len(hits)}')
    return hits[0]


def validate_graph(g):
    """Reject a individually plausible binding if another native path disagrees."""
    for a, b in (
        ('callback_slot_value', 'actor_think'), ('spawn_call_target', 'spawn_core'),
        ('spawn_enabled_consumer', 'spawn_enabled_registration'),
        ('spawn_disabled_consumer', 'spawn_disabled_registration'),
        ('entity_pool_reader', 'entity_pool_free_reader'),
        ('level_time', 'spawn_time_read'), ('level_time', 'spawn_time_write'),
        ('level_time', 'actor_init_time'), ('think_ai_gate', 'world_ai_gate'),
    ):
        if g[a] != g[b]:
            raise ValueError(f'Native AI graph disagrees: {a} != {b}')


def verify_ai_bindings(pe, signatures, header, entity_number):
    base, image_size = pe.OPTIONAL_HEADER.ImageBase, pe.OPTIONAL_HEADER.SizeOfImage
    sections = [(s.VirtualAddress, s.get_data()) for s in pe.sections if not s.Characteristics & 0x20]
    text = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
    source = header.read_text()

    def member(name):
        value = re.search(rf'\b{name}\s*=\s*(0x[0-9a-f]+|\d+)', source)
        if not value:
            raise ValueError(f'Missing AI partial field: {name}')
        return int(value[1], 0)

    def at(name):
        return int(signatures[name]['match_rva'], 16)

    def rip(name, offset):
        return int(signatures[name]['rip_targets'][str(offset)], 16)

    def call(name, offset):
        start = at(name) + offset
        raw = pe.get_data(start, 5)
        if raw[:1] != b'\xe8':
            raise ValueError(f'{name}: expected a direct native call')
        return start + 5 + struct.unpack_from('<i', raw, 1)[0]

    dispatch, array, spawn = 'kAiThinkDispatchPattern', 'kAiSpawnerArrayPattern', 'kAiSpawnCorePattern'
    callback_slot = rip(dispatch, 12) + member('kActorHandler') * 80
    graph = {
        'callback_slot_value': struct.unpack('<Q', pe.get_data(callback_slot, 8))[0] - base,
        'actor_think': at('kAiActorThinkStartPattern'),
        'spawn_call_target': call('kAiNormalSpawnMethodPattern', 11), 'spawn_core': at(spawn),
        'spawn_enabled_consumer': rip('kAiActorInitPattern', 26),
        'spawn_enabled_registration': rip('kAiSpawnEnabledRegistrationPattern', 27),
        'spawn_disabled_consumer': rip(spawn, 13),
        'spawn_disabled_registration': rip('kAiSpawnDisabledRegistrationPattern', 36),
        'entity_pool_reader': rip(array, 12), 'entity_pool_free_reader': rip('kAiEntitySerialPattern', 22),
        'level_time': rip('kAiActorThinkStartPattern', 25),
        'spawn_time_read': rip('kAiNormalSpawnEntryPattern', 24),
        'spawn_time_write': rip('kAiNormalSpawnMethodPattern', 31),
        'actor_init_time': rip('kAiActorLinksPattern', 21),
        'think_ai_gate': rip('kAiActorThinkStartPattern', 9), 'world_ai_gate': rip('kAiConsumerPattern', 0),
    }
    validate_graph(graph)
    partial = {}
    for name, signature, offset, width in (
        ('kAiHandler', dispatch, 3, 4), ('kAiSpawnerTeam', 'kAiSpawnerFieldsPattern', 14, 4),
        ('kAiEntityStride', 'kAiSpawnerFieldsPattern', 29, 4),
        ('kAiSpawnCount', spawn, 40, 4), ('kAiLastSpawn', 'kAiNormalSpawnMethodPattern', 39, 4),
        ('kAiActor', 'kAiActorLinksPattern', 3, 4), ('kAiSentient', 'kAiActorLinksPattern', 10, 4),
        ('kAiHandler', 'kAiActorHandlerPattern', 2, 4), ('kAiHealth', 'kAiActorHandlerPattern', 26, 4),
        ('kAiClient', 'kAiDeleteMethodPattern', 73, 4), ('kAiNumber', 'kAiDeleteGuardPattern', 3, 1),
        ('kAiUseCount', 'kAiEntitySerialPattern', 2, 1), ('kAiUseCount', 'kAiEntitySerialCommitPattern', 10, 1),
    ):
        actual = int.from_bytes(pe.get_data(at(signature) + offset, width), 'little')
        if actual != member(name):
            raise ValueError(f'{signature}: {name} {member(name):#x} differs from native {actual:#x}')
        partial.setdefault(name, []).append({'offset': hex(actual), 'signature': signature, 'encoded_rva': hex(at(signature) + offset)})
    if member('kAiNumber') != int(entity_number['offset'], 16):
        raise ValueError('AI and player native entity-number layouts differ')
    for name, signature, offset in (
        ('kActorType', 'kAiActorTypePattern', 2), ('kActorSpawnerType', 'kAiNormalSpawnEntryPattern', 16),
        ('kActorHandler', 'kAiActorHandlerPattern', 6),
    ):
        if pe.get_data(at(signature) + offset, 1)[0] != member(name):
            raise ValueError(f'Native {name} immediate disagrees')
    if pe.get_data(at('kAiActorSelfPattern'), 9) != bytes.fromhex('48 89 3E 33 D2 48 89 6E 08'):
        raise ValueError('Actor self/sentient link writer disagrees')
    if pe.get_data(at('kAiSentientLinksPattern'), 7) != bytes.fromhex('48 89 11 44 89 41 08'):
        raise ValueError('Sentient self/team link writer disagrees')
    if pe.get_data(at('kAiEntitySerialCommitPattern'), 3) != bytes.fromhex('8D 47 01'):
        raise ValueError('Native allocation serial is no longer incremented by one')
    if struct.unpack('<I', pe.get_data(at('kAiEntitySerialPattern') + 7, 4))[0] != member('kAiEntityStride'):
        raise ValueError('Freed entity zero-fill size disagrees with stride')
    alloc_pattern = 'C6 83 ?? ?? ?? ?? 01 48 2B C8 48 B8 ?? ?? ?? ?? ?? ?? ?? ?? 48 F7 E9'
    alloc = pattern_matches(text.get_data(), alloc_pattern)
    if len(alloc) != 1:
        raise ValueError('Native entity in-use writer is absent/ambiguous')
    alloc_rva = text.VirtualAddress + alloc[0].start()
    if struct.unpack('<I', pe.get_data(alloc_rva + 2, 4))[0] != member('kAiInUse'):
        raise ValueError('Native in-use field disagrees')
    fixed_pool = rip('kAiDeleteMethodPattern', 51)
    if fixed_pool + member('kAiEntityStride') * member('kAiEntityCapacity') > image_size:
        raise ValueError('Native entity pool exceeds image bounds')
    methods = {name: hex(method_entry(sections, pe.get_data, base, image_size, name, at(signature)))
               for name, signature in (('delete', 'kAiDeleteMethodPattern'), ('dospawn', 'kAiNormalSpawnEntryPattern'))}
    teams = team_table(sections, pe.get_data, base, image_size)
    fields = entity_field_table(sections, pe.get_data, base, image_size, member('kAiClassname'), member('kAiOrigin'), member('kAiSpawnFlags'))
    if (member('kAxisTeam'), member('kAlliesTeam')) != (1, 2):
        raise ValueError('AI controls disagree with verified axis/allies name order')
    return {
        'status': 'Static exact-build cross-checks passed; retail observations are recorded separately by iteration.',
        'calling_convention': 'Windows AMD64; think(RCX entity), spawn(RCX spawner, EDX targetname, R8D visibility/distance bypass, R9D additional check flag), delete(ECX packed class-0 entity number). Test13 submits (spawner,0,1,0) after independent clearance traces.',
        'graph': {name: hex(value) for name, value in graph.items()},
        'think_callback_slot_rva': hex(callback_slot), 'entity_pool_rva': hex(fixed_pool),
        'entity_count_slot_rva': hex(rip(array, 0)), 'active_bitmap_rva': hex(rip(array, 24)),
        'script_string_pool_slot_rva': hex(rip('kAiStringConvertPattern', 4)),
        'protected_entity_number_rva': hex(rip('kAiDeleteGuardPattern', 4)),
        'compiled_method_entries': methods, 'team_name_table_rva': hex(teams), 'team_names': TEAM_NAMES,
        'entity_field_descriptors_rva': hex(fields), 'script_classname_offset': hex(member('kAiClassname')),
        'entity_stride': hex(member('kAiEntityStride')), 'entity_capacity': member('kAiEntityCapacity'),
        'partial_field_evidence': partial, 'in_use_writer_rva': hex(alloc_rva),
        'lifetime': 'Native useCount DWORD increments when G_FreeEntity clears the slot; also require matching number/type/actor/sentient/self links and level identity.',
        'limits': 'Native mission NPC debugging, not BO1 zombie AI. Hook installs on first request and needs an existing actor callback. Test13 uses independently traced placement, temporary native spawners, team/goal adapters and dynamic generation ownership. Physical actor capacity32 remains; the mod does not impose eight.'
    }
