"""Test37 multi-pass sweep of every native reference the 256-actor build can
throw off. Runs on the exact executable and the two generated patch headers
(include/expanded_capacity_patches.hpp, include/script_vm_patches.hpp).

Pass 1  script VM id space: every immediate/displacement derived from the
        stock layout (0x9000 parents, 0x9002 child base, 0x19000 object-key
        names, 0x7e7000/0xfe6fff array-index bounds, 0xc801 pools, 24-byte
        entries, 0x12000 save maps) must be patched, dead, or classified.
        Every signed 16-bit read in the VM/save code must be classified.
Pass 2  relocation completeness: for each stock address the patch rows
        relocate, every other instruction reaching it (rip-relative, image
        base + disp, or a register loaded with lea and then indexed) must be
        patched or reach it through a patched base.
Pass 3  per-actor bit masks: every variable shift / register bit test in the
        AI range is listed with the instruction that defines its count.
Pass 4  native capacity limits: each fatal/drop limit that grows with actors
        is listed with its status (classification is data below).

This is an exhaustive static inventory over a linear sweep of .text, not a
proof of runtime behaviour.
"""
import argparse,bisect,collections,hashlib,json,re,struct
from pathlib import Path
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from capstone.x86 import X86_OP_IMM,X86_OP_MEM,X86_OP_REG,X86_REG_RIP
from audit_actor_capacity import EXACT_SHA256

ROOT=Path(__file__).resolve().parents[1]
CALLEE_SAVED=None

# Pass-1 hits outside the VM that share a value with the stock VM layout.
# Each was read in context (docs/capacity-sweep-test37.md).
VM_UNRELATED={
    0xbf7b:'allocator size argument (non-VM buffer)',0x2544d:'D3D resource size argument',
    0x296d1:'sound cache table at +0x9000',0x297f9:'sound cache table at +0x9000',
    0x5ce35:'D3D call argument',0x1119e1:'0x9000-byte static array scan (non-VM)',0x111c56:'same array',
    0x1122b2:'same array',0x11263f:'same array',0x114b43:'same array',
    0x2a2e06:'hash state at +0x9000',0x2a2fc4:'hash state',0x2a3024:'hash state',0x2a3513:'hash state',
    0xbe666:'UI draw flag argument',0x2714b:'table at +0x48000 (sound/stream)',0x27170:'same table',
    0x289f7:'same table',0x28a21:'same table',
    0xd56e3:'client skeleton allocator (patched by capacity rows)',0xd575d:'client skeleton allocator (patched by capacity rows)',
    0x221310:'debug overlay "num parent vars" display constant',
}
VM_DEAD=range(0x221600,0x221680)     # stock InitVariables body, replaced at its entry
SIGNED_OK={0x218cb7:'entity class number (status bits)',0x218f0d:'signed int16 array key'}
# Pass-2 references that reach a relocated address numerically but are a
# different object.
RELOC_UNRELATED={
    0xd9c4f8:'qword global at sentients[-1]; patched rows use it as an index-1 base',
    0xf4e1a0:'animation-globals root; only its +0x1ea0/+0x29a0/+0x2d20/+0x2d90 sub-arrays are relocated',
}
LIMITS=[
 {'limit':'script parent objects','stock':36856,'test37':65526,'fatal':'exceeded maximum number of parent script variables','status':'expanded; spawn guard + shedding'},
 {'limit':'script child variables','stock':102404,'test37':409592,'fatal':'exceeded maximum number of child0/child1 script variables','status':'expanded; guard + shedding'},
 {'limit':'object-key variable names','stock':'ids < 0x9000','test37':'ids < 0x10000','fatal':'refcount loss -> VM corruption','status':'fixed in Test37'},
 {'limit':'saved object numbers','stock':'signed 16-bit read','test37':'unsigned','fatal':'checkpoint load faults past 32767 objects','status':'fixed in Test37'},
 {'limit':'entity handle records','stock':4095,'test37':32767,'fatal':'ENT_HANDLE_COUNT exceeded - increase size','status':'expanded in Test37; telemetry, guard, shedding'},
 {'limit':'model lighting patches per frame','stock':4096,'test37':4096,'fatal':'modelLightingPatchList ran out of elements (ERR_FATAL)','status':'~1 per drawn model; telemetry, guard, shedding'},
 {'limit':'entities','stock':2046,'test37':2046,'fatal':'G_Spawn: no free entities','status':'shared helper spawners; guard + shedding'},
 {'limit':'script strings','stock':65536,'test37':65536,'fatal':'exceeded maximum number of script strings','status':'strings only (pointer data moved to owned heap); guard'},
 {'limit':'anim info','stock':4095,'test37':16383,'fatal':'exceeded maximum number of anim info','status':'expanded (Test33)'},
 {'limit':'anim transfer','stock':2048,'test37':16384,'fatal':'exceeded maximum number of anim transfer info','status':'expanded (Test33)'},
 {'limit':'dobj transfer','stock':1024,'test37':8192,'fatal':'exceeded maximum number of dobj transfer info','status':'expanded (Test34)'},
 {'limit':'checkpoint buffers','stock':'1 MiB','test37':'32 MiB','fatal':'MemFile_StartSegment: Out of memory','status':'expanded (Test34)'},
 {'limit':'client/server skeleton memory','stock':'576/512 KiB','test37':'9/8 MiB','fatal':'CL_SKEL_MEMORY_SIZE exceeded','status':'expanded (Test28)'},
 {'limit':'DObjs','stock':2047,'test37':2047,'fatal':'No free DObjs','status':'one per entity, bounded by entities'},
 {'limit':'renderer scene/model/surface limits','stock':'various','test37':'skin/surface pools expanded','fatal':'not drawn (warning)','status':'counted by AI_RENDER_OVERFLOW; 0 at 150 actors'},
 {'limit':'dropped weapons','stock':32,'test37':32,'fatal':'none (oldest recycled)','status':'fixed-size ring, not per actor'},
 {'limit':'FX ents (script spawnFx)','stock':768,'test37':768,'fatal':'Ran out of free FX Ents','status':'created by map scripts, not actors'},
 {'limit':'snapshot archive message','stock':'16 KiB','test37':'16 KiB','fatal':'msg overflowed','status':'carries glass/dynent change records, not actor states'},
 {'limit':'sound restore buffer','stock':'16 KiB','test37':'16 KiB','fatal':'SND_RESTORE_BUFFER_SIZE exceeded','status':'bounded by sound channels'},
]

