"""Audit the coordinated expansion against the exact owned native executable.

A data-flow inventory supplements declared bindings. This is deliberately a
consumer audit, not a claim that a static scan proves all Windows gameplay.
"""
import argparse,hashlib,json,re
from pathlib import Path
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from capstone.x86 import X86_OP_MEM,X86_OP_IMM,X86_OP_REG,X86_REG_RIP
from audit_actor_capacity import EXACT_SHA256
from run_expanded_lifecycle_probe import PERCEPTION_SITES
from run_expanded_layout_probe import STRIDE,ACTORS,SENTIENTS
from prepare_expanded_runtime import prepare
from run_expanded_iterator_probe import patches,HEADER
from run_expanded_awareness_probe import unwind_audit as awareness_unwind
from run_expanded_threat_probe import unwind_audit as threat_unwind
from run_expanded_resources_probe import unwind_audit as resources_unwind, reference_inventory


def perception_flow(pe,image):
    base=pe.OPTIONAL_HEADER.ImageBase
    cs=Cs(CS_ARCH_X86,CS_MODE_64);cs.detail=True;cs.skipdata=True
    tags={};hits=[]
    def reg(r):
     s=cs.reg_name(r) or ""
     mapping={'eax':'rax','ax':'rax','al':'rax','ecx':'rcx','cx':'rcx','cl':'rcx','edx':'rdx','dx':'rdx','dl':'rdx','ebx':'rbx','bx':'rbx','bl':'rbx','esi':'rsi','edi':'rdi','ebp':'rbp','esp':'rsp'}
     return mapping.get(s,s[:-1] if s.startswith('r') and s[1:-1].isdigit() and s[-1:] in ('d','w','b') else s)
    def tag(r):return tags.get(reg(r))
    for section in pe.sections:
     if not section.Characteristics&0x20000000:continue
     for i in cs.disasm(section.get_data(),base+section.VirtualAddress):
      if not i.id:tags.clear();continue
      a=i.address-base;ops=i.operands
      for o in ops:
       if o.type==X86_OP_MEM and o.mem.base!=X86_REG_RIP and 0x348<=o.mem.disp<=0x370:
        bt=tag(o.mem.base);it=tag(o.mem.index)
        factor=(bt if isinstance(bt,int) else 0)+(it*o.mem.scale if isinstance(it,int) else 0)
        if factor==48:
         hits.append({'rva':hex(a),'instruction':i.mnemonic+' '+i.op_str,'declared':a in PERCEPTION_SITES,'patched':bytes(i.bytes)!=bytes(image[a:a+i.size])})
      if i.mnemonic=='ret':tags.clear();continue
      if i.mnemonic=='call':
       tags={k:v for k,v in tags.items() if k in ('rbx','rbp','rsi','rdi','r12','r13','r14','r15')};continue
      if not ops or ops[0].type!=X86_OP_REG:continue
      dst=reg(ops[0].reg);value=None;handled=True
      if i.mnemonic=='sub' and len(ops)==2 and ops[1].type==X86_OP_MEM and ops[1].mem.base==X86_REG_RIP and a+i.size+ops[1].mem.disp==0xd96e98:value='sentient-bytes'
      elif i.mnemonic in ('mov','movabs') and len(ops)==2:
       if ops[1].type==X86_OP_IMM and ops[1].imm==0x7878787878787879:value='sentient-magic'
       elif ops[1].type==X86_OP_REG:value=tag(ops[1].reg)
      elif i.mnemonic=='imul' and len(ops)==1:
       if tags.get('rax')=='sentient-magic' and tag(ops[0].reg)=='sentient-bytes':tags['rdx']='sentient-product'
       tags.pop('rax',None);continue
      elif i.mnemonic=='sar' and len(ops)==2 and ops[1].type==X86_OP_IMM and ops[1].imm==6 and tag(ops[0].reg)=='sentient-product':value=1
      elif i.mnemonic=='shr' and len(ops)==2 and ops[1].type==X86_OP_IMM and ops[1].imm==63 and isinstance(tag(ops[0].reg),int):value='sign'
      elif i.mnemonic=='add' and len(ops)==2 and ops[1].type==X86_OP_REG:
       x,y=tag(ops[0].reg),tag(ops[1].reg)
       if x=='sign' and isinstance(y,int):value=y
       elif y=='sign' and isinstance(x,int):value=x
       elif isinstance(x,int) and isinstance(y,int):value=x+y
      elif i.mnemonic=='shl' and len(ops)==2 and ops[1].type==X86_OP_IMM and isinstance(tag(ops[0].reg),int):value=tag(ops[0].reg)<<ops[1].imm
      elif i.mnemonic=='lea' and len(ops)==2 and ops[1].type==X86_OP_MEM:
       m=ops[1].mem;x,y=tag(m.base),tag(m.index)
       if not m.disp and (isinstance(x,int) or isinstance(y,int)):value=(x if isinstance(x,int) else 0)+(y*m.scale if isinstance(y,int) else 0)
      elif i.mnemonic.startswith(('cmp','test','bt','ucomi','comi')):continue
      else:handled=False
      if handled:
       if value is None:tags.pop(dst,None)
       else:tags[dst]=value
      elif ops[0].access&2:tags.pop(dst,None)
    return hits


