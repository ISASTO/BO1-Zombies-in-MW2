"""Advance the private 256/274 fixture through cleanup and population serialization.

These edits are applied to a disposable mapped image, never to an installed EXE
or the DLL. Full engine checkpoint and client snapshot integration remain gated.
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
from run_expanded_layout_probe import prepare as prepare_test19, ACTORS, SENTIENTS, STRIDE, POOL, SENTIENT_POOL, ANIMATION_POINTERS

REFERENCE_HEADS=0x4570000
PERCEPTION_SITES=(
    0x124822,0x12482a,0x124832,0x125bc5,0x12b7ad,0x12b7c0,0x12b7cf,
    0x12d467,0x12d46f,0x12d479,0x12d481,0x12d489,0x12d491,0x12d499,0x12d4a1,0x12d4a9,0x12d4b1,0x12d4b9,
    0x12f165,0x12f4d9,0x12f4ed,0x12f4f6,0x12f504,0x12f779,0x12f94a,0x12f951,0x133c48,
    0x1380b8,0x13811e,0x138b78,0x138c94,0x138fbf,0x13a519,0x1485de,0x148615,0x148631,0x148731,
    0x149208,0x14f8af,0x15092b,0x150951,0x150a2f,0x150e9e,0x1510d2,0x1510e8,0x1519af,0x151adf,
    0x151afa,0x151b77,0x151b9d,0x151c6c,0x151c85,0x15224f,0x15234e,0x15606a,0x1560c9,0x1562df,
    0x1568f1,0x15695c,0x1569b9,0x1569d2,0x1569db,0x156a5a,0x156ad2,
)


def prepare(pe):
    image, previous=prepare_test19(pe)
    base=pe.OPTIONAL_HEADER.ImageBase
    decoder=Cs(CS_ARCH_X86,CS_MODE_64);decoder.detail=True
    edits=[];cursor=0x4601000
    def ins(at):return next(decoder.disasm(pe.get_data(at,16),base+at))
    def put(at,blob,role):
        if bytes(image[at:at+len(blob)])==blob:return
        before=bytes(image[at:at+len(blob)]);image[at:at+len(blob)]=blob
        edits.append({'rva':hex(at),'before':before.hex(),'after':blob.hex(),'role':role})
    def jump(at,target):return b'\xe9'+struct.pack('<i',target-at-5)
    def immediate(at,value,role,expected=None):
        first=ins(at)
        if expected is not None and not any(o.type==X86_OP_IMM and o.imm==expected for o in first.operands):
            raise ValueError(f'Wrong immediate at {at:x}')
        if first.imm_size not in (1,4,8):raise ValueError('Unsupported native immediate')
        put(at+first.imm_offset,int(value).to_bytes(first.imm_size,'little'),role)
    def rip(at,target,role):
        first=ins(at)
        if first.disp_size!=4 or not any(o.type==X86_OP_MEM and o.mem.base==X86_REG_RIP for o in first.operands):
            raise ValueError('Expected native RIP-relative address')
        put(at+first.disp_offset,struct.pack('<i',target-at-first.size),role)
    def bound(at,value,expected):
        nonlocal cursor
        first=ins(at);branch=ins(at+first.size)
        if first.mnemonic!='cmp' or first.imm_size!=1 or first.operands[-1].imm!=expected or not branch.mnemonic.startswith('j'):
            raise ValueError(f'Native capacity guard differs at {at:x}')
        code=bytearray(first.bytes);code[code.index(0x83)]=0x81
        code=code[:first.imm_offset]+struct.pack('<i',value)
        target=branch.operands[0].imm-base
        condition=(branch.bytes[0]&15) if len(branch.bytes)==2 else (branch.bytes[1]&15)
        code+=b'\x0f'+bytes([0x80|condition])+struct.pack('<i',target-(cursor+len(code)+6))
        length=first.size+branch.size
        put(cursor,bytes(code)+jump(cursor+len(code),at+length),'complete lifecycle capacity relay')
        put(at,jump(at,cursor)+b'\x90'*(length-5),'complete lifecycle capacity guard')
        cursor+=len(code)+5
    # Exact instruction inventory, not a blanket replacement of the value 32.
    inventory=json.loads((Path(__file__).parents[1]/'docs/actor-capacity-test17-evidence.json').read_text())['consumer_inventory']
    for row in inventory['actor_stride']:
        immediate(int(row['rva'],16),STRIDE,'full actor traversal stride',0x1560)
    for row in inventory['actor_pool_byte_length']:
        immediate(int(row['rva'],16),ACTORS*STRIDE,'full actor traversal span',0x2ac00)
    for row in inventory['sentient_storage']:
        rip(int(row['rva'],16),SENTIENT_POOL,'relocated direct sentient storage')
    for row in inventory['actor_storage']:
        rip(int(row['rva'],16),POOL,'relocated direct actor storage')
    for row in inventory['animation_pointers']:
        rip(int(row['rva'],16),ANIMATION_POINTERS,'relocated animation lifecycle array')
    for at in (0x170c56,0x1710e9,0x175151):immediate(at,ACTORS,'256-entry animation lifecycle',32)
    for at in PERCEPTION_SITES:
        first=ins(at)
        field=next((o.mem.disp for o in first.operands if o.type==X86_OP_MEM and o.mem.base!=X86_REG_RIP and 0x348<=o.mem.disp<=0x370),None)
        if field is None or first.disp_size!=4:raise ValueError(f'Perception access differs at {at:x}')
        put(at+first.disp_offset,struct.pack('<i',field+0x1560-0x348),'appended inline perception consumer')
    immediate(0x12d43a,0x1560,'common native perception accessor',0x348)
    for at in (0x1269fc,0x126e02,0x1286ad):
        immediate(at,0x709ad4e4ba80709b,'remaining actor index division',0xbfa02fe80bfa03)
    for at in (0x126aa4,0x126f4f,0x1271c7,0x1273b0,0x127528,0x1286ba):
        immediate(at,13,'remaining actor index division shift',4)
    for at in (0x1269d7,0x126ab5,0x126af7,0x126b65,0x126e33,0x126f60,0x126f9c,
               0x12708d,0x1271d8,0x127218,0x127267,0x1273c1,0x1273fd,0x127458,0x127539,0x127578,0x1286cb,0x1286ff):
        bound(at,ACTORS,32)
    # Replace the unrolled special-allocation census. Its original quota of
    # sixteen non-actor/non-player sentients is retained, over all 274 records.
    start=0x1b0c01
    # Local branch offsets are assembled from explicit labels to avoid relying
    # on the old ten-record unrolling's instruction sizes.
    code=bytearray(bytes.fromhex('31 d2 4c 89 c0 41 b9')+struct.pack('<I',SENTIENTS));loop=len(code)
    branches=[]
    code+=bytes.fromhex('38 98 82 00 00 00');branches.append(len(code));code+=b'\x74\0'
    code+=bytes.fromhex('48 8b 08 48 39 99 10 01 00 00');branches.append(len(code));code+=b'\x75\0'
    code+=bytes.fromhex('48 39 99 08 01 00 00');branches.append(len(code));code+=b'\x75\0'
    code+=bytes.fromhex('ff c2');next_record=len(code)
    for at in branches:code[at+1]=next_record-(at+2)
    code+=bytes.fromhex('48 05 88 00 00 00 49 ff c9 75')
    code+=struct.pack('<b',loop-(len(code)+1))
    code+=jump(start+len(code),0x1b0d77)
    put(start,bytes(code),'complete special-sentient census')
    for at in (0x1b0d98,0x1b1610,0x1b172f,0x1b1763):bound(at,SENTIENTS,50)
    # Each sentient owns a WORD head in a separate reference list. Relocating
    # the pool alone makes cleanup beyond index 49 write into adjacent globals.
    for at in (0x15b125,0x15b28f):rip(at,REFERENCE_HEADS,'274-entry sentient reference-head array')
    start=0x15b050
    code=bytearray(b'\x57\x48\x8d\x3d'+struct.pack('<i',REFERENCE_HEADS-(start+8)))
    code+=b'\xb9'+struct.pack('<I',SENTIENTS)+bytes.fromhex('31 c0 fc 66 f3 ab 5f')
    code+=b'\x89\x05'+struct.pack('<i',0xcf7074-(start+len(code)+6))+b'\xc3'
    put(start,bytes(code),'complete sentient reference-head reset')
    # The actual animation-array cleanup runs in full. Its following global
    # game/script/zone shutdown needs a booted engine, so only that tail is
    # omitted in this disposable fixture; the native stack epilogue remains.
    put(0x17119d,jump(0x17119d,0x1711ba),'fixture-only following engine teardown isolation')
    return image,{'previous_stage':previous,'stage21_edits':edits,'inline_perception_sites':len(PERCEPTION_SITES),
                  'common_perception_accessor':True,'relocated_reference_heads':SENTIENTS,
                  'runtime_activation':False,
                  'remaining_gates':['transitive perception/index consumer review',
                                     'snapshot client/transport consumers and old/new representation',
                                     'native checkpoint stream routing/format detection and surrounding engine data',
                                     'engine startup, animation assets and Windows gameplay stress']}


def run(executable):
    if platform.system()!='Linux' or platform.machine().lower() not in ('amd64','x86_64'):raise ValueError('Linux/x86-64 required')
    data=executable.read_bytes()
    if hashlib.sha256(data).hexdigest()!=EXACT_SHA256:raise ValueError('Exact executable SHA256 mismatch')
    pe=pefile.PE(data=data);image,manifest=prepare(pe)
    compiler=shutil.which('g++')
    if not compiler:raise ValueError('Host C++ compiler unavailable')
    tools=Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='bo1mw2-expanded-lifecycle-') as directory:
        root=Path(directory);fixture=root/'private-image.bin';fixture.write_bytes(image)
        reports=[]
        for source in ('expanded_layout_probe.cpp','expanded_lifecycle_probe.cpp'):
            program=root/Path(source).stem
            subprocess.run([compiler,'-std=c++17','-O2','-I',str(tools.parent/'include'),str(tools/source),'-o',str(program)],check=True)
            process=subprocess.run([str(program),str(fixture)],capture_output=True,text=True,timeout=45)
            if process.returncode:raise ValueError(f'{source} failed ({process.returncode}): {process.stderr.strip()}')
            reports.append(json.loads(process.stdout))
    return {'executable_sha256':EXACT_SHA256,'allocation_snapshot_regression':reports[0],
            'lifecycle_population_regression':reports[1],'manifest':manifest,'runtime_expansion_enabled':False}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();report=run(args.executable)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='manifest'},indent=2))
