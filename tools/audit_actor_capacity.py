"""Preflight a coordinated actor-capacity proposal against the exact retail EXE.

This tool writes an audit/relocation plan, never a patched executable. A pool
allocation or ai_count edit cannot satisfy the perception/snapshot/save checks.
"""
import argparse
import bisect
import hashlib
import json
import struct
from pathlib import Path

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP

EXACT_SHA256='fb7e8a987c371dea7b666091b0b9e2011c0cd9cd277b191ad453d11d557dd777'
ACTOR_STRIDE=0x1560
ACTORS=32
SENTIENT_STRIDE=0x88
SENTIENTS=50
PERCEPTION_OFFSET=0x348
PERCEPTION_STRIDE=48
CORPSES=64


def proposal(capacity):
    if not ACTORS<=capacity<=512:
        raise ValueError('Requested capacity must be between 32 and 512')
    sentients=capacity+(SENTIENTS-ACTORS)
    old_end=PERCEPTION_OFFSET+SENTIENTS*PERCEPTION_STRIDE
    new_end=PERCEPTION_OFFSET+sentients*PERCEPTION_STRIDE
    return {
        'requested_actors':capacity,'native_actors':ACTORS,
        'requested_sentients':sentients,'native_sentients':SENTIENTS,
        'non_actor_sentient_reserve':SENTIENTS-ACTORS,
        'actor_storage_bytes':capacity*ACTOR_STRIDE,
        'sentient_storage_bytes':sentients*SENTIENT_STRIDE,
        'animation_pointer_storage_bytes':capacity*8,
        'perception_record_bytes':PERCEPTION_STRIDE,
        'native_perception_end':hex(old_end),
        'requested_perception_end':hex(new_end),
        'perception_sidecar_bytes':capacity*sentients*PERCEPTION_STRIDE,
        'expanded_inline_actor_stride':hex(ACTOR_STRIDE+(sentients-SENTIENTS)*PERCEPTION_STRIDE),
        'first_extra_perception_record_overwrites_actor_tail':capacity>ACTORS,
        'highest_combined_actor_corpse_index':capacity+CORPSES-1,
        'combined_index_bits_required':(capacity+CORPSES-1).bit_length(),
        'native_combined_index_bits':8,
        'native_save_format_counts':{'actors':ACTORS,'sentients':SENTIENTS},
    }


def descriptors(pe,rva):
    result=[]
    for n in range(256):
        offset,kind,_=struct.unpack('<QII',pe.get_data(rva+n*16,16))
        if not kind:return result
        result.append({'offset':offset,'kind':kind})
    raise ValueError('Unterminated native save descriptor table')


