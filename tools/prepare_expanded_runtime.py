"""Exact-build 256/274 patch plan. No blanket integer substitutions.
The fixture uses synthetic addresses; the DLL relocates every arena reference
into separately allocated storage, validates original bytes and keeps the EXE
on disk untouched. Isolation stubs in earlier probes are excluded from the plan.
"""
import struct,json,hashlib,argparse,gc
from pathlib import Path
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from capstone.x86 import X86_OP_IMM,X86_OP_MEM,X86_REG_RIP,X86_REG_R8,X86_REG_RSI
from run_expanded_lifecycle_probe import prepare as previous
from audit_actor_capacity import EXACT_SHA256
FRAME=0x4510000;CLIENT_FRAME=0x4580000;LOAD_ACTORS=0x4590000;LOAD_SENTIENTS=LOAD_ACTORS+4
TEMP=0x45a0000;TEMP_COUNT=336;FLAGS=TEMP+TEMP_COUNT*8;CURSOR=FLAGS+TEMP_COUNT
CALLBACKS=0x47f0000
SNAPSHOT_BYTES=0x4404
# These native history and shooting scratch records contain 32 actors. They
# are independent of the live actor pool's capacity; enlarging a traversal
# without also enlarging its caller's stack buffers reads/writes past them.
REWIND_ACTORS=32
ANIMATION_ROOT=0xf4e1a0
ACTOR_TREES=0x4560000
# These two references use the native animation-global root, unlike snapshot
# references based on level. Relocations must preserve that distinct base.
NON_RIP_BASES={0x132000:ANIMATION_ROOT,0x13201e:ANIMATION_ROOT}

