"""Independent exact-retail friendly-fire and native checkpoint string checks."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
import pefile


def verify(path,include=Path('include')):
    data=path.read_bytes()
    expected='fb7e8a987c371dea7b666091b0b9e2011c0cd9cd277b191ad453d11d557dd777'
    if hashlib.sha256(data).hexdigest()!=expected:raise ValueError('Exact executable SHA256 mismatch')
    pe=pefile.PE(data=data);base=pe.OPTIONAL_HEADER.ImageBase
    controls=(include/'control_extensions.hpp').read_text();hooks=(include/'native_hooks.hpp').read_text()
    def value(text,name):return int(re.search(r'\b'+name+r'\s*=\s*(0x[0-9a-f]+|\d+)',text)[1],0)
    def rip(at):
        b=pe.get_data(at,7)
        if b[:3] not in (b'\x48\x8b\x05',b'\x48\x89\x05',b'\x48\x8d\x0d'):raise ValueError(f'RIP opcode mismatch at {at:x}')
        return at+7+struct.unpack_from('<i',b,3)[0]
    def literal(at):return pe.get_data(at,100).split(b'\0')[0]
    distance=value(controls,'kFriendlyFireDistanceSlotRva');scale=value(controls,'kFriendlyFireScaleSlotRva')
    parameters=value(hooks,'kScriptParametersRva');depth=value(hooks,'kScriptExecuteDepthRva')
    def operand(at,offset,length):return at+length+struct.unpack('<i',pe.get_data(at+offset,4))[0]
    if pe.get_data(0x223180,2)!=bytes.fromhex('8b 05') or operand(0x223180,2,6)!=parameters+12:
        raise ValueError('Native builtin outgoing parameter counter differs')
    if pe.get_data(0x223994,3)!=bytes.fromhex('48 8b 05') or operand(0x223994,3,7)!=parameters:
        raise ValueError('Native notification stack-top pointer differs')
    if operand(0x22398a,2,7)!=parameters+12 or operand(0x2239c8,2,6)!=parameters+8:
        raise ValueError('Native notification incoming/outgoing counters differ')
    if operand(0x224934,2,6)!=depth or operand(0x223910,2,10)!=depth or pe.get_data(0x223916,4)!=bytes.fromhex('ff ff ff ff'):
        raise ValueError('Native execution depth/idle value differs')
    if value(hooks,'kScriptNotifyRva')!=0x223970:
        raise ValueError('Native notification routine differs')
    vm=value(hooks,'kScriptVmStateRva');stack=value(hooks,'kScriptStackBaseRva');frame=value(hooks,'kScriptFrameBaseRva')
    for at,offset,length,expected_target in (
        (0x224777,3,7,vm),(0x2237f0,3,7,stack),(0x223867,2,10,stack+8),(0x223804,3,7,frame),
        (0x2237f7,2,6,value(hooks,'kScriptFunctionCountRva')),
        (0x22380b,3,7,value(hooks,'kScriptFramePointerRva')),
        (0x223812,3,7,value(hooks,'kScriptLocalVariablesBaseRva')),
        (0x223819,3,7,value(hooks,'kScriptLocalVariablesPointerRva'))):
        if operand(at,offset,length)!=expected_target:raise ValueError(f'Native VM work-context binding differs at {at:x}')
    if pe.get_data(0x22386d,4)!=bytes.fromhex('07 00 00 00'):
        raise ValueError('Native initial stack sentinel type differs')
    damage=value(hooks,'kDamageRva')
    prologue=bytes(int(n,16) for n in re.search(r'kDamagePrologue=\{([^}]+)',hooks)[1].split(',') if n.strip())
    if pe.get_data(damage,len(prologue))!=prologue or len(prologue)!=15 or damage&15:
        raise ValueError('Damage hook prologue/alignment differs')
    # Both null attribution arguments are replaced with the native world
    # entity before any damage notification or actor pain/death callback.
    if pe.get_data(0x167d99,3)!=bytes.fromhex('48 8d 35') or operand(0x167d99,3,7)!=0xd9e070+2046*0x2a0:
        raise ValueError('Native world-entity damage attribution differs')
    if pe.get_data(0x167da0,17)!=bytes.fromhex('48 85 ed 4c 8b ee 4c 0f 45 ed 48 85 ff 48 0f 45 f7'):
        raise ValueError('Native null inflictor/attacker substitutions differ')
    if literal(rip(0x1735bb))!=b'g_friendlyfireDist' or rip(0x1735e8)!=distance or rip(0x1acc97)!=distance:
        raise ValueError('Friendly aim-block registration/consumer slot mismatch')
    if literal(rip(0x1735ef))!=b'g_friendlyfireDamageScale' or rip(0x17361a)!=scale or rip(0x167f13)!=scale:
        raise ValueError('Friendly damage-scale registration/consumer slot mismatch')
    if pe.get_data(0x1acc9e,5)!=bytes.fromhex('f3 0f 10 40 10') or pe.get_data(0x1acca7,3)!=bytes.fromhex('0f 2f c6'):
        raise ValueError('Aim-block current-value/distance comparison differs')
    if pe.get_data(0x167f21,5)!=bytes.fromhex('f3 0f 59 40 10'):
        raise ValueError('Friendly damage current-value multiply differs')
    intern=value(hooks,'kAiInternStringRva');code=pe.get_data(intern,10)
    if code[:6]!=bytes.fromhex('ba 01 00 00 00 e9') or intern+10+struct.unpack_from('<i',code,6)[0]!=0x21ac70:
        raise ValueError('Native string-reference wrapper binding differs')
    # The native string factory computes the length and creates a user-1 ref.
    if pe.get_data(0x21ac8b,5)!=bytes.fromhex('ba 01 00 00 00'):
        raise ValueError('Native ownership-string factory user reference differs')
    table=value(hooks,'kAiEntitySaveFieldsRva')
    fields={}
    for n in range(128):
        offset,kind,_=struct.unpack('<QII',pe.get_data(table+n*16,16))
        if not kind:break
        fields[offset]=kind
    if fields.get(0x146)!=1 or fields.get(0x144)!=1 or fields.get(0x110)!=6 or fields.get(0x118)!=7:
        raise ValueError('Native entity save descriptors do not support targetname/actor/sentient restoration')
    # Save field kind 1 writes the referenced script string as text, not a stale
    # process-local numeric string ID. The read descriptor uses the same table.
    if pe.get_data(0x1aec41,4)!=bytes.fromhex('41 0f b7 00'):
        raise ValueError('Native script-string serializer WORD read differs')
    if pe.get_data(0x1aec4c,5)[0]!=0xe8 or 0x1aec51+struct.unpack('<i',pe.get_data(0x1aec4d,4))[0]!=0x21a370:
        raise ValueError('Native saved script string text conversion differs')
    if pe.get_data(0x1aec5f,5)[0]!=0xe9 or 0x1aec64+struct.unpack('<i',pe.get_data(0x1aec60,4))[0]!=0x270ba0:
        raise ValueError('Native checkpoint text writer differs')
    return {'executable_sha256':expected,'friendly_aim_block_slot_rva':hex(distance),
        'friendly_damage_scale_slot_rva':hex(scale),'enabled_aim_block_distance':0,
        'enabled_client_damage_scale':1,'off_behavior':'Guarded restoration of pre-override native values and normal player damage attribution',
        'enabled_allied_damage_attribution':'Native world entity instead of player; damage/death callbacks retain native processing',
        'script_parameters_rva':hex(parameters),'script_execute_depth_rva':hex(depth),'idle_script_execute_depth':-1,
        'cleanup_parameter_isolation':'Preserve caller stack/refcounts; hide incoming/outgoing counts only for the hidden native operation',
        'external_vm_context':'Idle native frame/stack/local-variable base; caller context restored after balanced work',
        'script_vm_state_rva':hex(vm),'script_stack_base_rva':hex(stack),'script_frame_base_rva':hex(frame),
        'damage_hook_rva':hex(damage),'damage_trampoline_bytes':len(prologue),
        'native_string_factory_rva':hex(intern),'native_entity_save_fields_rva':hex(table),
        'targetname_save_kind':1,'targetname_saved_as':'Text through SL_ConvertToString/MemFile_WriteCString',
        'checkpoint_write_behavior':'Keep live native actors/helpers; do not delete or enqueue save recovery',
        'checkpoint_read_behavior':'Clean current owned allocations before native read; adopt validated tagged actor/helper pairs afterward',
        'retail_execution':'Windows autosave/death/relaunch and friendly-fire gameplay require owner testing',
        'capacity':32,'expanded_capacity_enabled':False}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('--include',type=Path,default=Path('include'))
    parser.add_argument('--output',type=Path)
    args=parser.parse_args();result=verify(args.executable,args.include)
    rendered=json.dumps(result,indent=2)+'\n'
    if args.output:args.output.write_text(rendered)
    print(rendered,end='')
