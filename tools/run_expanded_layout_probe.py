"""Execute the appended 256-actor layout in a private exact-native fixture.

The fixture is never installed or distributed. This tests native allocation,
iteration, perception clear/copy, snapshot index production and pointer fixups.
It does not assert complete coverage of gameplay or checkpoint stream layouts.
"""
import argparse
import hashlib
import json
import platform
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
from audit_actor_capacity import EXACT_SHA256

ACTORS=256
SENTIENTS=274
STRIDE=0x48c0
POOL=0x4000000
SENTIENT_POOL=0x4500000
SNAPSHOT_MAP=0x4510000
GOAL_IDS=0x4520000
GOAL_STATE=0x4530000
PRESENCE=0x4540000
TEAM=PRESENCE+256
ANIMATION_POINTERS=0x4560000
RELAY=0x4600000


def prepare(pe):
    base=pe.OPTIONAL_HEADER.ImageBase
    image=bytearray(pe.get_memory_mapped_image());image.extend(bytes(max(0,0x4800000-len(image))))
    decoder=Cs(CS_ARCH_X86,CS_MODE_64);decoder.detail=True
    def ins(at):return next(decoder.disasm(pe.get_data(at,16),base+at))
    edits=[];cursor=RELAY
    def put(at,blob,role):
        before=bytes(image[at:at+len(blob)]);image[at:at+len(blob)]=blob
        edits.append({'rva':hex(at),'bytes':len(blob),'role':role})
    def jump(at,target):return b'\xe9'+struct.pack('<i',target-at-5)
    def detour(at,length,code,role):
        nonlocal cursor
        assert length>=5
        location=cursor;cursor+=len(code)+5
        put(location,code+jump(location+len(code),at+length),role+' relay')
        put(at,jump(at,location)+b'\x90'*(length-5),role)
    def bound(at,value):
        nonlocal cursor
        first=ins(at);branch=ins(at+first.size)
        assert first.mnemonic=='cmp' and first.imm_size==1 and branch.mnemonic.startswith('j')
        code=bytearray(first.bytes);op=code.index(0x83);code[op]=0x81
        code=code[:first.imm_offset]+struct.pack('<i',value)
        target=branch.operands[0].imm-base
        condition=(branch.bytes[0]&15) if len(branch.bytes)==2 else (branch.bytes[1]&15)
        code+=b'\x0f'+bytes([0x80|condition])+struct.pack('<i',target-(cursor+len(code)+6))
        detour(at,first.size+branch.size,bytes(code),'expanded capacity bound')
    def immediate(at,value,role):
        first=ins(at);assert first.imm_size in (1,4,8)
        put(at+first.imm_offset,int(value).to_bytes(first.imm_size,'little',signed=False),role)
    def displacement(at,value,role):
        first=ins(at);assert first.disp_size==4
        put(at+first.disp_offset,struct.pack('<i',value),role)
    def rip(at,target,role):
        first=ins(at);assert any(o.type==X86_OP_MEM and o.mem.base==X86_REG_RIP for o in first.operands)
        displacement(at,target-at-first.size,role)
    # Known actor allocation/iteration windows. The native brain's original
    # fields retain their offsets; only its slot stride and allocation size grow.
    for at in (0x12398a,0x123bfc,0x123c77,0x123cb7,0x174f7b,0x1ae24a,0x1ae7d8):bound(at,ACTORS)
    for at in (0x1ae28f,0x1ae822,0x1b0dd4):bound(at,SENTIENTS)
    for at in (0x123983,0x123bf5,0x123c0f,0x123c7d,0x123cb1,0x174f69,0x1ae277):immediate(at,STRIDE,'expanded actor stride')
    immediate(0x1239a7,STRIDE,'clear complete appended actor allocation')
    # Recompute signed division by the new stride. These exact windows use a
    # positive magic multiplier and SAR; constants are not patched by value alone.
    for magic,shift in ((0x123c24,0x123c66),(0x1ae7b9,0x1ae7c6),(0x12c777,0x12c78b),(0x12c7e5,0x12c7f2),(0x12c817,0x12c824)):
        assert ins(magic).mnemonic=='movabs' and ins(magic).operands[-1].imm==0xbfa02fe80bfa03
        assert ins(shift).mnemonic=='sar' and ins(shift).operands[-1].imm==4
        immediate(magic,0x709ad4e4ba80709b,'division multiplier for 18624-byte actor')
        immediate(shift,13,'division shift for 18624-byte actor')
    # Confirmed perception code: actual clear, copy and visibility-time query.
    perception=[]
    for begin,end in ((0x1247e0,0x1248d0),(0x12d450,0x12d4c2),(0x151970,0x1519d9)):
        for first in decoder.disasm(pe.get_data(begin,end-begin),base+begin):
            for operand in first.operands:
                if operand.type==X86_OP_MEM and 0x348<=operand.mem.disp<=0x370 and operand.mem.base!=X86_REG_RIP:
                    displacement(first.address-base,operand.mem.disp+(0x1560-0x348),'appended perception record')
                    perception.append(hex(first.address-base));break
    # Snapshot producer: 16-bit shared actor/corpse indices and full-sized
    # goals, animation-presence and team arrays. Consumer/wire-format coverage
    # remains a separate activation requirement.
    rip(0x174e7f,TEAM,'relocated 256-entry snapshot team array')
    rip(0x174e89,GOAL_IDS+8,'relocated 256-entry snapshot goal IDs/positions')
    rip(0x174e92,GOAL_STATE+16,'relocated 256-entry snapshot goal state')
    rip(0x12c784,ANIMATION_POINTERS,'relocated 256-entry animation pointer lookup')
    for first in decoder.disasm(pe.get_data(0x1711d0,0x171288-0x1711d0),base+0x1711d0):
        for operand in first.operands:
            if operand.type==X86_OP_MEM and 0x3f2c<=operand.mem.disp<=0x3f38:
                displacement(first.address-base,GOAL_IDS-0xd96e70+operand.mem.disp-0x3f2c,'320-entry corpse snapshot goal cache')
            if operand.type==X86_OP_MEM and 0x362c<=operand.mem.disp<=0x3640:
                displacement(first.address-base,GOAL_STATE-0xd96e70+operand.mem.disp-0x362c,'320-entry corpse snapshot goal state')
    delta=SNAPSHOT_MAP-0xd96e70
    actor_store=bytes.fromhex('66 45 89 bc 4d')+struct.pack('<i',delta)
    detour(0x174ebd,ins(0x174ebd).size,actor_store,'16-bit actor snapshot map')
    corpse_store=bytes.fromhex('8d 8d 00 01 00 00 66 41 89 8c 45')+struct.pack('<i',delta)
    detour(0x174f98,ins(0x174f98).size+ins(0x174f9b).size,corpse_store,'corpse map indices 256..319')
    location=cursor
    corpse_call=bytes.fromhex('8d 8d 00 01 00 00 e8')+struct.pack('<i',0x1711d0-(location+11))
    detour(0x174fa3,8,corpse_call,'expanded corpse callback index')
    detour(0x174f54,5,bytes.fromhex('41 88 84 24 00 ff ff ff'),'256-entry animation-presence array')
    # Convert the unrolled 32-slot active-flag reset into a complete 256-slot
    # loop at its native entry. Original field offsets remain unchanged.
    reset=b'\x48\x8b\x05'+struct.pack('<i',0xd96ea0-(0x12c840+7))
    reset+=b'\xc7\x05'+struct.pack('<i',0xd96ee0-(0x12c840+17))+struct.pack('<I',0)
    reset+=b'\xb9'+struct.pack('<I',ACTORS)
    loop=len(reset)
    reset+=bytes.fromhex('c6 80 ff 14 00 00 00 48 05')+struct.pack('<I',STRIDE)+bytes.fromhex('ff c9 75')
    reset+=struct.pack('<b',loop-(len(reset)+1))+b'\xc3'
    put(0x12c840,reset,'converted full actor active-flag reset')
    # Dependencies deliberately isolated from game startup and imports. The
    # animation pointer/index lookup and corpse snapshot copy execute natively.
    image[0x6b980]=0xc3
    fill=bytes.fromhex('57 48 89 cf 89 d0 4c 89 c1 fc f3 aa 5f c3')
    image[0x2e42d0:0x2e42d0+len(fill)]=fill
    # Native iterator predicate records visits and returns false.
    predicate=0x46f0000;visits=0x4550100;last=visits+8
    code=b'\x48\xb8'+struct.pack('<Q',base+visits)+b'\xff\x00\x48\xb8'+struct.pack('<Q',base+last)+bytes.fromhex('48 89 08 31 c0 c3')
    # The harness rebases these two absolute instrumentation pointers after mmap.
    image[predicate:predicate+len(code)]=code
    image[0x123c3a:0x123c3f]=b'\xe8'+struct.pack('<i',predicate-0x123c3f)
    # Stop clear before its native notification/model callbacks.
    image[0x12483a:0x12483f]=jump(0x12483a,0x1248b5)
    return image,{'actor_capacity':ACTORS,'sentient_capacity':SENTIENTS,'actor_stride':hex(STRIDE),
        'perception_base':hex(0x1560),'snapshot_index_bits':16,'highest_corpse_index':319,
        'confirmed_perception_sites':perception,'prototype_edits':edits,'runtime_activation':False,
        'untested_dependencies':['remaining inline perception consumers and actor-index divisions','pool reset/free/animation lifetime beyond 31','snapshot consumers and client/wire representation','full checkpoint counts, format marker and legacy stream compatibility']}