def prepare(pe):
 image,meta=previous(pe);base=pe.OPTIONAL_HEADER.ImageBase
 dec=Cs(CS_ARCH_X86,CS_MODE_64);dec.detail=True;edits=[];cursor=0x4603000
 def ins(at):return next(dec.disasm(pe.get_data(at,16),base+at))
 def put(at,b,role):
  image[at:at+len(b)]=b;edits.append({'rva':hex(at),'bytes':len(b),'role':role})
 def jump(at,target):return b'\xe9'+struct.pack('<i',target-at-5)
 def rip(at,target,role):
  i=ins(at);assert i.disp_size==4
  put(at+i.disp_offset,struct.pack('<i',target-at-i.size),role)
 def imm(at,value,role):
  i=ins(at);put(at+i.imm_offset,value.to_bytes(i.imm_size,'little'),role)
 def disp(at,value,role):
  i=ins(at);put(at+i.disp_offset,struct.pack('<i',value),role)
 def relay(at,length,code,role):
  nonlocal cursor
  where=cursor;put(where,code+jump(where+len(code),at+length),role+' relay')
  put(at,jump(at,where)+b'\x90'*(length-5),role);cursor+=len(code)+5
 def actor_disp(at,value,role):
  i=ins(at)
  if i.disp_size==4:disp(at,value,role);return
  if i.disp_size!=1 or i.size<5:raise ValueError(f'Unsupported actor displacement at {at:x}')
  # The old actor-1 active test has a signed-byte displacement. Preserve its
  # SIB/registers and immediate while widening ModRM to a DWORD displacement.
  code=bytearray(i.bytes);code[i.modrm_offset]=(code[i.modrm_offset]&0x3f)|0x80
  code=code[:i.disp_offset]+struct.pack('<i',value)+code[i.disp_offset+1:]
  relay(at,i.size,bytes(code),role)
 def bound(at,value,role,variable=False):
  i=ins(at);b=ins(at+i.size);assert i.mnemonic=='cmp' and b.mnemonic.startswith('j')
  # EAX is the loop counter at these four population boundaries.
  code=(b'\x3b\x05'+struct.pack('<i',value-(cursor+6))) if variable else (b'\x3d'+struct.pack('<I',value))
  cond=(b.bytes[0]&15) if b.size==2 else (b.bytes[1]&15)
  target=b.operands[0].imm-base
  code+=b'\x0f'+bytes([0x80|cond])+struct.pack('<i',target-(cursor+len(code)+6))
  relay(at,i.size+b.size,code,role)
 # One coherent snapshot frame used by producer, corpse cache and all three
 # client readers. Keep entity number indexing, widen the actor/corpse map.
 for at,target in ((0x174e72,FRAME),(0x174e7f,FRAME+0x4304),(0x174e89,FRAME+0x2e04+8),(0x174e92,FRAME+0x1004+16)):
  rip(at,target,'coherent expanded snapshot producer')
 relay(0x174ebd,ins(0x174ebd).size,bytes.fromhex('66 45 89 bc 4d')+struct.pack('<i',FRAME+4-0xd96e70),'16-bit coherent actor map')
 relay(0x174f98,ins(0x174f98).size+ins(0x174f9b).size,bytes.fromhex('8d 8d 00 01 00 00 66 41 89 8c 45')+struct.pack('<i',FRAME+4-0xd96e70),'16-bit coherent corpse map')
 for i in dec.disasm(pe.get_data(0x1711d0,0xb8),base+0x1711d0):
  for o in i.operands:
   if o.type==X86_OP_MEM and 0x3f2c<=o.mem.disp<=0x3f38:disp(i.address-base,FRAME+0x2e04-0xd96e70+o.mem.disp-0x3f2c,'coherent corpse snapshot IDs')
   if o.type==X86_OP_MEM and 0x362c<=o.mem.disp<=0x3640:disp(i.address-base,FRAME+0x1004-0xd96e70+o.mem.disp-0x362c,'coherent corpse snapshot goals')
 imm(0x170af9,SNAPSHOT_BYTES,'full expanded snapshot copy');rip(0x170aff,FRAME,'expanded snapshot source');rip(0x170b06,CLIENT_FRAME,'expanded snapshot destination')
 rip(0x12c6a0,CLIENT_FRAME,'expanded client team frame');put(0x12c6aa,bytes.fromhex('41 0f b7 44 41 04'),'16-bit client team map');disp(0x12c6b2,0x4304,'256-entry client team array')
 rip(0x15cec3,CLIENT_FRAME+4,'expanded client goal map');put(0x15ceca,bytes.fromhex('0f b7 04 41'),'16-bit client goal map');rip(0x15cece,CLIENT_FRAME+0x2e04,'320-entry client goal IDs')
 rip(0x15cee3,CLIENT_FRAME,'expanded client goal frame');put(0x15ceea,bytes.fromhex('0f b7 44 42 04'),'16-bit client goal state map');disp(0x15cef3,0x1004,'320-entry client goal state')
 # All 274 startup active flags, preserving the native global clears.
 at=0x1b0921;code=b'\x48\x8b\x05'+struct.pack('<i',0xd96e98-(at+7))+b'\xb9'+struct.pack('<I',274)
 loop=len(code);code+=bytes.fromhex('c6 80 82 00 00 00 00 48 05 88 00 00 00 ff c9 75')+struct.pack('<b',loop-(len(code)+17))+b'\xc3';put(at,code,'full sentient startup reset')
 for at in (0x1b12b2,0x1b13df,0x1b1413,0x1b147b,0x1b1c1d,0x1b1c4a,0x1ae2d4,0x1ae87d):
  i=ins(at);b=ins(at+i.size);value=274
  code=bytearray(i.bytes);code[code.index(0x83)]=0x81;code=code[:i.imm_offset]+struct.pack('<i',value)
  target=b.operands[0].imm-base;cond=(b.bytes[0]&15) if b.size==2 else (b.bytes[1]&15)
  code+=b'\x0f'+bytes([0x80|cond])+struct.pack('<i',target-(cursor+len(code)+6));relay(at,i.size+b.size,bytes(code),'274 sentient traversal/linked handle bound')
 disp(0x1ae85e,0x4500000-0x88,'relocated linked sentient handle conversion')
 # Only actor/sentient loops change. FX and vehicle counts retain stock 32.
 bound(0x1813dc,256,'expanded checkpoint actor writer count');bound(0x18013a,LOAD_ACTORS,'versioned checkpoint actor reader count',True)
 bound(0x1814ae,274,'expanded checkpoint sentient writer count');bound(0x1801b3,LOAD_SENTIENTS,'versioned checkpoint sentient reader count',True)
 for n,at in enumerate((0x180f43,0x17fba0,0x1813c9,0x180120)):
  assert ins(at).mnemonic=='call'
  put(at,b'\xe8'+struct.pack('<i',CALLBACKS+n*16-at-5),'versioned native checkpoint callback')
 # Temporary animation trees must also grow: actors + corpses + spare trees.
 biased=TEMP-0x29a0
 for at in (0x12c6d6,0x12c740):rip(at,biased,'expanded temporary animation biased base')
 rip(0x12c747,TEMP,'expanded temporary animation first pointer')
 for at in (0x12c6f5,0x12c70c,0x12c75c):disp(at,FLAGS-biased,'expanded temporary animation occupancy')
 for at in (0x12c6d0,0x12c6ef,0x12c715,0x170cdf):rip(at,CURSOR,'expanded temporary animation ring cursor')
 # 0xf50b48 also names the one-past-end entity-number field of the unchanged
 # 64-corpse array. Its three search bounds are NOT temporary tree users.
 # Leave 0x131eae, 0x13229f and 0x132dde at their original native addresses.
 for at in NON_RIP_BASES:
  i=ins(at)
  if not any(o.type==X86_OP_MEM and o.mem.base==X86_REG_RSI and o.mem.disp==0x1ea0 for o in i.operands):raise ValueError(f'Unexpected native corpse tree swap {at:x}')
  disp(at,ACTOR_TREES-ANIMATION_ROOT,'expanded actor-to-corpse animation tree ownership swap')
 for at in (0x170cb3,0x171134,0x17519c):rip(at,TEMP,'expanded temporary animation lifetime')
 for at in (0x170cba,0x17113b,0x1751a3):imm(at,TEMP_COUNT,'expanded temporary animation lifecycle count')
 i=ins(0x12c6e8);relay(0x12c6e5,0x12c6ec-0x12c6e5,b'\x81\xfa'+struct.pack('<I',TEMP_COUNT)+bytes(i.bytes),'expanded temporary animation cursor wrap')
 i=ins(0x12c701);b=ins(0x12c704);code=b'\x3d'+struct.pack('<I',TEMP_COUNT)+b'\x0f\x82'+struct.pack('<i',0x12c6e3-(cursor+11));relay(0x12c701,i.size+b.size,code,'expanded temporary animation exhaustion')
 code=bytearray(b'\x57\x48\x8d\x3d'+struct.pack('<i',FLAGS-(cursor+8))+b'\xb9'+struct.pack('<I',TEMP_COUNT)+bytes.fromhex('31 c0 fc f3 aa 5f'))
 i=ins(0x170cec);relocated=bytearray(i.bytes);target=i.address+i.size+i.disp
 relocated[i.disp_offset:i.disp_offset+4]=struct.pack('<i',target-base-(cursor+len(code)+i.size));code+=relocated
 code+=bytes(ins(0x170cfa).bytes) # native two-client counter, indispensable
 relay(0x170ce5,0x170d20-0x170ce5,bytes(code),'full temporary animation occupancy reset')
 # The frame-history writer is unrolled eight ways. These are actor-relative
 # displacements, not immediate stride constants, so the earlier inventory
 # did not cover them. Keep the original 32-entry history representation.
 from run_expanded_layout_probe import STRIDE
 for i in dec.disasm(pe.get_data(0x232de0,0x233154-0x232de0),base+0x232de0):
  for o in i.operands:
   if o.type==X86_OP_MEM and o.mem.base==X86_REG_R8:
    slot,field=divmod(o.mem.disp,0x1560)
    if not 0<=slot<8 or field not in (0,0x14ff):raise ValueError(f'Unexpected frame-history actor field {i.address-base:x}')
    if slot:disp(i.address-base,slot*STRIDE+field,'frame-history expanded actor displacement')
 imm(0x23313b,8*STRIDE,'frame-history eight-actor stride')
 # History validation similarly biases its cursor to actor 2 and reads
 # actors -2..5 relative to it. Preserve slot-reuse checks against entity+78.
 imm(0x1a2ed8,2*STRIDE,'history validation actor cursor bias')
 for at,slot,field in ((0x1a2f37,-2,0x14ff),(0x1a2f56,-2,0),
                       (0x1a2f73,-1,0x14ff),(0x1a2f8f,-1,0),
                       (0x1a2fe3,1,0x14ff),(0x1a3000,1,0),
                       (0x1a301c,2,0x14ff),(0x1a3039,2,0),
                       (0x1a3055,3,0x14ff),(0x1a3072,3,0),
                       (0x1a308e,4,0x14ff),(0x1a30ad,4,0),
                       (0x1a30ca,5,0x14ff),(0x1a30e7,5,0)):
  if ins(at).disp!=slot*0x1560+field:raise ValueError(f'Unexpected history validation field {at:x}')
  actor_disp(at,slot*STRIDE+field,'history validation expanded actor displacement')
 imm(0x1a30fc,8*STRIDE,'history validation eight-actor stride')
 # Three restore/rewind loops use byte extents rather than an actor counter.
 # Their stack records stay 0x320 bytes (32 origins/angles/active flags), and
 # their companion loops already stop after 32. Match both halves exactly.
 for at in (0x1a1663,0x1a1945,0x1a34a0):
  imm(at,REWIND_ACTORS*STRIDE,'bounded native shooting scratch traversal')
 return image,{'previous_stage':meta,'stage22_edits':edits,'frame_bytes':SNAPSHOT_BYTES,'temporary_animation_trees':TEMP_COUNT,
               'native_rewind_actor_slots':REWIND_ACTORS,
               'high_actor_collision':'Actors 32..255 keep their current server pose during shots; live pool capacity stays 256.'}