def linear(pe):
    t=next(s for s in pe.sections if s.Name.startswith(b'.text'))
    cs=Cs(CS_ARCH_X86,CS_MODE_64);cs.detail=True;cs.skipdata=True
    return [i for i in cs.disasm(t.get_data(),t.VirtualAddress) if i.id]

def patch_spans():
    spans=[];rows=[];kept=set()
    cs=Cs(CS_ARCH_X86,CS_MODE_64);cs.detail=True
    for name in ('expanded_capacity_patches.hpp','script_vm_patches.hpp'):
        text=(ROOT/'include'/name).read_text()
        for m in re.finditer(r'\{0x([0-9a-f]+),"([0-9a-f]*)","([0-9a-f]*)",\{(.*?)\}\}',text):
            a=int(m.group(1),16);before=bytes.fromhex(m.group(2));after=bytes.fromhex(m.group(3))
            if a<0x4000000:
                spans.append((a,a+len(after)))
                if before:rows.append((a,before))
                for i in cs.disasm(after,a):
                    for o in i.operands:
                        if o.type==X86_OP_MEM and o.mem.base==X86_REG_RIP:kept.add((i.address+i.size+o.mem.disp)&0xffffffff)
                        elif o.type==X86_OP_MEM and 0x300000<=o.mem.disp<0x5000000:kept.add(o.mem.disp)
            for f in re.finditer(r'\{(\d+),(\d+),0x([0-9a-f]+),0x([0-9a-f]+),(true|false)\}',m.group(4)):
                t=int(f.group(3),16)
                if t&0x80000000:kept.add(t&0x7fffffff)
                elif t<0x4000000:kept.add(t)
    spans.sort();starts=[s for s,_ in spans]
    def covered(a):
        k=bisect.bisect_right(starts,a)-1
        return k>=0 and spans[k][0]<=a<spans[k][1]
    return covered,rows,kept

