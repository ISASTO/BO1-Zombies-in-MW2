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
    text=(include/'zombie_bindings.hpp').read_text()+(include/'native_hooks.hpp').read_text()
    def value(name):
        m=re.search(r'\b'+name+r'\s*=\s*(0x[0-9a-fA-F]+|[0-9.]+f?)',text)
        if not m:raise ValueError(f'{name} missing from zombie_bindings.hpp')
        v=m[1] if m[1].lower().startswith('0x') else m[1].rstrip('f')
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
        'ignoresuppression':('kActorIgnoreSuppression',0),'nododgemove':('kActorNoDodgeMove',2),
        'allowdeath':('kActorAllowDeath',2)}
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
    # ---- Test39: level-load hook, Spec Ops end paths, BO1 animation import ----
    def jump_target(at):
        if get(at,1)!=b'\xe9':raise ValueError(f'No jump at {at:#x}')
        return at+5+struct.unpack('<i',get(at+1,4))[0]
    def bytes_of(name):
        return bytes(int(x,16) for x in re.search(name+r'=\{([^}]+)\}',text)[1].split(','))
    def rip(at,disp,length):return at+length+struct.unpack('<i',get(at+disp,4))[0]
    if jump_target(value('kLevelScriptsLoadedJumpRva'))!=value('kScrEndLoadScriptsRva'):raise ValueError('Level script loader tail jump differs')
    if call_target(0x170b91)!=0x195200:raise ValueError('GScr_LoadScripts caller differs')
    for rva,prologue in (('kScrEndLoadScriptsRva','kScrEndLoadScriptsPrologue'),('kScrGetFunctionHandleRva','kScrGetFunctionHandlePrologue'),
                         ('kDbFindXAssetEntryRva','kDbFindXAssetEntryPrologue'),('kSlGetStringRva','kSlGetStringPrologue'),
                         ('kScrAddAnimRva','kScrAddAnimPrologue')):
        expected=bytes_of(prologue)
        if get(value(rva),len(expected))!=expected:raise ValueError(f'{rva} prologue differs')
    # Scr_EndLoadScripts frees the compiler's file/function table (scriptsPos 0x1443b8c).
    if get(0x216702,6)!=bytes.fromhex('8b 0d 84 d4 22 01') or get(0x216726,6)!=bytes.fromhex('89 1d 60 d4 22 01'):
        raise ValueError('Scr_EndLoadScripts no longer releases scriptsPos')
    # Scr_GetFunctionHandle: handle = code position - program buffer, below program size.
    if rip(0x21698d,2,6)!=value('kScrProgramBufferRva') or rip(0x216993,2,6)!=value('kScrProgramSizeRva'):
        raise ValueError('Program buffer references differ')
    # The script loader resolves codescripts' "main" through the same routine.
    if call_target(0x184c67)!=value('kScrGetFunctionHandleRva'):raise ValueError('Scr_GetFunctionHandle caller differs')
    # VM_Execute dispatch: byte opcode from fs.pos, 0x8c entries, table entry 0 is OP_End.
    if get(value('kVmDispatchRva')+29,7)!=bytes.fromhex('41 81 f8 8b 00 00 00'):raise ValueError('VM opcode bound differs')
    if struct.unpack('<I',get(value('kVmDispatchRva')+42,4))[0]!=value('kVmOpcodeTableRva'):raise ValueError('VM opcode table differs')
    if struct.unpack('<I',get(value('kVmOpcodeTableRva'),4))[0]!=value('kVmOpEndHandlerRva'):raise ValueError('OP_End handler differs')
    end=value('kVmOpEndHandlerRva')
    if call_target(end+6)!=0x21d4b0 or call_target(end+0x13)!=0x2216d0 or get(0x224a34,3)!=bytes.fromhex('83 f9 07'):
        raise ValueError('OP_End no longer kills the thread and unwinds to VAR_CODEPOS')
    # Asset database: hash of the name into 37000 buckets, 24-byte entries, type at +0, header at +8.
    if get(0xef55b,6)!=bytes.fromhex('69 c2 88 90 00 00') or get(0xef566,8)!=bytes.fromhex('0f b7 8c 47 20 87 a7 00') or get(0xef576,7)!=bytes.fromhex('48 8d 9f 60 e8 a8 00'):
        raise ValueError('DB_FindXAssetEntry hash/entry layout differs')
    if cstring(struct.unpack('<Q',get(value('kAssetNamesRva')+8*value('kAssetTypeXAnimParts'),8))[0]-IMAGE_BASE)!='xanim':raise ValueError('xanim asset type differs')
    # SL_GetString(str,user,type): strlen+1 then SL_GetStringOfSize, which refuses past the string limit.
    if jump_target(0x21b2ef)!=0x21af60:raise ValueError('SL_GetString tail differs')
    # Scr_AddAnim pushes VAR_ANIMATION (13); Scr_GetAnim requires it and checks the tree.
    if call_target(value('kScrAddAnimRva')+9)!=value('kScrIncInParamRva') or get(0x222085,7)!=bytes.fromhex('c7 40 08 0d 00 00 00'):
        raise ValueError('Scr_AddAnim differs')
    if get(0x22286f,4)!=bytes.fromhex('83 7e 08 0d') or call_target(0x222891)!=value('kAnimTreeByIndexRva') or call_target(0x22289c)!=0x2968e0:
        raise ValueError('Scr_GetAnim tree check differs')
    if get(0x2968e0,4)!=bytes.fromhex('48 8b 01 c3'):raise ValueError('XAnimTree anims accessor differs')
    # animscripted -> 0x183150: anim from argument 3 in the entity's tree (0x19fd90).
    if jump_target(0x181c9c)!=0x183150 or call_target(0x18318b)!=value('kEntAnimTreeRva') or get(0x183227,8)!=bytes.fromhex('48 8b d7 b9 03 00 00 00') or call_target(0x18322f)!=0x222840:
        raise ValueError('animscripted argument layout differs')
    # XAnim_s entries: XAnimIsLeafNode (numAnims at 16*i+8) and XAnimGetLength (parts at 16*(i+1), numframes 0x12, framerate 0x2c).
    if get(0x29d110,15)!=bytes.fromhex('8b c2 48 03 c0 66 83 7c c1 08 00 0f 94 c0 c3'):raise ValueError('XAnimIsLeafNode differs')
    if get(0x29c920,32)!=bytes.fromhex('48 8b c1 8b ca 48 ff c1 48 03 c9 48 8b 0c c8 0f b7 41 12 66 0f 6e c0 0f 5b c0 f3 0f 5e 41 2c c3'):
        raise ValueError('XAnimGetLength differs')
    if get(0x29d12c,4)!=bytes.fromhex('49 8b 51 78') or get(0x29d135,5)!=bytes.fromhex('45 0f b6 49 1f'):raise ValueError('XAnimParts notify offsets differ')
    anim_builtins={}
    for name in ('animscripted','stopanimscripted','hide','show'):
        rva=builtins.get(name) or value('kBuiltin'+{'animscripted':'AnimScripted','stopanimscripted':'StopAnimScripted','hide':'Hide','show':'Show'}[name])
        rva=int(rva,16) if isinstance(rva,str) else rva
        if sum(1 for _,f in pairs(name) if f==rva)!=1:raise ValueError(f'Method {name} not at {rva:#x}')
        anim_builtins[name]=hex(rva)
    # Script threads on entities, as animscripted runs animscripts\scripted::init:
    # Scr_ExecEntThread(ent,handle,count) -> Scr_ExecEntThreadNum(ent->number,0,handle,count),
    # then Scr_FreeThread(id) -> RemoveRefToObject.
    for rva,prologue in (('kScrExecEntThreadNumRva','kScrExecEntThreadNumPrologue'),('kScrExecEntThreadRva','kScrExecEntThreadPrologue'),
                         ('kScrFreeThreadRva','kScrFreeThreadPrologue')):
        expected=bytes_of(prologue)
        if get(value(rva),len(expected))!=expected:raise ValueError(f'{rva} prologue differs')
    if jump_target(value('kScrExecEntThreadRva')+12)!=value('kScrExecEntThreadNumRva'):raise ValueError('Scr_ExecEntThread tail differs')
    if jump_target(value('kScrFreeThreadRva')+3)!=0x21db10:raise ValueError('Scr_FreeThread tail differs')
    if call_target(value('kAnimScriptedExecCallRva'))!=value('kScrExecEntThreadRva') or call_target(value('kAnimScriptedFreeCallRva'))!=value('kScrFreeThreadRva'):
        raise ValueError('animscripted no longer runs scripted::init through the thread routines')
    # Handle is program-buffer relative; VM_Execute runs it; the return value is
    # popped and inparamcount (scrVmPub+8) decremented before returning the id.
    if rip(0x22270a,3,7)!=value('kScrProgramBufferRva') or call_target(0x22274a)!=0x224700 or rip(0x22278b,2,6)!=value('kScriptParametersRva')+8:
        raise ValueError('Scr_ExecEntThreadNum layout differs')
    # VM_Execute releases pending outparams first and leaves inparamcount = remaining + 1.
    if rip(0x224712,2,7)!=value('kScriptParametersRva')+12 or get(0x22485b,9)!=bytes.fromhex('8d 4e 01 89 0d 54 fc 6c 01'):
        raise ValueError('VM_Execute parameter accounting differs')
    report['script_threads']={'exec_ent_thread_num':hex(value('kScrExecEntThreadNumRva')),'free_thread':hex(value('kScrFreeThreadRva')),
        'setup_functions':re.findall(r'\{"(maps/[a-z_]+)","([a-z_]+)",([0-9])\}',text)}
    report['level_prep']={'load_hook':hex(value('kLevelScriptsLoadedJumpRva')),'get_function_handle':hex(value('kScrGetFunctionHandleRva')),
        'op_end':hex(end),'find_asset_entry':hex(value('kDbFindXAssetEntryRva')),'sl_get_string':hex(value('kSlGetStringRva')),
        'scr_add_anim':hex(value('kScrAddAnimRva')),'anim_builtins':anim_builtins,
        'specops_end_functions':re.findall(r'\{"(maps/[a-z_]+)","([a-z_]+)"\}',text)}
    # ---- Test40: BO1 character models ----
    # SV_SpawnServer -> Com_LoadLevelFastFiles(map); its last call loads the
    # level zone as XZoneInfo{name=rsi, allocFlags=4, freeFlags=0}, 1 zone, async.
    if call_target(0x23284a)!=value('kLoadLevelFastFilesRva'):raise ValueError('Com_LoadLevelFastFiles caller differs')
    zone_call=value('kLevelZoneLoadCallRva')
    if call_target(zone_call)!=value('kDbLoadXAssetsRva'):raise ValueError('Level zone DB_LoadXAssets call differs')
    if not value('kLoadLevelFastFilesRva')<zone_call<value('kDbLoadXAssetsRva'):raise ValueError('Level zone call is outside Com_LoadLevelFastFiles')
    if get(zone_call-0x1a,0x1a)!=bytes.fromhex('45 33 c0 48 89 74 24 20 48 8d 4c 24 20 48 c7 44 24 28 04 00 00 00 41 8d 50 01'):
        raise ValueError('Level zone XZoneInfo layout differs')
    if value('kLevelZoneAllocFlags')!=4:raise ValueError('Level zone allocation flags differ')
    if get(value('kDbLoadXAssetsRva'),15)!=bytes_of('kDbLoadXAssetsPrologue'):raise ValueError('DB_LoadXAssets prologue differs')
    # DB_LoadXAssets queues each zone: name copied (64 chars), allocFlags kept, 16-byte XZoneInfo stride.
    if get(0xf0f6c,6)!=bytes.fromhex('41 b8 40 00 00 00') or get(0xf0f7f,2)!=bytes.fromhex('8b 07') or get(0xf0f91,4)!=bytes.fromhex('48 83 c7 10'):
        raise ValueError('DB_LoadXAssets queue layout differs')
    # Test42: the Test40/41 freeze. IncInParam (0x221ff0) releases the out-params
    # whenever anything is pushed; animscripted takes its notify name with
    # Scr_GetConstString(0), pushes the scripted::init arguments, then pushes that
    # id again with Scr_AddConstString. A string only the caller's parameter kept
    # alive is freed in between, so the DLL pins its strings like script literals
    # with the same SL_GetString call Scr_AddString makes (type 7).
    if get(0x221ff4,7)!=bytes.fromhex('83 3d c1 24 6d 01 00') or call_target(0x22200a)!=0x21dc30:
        raise ValueError('IncInParam out-param release differs')
    if call_target(0x222309)!=0x221ff0 or call_target(0x222321)!=0x21a670 or get(0x21a68f,6)!=bytes.fromhex('41 b9 07 00 00 00'):
        raise ValueError('Scr_AddString interning differs')
    if value('kScrAddStringType')!=7:raise ValueError('Pinned string type differs')
    if call_target(0x183257)!=0x222c40 or get(0x18334a,7)!=bytes.fromhex('8b 8c 24 e0 00 00 00') or call_target(0x183351)!=0x222170:
        raise ValueError('animscripted notify-name reuse differs')
    # Test41: an actor's path point count, as Path_Exists and the pathgoalpos getter read it.
    if get(0x144d30,11)!=bytes.fromhex('80 b9 80 03 00 00 00 0f 9f c0 c3') or get(0x127bb0,8)!=bytes.fromhex('48 81 c1 a4 0f 00 00 e9'):
        raise ValueError('Path_Exists / Actor_HasPath differ')
    if get(0x127bb7,1)!=b'\xe9' or 0x127bb7+5+struct.unpack('<i',get(0x127bb8,4))[0]!=0x144d30 or call_target(0x13ac49)!=0x127bb0:
        raise ValueError('pathgoalpos getter chain differs')
    if value('kActorPath')!=0xfa4 or value('kPathPointCount')!=0x380:raise ValueError('Actor path constants differ')
    if not any(struct.unpack('<Q',get(at+32,8))[0]-IMAGE_BASE==0x13ac40 for at,_ in pairs('pathgoalpos')):
        raise ValueError('pathgoalpos field getter differs')
    # Test41: the database thread loads the queued names in list order (0x44-byte
    # records from 0xb69450, one after another), so the model zone is put first.
    if get(0xf19b5,14)!=bytes.fromhex('48 83 c5 44 49 83 c7 44 49 83 ec 01 0f 85') or rip(0xf17e7,3,7)!=0xb69450:
        raise ValueError('Database thread queue order differs')
    # The database thread opens <exe dir>\zone\<language>\<name>.ff (names starting mp_ use zone\dlc).
    for rva,text_ in ((0x332468,'%s.ff'),(0x35a1c8,'zone\\%s\\'),(0x357060,'%s\\%s%s')):
        if cstring(rva)!=text_:raise ValueError(f'Zone path literal at {rva:#x} differs')
    if call_target(0xf1810)!=0x290a80 or call_target(0xf181d)!=0x2740c0 or call_target(0x2740d3)!=0x274420:
        raise ValueError('Zone file path construction differs')
    # Unsigned fastfiles (IWffu100) at version 0x114 with a zlib stream.
    if get(0xdb82a,10)!=bytes.fromhex('48 bd 49 57 66 66 75 31 30 30') or get(0xdb85d,7)!=bytes.fromhex('41 81 f9 14 01 00 00'):
        raise ValueError('Fastfile header check differs')
    # G_ModelIndex: model configstrings 0x897+i for i<0x200, adding only while level.initializing.
    if get(value('kGModelIndexRva'),12)!=bytes_of('kGModelIndexPrologue'):raise ValueError('G_ModelIndex prologue differs')
    if get(0x1a02e0,6)!=bytes.fromhex('8d 8b 97 08 00 00') or get(0x1a02fc,6)!=bytes.fromhex('81 fb 00 02 00 00'):
        raise ValueError('G_ModelIndex configstring range differs')
    if value('kModelConfigstringBase')!=0x897 or value('kMaxModels')!=0x200:raise ValueError('Model configstring constants differ')
    if rip(0x1a0304,2,7)!=value('kLevelInitializingRva') or rip(0x1a02eb,3,7)!=value('kEmptyStringIdRva'):
        raise ValueError('G_ModelIndex initializing/empty-string references differ')
    if rip(0x2321d3,3,7)!=value('kConfigstringsRva'):raise ValueError('Configstring table differs')
    # G_InitGame sets level.initializing before GScr_LoadScripts (via 0x170b20).
    if get(0x1713d3,10)!=bytes.fromhex('c7 05 db 5a c2 00 01 00 00 00') or call_target(0x1715e7)!=0x170b20 or rip(0x1713d3,2,10)!=value('kLevelInitializingRva'):
        raise ValueError('G_InitGame initializing window differs')
    if cstring(struct.unpack('<Q',get(value('kAssetNamesRva')+8*value('kAssetTypeXModel'),8))[0]-IMAGE_BASE)!='xmodel':raise ValueError('xmodel asset type differs')
    model_builtins={}
    for name,const in (('setmodel','kBuiltinSetModel'),('attach','kBuiltinAttach'),('detachall','kBuiltinDetachAll')):
        rva=int(builtins[name],16)
        if sum(1 for _,f in pairs(name) if f==rva)!=1:raise ValueError(f'Method {name} not at {rva:#x}')
        model_builtins[name]=hex(rva)
    # setmodel -> G_SetModel -> G_ModelIndex; attach reads (model, tag, ignoreCollision).
    if call_target(0x18731a)!=0x1a0580 or call_target(0x1a05a0)!=value('kGModelIndexRva') or call_target(0x187567)!=0x18f180:
        raise ValueError('setmodel/attach implementation differs')
    if get(0x2958ba,6)!=bytes.fromhex('81 f9 c0 00 00 00'):raise ValueError('DObj bone limit differs')
    report['bo1_models']={'level_zone_call':hex(zone_call),'db_load_xassets':hex(value('kDbLoadXAssetsRva')),
        'g_model_index':hex(value('kGModelIndexRva')),'model_builtins':model_builtins,'dobj_bone_limit':0xc0,
        'helmet_swap':re.search(r'kHelmetSwapFunction\{"([^"]+)","([^"]+)"\}',text).groups()}
    # ---- Test43: BO1 zombie sounds ----
    names=[cstring(struct.unpack('<Q',get(value('kAssetNamesRva')+8*t,8))[0]-IMAGE_BASE) for t in (11,12,13)]
    if names!=['sound','sndcurve','loaded_sound'] or value('kAssetTypeSound')!=11:raise ValueError(f'Sound asset types differ: {names}')
    if get(value('kGSoundAliasIndexRva'),10)!=bytes_of('kGSoundAliasIndexPrologue') or get(value('kGAddEventRva'),15)!=bytes_of('kGAddEventPrologue'):
        raise ValueError('G_SoundAliasIndex / G_AddEvent prologue differs')
    # G_SoundAliasIndex: a 256-slot ring of sound alias configstrings at 0xab7 (no "not precached" error).
    if get(0x1a07b3,6)!=bytes.fromhex('81 c1 b7 0a 00 00') or get(0x1a0837,6)!=bytes.fromhex('81 c1 b7 0a 00 00') \
       or get(0x1a0794,5)!=bytes.fromhex('bd 00 02 00 00') or get(0x1a0799,6)!=bytes.fromhex('41 be 00 01 00 00'):
        raise ValueError('Sound alias configstring ring differs')
    # G_AddEvent stamps the event time on the entity (+0x104).
    if get(0x19e59b,6)!=bytes.fromhex('89 83 04 01 00 00'):raise ValueError('G_AddEvent entity event time differs')
    # The playsound method: a thunk (mov edx, 3; jmp impl) whose one-argument
    # path reads the alias, indexes it and adds EV_SOUND_ALIAS on the entity.
    thunk=value('kPlaySoundMethodRva');impl=value('kPlaySoundImplRva')
    if get(thunk,16)!=bytes_of('kPlaySoundMethodBytes') or jump_target(thunk+5)!=impl or value('kEventSoundAlias')!=3:
        raise ValueError('playsound thunk differs')
    if thunk&15:raise ValueError('playsound thunk not 16-byte aligned')
    if get(impl,15)!=bytes_of('kPlaySoundImplPrologue'):raise ValueError('playsound implementation prologue differs')
    if call_target(0x195da1)!=value('kScrGetStringRva') or call_target(0x195dd2)!=value('kGSoundAliasIndexRva') \
       or call_target(0x195de6)!=value('kScrGetNumParamRva') or call_target(0x195f88)!=value('kGAddEventRva'):
        raise ValueError('playsound calls differ')
    if get(0x195deb,6)!=bytes.fromhex('83 e8 01 0f 84 8c') or get(0x195f80,8)!=bytes.fromhex('44 8b c7 8b d5 48 8b cb'):
        raise ValueError('playsound one-argument path differs')
    if get(value('kScrGetStringRva'),15)!=bytes_of('kScrGetStringPrologue') or call_target(0x223444)!=0x222c40 or jump_target(0x22344f)!=0x21a370:
        raise ValueError('Scr_GetString differs')
    if get(value('kScrGetNumParamRva'),7)!=bytes_of('kScrGetNumParamBytes'):raise ValueError('Scr_GetNumParam differs')
    # Method entity references: entity number in the low 16 bits, class 0 = g_entities (stride 0x2a0).
    if get(0x18fa30,14)!=bytes.fromhex('48 83 ec 28 8b c1 c1 e8 10 66 85 c0 75 19') or get(0x18fa48,7)!=bytes.fromhex('48 69 c0 a0 02 00 00'):
        raise ValueError('Method entity reference decoding differs')
    report['bo1_sounds']={'asset_types':{'sound':11,'sndcurve':12,'loaded_sound':13},'g_sound_alias_index':hex(value('kGSoundAliasIndexRva')),
        'g_add_event':hex(value('kGAddEventRva')),'playsound_thunk':hex(thunk),'playsound_impl':hex(impl),
        'scr_get_string':hex(value('kScrGetStringRva')),'scr_get_num_param':hex(value('kScrGetNumParamRva')),
        'alias_configstrings':'0xab7 + (0..255)'}
    # ---- Test44: the loaded_sound pool (src/asset_pools.cpp) ----
    def q(rva):return struct.unpack('<Q',get(rva,8))[0]
    def d(rva):return struct.unpack('<i',get(rva,4))[0]
    t=value('kAssetTypeLoadedSound')
    if cstring(q(value('kAssetNamesRva')+8*t)-IMAGE_BASE)!='loaded_sound':raise ValueError('Asset type 13 is not loaded_sound')
    if d(value('kPoolSizeRva')+4*t)!=value('kLoadedSoundStockCount'):raise ValueError('loaded_sound pool size differs')
    if q(value('kXAssetPoolRva')+8*t)-IMAGE_BASE!=value('kLoadedSoundPoolRva'):raise ValueError('loaded_sound pool pointer differs')
    for table,routine,constant in (('kPoolInitTableRva','kLoadedSoundPoolInitRva','kLoadedSoundPoolInitBytes'),
                                   ('kPoolAllocTableRva','kPoolAllocRva','kPoolAllocBytes'),('kPoolFreeTableRva','kPoolFreeRva','kPoolFreeBytes')):
        if q(value(table)+8*t)-IMAGE_BASE!=value(routine) or get(value(routine),len(bytes_of(constant)))!=bytes_of(constant):
            raise ValueError(f'loaded_sound {table} differs')
    if value('kLoadedSoundEntryBytes')!=0x40 or bytes_of('kLoadedSoundPoolInitBytes')[39:43]!=bytes.fromhex('48 c1 e0 06'):
        raise ValueError('loaded_sound entry size differs')
    # DB_Init: flag store, then for 43 types init[type](pool[type], size[type]); its one caller.
    db_init=value('kDbInitRva')
    if get(db_init+0x16,2)!=b'\xc6\x05' or rip(db_init+0x16,2,7)!=value('kDbInitDoneRva') or get(db_init+0x1c,1)!=b'\x01':
        raise ValueError('DB_Init flag store differs')
    if rip(0xeff2d,3,7)!=value('kPoolSizeRva') or get(0xeff40,8)!=bytes.fromhex('48 8b 8c 2b')+struct.pack('<I',value('kXAssetPoolRva')) \
       or get(0xeff4d,2)!=bytes.fromhex('8b 17') or get(0xeff4f,7)!=bytes.fromhex('ff 94 2b')+struct.pack('<I',value('kPoolInitTableRva')) \
       or get(0xeff3b,3)!=bytes.fromhex('8d 73 2b'):
        raise ValueError('DB_Init pool loop differs')
    if call_target(value('kDbInitCallRva'))!=db_init:raise ValueError('DB_Init caller differs')
    # The limit error reads g_poolSize and g_assetNames by type.
    if get(0xeeed9,8)!=bytes.fromhex('4d 8b 84 ce')+struct.pack('<I',value('kAssetNamesRva')) or get(0xeeee1,8)!=bytes.fromhex('41 8b 94 8e')+struct.pack('<I',value('kPoolSizeRva')):
        raise ValueError('Asset limit error differs')
    # Nothing but its table slot points into the stock loaded_sound pool.
    lo=value('kLoadedSoundPoolRva');hi=lo+8+value('kLoadedSoundStockCount')*0x40
    slots=[slot for target,where in pointer_slots.items() if lo<=target<hi for slot in where]
    if slots!=[value('kXAssetPoolRva')+8*t]:raise ValueError(f'Other pointers into the loaded_sound pool: {[hex(x) for x in slots]}')
    text_section=next(s_ for s_ in pe.sections if s_.Name.rstrip(b'\0')==b'.text')
    code=text_section.get_data();base=text_section.VirtualAddress;refs=[]
    for i in range(len(code)-12):
        if (code[i+1]&0xc7)==5 or (code[i+2]&0xc7)==5:
            for k in (1,2):
                if (code[i+k]&0xc7)!=5:continue
                disp=struct.unpack_from('<i',code,i+k+1)[0]
                for length in range(k+5,k+10):
                    if lo<=base+i+length+disp<hi:refs.append(base+i)
    if refs:raise ValueError(f'Code addresses the loaded_sound pool directly: {[hex(x) for x in sorted(set(refs))[:8]]}')
    report['asset_pools']={'loaded_sound':{'type':t,'stock':value('kLoadedSoundStockCount'),'expanded':value('kLoadedSoundExpandedCount'),
        'entry_bytes':0x40,'pool':hex(lo),'init':hex(value('kLoadedSoundPoolInitRva')),'alloc':hex(value('kPoolAllocRva')),'free':hex(value('kPoolFreeRva'))},
        'db_init':hex(db_init),'db_init_caller':hex(value('kDbInitCallRva')),'tables':{'pool':hex(value('kXAssetPoolRva')),
        'size':hex(value('kPoolSizeRva')),'init':hex(value('kPoolInitTableRva'))},'other_references':0}
    # ---- Test44: the navigation graph (include/nav_graph.hpp) ----
    index=value('kPathNodeIndexRva')
    if get(index,18)!=bytes_of('kPathNodeIndexBytes') or rip(index+10,3,7)!=value('kPathNodesRva'):raise ValueError('Path_ConvertIndexToNode differs')
    if rip(0x1a5745,2,6)!=value('kPathNodeCountRva') or get(0x1a574b,6)!=bytes.fromhex('81 f9 00 20 00 00') \
       or get(0x1a5763,7)!=bytes.fromhex('48 69 f1 a8 00 00 00') or rip(0x1a5771,3,7)!=value('kPathNodesRva') \
       or rip(0x1a5778,2,6)!=value('kPathNodeCapacityRva') or rip(0x1a577e,2,6)!=value('kPathNodeCountRva'):
        raise ValueError('Path_LoadNode differs')
    if rip(0x1aa8dd,2,6)!=value('kPathNodeCountRva'):raise ValueError('getallnodes count differs')
    if get(0x1a5b57,4)!=bytes.fromhex('48 83 c2 14') or get(0x1a5b68,6)!=bytes.fromhex('66 41 83 78 6c 00'):
        raise ValueError('Path_NodesInRadius origin/link count differs')
    if get(0x140030,4)!=bytes.fromhex('48 8b 4f 40') or get(0x140042,4)!=bytes.fromhex('0f b7 49 04') or get(0x1400bd,6)!=bytes.fromhex('f3 41 0f 10 04 06') \
       or get(0x1401c7,4)!=bytes.fromhex('0f bf 47 6c') or get(0x1401cd,4)!=bytes.fromhex('49 83 c6 0c'):
        raise ValueError('A* link walk differs')
    if get(0x14636c,4)!=bytes.fromhex('0f b7 50 38') or get(0x146384,4)!=bytes.fromhex('48 8b 48 40') or get(0x146390,4)!=bytes.fromhex('0f b7 41 04') \
       or get(0x1463a7,4)!=bytes.fromhex('80 79 06 00') or get(0x14639c,4)!=bytes.fromhex('48 83 c1 0c'):
        raise ValueError('Path link check differs')
    nav=(include/'nav_graph.hpp').read_text()
    for name,want in (('kNodeBytes',0xa8),('kNodeOrigin',0x14),('kNodeTotalLinks',0x38),('kNodeLinks',0x40),('kNodeLinkCount',0x6c),
                      ('kLinkBytes',0xc),('kLinkDistance',0),('kLinkNode',4),('kLinkDisconnect',6),('kMaxNodes',0x2000)):
        m=re.search(r'\b'+name+r'=(0x[0-9a-fA-F]+|[0-9]+)',nav)
        if not m or int(m[1],0)!=want:raise ValueError(f'nav_graph.hpp {name} differs')
    report['nav_graph']={'nodes':hex(value('kPathNodesRva')),'count':hex(value('kPathNodeCountRva')),'capacity':hex(value('kPathNodeCapacityRva')),
        'node_bytes':0xa8,'origin':0x14,'total_links':0x38,'links':0x40,'active_links':0x6c,'link_bytes':0xc,'link_node':4,'link_distance':0,
        'link_disconnect':6,'max_nodes':0x2000}
    report.update({'goal_entity':'setgoalentity replica: 0x1244b0 clear, 0x15b190 handle, node/volume zero',
        'command_buffer':'Cbuf_AddText(0,"set specialops 1; ...; devmap <so_map>\\n") (critical section 0x24)',
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
