"""Test the Test22 patch plan in an isolated exact-native MS-ABI fixture."""
import argparse,json,hashlib,subprocess,tempfile,gc,re,struct
from pathlib import Path
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from capstone.x86 import X86_OP_IMM
from prepare_expanded_runtime import prepare,runtime_plan
from audit_actor_capacity import EXACT_SHA256

def run(executable):
 data=executable.read_bytes()
 if hashlib.sha256(data).hexdigest()!=EXACT_SHA256:raise ValueError('Wrong executable')
 pe=pefile.PE(data=data,fast_load=True);image,meta=prepare(pe);plan=runtime_plan(pe,image,meta);base=pe.OPTIONAL_HEADER.ImageBase
 d=Cs(CS_ARCH_X86,CS_MODE_64);d.detail=True
 keep={0x270080,0x270c90,0x26feb0,0x1af020,0x1af040,0x1ad910,0x1adfb0,0x1adf50,0x1b2070,0x1b1c90,0x1fb5f0,0x2e3b40,0x2b9300,0x21a370}
 isolated=set()
 for a,z in ((0x180cf0,0x1819d3),(0x17f8e0,0x180749)):
  for i in d.disasm(pe.get_data(a,z-a),base+a):
   if i.mnemonic=='call' and i.operands[0].type==X86_OP_IMM:
    target=i.operands[0].imm-base
    if target not in keep:isolated.add(target)
 isolated.update((0x268540,0x268740,0x268650,0x184c10,0x195200,0x6d9f0,0x6bc00,0x216de0,0x6d940,0x216660,0x6d570))
 for at in isolated:image[at:at+3]=bytes.fromhex('31 c0 c3')

 # Assemble a second image ONLY from the checked runtime plan, with its arena
 # outside the main image. Earlier fixture-only stubs cannot leak into this plan.
 relocated=bytearray(pe.get_memory_mapped_image());relocated.extend(bytes(max(0,0x5800000-len(relocated))))
 def address(rva):
  if rva&0x80000000:return rva&0x7fffffff
  return rva+0x1000000 if 0x4000000<=rva<0x4800000 else rva
 for patch in plan:
  at=address(patch['rva']);blob=bytearray.fromhex(patch['after'])
  if patch['rva']<0x4000000 and bytes(relocated[at:at+len(blob)]).hex()!=patch['before']:raise ValueError('Original-byte validation failed')
  for fix in patch['fixups']:
   value=address(fix['target'])-(address(fix['end']) if fix['relative'] else fix['end'])
   blob[fix['offset']:fix['offset']+fix['width']]=value.to_bytes(fix['width'],'little',signed=True)
  relocated[at:at+len(blob)]=blob
 for at in isolated:relocated[at:at+3]=bytes.fromhex('31 c0 c3')
 # Explicit harness-only substitutions, excluded from the runtime manifest.
 relocated[0x6b980]=0xc3
 fill=bytes.fromhex('57 48 89 cf 89 d0 4c 89 c1 fc f3 aa 5f c3');relocated[0x2e42d0:0x2e42d0+len(fill)]=fill
 relocated[0x17119d:0x1711a2]=b'\xe9'+struct.pack('<i',0x1711b8-0x1711a2)
 tools=Path(__file__).resolve().parent
 with tempfile.TemporaryDirectory(prefix='bo1mw2-test22-') as temp:
  root=Path(temp);fixture=root/'private-image.bin';fixture.write_bytes(image)
  external=root/'private-relocated.bin';external.write_bytes(relocated)
  del image,relocated,pe,data;gc.collect()
  source=(tools/'expanded_runtime_probe.cpp').read_text()
  source=re.sub(r'0x[0-9a-fA-F]+',lambda m:hex(int(m[0],16)+0x1000000) if 0x4000000<=int(m[0],16)<0x4800000 else m[0],source)
  source=source.replace('mmap(nullptr,0x5000000','mmap(nullptr,0x6000000')
  relocated_source=root/'relocated-probe.cpp';relocated_source.write_text(source)
  reports=[]
  for cpp,fixture_path in ((tools/'expanded_runtime_probe.cpp',fixture),(relocated_source,external)):
   program=root/cpp.stem
   subprocess.run(['g++','-std=c++17','-O2','-I',str(tools.parent/'include'),str(cpp),'-o',str(program)],check=True)
   process=subprocess.run([str(program),str(fixture_path)],capture_output=True,text=True,timeout=45)
   if process.returncode:raise ValueError(f'Native test22 {cpp.name} failed ({process.returncode}): {process.stderr.strip()}')
   reports.append(json.loads(process.stdout))
  if reports[0]!=reports[1]:raise ValueError('External arena changed native results')
  report=reports[0]
  report['checked_runtime_plan_external_arena_tested']=True
  report['arena_relocation_delta']='0x1000000'
  report['substituted_dependencies']=['non-booted engine checkpoint domains listed below','Windows thread-identity queries','post-read VM cleanup','Windows stack probe','native memcpy/memset MS-ABI byte transport','animation tree creation/release recorders','following engine teardown omitted while preserving native final argument reset','snapshot profiler']
 report.update(executable_sha256=EXACT_SHA256,checked_runtime_patches=sum(p['rva']<0x4000000 for p in plan),isolated_engine_function_rvas=[hex(v) for v in sorted(isolated)])
 return report
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();report=run(a.executable);a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