def pass1(code,covered):
    fam={}
    def add(v,why):
        for x in (v&0xffffffff,(-v)&0xffffffff):fam.setdefault(x,why)
    P,C=0x9000,0x9002
    for d in range(-2,3):
        add(P+d,f'parent count{d:+d}');add(0x10000+P+d,f'object-key name end{d:+d}')
        add(0x800000-(0x10000+P)+d,f'array index bias{d:+d}');add(0x1000000-(0x10000+P)+d,f'array index span{d:+d}')
        add(C+0xc801+d,f'pool1 base{d:+d}');add(C+2*0xc801+d,f'list end{d:+d}')
    for k in (2,4,8,24):
        add(P*k,f'parent count x{k}');add(C*k,f'child base x{k}')
    add(0x12068,'scrVarPub map B')
    rows=[];unclassified=[]
    for i in code:
        if i.mnemonic.startswith(('call','j','loop')):continue
        for o in i.operands:
            v=o.imm&0xffffffff if o.type==X86_OP_IMM else (o.mem.disp&0xffffffff if o.type==X86_OP_MEM and o.mem.base!=X86_REG_RIP else None)
            if v is None or v not in fam or v<0x200:continue
            a=i.address
            status='patched' if covered(a) else 'dead (replaced InitVariables)' if a in VM_DEAD else VM_UNRELATED.get(a)
            rows.append({'rva':hex(a),'instruction':f'{i.mnemonic} {i.op_str}','family':fam[v],'status':status or 'UNCLASSIFIED'})
            if not status:unclassified.append(rows[-1])
    signed=[];bad=[]
    for i in code:
        if not(0x216000<=i.address<0x228000):continue
        if i.mnemonic in('movsx','movsxd','cwde','cwd') and (i.mnemonic in('cwde','cwd') or 'word ptr' in i.op_str or re.search(r', (ax|bx|cx|dx|si|di|bp|r\d+w)$',i.op_str)):
            if i.mnemonic=='movsxd':continue
            st='patched to movzx' if covered(i.address) else SIGNED_OK.get(i.address)
            signed.append({'rva':hex(i.address),'instruction':f'{i.mnemonic} {i.op_str}','status':st or 'UNCLASSIFIED'})
            if not st:bad.append(signed[-1])
    return {'hits':rows,'unclassified':unclassified,'signed_16bit_reads':signed,'signed_unclassified':bad}

def datarefs(code):
    regs={};out=[]
    keep={36,37,39,43,110,111,112,113}
    for i in code:
        m=i.mnemonic
        if m in('ret','int3') or (m=='jmp' and i.operands and i.operands[0].type!=X86_OP_MEM):regs={}
        for o in i.operands:
            if o.type!=X86_OP_MEM:continue
            if o.mem.base==X86_REG_RIP:out.append((i.address,m,i.op_str,'rip',None,(i.address+i.size+o.mem.disp)&0xffffffff));continue
            if 0x300000<=o.mem.disp<0x5000000:out.append((i.address,m,i.op_str,'imagebase',None,o.mem.disp))
            for r in (o.mem.base,o.mem.index):
                if r in regs and (r==o.mem.base or o.mem.scale==1):
                    b,src=regs[r];out.append((i.address,m,i.op_str,'based',src,(b+o.mem.disp)&0xffffffff))
        if m=='lea' and i.operands[1].mem.base==X86_REG_RIP:
            regs[i.operands[0].reg]=((i.address+i.size+i.operands[1].mem.disp)&0xffffffff,i.address);continue
        if m=='mov' and len(i.operands)==2 and i.operands[0].type==X86_OP_REG and i.operands[1].type==X86_OP_REG and i.operands[1].reg in regs and i.operands[0].size==8:
            regs[i.operands[0].reg]=regs[i.operands[1].reg];continue
        if m=='call':regs={k:v for k,v in regs.items() if k in keep}
        for r in i.regs_access()[1]:regs.pop(r,None)
    return out

