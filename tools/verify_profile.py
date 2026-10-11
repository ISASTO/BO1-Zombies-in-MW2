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
        rip_offsets = {
            'kSvRunningPattern': (29,54), 'kMapNamePattern': (13,27), 'kEntityPattern': (0,),
            'kFovRegistrationPattern': (9,68), 'kFovCameraPattern': (0,),
            'kSustainRegistrationPattern': (2,20), 'kSustainConsumerPattern': (0,),
            'kSpeedRegistrationPattern': (0,64), 'kSpeedConsumerPattern': (0,),
            'kJumpRegistrationPattern': (8,62),
        }.get(name, ())
        record['rip_targets'] = {str(n): hex(at+n+7+struct.unpack_from('<i', text, matches[0].start()+n+3)[0]) for n in rip_offsets}
        if name in ('kSvRunningPattern','kMapNamePattern','kFovRegistrationPattern',
                    'kSustainRegistrationPattern','kSpeedRegistrationPattern','kJumpRegistrationPattern'):
            offset = rip_offsets[0]
            target = int(record['rip_targets'][str(offset)], 16)
            anchor = pe.get_data(target,64).split(b'\0',1)[0].decode()
            wanted = {'kSvRunningPattern': 'sv_running', 'kMapNamePattern': 'mapname',
                      'kFovRegistrationPattern': 'cg_fov', 'kSustainRegistrationPattern':'player_sustainAmmo',
                      'kSpeedRegistrationPattern':'g_speed', 'kJumpRegistrationPattern':'jump_height'}[name]
            if anchor != wanted:
                raise ValueError(f'{name}: wrong name anchor {anchor}')
            record['name_anchor'] = anchor
        result['signatures'][name] = record
    wanted_patterns = {'kSvRunningPattern', 'kMapNamePattern', 'kEntityPattern',
                       'kGodDamagePattern', 'kHealthGuardPattern',
                       'kFovRegistrationPattern', 'kFovCameraPattern', 'kClientReadinessPattern',
                       'kClientDamageGuardPattern','kClientInvulnerabilityClearPattern','kNoclipCommandPattern',
                       'kNoclipMovementPattern','kReserveReadPattern','kReserveWritePattern',
                       'kSustainRegistrationPattern','kSustainConsumerPattern','kSpeedRegistrationPattern',
                       'kSpeedConsumerPattern','kJumpRegistrationPattern','kJumpImpulsePattern',
                       'kJumpClampPattern','kJumpStepPattern','kJumpCeilingPattern'}
    if set(result['signatures']) != wanted_patterns:
        raise ValueError('Expected five core, two FOV, and sixteen player signatures')
    # Independent registration constants and the native camera's four-byte read.
    fov_layout = header.with_name('fov_preferences.hpp').read_text()
    default = int(re.search(r'kDefaultFov = (\d+)', fov_layout)[1])
    member = int(re.search(r'kDvarFloatCurrent = (0x[0-9a-f]+)', fov_layout)[1], 16)
    registration = result['signatures']['kFovRegistrationPattern']
    camera = result['signatures']['kFovCameraPattern']
    if registration['rip_targets']['68'] != camera['rip_targets']['0']:
        raise ValueError('FOV registration and camera read use different pointer slots')
    at = int(registration['match_rva'], 16)
    constants = {}
    for name, offset, displacement, length, wanted in (
        ('minimum', 0, 5, 9, 1.0), ('maximum', 16, 4, 8, 160.0),
        ('default', 28, 4, 8, float(default)),
    ):
        instruction = pe.get_data(at+offset, length)
        target = at+offset+length+struct.unpack_from('<i', instruction, displacement)[0]
        actual_value = struct.unpack('<f', pe.get_data(target, 4))[0]
        if actual_value != wanted:
            raise ValueError(f'FOV {name}: native {actual_value} differs from our {wanted}')
        constants[name] = {'value': actual_value, 'rva': hex(target)}
    camera_at = int(camera['match_rva'], 16)
    actual_member = pe.get_data(camera_at+20, 1)[0]
    if actual_member != member:
        raise ValueError(f'FOV camera member {actual_member:#x} differs from our {member:#x}')
    result['fov'] = {
        'slot_rva': registration['rip_targets']['68'], 'type': 1,
        'current_offset': hex(member), 'width_bytes': 4,
        'camera_read_rva': hex(camera_at+16), 'registration_constants': constants,
        'limits': 'Static exact-build evidence; owner confirms test6 FOV behavior. See runtime-test6-evidence.json for recorded coverage.',
    }
    # Independent native WORD store/read evidence for the field that blocked test 3.
    # The member displacement is decoded from the file, then compared to our layout.
    layout = header.with_name('player_fields.hpp').read_text()
    number_offset = int(re.search(r'kEntityNumber = (0x[0-9a-f]+)', layout)[1], 16)
    evidence = {
        'allocator_word_store': ('48 8B C3 66 89 53 ?? 40 88 BB AF 00 00 00 89 BB 04 01 00 00', 6, 3),
        'native_word_read': ('48 85 C9 74 0B 0F B7 49 ?? 33 D2 E8 ?? ?? ?? ?? 4C 8B 74 24 28', 8, 5),
    }
    result['entity_number'] = {'offset': hex(number_offset), 'width_bytes': 2, 'evidence': {}}
    for name, (pat, displacement, instruction) in evidence.items():
        expr = b''.join(b'.' if t == '??' else re.escape(bytes.fromhex(t)) for t in pat.split())
        matches = list(re.finditer(expr, text, re.S))
        if len(matches) != 1:
            raise ValueError(f'{name}: {len(matches)} native field matches, expected one')
        start = matches[0].start()
        actual_offset = text[start + displacement]
        if actual_offset != number_offset:
            raise ValueError(f'{name}: native entity-number offset {actual_offset:#x} differs from our {number_offset:#x}')
        result['entity_number']['evidence'][name] = {
            'match_count': 1, 'instruction_rva': hex(section.VirtualAddress + start + instruction),
            'decoded_member_offset': hex(actual_offset),
        }
    controls = header.with_name('player_controls.hpp').read_text()
    def member(name):
        return int(re.search(rf'{name} = (0x[0-9a-f]+)',controls)[1],16)
    def match_data(name):
        at=int(result['signatures'][name]['match_rva'],16)
        return at,pe.get_data(at,100)
    client_fields = (
        ('kEntityClient','kClientReadinessPattern',3,4),
        ('kEntityClient','kNoclipCommandPattern',3,4),
        ('kEntityClient','kClientInvulnerabilityClearPattern',8,4),
        ('kClientConnected','kClientReadinessPattern',20,4),
        ('kClientCheatFlags','kClientReadinessPattern',38,4),
        ('kClientCheatFlags','kNoclipCommandPattern',31,4),
        ('kClientOtherFlags','kClientDamageGuardPattern',2,1),
        ('kClientOtherFlags','kClientInvulnerabilityClearPattern',14,1),
        ('kReserveAmmo','kReserveReadPattern',3,4),
        ('kReserveAmmo','kReserveWritePattern',6,4),
    )
    decoded={}
    for field,signature,offset,width in client_fields:
        at,code=match_data(signature)
        actual=int.from_bytes(code[offset:offset+width],'little')
        if actual!=member(field):
            raise ValueError(f'{signature}: decoded {field} {actual:#x} differs from header {member(field):#x}')
        decoded.setdefault(field,[]).append({'signature':signature,'encoded_member_rva':hex(at+offset),'offset':hex(actual)})
    # Additional independent exact-build instruction anchors. They are checked
    # locally, not copied structures or old-engine offsets.
    for field,pat,offset,width in (
        ('kEntityTakeDamage','80 B9 ?? ?? ?? ?? 00 4D 8B F9 0F 29 78 B8 49 8B F8',2,4),
        ('kClientHealth','89 90 ?? ?? ?? ?? 48 8B D1 C7 44 24 20 0C 00 00 00',2,4),
        ('kClientMoveType','41 89 46 ?? F3 0F 10 05 ?? ?? ?? ?? E8 ?? ?? ?? ?? F3 0F 10 3D',3,1),
    ):
        expression=b''.join(b'.' if t=='??' else re.escape(bytes.fromhex(t)) for t in pat.split())
        matches=list(re.finditer(expression,text,re.S))
        if len(matches)!=1: raise ValueError(f'{field}: independent anchor has {len(matches)} matches')
        at=matches[0].start(); actual=int.from_bytes(text[at+offset:at+offset+width],'little')
        if actual!=member(field): raise ValueError(f'{field}: independent member {actual:#x} differs from header')
        decoded.setdefault(field,[]).append({'instruction_rva':hex(section.VirtualAddress+at),'offset':hex(actual)})
    stock_at,stock_code=match_data('kReserveReadPattern')
    if stock_code[22]!=8 or stock_code[26]!=int(re.search(r'kAmmoPoolCount = (\d+)',controls)[1]):
        raise ValueError('Native reserve stride/count differs from the partial table')
    for reg,consumer,offset in (('kSustainRegistrationPattern','kSustainConsumerPattern','20'),
                                ('kSpeedRegistrationPattern','kSpeedConsumerPattern','64')):
        if result['signatures'][reg]['rip_targets'][offset]!=result['signatures'][consumer]['rip_targets']['0']:
            raise ValueError(f'{reg}: registration and consumer use different slots')
    result['player']={'partial_fields':decoded,'reserve_entries':15,'reserve_stride':8,
        'sustain_slot_rva':result['signatures']['kSustainConsumerPattern']['rip_targets']['0'],
        'sustain_type':0,'speed_slot_rva':result['signatures']['kSpeedConsumerPattern']['rip_targets']['0'],
        'speed_type':5,'limits':'Static exact-build evidence; new player behavior and God regression await test7 owner run.'}
    def scalar_target(signature,offset):
        at,code=match_data(signature)
        return at+offset+8+struct.unpack_from('<i',code,offset+4)[0]
    height=scalar_target('kJumpRegistrationPattern',0)
    twice=scalar_target('kJumpImpulsePattern',8)
    for sig in ('kJumpClampPattern','kJumpStepPattern','kJumpCeilingPattern'):
        if scalar_target(sig,8)!=height: raise ValueError(f'{sig}: fixed height differs from registration')
    if twice!=height+4 or struct.unpack('<ff',pe.get_data(height,8))!=(39.0,78.0):
        raise ValueError('Native jump constants are not adjacent 39/78 floats')
    references={height:[],twice:[]}
    for at in range(len(text)-7):
        if text[at]==0xf3 and text[at+1]==0x0f and text[at+3]&0xc7==5:
            target=section.VirtualAddress+at+8+struct.unpack_from('<i',text,at+4)[0]
            if target in references: references[target].append(hex(section.VirtualAddress+at))
    if len(references[height])!=4 or len(references[twice])!=1:
        raise ValueError('Unexpected additional scalar references to jump constants')
    result['player']['jump']={'height_constant_rva':hex(height),'twice_height_constant_rva':hex(twice),
        'height_references':references[height],'twice_height_references':references[twice],
        'native_height':39,'enabled_height':117,'compatibility':'Runtime data-only float pair; jump_height dvar is not consumed by the observed jump paths.'}
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