def audit(path,capacity=256):
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=EXACT_SHA256:
        raise ValueError('Exact executable SHA256 mismatch; no capacity proposal allowed')
    pe=pefile.PE(data=data);base=pe.OPTIONAL_HEADER.ImageBase
    decoder=Cs(CS_ARCH_X86,CS_MODE_64);decoder.detail=True;decoder.skipdata=True
    def decoded(at,size):return list(decoder.disasm(pe.get_data(at,size),base+at))
    def instruction(at):
        result=decoded(at,16)
        if not result or result[0].address-base!=at:raise ValueError(f'Instruction missing at {at:x}')
        return result[0]
    def rip(at):
        ins=instruction(at)
        operand=next((o for o in ins.operands if o.type==X86_OP_MEM and o.mem.base==X86_REG_RIP),None)
        if operand is None:raise ValueError(f'RIP-relative operand missing at {at:x}')
        return ins.address+ins.size+operand.mem.disp-base
    def immediate(at,expected):
        ins=instruction(at)
        if not any(o.type==X86_OP_IMM and o.imm==expected for o in ins.operands):
            raise ValueError(f'Expected capacity/stride constant missing at {at:x}')
        return {'rva':hex(at),'instruction':f'{ins.mnemonic} {ins.op_str}',
            'encoded_immediate_bytes':ins.imm_size,
            'needs_instruction_rewrite_for_256':ins.imm_size==1 and expected==ACTORS}
    bindings={
        'actor_pointer_slot':rip(0x12396c),
        'actor_static_storage':rip(0x1716ef),
        'sentient_pointer_slot':rip(0x151986),
        'sentient_static_storage':rip(0x1716da),
        'animation_pointers':rip(0x12c784),
        'snapshot_level_base':rip(0x174e99),
        'snapshot_goal_ids_and_positions':rip(0x174e89)-8,
        'snapshot_goal_state':rip(0x174e92)-16,
        'snapshot_has_animation_tree':rip(0x174e7f)-ACTORS,
        'snapshot_team':rip(0x174e7f),
    }
    expected={'actor_pointer_slot':0xd96ea0,'actor_static_storage':0xf04030,
        'sentient_pointer_slot':0xd96e98,'sentient_static_storage':0xd9c580,'animation_pointers':0xf50040}
    if any(bindings[k]!=v for k,v in expected.items()):raise ValueError('Independent pool bindings disagree')
    # The native byte write maps an entity to an actor index. Corpses use the
    # same map, starting at 32; 256 actors + 64 corpses need at least nine bits.
    actor_map=instruction(0x174ebd)
    if actor_map.mnemonic!='mov' or actor_map.operands[0].size!=1 or actor_map.operands[0].mem.disp!=0x2e2c:
        raise ValueError('Native snapshot actor/corpse index width changed')
    bindings['snapshot_entity_actor_corpse_map']=bindings['snapshot_level_base']+0x2e2c
    if instruction(0x174f98).operands[-1].mem.disp!=ACTORS:raise ValueError('Native corpse index base changed')
    perception=instruction(0x1519af)
    if perception.operands[-1].mem.disp!=PERCEPTION_OFFSET+16:
        raise ValueError('Independent perception sight-time access disagrees')
    actor_fields=descriptors(pe,0x340bb0)
    awareness=[d['offset'] for d in actor_fields if d['kind']==14 and 0x370<=d['offset']<=0xc70]
    if awareness!=[0x370+PERCEPTION_STRIDE*n for n in range(49)] or not any(d['offset']==0x330 and d['kind']==14 for d in actor_fields):
        raise ValueError('Native actor perception save pointer layout changed')
    fixed=[immediate(at,value) for at,value in (
        (0x123983,ACTOR_STRIDE),(0x12398a,ACTORS),(0x123e4a,ACTOR_STRIDE*ACTORS),
        (0x174f7b,ACTORS),(0x17fdfe,ACTORS),(0x181187,ACTORS),
        (0x1801b3,SENTIENTS),(0x1814ae,SENTIENTS),
        (0x1ae7d8,ACTORS),(0x1ae822,SENTIENTS))]
    # Build a read-only consumer inventory. It is deliberately not a blanket
    # patch list: constants such as stack-frame sizes also have the value 32.
    ranges=[(e.struct.BeginAddress,e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION]
    starts=[a for a,_ in ranges]
    def owner(rva):
        n=bisect.bisect_right(starts,rva)-1
        return ranges[n][0] if n>=0 and rva<ranges[n][1] else None
    pools={'actor_pointer':0xd96ea0,'sentient_pointer':0xd96e98,
        'actor_storage':0xf04030,'sentient_storage':0xd9c580,'animation_pointers':0xf50040}
    inventory={k:[] for k in pools};inventory['actor_stride']=[];inventory['actor_pool_byte_length']=[]
    text=next(s for s in pe.sections if s.Name.startswith(b'.text'))
    for ins in decoder.disasm(text.get_data(),base+text.VirtualAddress):
        if not ins.id:continue
        keys=set()
        for op in ins.operands:
            if op.type==X86_OP_MEM and op.mem.base==X86_REG_RIP:
                target=ins.address+ins.size+op.mem.disp-base
                keys.update(k for k,v in pools.items() if target==v)
            if op.type==X86_OP_IMM and op.imm==ACTOR_STRIDE:keys.add('actor_stride')
            if op.type==X86_OP_IMM and op.imm==ACTOR_STRIDE*ACTORS:keys.add('actor_pool_byte_length')
        if keys:
            rva=ins.address-base;function=owner(rva)
            row={'rva':hex(rva),'function_rva':hex(function) if function else None,
                'instruction':f'{ins.mnemonic} {ins.op_str}'}
            for key in keys:inventory[key].append(row)
    plan=proposal(capacity)
    blockers=[]
    if capacity>ACTORS:
        blockers=[
            {'dependency':'perception','reason':'Record 50 starts at actor+0xca8, the native actor tail. Relocating only the actor pool does not move this embedded 50-record layout.',
             'required':'Redirect all inline perception accesses and serialize sidecars, or change actor stride, downstream field offsets, descriptors and stack copies together.'},
            {'dependency':'snapshot','reason':f'The shared byte map reserves indices 32..95 for corpses; {capacity} actors plus 64 corpses need {plan["combined_index_bits_required"]} bits.',
             'required':'Widen/relocate the entity-index map and every reader/writer; move all actor-index goal/team arrays and corpse index bases together.'},
            {'dependency':'save_format','reason':'Native save loops use 32 actor flags and 50 sentient flags. Save pointer fixups independently reject larger indices; the actor descriptor has indexed perception pointer fixups at 0x370..0xc70 and a separate entry at 0x330.',
             'required':'Coordinate serialized counts, descriptors and fixups, with a format discriminator and legacy checkpoint reader.'},
            {'dependency':'lifecycle','reason':'Actor and sentient resets are unrolled; animation scratch and several consumers reference fixed storage directly.',
             'required':'Replace reset coverage, relocate direct storage references and validate free/reuse/animation paths past slot 31.'},
        ]
    return {'executable_sha256':EXACT_SHA256,'status':'BLOCKED' if blockers else 'NATIVE_LAYOUT',
        'runtime_capacity':ACTORS,'expanded_population_enabled':False,'native_code_modified':False,
        'proposal':plan,'bindings':{k:hex(v) for k,v in bindings.items()},
        'independently_decoded_capacity_sites':fixed,'actor_save_descriptor_count':len(actor_fields),
        'perception_save_pointer_records':len(awareness),
        'consumer_inventory_counts':{k:len(v) for k,v in inventory.items()},
        'consumer_inventory':inventory,'activation_blockers':blockers,
        'inventory_scope':'Direct pool references/stride constants, not a complete proof of transitive consumers.',
        'retail_execution':'No allocation past slot 31 or native save/reload stress test performed in this Linux workspace.'}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('--capacity',type=int,default=256)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    result=audit(args.executable,args.capacity)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('status','runtime_capacity','expanded_population_enabled','consumer_inventory_counts','proposal')},indent=2))