def run(executable):
    if platform.system()!='Linux' or platform.machine().lower() not in ('amd64','x86_64'):raise ValueError('Linux/x86-64 required')
    data=executable.read_bytes()
    if hashlib.sha256(data).hexdigest()!=EXACT_SHA256:raise ValueError('Exact executable SHA256 mismatch')
    pe=pefile.PE(data=data);image,manifest=prepare(pe)
    compiler=shutil.which('g++')
    if not compiler:raise ValueError('Host C++ compiler unavailable')
    tools=Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='bo1mw2-expanded-layout-') as directory:
        root=Path(directory);fixture=root/'private-mapped-image.bin';program=root/'expanded-layout-probe'
        fixture.write_bytes(image)
        subprocess.run([compiler,'-std=c++17','-O2','-I',str(tools.parent/'include'),str(tools/'expanded_layout_probe.cpp'),'-o',str(program)],check=True)
        process=subprocess.run([str(program),str(fixture)],capture_output=True,text=True,timeout=30)
        if process.returncode:raise ValueError(f'Expanded native probe failed ({process.returncode}): {process.stderr.strip()}')
        report=json.loads(process.stdout);report['executable_sha256']=EXACT_SHA256;report['manifest']=manifest
        return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();report=run(args.executable)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='manifest'},indent=2))