def pass2(code,covered,rows,kept):
    cs=Cs(CS_ARCH_X86,CS_MODE_64);cs.detail=True
    targets=set()
    for a,b in rows:
        for i in cs.disasm(b,a):
            for o in i.operands:
                if o.type==X86_OP_MEM and o.mem.base==X86_REG_RIP:targets.add((i.address+i.size+o.mem.disp)&0xffffffff)
                elif o.type==X86_OP_MEM and 0x300000<=o.mem.disp<0x5000000:targets.add(o.mem.disp)
    targets-=kept
    refs=collections.defaultdict(list)
    for a,m,ops,kind,src,t in datarefs(code):
        if t in targets:refs[t].append((a,m,ops,kind,src))
    missing=[];explained=[]
    for t,rr in sorted(refs.items()):
        for a,m,ops,kind,src in rr:
            if covered(a) or (src is not None and covered(src)):continue
            row={'target':hex(t),'rva':hex(a),'instruction':f'{m} {ops}','kind':kind}
            if t in RELOC_UNRELATED:row['reason']=RELOC_UNRELATED[t];explained.append(row)
            else:missing.append(row)
    return {'relocated_targets':len(targets),'explained':explained,'unpatched':missing}

def pass3(code):
    by={a.address:a for a in code};order=[a.address for a in code];idx={a:n for n,a in enumerate(order)}
    rows=[]
    for i in code:
        if not(0x120000<=i.address<0x1c0000):continue
        var=(i.mnemonic in('shl','sal','rol') and i.op_str.endswith(', cl')) or (i.mnemonic in('bt','bts','btr','btc') and i.operands[1].type==X86_OP_REG)
        if not var:continue
        reg=i.reg_name(i.operands[1].reg) if i.mnemonic.startswith('bt') else 'cl'
        names={'cl':('cl','ecx','rcx','cx')}.get(reg,(reg,))
        source=None
        for k in range(idx[i.address]-1,max(idx[i.address]-16,0),-1):
            j=by[order[k]]
            if j.operands and j.operands[0].type==X86_OP_REG and j.reg_name(j.operands[0].reg) in names and j.mnemonic in('mov','movzx','movsx','movsxd','lea','cmovle','xor'):
                source=f'{j.address:x}: {j.mnemonic} {j.op_str}';break
        rows.append({'rva':hex(i.address),'instruction':f'{i.mnemonic} {i.op_str}','count_source':source})
    return rows

def audit(executable):
    raw=executable.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=EXACT_SHA256:raise ValueError('Exact executable SHA256 differs')
    pe=pefile.PE(data=raw,fast_load=True);code=linear(pe)
    covered,rows,kept=patch_spans()
    p1=pass1(code,covered);p2=pass2(code,covered,rows,kept);p3=pass3(code)
    if p1['unclassified'] or p1['signed_unclassified']:raise ValueError('Unclassified script-VM layout reference: '+json.dumps(p1['unclassified']+p1['signed_unclassified'])[:2000])
    if p2['unpatched']:raise ValueError('Unpatched reference to a relocated address: '+json.dumps(p2['unpatched'])[:2000])
    return {'executable_sha256':EXACT_SHA256,'instructions_swept':len(code),
            'pass1_script_vm':{'hits':len(p1['hits']),'patched':sum(1 for r in p1['hits'] if r['status']=='patched'),'rows':p1['hits'],'signed_16bit_reads':p1['signed_16bit_reads']},
            'pass2_relocation':{'relocated_targets':p2['relocated_targets'],'explained_numeric_overlaps':p2['explained'],'unpatched':p2['unpatched']},
            'pass3_bit_masks':{'sites':len(p3),'rows':p3,'review':'counts come from team fields, entity types, option tables, attachment slots or player client numbers; none from an actor or sentient index (docs/capacity-sweep-test37.md)'},
            'pass4_limits':LIMITS,
            'scope':'Static linear sweep of .text against the generated patch headers; complements the native fixtures. Not a proof of Windows gameplay.'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    r=audit(a.executable);a.output.write_text(json.dumps(r,indent=1)+'\n')
    print(json.dumps({'instructions':r['instructions_swept'],'pass1_hits':r['pass1_script_vm']['hits'],'pass1_patched':r['pass1_script_vm']['patched'],
                      'pass2_targets':r['pass2_relocation']['relocated_targets'],'pass2_explained':len(r['pass2_relocation']['explained_numeric_overlaps']),
                      'pass3_sites':r['pass3_bit_masks']['sites']},indent=1))
