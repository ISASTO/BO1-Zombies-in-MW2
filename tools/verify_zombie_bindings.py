"""Independent exact-retail checks for the Test38 squad-follow and zombies bindings.

Parses include/zombie_bindings.hpp and checks every RVA, field offset and
builtin against the SHA256-gated executable: native script field descriptors,
builtin name tables, call sites and routine prologues.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
import pefile

EXPECTED_SHA256='fb7e8a987c371dea7b666091b0b9e2011c0cd9cd277b191ad453d11d557dd777'
IMAGE_BASE=0x140000000


def verify(path,include=Path('include')):
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=EXPECTED_SHA256:raise ValueError('Exact executable SHA256 mismatch')
    pe=pefile.PE(data=data)
    text=(include/'zombie_bindings.hpp').read_text()
    def value(name):
        m=re.search(r'\b'+name+r'\s*=\s*(0x[0-9a-fA-F]+|[0-9.]+f?)',text)
        if not m:raise ValueError(f'{name} missing from zombie_bindings.hpp')
        v=m[1].rstrip('f')
        return float(v) if '.' in v else int(v,0)
    def get(rva,n):return pe.get_data(rva,n)
    def call_target(at):
        if get(at,1)!=b'\xe8':raise ValueError(f'No call at {at:#x}')
        return at+5+struct.unpack('<i',get(at+1,4))[0]
    sections=[s for s in pe.sections if not s.Characteristics&0x20]
    blobs=[(s.VirtualAddress,s.get_data()) for s in sections]
    def cstring(rva):
        try:return pe.get_string_at_rva(rva).decode()
        except Exception:return None
    pointer_slots={}
    for va,blob in blobs:
        for i in range(0,len(blob)-16,8):
            p=struct.unpack_from('<Q',blob,i)[0]
            if IMAGE_BASE<p<IMAGE_BASE+0x5000000:pointer_slots.setdefault(p-IMAGE_BASE,[]).append(va+i)
    def string_rvas(name):
        needle=b'\0'+name.encode()+b'\0';out=[]
        for va,blob in blobs:
            i=blob.find(needle)
            while i>=0:out.append(va+i+1);i=blob.find(needle,i+1)
        return out
    def pairs(name):
        """(slot, following pointer) for every pointer to the string `name`."""
        found=[]
        for rva in string_rvas(name):
            for slot in pointer_slots.get(rva,[]):
                found.append((slot,struct.unpack('<Q',get(slot+8,8))[0]-IMAGE_BASE))
        return found
    report={'executable_sha256':EXPECTED_SHA256}
    # Actor script field descriptors {name*, offset u32, ?, type u64, setter*}.
    fields={'goalradius':('kActorGoalRadius',3),'goalheight':('kActorGoalHeight',3),'fixednode':('kActorFixedNode',2),
        'pacifist':('kActorPacifist',2),'grenadeawareness':('kActorGrenadeAwareness',3),'grenadeammo':('kActorGrenadeAmmo',0),
        'walkdist':('kActorWalkDist',3),'pathenemylookahead':('kActorPathEnemyLookahead',3),
        'pathenemyfightdist':('kActorPathEnemyFightDist',3),'meleeattackdist':('kActorMeleeAttackDist',3),
        'dropweapon':('kActorDropWeapon',0),'maxsightdistsqrd':('kActorMaxSightDistSq',3),
        'keepclaimednode':('kActorKeepClaimedNode',2),'requestarrivalnotify':('kActorRequestArrival',0),
        'desiredangle':('kActorDesiredAngle',3),'allowpain':('kActorAllowPain',2),
        'ignoresuppression':('kActorIgnoreSuppression',0),'nododgemove':('kActorNoDodgeMove',2)}
    for name,(constant,kind) in fields.items():
        matched=False
        for at,_ in pairs(name):
            offset=struct.unpack('<I',get(at+8,4))[0];ftype=struct.unpack('<Q',get(at+16,8))[0]
            if offset==value(constant) and ftype==kind:matched=True
        if not matched:raise ValueError(f'Actor field {name} differs from {constant}')
    report['actor_fields']=sorted(fields)
    # Entity field "angles" and "maxhealth".
    for name,offset,kind in (('angles',value('kEntityAngles'),6),('maxhealth',value('kEntityMaxHealth'),0)):
        if not any(struct.unpack('<II',get(at+8,8))==(offset,kind) for at,_ in pairs(name)):
            raise ValueError(f'Entity field {name} differs')
    # Goal entity effective-goal read (0x12b89c) and setter routines.
    if get(0x12b89c,8)!=bytes.fromhex('41 0f b7 88 c0 14 00 00'):raise ValueError('Native goal-entity read differs')
    handle=value('kEntityHandleSetRva')
    prologue=bytes(int(x,16) for x in re.search(r'kEntityHandleSetPrologue=\{([^}]+)\}',text)[1].split(','))
    if get(handle,len(prologue))!=prologue:raise ValueError('Entity-handle setter prologue differs')
    if call_target(value('kSetGoalEntityHandleCallRva'))!=handle or call_target(value('kGoalPositionHandleCallRva'))!=handle:
        raise ValueError('Goal setters do not call the entity-handle setter')
    if not any(f==value('kSetGoalEntityRva') for _,f in pairs('setgoalentity')):raise ValueError('setgoalentity method differs')
    if get(0x1244b0,18)!=bytes.fromhex('33 c0 66 c7 81 d9 02 00 00 00 00 48 89 81 a0 02 00 00'):
        raise ValueError('Native claimed-node/arrival clear differs')
    if get(0x13ac82,10)!=bytes.fromhex('c7 83 40 0d 00 00 00 00 00 80'):raise ValueError('Native fixednode setter differs')
    if struct.unpack('<f',get(0x385abc,4))[0]!=value('kGoalRadiusMinimum'):raise ValueError('Goal radius minimum differs')
    # Walk/run decision reads walkdist.
    if get(0x1551ff,8)!=bytes.fromhex('f3 0f 10 99 94 0f 00 00'):raise ValueError('Native walkdist consumer differs')
    # Script value pushes all begin with IncInParam.
    inc=value('kScrIncInParamRva')
    for name in ('kScrAddIntRva','kScrAddFloatRva','kScrAddStringRva','kScrAddVectorRva','kScrAddUndefinedRva'):
        code=get(value(name),24);at=code.index(0xe8)
        if call_target(value(name)+at)!=inc:raise ValueError(f'{name} does not push through IncInParam')
    # Builtin name tables.
    builtins={}
    for m in re.finditer(r'kBuiltin\w+\{"(\w+)",(0x[0-9a-f]+)\}',text):
        name,rva=m[1],int(m[2],16)
        if sum(1 for _,f in pairs(name) if f==rva)!=1:raise ValueError(f'Builtin {name} not at {rva:#x}')
        builtins[name]=hex(rva)
    report['builtins']=builtins
    # Command buffer and the native spmap issuer.
    if call_target(value('kCbufAddTextCallerRva'))!=value('kCbufAddTextRva'):raise ValueError('Cbuf_AddText caller differs')
    if get(value('kCbufAddTextRva')+15,5)!=bytes.fromhex('b9 24 00 00 00'):raise ValueError('Cbuf critical section differs')
    for literal in (b'spmap %s\n',b'spmap\0'):
        if data.find(literal)<0:raise ValueError('spmap command literal missing')
    # G_Damage argument layout: melee passes mod 8 (MOD_MELEE) as argument 8.
    if get(0x1a36eb,8)!=bytes.fromhex('c7 44 24 38 08 00 00 00') or call_target(0x1a370d)!=0x167d30:
        raise ValueError('Native melee damage argument layout differs')
    if value('kModMelee')!=8:raise ValueError('MOD_MELEE constant differs')
    # Hit-location names: none, helmet, head, neck, torso_upper, torso_lower.
    names=[cstring(struct.unpack('<Q',get(0x3c08f0-4*8+8*k,8))[0]-IMAGE_BASE) for k in range(6)]
    if names!=['none','helmet','head','neck','torso_upper','torso_lower']:raise ValueError(f'Hit locations differ: {names}')
    # Client view angles/height used by getplayerangles/getplayerviewheight.
    if get(0x160dac,14)!=bytes.fromhex('48 8b 8b 08 01 00 00 48 81 c1 08 01 00 00'):
        raise ValueError('Native view-angle read differs')
    if get(0x160e13,8)!=bytes.fromhex('f3 0f 10 80 18 01 00 00'):raise ValueError('Native view-height read differs')
    report.update({'goal_entity':'setgoalentity replica: 0x1244b0 clear, 0x15b190 handle, node/volume zero',
        'command_buffer':'Cbuf_AddText(0,"spmap <map>\\n") (critical section 0x24)',
        'damage_args':'G_Damage(targ,inflictor,attacker,dir,point,damage,dflags,mod,weapon,hitloc,...)',
        'scope':'Static exact-build checks; gameplay requires owner testing'})
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('--include',type=Path,default=Path('include'))
    parser.add_argument('--output',type=Path)
    args=parser.parse_args();result=verify(args.executable,args.include)
    rendered=json.dumps(result,indent=2)+'\n'
    if args.output:args.output.write_text(rendered)
    print(rendered,end='')
