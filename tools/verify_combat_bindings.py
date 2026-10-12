"""Independent instruction/descriptor checks for exact-retail combat and recoil controls."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP, X86_OP_IMM


def verify(path, header):
    data=path.read_bytes()
    expected='fb7e8a987c371dea7b666091b0b9e2011c0cd9cd277b191ad453d11d557dd777'
    if hashlib.sha256(data).hexdigest()!=expected:raise ValueError('Exact executable hash mismatch')
    pe=pefile.PE(data=data);base=pe.OPTIONAL_HEADER.ImageBase
    text=header.read_text()
    def value(name):return int(re.search(r'\b'+name+r'\s*=\s*(0x[0-9a-f]+|\d+)',text)[1],0)
    def branch(at,op=0xe8):
        b=pe.get_data(at,5)
        if b[0]!=op:raise ValueError(f'Expected branch opcode at {at:x}')
        return at+5+struct.unpack_from('<i',b,1)[0]
    calls={}
    for role in ('FrameClock','SaveRead','SaveWrite'):
        at=value('k'+role+'CallRva');target=value('k'+role+'Rva')
        if branch(at)!=target:raise ValueError(f'{role} caller/callee disagreement')
        if (at&15)+5>16:raise ValueError('Call does not fit one atomic 16-byte block')
        calls[role]={'call_rva':hex(at),'function_rva':hex(target),'atomic_block_rva':hex(at&~15)}
    prologue=bytes(int(n,16) for n in re.search(r'kActorInitPrologue=\{([^}]+)',text)[1].split(',') if n.strip())
    init=value('kActorInitRva')
    if pe.get_data(init,len(prologue))!=prologue:raise ValueError('Actor init complete-instruction prologue mismatch')
    decoder=Cs(CS_ARCH_X86,CS_MODE_64);decoder.detail=True
    instructions=list(decoder.disasm(prologue,base+init))
    if sum(i.size for i in instructions)!=len(prologue):raise ValueError('Trampoline truncates an instruction')
    for i in instructions:
        if i.mnemonic in ('call','jmp') or any(o.type==X86_OP_MEM and o.mem.base==X86_REG_RIP for o in i.operands):
            raise ValueError('Copied prologue needs PC-relative relocation')
    # The native kill method has twelve arguments: target, inflictor, attacker,
    # direction, point, damage, flags, MOD, weapon, hitloc, time and surface.
    if branch(0x1927fe)!=value('kDamageRva'):raise ValueError('Native kill method damage binding disagrees')
    kill=list(decoder.disasm(pe.get_data(0x1927c7,0x192803-0x1927c7),base+0x1927c7))
    stack={}
    for i in kill:
        if i.mnemonic=='mov' and len(i.operands)==2 and i.operands[0].type==X86_OP_MEM:
            dst,src=i.operands
            if i.reg_name(dst.mem.base)=='rsp' and src.type==X86_OP_IMM:stack[dst.mem.disp]=src.imm
    if stack.get(0x28)!=0x3fffffff or stack.get(0x40)!=0xffffffff:raise ValueError('Kill damage/weapon argument mismatch')
    if branch(0x187360,0xe9)!=value('kDObjUpdateRva'):raise ValueError('Setmodel native DObj-update tail disagrees')
    # Independent native renderer assembles 31 additional models from +0x208,
    # tags from +0x246 and per-attachment flags from +0x148.
    if value('kAiAttachmentModels')!=0x208 or value('kAiAttachmentTags')!=0x246 or value('kAiAttachmentFlags')!=0x148 or value('kAiAttachmentCount')!=31:
        raise ValueError('Appearance attachment layout differs from native DObj builder')
    if pe.get_data(0x19ef1f,1)!=b'\x90' or pe.get_data(0x19ef5f,3)!=bytes.fromhex('83 fa 1f'):
        raise ValueError('Native appearance loop count changed')
    field=pe.get_data(0x3464c0,16);name,offset,kind=struct.unpack('<QII',field)
    if pe.get_data(name-base,16).split(b'\0')[0]!=b'model' or offset!=value('kAiModelIndex') or kind!=15:
        raise ValueError('Native model field descriptor disagrees')
    for at,key,constant in ((0x3360a0,b'ignoreme','kSentientIgnoreMe'),(0x3360c8,b'ignoreall','kSentientIgnoreAll')):
        name,offset,kind=struct.unpack('<QQI',pe.get_data(at,20))
        if pe.get_data(name-base,20).split(b'\0')[0]!=key or offset!=value(constant) or kind!=2:
            raise ValueError('Native sentient boolean descriptor disagrees')
    # The setter for combatmode=0 independently writes the same actor offsets.
    mode=pe.get_data(0x13a9fd,17)
    if mode[:10]!=bytes.fromhex('c7 83 4c 0d 00 00 00 00 00 00') or mode[10:17]!=bytes.fromhex('c6 83 49 0d 00 00 00'):
        raise ValueError('Native default combat-mode fields disagree')
    name,offset,kind=struct.unpack('<QQI',pe.get_data(0x335998,20))
    if pe.get_data(name-base,32).split(b'\0')[0]!=b'lastenemysightpos' or offset!=value('kAiEnemySightPosition') or kind!=6:
        raise ValueError('Native enemy sight-position descriptor disagrees')
    setter=pe.get_data(0x13ac00,21)
    if struct.unpack_from('<I',setter,3)[0]!=value('kAiEnemySightPosition') or struct.unpack_from('<I',setter,16)[0]!=value('kAiEnemySightValid'):
        raise ValueError('Native enemy sight-position setter offsets disagree')
    # Native player_recoilscaleon(percentage) sets client weaponFlags bit 10
    # and its percentage field; off clears that bit. The recoil consumer gates
    # a percentage/100 multiply using exactly those same fields.
    controls=(header.parent/'control_extensions.hpp').read_text()
    def control(name):return int(re.search(r'\b'+name+r'\s*=\s*(0x[0-9a-f]+|\d+)',controls)[1],0)
    flags=control('kClientWeaponFlags');scale=control('kClientRecoilScale');mask=control('kRecoilScaleFlag')
    if pe.get_data(0x16355a,16)!=b'\x81\x8b'+struct.pack('<II',flags,mask)+b'\x89\xbb'+struct.pack('<I',scale):
        raise ValueError('Native recoil on setter disagrees')
    if pe.get_data(0x1635c3,10)!=b'\x81\xa0'+struct.pack('<II',flags,(~mask)&0xffffffff):
        raise ValueError('Native recoil off setter disagrees')
    consumer=list(decoder.disasm(pe.get_data(0x87248,0x28),base+0x87248))
    offsets={o.mem.disp for i in consumer for o in i.operands if o.type==X86_OP_MEM}
    if flags not in offsets or scale not in offsets or not any(i.mnemonic=='bt' and i.operands[-1].type==X86_OP_IMM and i.operands[-1].imm==10 for i in consumer):
        raise ValueError('Independent recoil consumer offsets/bit disagree')
    divide=next(i for i in consumer if i.mnemonic=='divss')
    constant=divide.address+divide.size+divide.operands[-1].mem.disp-base
    if struct.unpack('<f',pe.get_data(constant,4))[0]!=100.0:raise ValueError('Recoil percentage denominator disagrees')
    for table,name,target in ((0x339860,b'player_recoilscaleon',0x163500),(0x339878,b'player_recoilscaleoff',0x163580)):
        literal,function=struct.unpack('<QQ',pe.get_data(table,16))
        if pe.get_data(literal-base,32).split(b'\0')[0]!=name or function-base!=target:raise ValueError('Native recoil method table binding disagrees')
    return {'executable_sha256':expected,'calls':calls,'actor_init_rva':hex(init),'copied_prologue_bytes':len(prologue),
            'copied_prologue_instructions':len(instructions),'damage_rva':hex(value('kDamageRva')),
            'kill_argument_count':12,'kill_damage':0x3fffffff,'kill_weapon':-1,
            'dobj_update_rva':hex(value('kDObjUpdateRva')),'model_field':'0x284',
            'sentient_ignoreme':'0x14','sentient_ignoreall':'0x15','default_combat_mode':0,
            'appearance_attachment_models':'0x208','appearance_attachment_tags':'0x246',
            'appearance_attachment_flags':'0x148','appearance_attachment_count':31,
            'enemy_sight_override_flag':'0x309','enemy_sight_override_position':'0x30c',
            'enemy_handle_field':'sentient+0x44, low WORD native entity-number+1',
            'player_recoil_flags':hex(flags),'player_recoil_scale':hex(scale),'player_recoil_mask':hex(mask),
            'player_recoil_percent_denominator':100.0,'player_recoil_target_percent':0,
            'hook_publication':'Clock call publishes first; remaining hooks publish on the next server-clock callback.',
            'native_reentrancy':'Recursive ownership lock, snapshot iteration and deferred cleanup prevent NPC freeing during active native mod calls.',
            'retail_execution':'Not executed in this Linux environment; owner test required.',
            'actor_capacity':32,'actor_capacity_expansion':'Not implemented: snapshot, save, scratch and perception dependencies remain.'}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('--header',type=Path,default=Path('include/native_hooks.hpp'))
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    result=verify(args.executable,args.header)
    rendered=json.dumps(result,indent=2)+'\n'
    if args.output:args.output.write_text(rendered)
    print(rendered,end='')