def audit(executable):
    raw=executable.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=EXACT_SHA256:raise ValueError('Exact executable SHA256 differs')
    pe=pefile.PE(data=raw,fast_load=True)
    image,metadata=prepare(pe)
    decoder=Cs(CS_ARCH_X86,CS_MODE_64);decoder.detail=True
    def old(at):return next(decoder.disasm(pe.get_data(at,16),at))
    def current(at):return next(decoder.disasm(bytes(image[at:at+16]),at))
    inventory=json.loads((HEADER.parents[1]/'docs/actor-capacity-test17-evidence.json').read_text())['consumer_inventory']
    checks={}
    for key,expected in (('actor_stride',STRIDE),('actor_pool_byte_length',ACTORS*STRIDE)):
        for row in inventory[key]:
            at=int(row['rva'],16)
            value=32*STRIDE if at in (0x1a1663,0x1a1945,0x1a34a0) else expected
            if current(at).operands[-1].imm!=value:raise ValueError(f'Coordinated actor stride/span differs at {at:x}')
        checks[key]=len(inventory[key])
    for key,target in (('actor_storage',0x4000000),('sentient_storage',0x4500000),('animation_pointers',0x4560000)):
        for row in inventory[key]:
            at=int(row['rva'],16);ins=current(at)
            if at+ins.size+ins.disp!=target:raise ValueError(f'Direct storage reference differs at {at:x}')
        checks[key]=len(inventory[key])
    for at in PERCEPTION_SITES:
        source=old(at);target=current(at)
        if target.disp!=source.disp+0x1560-0x348:raise ValueError(f'Perception field remains inline at {at:x}')
    if current(0x12d43a).operands[-1].imm!=0x1560:raise ValueError('Common perception accessor differs')
    flow=perception_flow(pe,image)
    if not flow or any(not row['declared'] or not row['patched'] for row in flow):
        raise ValueError('Sentient-derived indexed perception consumer is not relocated')
    if current(0x172ca6).operands[-1].imm!=256 or pe.get_data(0x172cb4,3)!=bytes.fromhex('418bd1'):
        raise ValueError('ai_count default/domain coupling differs')
    # These similarly valued bounds belong to unchanged FX save groups, not
    # actors. Both native save probes retain 32 records in these domains.
    fx_sites=(0x17fdfe,0x181187,0x1ae361,0x1ae917)
    for at in fx_sites:
        if bytes(current(at).bytes)!=bytes(old(at).bytes):raise ValueError('Unrelated fixed FX domain was enlarged')
    source_hits=[]
    for directory in ('src','include'):
        for path in sorted((HEADER.parents[1]/directory).glob('*')):
            if path.suffix not in ('.cpp','.c','.hpp','.h') or path.name in ('expanded_capacity_patches.hpp','mp_hit_alert_pcm.hpp'):continue
            for number,line in enumerate(path.read_text().splitlines(),1):
                if re.search(r'\b32\b',line):source_hits.append({'file':str(path.relative_to(HEADER.parents[1])),'line':number,'text':line.strip()})
    rows=patches(HEADER.read_text())
    return {'executable_sha256':EXACT_SHA256,'live_actors':ACTORS,'sentients':SENTIENTS,'actor_stride':STRIDE,
            'direct_native_consumers_checked':checks,'declared_relocated_perception_sites':len(PERCEPTION_SITES),
            'sentient_index_data_flow_sites':len(flow),'unrelocated_data_flow_sites':0,'data_flow_inventory':flow,
            'native_ai_count_default_and_maximum':256,'retained_native_fx_bound_sites':[hex(at) for at in fx_sites],
            'native_rewind_records':metadata['native_rewind_actor_slots'],
            'native_unwind_audits':{'threat':threat_unwind(pe,rows),'awareness':awareness_unwind(pe,rows),'crowd_resources':resources_unwind(pe,rows)},
            'crowd_resources':metadata['crowd_resources'],
            'skeleton_reference_inventory':reference_inventory(pe),
            'remaining_source_literal_32':source_hits,
            'source_review':'Remaining 32s are stock-layout migration/bootstrap sizes, bounded shot history, a 32-byte checkpoint header, SHA256/graphics/ASCII dimensions, bit widths and original profile byte patterns. See the accompanying audit document for reasons.',
            'scope':'Exact declared storage/stride/span checks and linear native sentient-index data flow, plus manually reviewed iterator callers. This is not a formal proof of every transitive engine consumer or Windows gameplay stability.'}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();result=audit(args.executable)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({key:value for key,value in result.items() if key not in ('data_flow_inventory','remaining_source_literal_32')},indent=2))