def flatten(meta):
 rows=[]
 if 'previous_stage' in meta:rows+=flatten(meta['previous_stage'])
 rows+=meta.get('prototype_edits',[])+meta.get('stage21_edits',[])+meta.get('stage22_edits',[])
 return [r for r in rows if not r['role'].startswith('fixture-only')]

def validate_control_entry(role,at,instruction_start):
 if ('capacity guard' in role or 'capacity bound' in role) and at!=instruction_start:
  raise ValueError(f'Capacity guard begins inside instruction: {at:x}, actual start {instruction_start:x}')

def runtime_plan(pe,image,meta):
 base=pe.OPTIONAL_HEADER.ImageBase;dec=Cs(CS_ARCH_X86,CS_MODE_64);dec.detail=True
 # Find original instruction boundaries in executable sections to expand
 # partial immediate/displacement writes into checked complete instructions.
 boundaries={}
 dec.skipdata=True
 for section in pe.sections:
  if not section.Characteristics&0x20000000:continue
  start=section.VirtualAddress
  for i in dec.disasm(pe.get_data(start,section.SizeOfRawData),base+start):
   if not i.id:continue
   for offset in range(i.size):boundaries[i.address-base+offset]=(i.address-base,i.size)
 spans=[]
 for r in flatten(meta):
  at=int(r['rva'],16);length=r.get('bytes',len(bytes.fromhex(r.get('after',''))))
  if at<0x4000000:
   if at not in boundaries or at+length-1 not in boundaries:raise ValueError(f'Instruction boundary missing {at:x}')
   a,_=boundaries[at];z,n=boundaries[at+length-1]
   validate_control_entry(r['role'],at,a)
   spans.append((a,z+n))
  else:spans.append((at,at+length))
 spans.sort();merged=[]
 for a,z in spans:
  if merged and a<=merged[-1][1]:merged[-1]=(merged[-1][0],max(z,merged[-1][1]))
  else:merged.append((a,z))
 plan=[]
 for a,z in merged:
  blob=bytes(image[a:z]);fixups=[]
  parsed=list(dec.disasm(blob,base+a))
  if sum(i.size for i in parsed)!=len(blob) or any(not i.id for i in parsed):raise ValueError(f'Incomplete patch disassembly {a:x}')
  for i in parsed:
   at=i.address-base
   for o in i.operands:
    if o.type==X86_OP_MEM and o.mem.base==X86_REG_RIP:
     target=at+i.size+o.mem.disp
     if target>=0x4000000 or a>=0x4000000:fixups.append({'offset':at-a+i.disp_offset,'width':4,'target':target,'end':at+i.size,'relative':True})
    elif o.type==X86_OP_MEM and o.mem.base!=X86_REG_RIP:
     # level-relative maps/corpse caches and image-relative handle conversion.
     root=NON_RIP_BASES.get(at,0xd96e70 if 0x3000000<o.mem.disp<0x4000000 else 0)
     target=o.mem.disp+root
     if 0x4000000<=target<0x4800000:fixups.append({'offset':at-a+i.disp_offset,'width':4,'target':target,'end':root,'relative':False})
    elif o.type==X86_OP_IMM and (i.mnemonic=='call' or i.mnemonic.startswith('j')):
     target=o.imm-base
     if target>=0x4000000 or a>=0x4000000:fixups.append({'offset':at-a+i.imm_offset,'width':i.imm_size,'target':target,'end':at+i.size,'relative':True})
  before=pe.get_data(a,z-a).hex() if a<0x4000000 else ''
  plan.append({'rva':a,'before':before,'after':blob.hex(),'fixups':fixups})
 return plan

