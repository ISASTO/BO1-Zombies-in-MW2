"""Derived evidence for exact-build placement/team/goal/native hand creation."""
import re
import struct


def validate_spawn_graph(graph):
    for a, b in (
        ('copy_static_call', 'copy_static'), ('copy_dynamic_call', 'copy_dynamic'),
        ('physics_trace_call', 'bounds_trace'), ('actor_trace_call', 'bounds_trace'),
        ('origin_set_call', 'origin_set'), ('team_script_tail', 'team_set'),
        ('goal_script_call', 'goal_set'), ('enemy_script_tail', 'enemy_set'),
        ('right_hand_call', 'animation_builder'), ('left_hand_call', 'animation_builder'),
    ):
        if graph[a] != graph[b]:
            raise ValueError(f'Native spawn graph disagrees: {a} != {b}')


def verify_spawn_bindings(pe, signatures, controls, placement):
    def at(name):
        return int(signatures[name]['match_rva'], 16)

    def rip(name, n):
        return int(signatures[name]['rip_targets'][str(n)], 16)

    def branch(name, n, op=0xe8):
        pos=at(name)+n;code=pe.get_data(pos,5)
        if code[0] != op:
            raise ValueError(f'{name}: expected native branch opcode')
        return pos+5+struct.unpack_from('<i',code,1)[0]

    graph={
        'copy_static_call':branch('kAiSpawnCopyCallsPattern',14),'copy_static':at('kAiCopyFieldsStartPattern'),
        'copy_dynamic_call':branch('kAiSpawnCopyCallsPattern',25),'copy_dynamic':at('kAiCopyDynamicStartPattern'),
        'physics_trace_call':branch('kAiTraceScriptCallPattern',38),'bounds_trace':at('kAiTraceStartPattern'),
        'actor_trace_call':branch('kAiActorTraceResultPattern',50),
        'origin_set_call':branch('kAiPositionMethodCallPattern',8),'origin_set':at('kAiPositionSetterPattern'),
        'team_script_tail':branch('kAiTeamScriptSetterPattern',33,0xe9),'team_set':at('kAiTeamChangePattern'),
        'goal_script_call':branch('kAiGoalScriptCallPattern',23),'goal_set':at('kAiGoalPositionPattern'),
        'enemy_script_tail':branch('kAiClearEnemyTailPattern',28,0xe9),'enemy_set':at('kAiEnemySetterStartPattern'),
        'right_hand_call':branch('kNativeWeaponHandsCallsPattern',7),'left_hand_call':branch('kNativeWeaponHandsCallsPattern',24),
        'animation_builder':at('kWeaponAnimBuildStartPattern'),
    }
    validate_spawn_graph(graph)
    source=controls.read_text();p=placement.read_text()
    def field(name):
        return int(re.search(rf'\b{name}\s*=\s*(0x[0-9a-f]+|\d+)',source)[1],0)
    capacity=pe.get_data(at('kAiActorCapacityPattern'),66)
    if capacity[61]!=field('kAiNativeActorCapacity') or struct.unpack_from('<I',capacity,54)[0]!=0x1560:
        raise ValueError('Native actor capacity/stride disagrees; cannot remove it by changing a menu counter')
    bounds=struct.unpack('<6f',pe.get_data(rip('kAiActorBoundsPattern',0),24))
    if bounds!=(0.,0.,36.,15.,15.,36.):
        raise ValueError('Native actor Bounds midpoint/halfsize differs')
    if struct.unpack('<6f',pe.get_data(rip('kAiTraceScriptCallPattern',0),24))!=(0.,)*6:
        raise ValueError('Point trace native zero Bounds differs')
    mask=int(re.search(r'kAiPlacementMask=(0x[0-9a-f]+)',p)[1],16)
    if struct.unpack('<I',pe.get_data(at('kAiActorBoundsPattern')+36,4))[0]!=mask:
        raise ValueError('Placement mask differs from the native actor clip-mask store')
    code=pe.get_data(at('kAiActorTraceResultPattern'),63)
    if code[12:14]!=bytes.fromhex('24 68') or code[55:63]!=bytes.fromhex('44 38 BC 24 95 00 00 00'):
        raise ValueError('Native actor trace result buffer/startsolid displacement differs')
    if 'bytes[0x2d]' not in p:
        raise ValueError('Trace reader differs from confirmed startsolid member')
    init=pe.get_data(at('kAiTraceLayoutPattern'),55)
    if init[9:15]!=bytes.fromhex('0F 57 C0 0F 11 01') or init[32:36]!=bytes.fromhex('0F 11 41 20'):
        raise ValueError('Native trace output no longer clears 48 bytes')
    target=struct.unpack('<I',pe.get_data(at('kAiSpawnTargetnamePattern')+3,4))[0]
    if target!=field('kAiTargetname'):
        raise ValueError('Native targetname WORD setter offset differs')
    base=pe.OPTIONAL_HEADER.ImageBase
    # Source copy starts at the actual Gentity field table; first two pointers
    # name code_classname/classname, then target at the independently known row.
    table=rip('kAiCopyFieldsStartPattern',10)
    target_pointer,target_offset,target_type=struct.unpack('<QII',pe.get_data(table+0xa0,16))
    target_name=pe.get_data(target_pointer-base,64).split(b'\0',1)[0]
    if target_name!=b'target' or target_offset!=field('kAiTarget') or target_type!=4:
        raise ValueError('Native target descriptor differs; cannot use a raw pointer store for script strings')
    builder=pe.get_data(at('kWeaponAnimBuildStartPattern'),53)
    if builder[26:29]!=bytes.fromhex('48 8B EA'):
        raise ValueError('Native animation builder no longer saves RDX names table')
    return {
        'graph':{k:hex(v) for k,v in graph.items()},
        'g_spawn_rva':hex(branch('kAiSpawnCopyCallsPattern',0)),
        'g_free_entity_rva':hex(branch('kAiDeleteGuardPattern',47,0xe9)),
        'script_string_setter_rva':hex(branch('kAiSpawnTargetnamePattern',9)),
        'actor_used_count_slot_rva':hex(rip('kAiActorCapacityPattern',17)),
        'actor_soft_limit_slot_rva':hex(rip('kAiActorCapacityPattern',10)),
        'actor_pool_slot_rva':hex(rip('kAiActorCapacityPattern',28)),
        'physical_actor_capacity':32,'actor_stride':'0x1560','bounds_midpoint_halfsize':bounds,
        'placement_mask':hex(mask),'trace_output_bytes':48,'trace_fraction_offset':0,'trace_startsolid_offset':'0x2d',
        'team_transfer':'Native helper writes sentient team+8 and actor team mirror+0x1378.',
        'goal':'Native setgoalpos caller uses actor pointer, Vec3 pointer, optional node=null; helper has no boolean success return.',
        'animation':'Native caller builds each hand with (WeaponDef*, names**) returning distinct arena-owned opaque definitions. Runtime requests use already validated/loaded normal names, not guessed asset names.',
        'scope':'First retail test pending for temporary helper spawners, forced-visibility bypass with independent collision traces, team conversion, goals and queued hand creation.'
    }