def generate_header(plan,output):
 # Each replacement fragment is narrowly scoped native code, never game assets
 # or full image/zone data. Original fragments allow fail-closed validation.
 with output.open('w') as f:
  f.write('#pragma once\n// Generated by tools/prepare_expanded_runtime.py for the exact supported SP SHA256.\n#include <vector>\n#include <cstdint>\nnamespace mod::expansion {\nstruct Fixup {unsigned offset,width;std::uint32_t target,end;bool relative;};\nstruct Patch {std::uint32_t rva;const char* before;const char* after;std::vector<Fixup> fixups;};\ninline const std::vector<Patch> kCapacityPatches{\n')
  for p in plan:
   fs=','.join('{%d,%d,0x%x,0x%x,%s}'%(v['offset'],v['width'],v['target'],v['end'],'true' if v['relative'] else 'false') for v in p['fixups'])
   f.write('    {0x%x,"%s","%s",{%s}},\n'%(p['rva'],p['before'],p['after'],fs))
  f.write('};\n} // namespace mod::expansion\n')

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path);p.add_argument('--header',type=Path);p.add_argument('--fixture',type=Path);p.add_argument('--manifest',type=Path)
 a=p.parse_args();raw=a.executable.read_bytes()
 if hashlib.sha256(raw).hexdigest()!=EXACT_SHA256:raise ValueError('Wrong executable')
 pe=pefile.PE(data=raw,fast_load=True);image,meta=prepare(pe);plan=runtime_plan(pe,image,meta)
 if a.header:generate_header(plan,a.header)
 if a.fixture:a.fixture.write_bytes(image)
 if a.manifest:a.manifest.write_text(json.dumps({'patches':plan,'metadata':meta},indent=2)+'\n')
 print(json.dumps({'checked_patches':sum(p['rva']<0x4000000 for p in plan),'arena_fragments':sum(p['rva']>=0x4000000 for p in plan),'fixups':sum(len(p['fixups']) for p in plan)}))
