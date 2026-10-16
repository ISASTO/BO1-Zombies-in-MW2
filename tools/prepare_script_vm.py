"""Expand native script child variables from 2 to 8 hashed pools.

Test33 Estate (179 actors) and Contingency (~111 actors) stopped with
"exceeded maximum number of child1 script variables". The native variable
list holds 0x9000 parent objects in interleaved free lists and two child
pools of 0xc801 entries; a parent's children live in pool (parent & 1).
Relative child ids are 16-bit, so pools cannot simply grow. AllocObject
already balances new parents toward the less-used pool.

This generalizes that design to eight pools: the list is relocated to an
owned region, parent free lists use step 8, every parent->pool selector
masks with 7, per-pool counters drive AllocObject's choice, and the
exhaustion messages cover every pool. Child pools 0 and 1 keep their stock
absolute positions, so every native offset folded against them stays valid.
Applied from DllMain, before the engine initializes script variables.
"""
import argparse, hashlib, json, struct, subprocess, tempfile
from pathlib import Path
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_OP_REG, X86_REG_RIP
from audit_actor_capacity import EXACT_SHA256
from prepare_expanded_crowd import native_unwind

POOLS=8
POOL_STRIDE=0xc801
# Stock: parents are entries 1..0x9000 (ids 0..0x8fff), children start at 0x9002.
OLD_PARENT_END=0x9001; OLD_CHILD_BASE=0x9002
# Test35: parents fill the whole 16-bit id space (ids 0..0xfffd); children
# move above it. Child relative ids stay 16-bit within each pool.
PARENT_END=0xffff; CHILD_BASE=0x10002
# Variable names: < 0x10000 string, 0x10000+id an object key (notify/endon
# wait lists, object-keyed arrays), else an int key (index+0x800000 mod 2^24).
# Stock decodes object keys as < 0x10000+0x9000; with ids up to 0xfffd that
# range must end at 0x20000, and the int-index range must shrink to match.
OLD_OBJECT_NAME_END=0x10000+(OLD_PARENT_END-1); OBJECT_NAME_END=0x10000+PARENT_END+1
OBJECT_NAME_SITES=(0x21d15d,0x21d766,0x21df11,0x21e550,0x21e682,0x221138)
# Checkpoint loader: saved object numbers are unsigned 16-bit (written by
# 0x219bc0 as a raw WORD) but 0x219360 reads them with movsx, so a save with
# >= 0x8000 objects indexes the save map with 0xffff8000+ and faults.
SAVE_ID_READ=(0x2193b6,'0fbf7c2438','0fb77c2438')
ARRAY_INDEX_SITES=((0x21bb11,0x21bb17),(0x21d6c0,0x21d6c6),(0x21e5b6,0x21e5bc),(0x21f1c6,0x21f1cd))
OLD_LIST=0x15c4400; OLD_ENTRIES=OLD_CHILD_BASE+2*POOL_STRIDE; OLD_END=OLD_LIST+OLD_ENTRIES*24
# Savegame object maps indexed by parent id / save id (0x9000 WORDs each).
OLD_MAPS=(0x15a03e8,0x15b23e8); OLD_MAP_BYTES=0x12000; MAP_BYTES=(PARENT_END*2+0xfff)&~0xfff
OLD_COUNTS=0x15a03b4
REGION=0x6000000; COUNTS=REGION+0x1000
# Map A grows in place over the old map B; map B moves into the stock
# variable-list storage, which the relocated list no longer uses. Both stay
# inside the image, so every short scrVarPub+0x68 access keeps working.
MAPS=(OLD_MAPS[0],OLD_LIST)
LIST=REGION+0x2000
SAVE_CELLS=REGION+0x1100; MT_CELLS=REGION+0x1200
BASE_SITE_COUNT=199; MAP_REFERENCE_COUNT=16
MAP_CLEARS=(0x2190ca,0x2196e3,0x2196f7,0x223d3d)
STRUCT_MAP_B_SITES=4
ENTRIES=CHILD_BASE+POOLS*POOL_STRIDE
LIST_BYTES=ENTRIES*24
LIST_GUARD=(LIST+LIST_BYTES+0xfff)&~0xfff          # inaccessible page after the list
# Native dobj transfer records (40 bytes, stock 0x400; Com_Error(ERR_DROP)).
DOBJ_OLD=0x1c0f240; DOBJ_STOCK=0x400; DOBJ_RECORDS=0x2000; DOBJ_STRIDE=40
DOBJ=LIST_GUARD+0x1000
DOBJ_GUARD=(DOBJ+DOBJ_RECORDS*DOBJ_STRIDE+0xfff)&~0xfff
# Entity/sentient handle info records (16 bytes, u16 index, stock 4096 incl.
# the null record; exhaustion is the fatal "ENT_HANDLE_COUNT exceeded").
# Every set EntHandle and SentientHandle owns one: ~10-15 per busy actor.
ENTH_OLD=0xce6000; ENTH_STOCK=0x1000; ENTH_RECORDS=0x8000; ENTH_STRIDE=16; ENTH_HEAD=0xcf6000
ENTH=DOBJ_GUARD+0x1000
ENTH_GUARD=(ENTH+ENTH_RECORDS*ENTH_STRIDE+0xfff)&~0xfff
ENTH_REFERENCES=7      # 4 base leas, init cursor lea, last-record terminator, init bound
REGION_BYTES=ENTH_GUARD+0x1000-REGION
CODE_BYTES=0x500; UNWIND=REGION+0x800
# Checkpoint slots: two static 1 MiB buffers; overflow is the fatal
# "MemFile_StartSegment: Out of memory". Owned buffers are published through
# cells so native lea sites become loads.
SAVE_OLD=(0xf56e00,0x1056e00); SAVE_STOCK=0x100000; SAVE_BYTES=0x2000000
SAVE_LEAS=(0x1aee8d,0x1aeea5,0x1af083,0x1af09e,0x1af100,0x1af10e)
SAVE_SIZES=(0x1aee38,0x1aee94,0x1aeec1,0x1af08a,0x1af0a5,0x1ad9e0,0x1ada03)
# (relay begin, end, native site whose frame the body relay continues)
RELAYS=((REGION+0x000,REGION+0x100,None),
        (REGION+0x100,REGION+0x200,0x21b7fa),
        (REGION+0x200,REGION+0x280,0x21b79b),
        (REGION+0x280,REGION+0x300,0x21c66c),
        (REGION+0x300,REGION+0x380,0x21c811),
        (REGION+0x380,REGION+0x400,0x21d0fa),
        (REGION+0x480,REGION+0x4c0,'mt_init'))
INIT_UNWIND=bytes((1,5,2,0, 5,0x32, 1,0x30))       # push rbx; sub rsp,0x20
MT_INIT_UNWIND=bytes((1,4,1,0, 4,0x42, 0,0))       # sub rsp,0x28
# Relay entries: (site, bytes replaced, expected original, relay)
ENTRIES_SITES=((0x221600,5,'4883ec2833',REGION+0x000),
               (0x21b7fa,6,'8b05b84b3801',REGION+0x100),
               (0x21b79b,11,'81fb02900000488b5c2430',REGION+0x200),
               (0x21c66c,16,'81fb02900000b9040000008bc50f45c1',REGION+0x280),
               (0x21c811,17,'81ff02900000b904000000418bc10f45c1',REGION+0x300),
               (0x21d0fa,20,'b80400000081fb02900000488b5c24784c0f45e8',REGION+0x380),
               (0x216ee0,5,'4883ec28e8',REGION+0x400),                 # MT_Alloc
               (0x217500,5,'48895c2408',REGION+0x440),                 # MT_Free
               (0x217780,11,'4883ec28488d05f5e82401',REGION+0x480))    # MT_Init
# Native counter updates whose displacement moves to the per-pool counters.
COUNTER_SITES=(0x21b7ca,0x21c67c,0x21c82e,0x21d112,0x21e8f8)
# 0x21b7bf cmovne eax,ecx -> mov eax,ecx: ecx carries k*4 from the relay.
CMOV_SITE=(0x21b7bf,'0f45c1','89c890')
# Exhaustion dispatch: stock only names child0/child1; the cmp/jne pair let
# any other pool fall through *without* an error. Every non-parent,
# non-child0 pool now reports through the child1 message.
ERROR_SITES=((0x21b6f2,'3d01c80000751e'),(0x21d91d,'81f901c800007524'))
PARENT_HEADS=(0x21bbb8,0x21dac7,0x21dbda)
EXTRA_SELECTORS=(0x2219be,)          # crosses a chunk boundary; reviewed
NOT_SELECTORS=(0x20ad6d,0x1c8451)    # bit extraction / unrelated code


def text_section(pe):
    return next(s for s in pe.sections if s.Name.startswith(b'.text'))


def decode_all(pe):
    s=text_section(pe);cs=Cs(CS_ARCH_X86,CS_MODE_64);cs.detail=True;cs.skipdata=True
    return [i for i in cs.disasm(s.get_data(),s.VirtualAddress) if i.id]


def build_relays():
    with tempfile.TemporaryDirectory(prefix='bo1mw2-script-vm-') as d:
        root=Path(d);obj=root/'g.o';binary=root/'g.bin'
        subprocess.run(['as','--64','--defsym',f'CHILD_BASE={CHILD_BASE}','--defsym',f'PARENT_END={PARENT_END}',
                        '--defsym',f'LIST_RVA={LIST}',str(Path(__file__).with_name('script_vm_guard.S')),'-o',str(obj)],check=True,capture_output=True)
        subprocess.run(['ld','-Ttext',hex(REGION),'--oformat=binary','-o',str(binary),str(obj)],check=True,capture_output=True)
        blob=binary.read_bytes()
    if len(blob)!=CODE_BYTES:raise ValueError('Script VM relay layout differs')
    return blob


def zone(t):
    if OLD_MAPS[0]<=t<OLD_MAPS[0]+OLD_MAP_BYTES:return 'mapA'
    if OLD_MAPS[1]<=t<OLD_MAPS[1]+OLD_MAP_BYTES:return 'mapB'
    if OLD_LIST<=t<OLD_LIST+OLD_CHILD_BASE*24:return 'parents'
    if OLD_LIST+OLD_CHILD_BASE*24<=t<OLD_END:return 'children'
    if OLD_COUNTS<=t<OLD_COUNTS+8:return 'counts'
    return 'other'


def based_accesses(code):
    """Memory operands whose base register holds a rip-relative address in the
    script VM globals (e.g. scrVarPub 0x15a0380) and whose displacement
    reaches a different relocation zone. Test35 missed exactly these: the
    save marker addressed the per-parent maps as scrVarPub+0x68/+0x12068."""
    regs={};out=[]
    for i in code:
        if i.mnemonic=='ret' or (i.mnemonic=='jmp' and i.operands and i.operands[0].type!=X86_OP_MEM):regs={}
        for o in i.operands:
            if o.type==X86_OP_MEM and o.mem.base!=X86_REG_RIP and o.mem.base in regs:
                b=regs[o.mem.base];t=b+o.mem.disp
                if zone(b)!=zone(t):out.append((i,b,t))
            elif (o.type==X86_OP_MEM and o.mem.base!=X86_REG_RIP and o.mem.index in regs and o.mem.scale==1
                  and o.mem.base not in regs):
                b=regs[o.mem.index];t=b+o.mem.disp
                if zone(b)!=zone(t):out.append((i,b,t))
        if i.mnemonic=='lea' and i.operands[1].mem.base==X86_REG_RIP:
            t=i.address+i.size+i.operands[1].mem.disp
            if 0x15a0380<=t<0x18f4460:regs[i.operands[0].reg]=t;continue
        if (i.mnemonic=='mov' and len(i.operands)==2 and i.operands[0].type==X86_OP_REG and i.operands[1].type==X86_OP_REG
            and i.operands[1].reg in regs and i.operands[0].size==8):
            regs[i.operands[0].reg]=regs[i.operands[1].reg];continue
        for r in i.regs_access()[1]:regs.pop(r,None)
    return out


def mask_sites(pe,code):
    """Every 32-bit 'and reg,1' whose result reaches imul ...,0xc801."""
    by={i.address:i for i in code}
    imuls=[i.address for i in code if i.mnemonic=='imul' and len(i.operands)==3 and i.operands[2].type==X86_OP_IMM and i.operands[2].imm==POOL_STRIDE]
    order=[i.address for i in code];index={a:n for n,a in enumerate(order)}
    sel=set(EXTRA_SELECTORS);reach={}
    for i in code:
        if not(i.mnemonic=='and' and i.operands[0].type==X86_OP_REG and i.operands[0].size==4 and i.operands[1].type==X86_OP_IMM and i.operands[1].imm==1):continue
        reg=i.reg_name(i.operands[0].reg)
        # Follow straight-line and both branch targets within 0x60 bytes.
        work=[i.address+i.size];seen=set();hit=[]
        while work:
            a=work.pop()
            while a in by and a not in seen and a<i.address+0x60:
                seen.add(a);j=by[a]
                if j.mnemonic=='imul' and a in imuls and j.reg_name(j.operands[1].reg)==reg:hit.append(a);break
                written=[j.reg_name(r) for r in j.regs_access()[1]]
                volatile=reg in('eax','ecx','edx','r8d','r9d','r10d','r11d')
                if reg in written or j.mnemonic=='ret' or (j.mnemonic=='call' and volatile):break
                if j.mnemonic=='jmp' and j.operands[0].type==X86_OP_IMM:a=j.operands[0].imm;continue
                if j.mnemonic.startswith('j') and j.operands[0].type==X86_OP_IMM:work.append(j.operands[0].imm)
                a=j.address+j.size
        if hit:sel.add(i.address);reach[i.address]=hit
    unreached=[a for a in imuls if not any(a in v for v in reach.values()) and a not in (0x2219cb,)]
    if len(imuls)!=110 or unreached:raise ValueError(f'Child-pool selector inventory differs: {len(imuls)} sites, unreached {[hex(a) for a in unreached]}')
    for a in sel|set(PARENT_HEADS):
        j=by[a]
        if j.mnemonic!='and' or j.bytes[-1]!=1 or a in NOT_SELECTORS:raise ValueError(f'Unexpected selector at {a:x}')
    return sorted(sel),imuls


def prepare(pe,raw):
    code=decode_all(pe);base=pe.OPTIONAL_HEADER.ImageBase
    edits={}                     # rva -> (after bytes, fixups, role)
    def put(at,after,role,fixups=()):
        if any(a<at+len(after) and at<a+len(e[0]) for a,e in edits.items()):raise ValueError(f'Overlapping script VM edit {at:x}')
        edits[at]=(bytes(after),list(fixups),role)
    def orig(at,n):return pe.get_data(at,n)
    # 1. Variable-list references (rip-relative or image-relative).
    listrefs=[];text=text_section(pe)
    for i in code:
        for o in i.operands:
            if o.type!=X86_OP_MEM:continue
            if o.mem.base==X86_REG_RIP:t=i.address+i.size+o.mem.disp;kind='rip'
            elif OLD_LIST<=o.mem.disp<OLD_END:t=o.mem.disp;kind='image'
            else:continue
            if not OLD_LIST<=t<OLD_END:continue
            listrefs.append((i,kind,t))
    if len(listrefs)!=291:raise ValueError(f'Variable list reference count differs: {len(listrefs)}')
    for i,kind,t in listrefs:
        off=t-OLD_LIST
        if off//24>=OLD_CHILD_BASE:off+=(CHILD_BASE-OLD_CHILD_BASE)*24
        elif off//24>=OLD_PARENT_END-16:raise ValueError(f'List reference near the parent end at {i.address:x}')
        new=LIST+off
        if kind=='rip':fx=[(i.disp_offset,4,new,i.address+i.size,True)]
        else:fx=[(i.disp_offset,4,new,0,False)]
        put(i.address,i.bytes,'relocated native variable list reference',fx)
    # 2. Relay entries.
    for site,n,before,target in ENTRIES_SITES:
        if orig(site,n).hex()!=before:raise ValueError(f'Script VM relay site differs at {site:x}')
        put(site,b'\xe9\0\0\0\0'+b'\x90'*(n-5),'complete native script VM relay entry',[(1,4,target,site+5,True)])
    # 3. Counter displacement moves (and 0x21e8f8 rip-relative child0 count).
    for site in COUNTER_SITES:
        i=next(x for x in code if x.address==site)
        m=[o for o in i.operands if o.type==X86_OP_MEM][0]
        if i.mnemonic not in('inc','dec'):raise ValueError(f'Counter site differs at {site:x}')
        if m.mem.base==X86_REG_RIP:
            if i.address+i.size+m.mem.disp!=OLD_COUNTS:raise ValueError('Child0 counter site differs')
            fx=[(i.disp_offset,4,COUNTS,i.address+i.size,True)]
        else:
            if m.mem.disp!=OLD_COUNTS:raise ValueError(f'Counter displacement differs at {site:x}')
            fx=[(i.disp_offset,4,COUNTS,0,False)]
        put(site,i.bytes,'per-pool native child counter',fx)
    a,before,after=CMOV_SITE
    if orig(a,3).hex()!=before:raise ValueError('Counter select differs')
    put(a,bytes.fromhex(after),'pool counter offset from relay')
    for a,before in ERROR_SITES:
        if orig(a,len(before)//2).hex()!=before:raise ValueError(f'Pool error dispatch differs at {a:x}')
        put(a,b'\x90'*(len(before)//2),'report exhaustion of every child pool')
    # 4. Selectors and parent free-list heads: and reg,1 -> and reg,7.
    selectors,imuls=mask_sites(pe,code)
    for a in selectors+list(PARENT_HEADS):
        i=next(x for x in code if x.address==a)
        put(a,i.bytes[:-1]+bytes((POOLS-1,)),'parent->pool / parent free-list mask')
    # 4b. Child base / parent bound constants.
    relay_spans=[(a,a+n) for a,n,_,_ in ENTRIES_SITES]
    def in_relay(a):return any(b<=a<e for b,e in relay_spans)
    base_sites=[]
    for i in code:
        if in_relay(i.address) or not(0x190000<=i.address<0x230000):continue
        for o in i.operands:
            if o.type==X86_OP_IMM and o.imm==OLD_CHILD_BASE and i.imm_size==4:base_sites.append((i,i.imm_offset,CHILD_BASE))
            if o.type==X86_OP_MEM and o.mem.base!=X86_REG_RIP and o.mem.disp in (OLD_CHILD_BASE,):base_sites.append((i,i.disp_offset,CHILD_BASE))
            if o.type==X86_OP_MEM and o.mem.base!=X86_REG_RIP and o.mem.disp in (OLD_CHILD_BASE*24,OLD_CHILD_BASE*24+2):
                base_sites.append((i,i.disp_offset,o.mem.disp+(CHILD_BASE-OLD_CHILD_BASE)*24))
    # Stock sites outside replaced relays: 197 child-base adds/leas/movs,
    # two folded tempVariable offsets.
    if len(base_sites)!=BASE_SITE_COUNT:raise ValueError(f'Child base inventory differs: {len(base_sites)}')
    for i,off,value in base_sites:
        after=bytearray(i.bytes);after[off:off+4]=struct.pack('<I',value)
        put(i.address,after,'child pools above the 16-bit parent id space')
    for at,before,value in ((0x21b6eb,OLD_CHILD_BASE-1,CHILD_BASE-1),(0x21d915,OLD_CHILD_BASE-1,CHILD_BASE-1),(0x2210fc,OLD_PARENT_END-1,PARENT_END-1)):
        i=next(x for x in code if x.address==at)
        if i.operands[1].imm!=before or i.imm_size!=4:raise ValueError(f'Parent bound differs at {at:x}')
        after=bytearray(i.bytes);after[i.imm_offset:i.imm_offset+4]=struct.pack('<I',value)
        put(at,after,'parent id bound')
    # 4c. Object-key name range and the int array-index range next to it.
    # Every operand in .text equal to the stock bounds must be one of these
    # sites; anything else fails the build.
    bounds={OLD_OBJECT_NAME_END:'object name end',0x800000-OLD_OBJECT_NAME_END:'array index bias',
            0x1000000-OLD_OBJECT_NAME_END-1:'array index span'}
    found=sorted((i.address,bounds[o.imm if o.type==X86_OP_IMM else o.mem.disp]) for i in code for o in i.operands
                 if (o.type==X86_OP_IMM and o.imm in bounds and not i.mnemonic.startswith(('call','j','loop'))) or (o.type==X86_OP_MEM and o.mem.base!=X86_REG_RIP and o.mem.disp in bounds))
    want=sorted([(a,'object name end') for a in OBJECT_NAME_SITES]+[(a,'array index bias') for a,_ in ARRAY_INDEX_SITES]+
                [(b,'array index span') for _,b in ARRAY_INDEX_SITES])
    if found!=want:raise ValueError(f'Object-key name range inventory differs: {[(hex(a),k) for a,k in found]}')
    for at in OBJECT_NAME_SITES:
        i=next(x for x in code if x.address==at)
        if i.mnemonic!='cmp' or i.operands[1].imm!=OLD_OBJECT_NAME_END or i.imm_size!=4:raise ValueError(f'Object name bound differs at {at:x}')
        after=bytearray(i.bytes);after[i.imm_offset:i.imm_offset+4]=struct.pack('<I',OBJECT_NAME_END)
        put(at,after,'object-key names cover every parent id')
    for bias,span in ARRAY_INDEX_SITES:
        i=next(x for x in code if x.address==bias);j=next(x for x in code if x.address==span)
        if (i.mnemonic!='lea' or i.disp_size!=4 or i.operands[1].mem.disp!=0x800000-OLD_OBJECT_NAME_END or
            j.mnemonic!='cmp' or j.imm_size!=4 or j.operands[1].imm!=0x1000000-OLD_OBJECT_NAME_END-1 or i.address+i.size!=j.address):
            raise ValueError(f'Array index check differs at {bias:x}')
        a=bytearray(i.bytes);a[i.disp_offset:i.disp_offset+4]=struct.pack('<I',0x800000-OBJECT_NAME_END)
        b=bytearray(j.bytes);b[j.imm_offset:j.imm_offset+4]=struct.pack('<I',0x1000000-OBJECT_NAME_END-1)
        put(bias,a,'int array keys stay clear of object-key names');put(span,b,'int array keys stay clear of object-key names')
    at,before,after=SAVE_ID_READ
    if orig(at,len(before)//2).hex()!=before:raise ValueError(f'Saved object number read differs at {at:x}')
    put(at,bytes.fromhex(after),'saved object numbers are unsigned 16-bit')
    mapref=[]
    for i in code:
        for o in i.operands:
            if o.type!=X86_OP_MEM:continue
            t=i.address+i.size+o.mem.disp if o.mem.base==X86_REG_RIP else o.mem.disp
            b=OLD_MAPS[1]
            if b<=t<b+OLD_MAP_BYTES:mapref.append((i,o.mem.base==X86_REG_RIP,MAPS[1]+(t-b)))
    if len(mapref)!=MAP_REFERENCE_COUNT:raise ValueError(f'Save map reference count differs: {len(mapref)}')
    for i,rip,new in mapref:
        put(i.address,i.bytes,'relocated per-parent savegame map',[(i.disp_offset,4,new,i.address+i.size,True)] if rip else [(i.disp_offset,4,new,0,False)])
    # scrVarPub-relative map B accesses: a register holds &scrVarPub
    # (0x15a0380), sometimes passed in from a caller, and map B is reached at
    # +0x12068. Map A (+0x68, often a short displacement) stays in place.
    rel=OLD_MAPS[1]-0x15a0380
    struct_refs=[(i,o) for i in code for o in i.operands
                 if 0x210000<=i.address<0x230000 and o.type==X86_OP_MEM and o.mem.base!=X86_REG_RIP and rel<=o.mem.disp<rel+OLD_MAP_BYTES]
    for i,b,t in based_accesses(code):
        z=zone(t)
        if z=='children' and i.address not in (0x21e8db,0x21e90c):raise ValueError(f'Unhandled based child access at {i.address:x}')
        if z not in('children','mapA','mapB'):raise ValueError(f'Unhandled based VM access at {i.address:x} {b:x}+{t-b:x}')
        if z=='mapB' and not any(x.address==i.address for x,_ in struct_refs):raise ValueError(f'Map B access outside inventory at {i.address:x}')
    if len(struct_refs)!=STRUCT_MAP_B_SITES:raise ValueError(f'scrVarPub-relative map B inventory differs: {len(struct_refs)}')
    for i,o in struct_refs:
        if int.from_bytes(i.bytes[i.disp_offset:i.disp_offset+4],'little')!=o.mem.disp:raise ValueError(f'Short displacement at {i.address:x}')
        put(i.address,i.bytes,'map B via scrVarPub',[(i.disp_offset,4,MAPS[1]+(o.mem.disp-rel),0x15a0380,False)])
    for at in MAP_CLEARS:
        i=next(x for x in code if x.address==at)
        if i.mnemonic!='mov' or i.operands[1].imm!=OLD_MAP_BYTES:raise ValueError(f'Save map clear differs at {at:x}')
        after=bytearray(i.bytes);after[i.imm_offset:i.imm_offset+4]=struct.pack('<I',MAP_BYTES)
        put(at,after,'per-parent savegame map size')
    # 5. Checkpoint slot buffers.
    for n,at in enumerate(SAVE_LEAS):
        i=next(x for x in code if x.address==at)
        m=i.operands[1]
        if i.mnemonic!='lea' or i.bytes[:3]!=b'\x48\x8d\x05' or i.address+i.size+m.mem.disp!=SAVE_OLD[n&1]:raise ValueError(f'Save buffer site differs at {at:x}')
        put(at,b'\x48\x8b\x05'+bytes(4),'checkpoint slot buffer from owned cell',[(3,4,SAVE_CELLS+8*(n&1),at+7,True)])
    saverefs=[i.address for i in code for o in i.operands if o.type==X86_OP_MEM and
              ((o.mem.base==X86_REG_RIP and any(b<=i.address+i.size+o.mem.disp<b+SAVE_STOCK for b in SAVE_OLD)) or
               (o.mem.base!=X86_REG_RIP and any(b<=o.mem.disp<b+SAVE_STOCK for b in SAVE_OLD)))]
    if sorted(saverefs)!=sorted(SAVE_LEAS):raise ValueError(f'Save buffer references differ: {[hex(a) for a in saverefs]}')
    for at in SAVE_SIZES:
        i=next(x for x in code if x.address==at)
        imm=[o for o in i.operands if o.type==X86_OP_IMM]
        if len(imm)!=1 or imm[0].imm!=SAVE_STOCK or i.imm_size!=4:raise ValueError(f'Save size site differs at {at:x}')
        after=bytearray(i.bytes);after[i.imm_offset:i.imm_offset+4]=struct.pack('<I',SAVE_BYTES)
        put(at,after,'checkpoint slot capacity')
    # 6. Dobj transfer records.
    dobjrefs=[]
    for i in code:
        for o in i.operands:
            if o.type!=X86_OP_MEM:continue
            t=i.address+i.size+o.mem.disp if o.mem.base==X86_REG_RIP else o.mem.disp
            if DOBJ_OLD<=t<DOBJ_OLD+DOBJ_STOCK*DOBJ_STRIDE:dobjrefs.append((i,o.mem.base==X86_REG_RIP,t))
    if len(dobjrefs)!=7:raise ValueError(f'Dobj transfer reference count differs: {len(dobjrefs)}')
    for i,rip,t in dobjrefs:
        new=DOBJ+(t-DOBJ_OLD)
        put(i.address,i.bytes,'relocated dobj transfer records',[(i.disp_offset,4,new,i.address+i.size,True)] if rip else [(i.disp_offset,4,new,0,False)])
    i=next(x for x in code if x.address==0x29a151)
    if i.mnemonic!='cmp' or i.operands[1].imm!=DOBJ_STOCK or i.imm_size!=4:raise ValueError('Dobj transfer bound differs')
    after=bytearray(i.bytes);after[i.imm_offset:i.imm_offset+4]=struct.pack('<I',DOBJ_RECORDS)
    put(i.address,after,'dobj transfer capacity')
    # 6b. Entity handle info records.
    enthrefs=[]
    for i in code:
        for o in i.operands:
            if o.type!=X86_OP_MEM or o.mem.base!=X86_REG_RIP:continue
            t=i.address+i.size+o.mem.disp
            if ENTH_OLD<=t<ENTH_OLD+ENTH_STOCK*ENTH_STRIDE:enthrefs.append((i,t))
    want={0x15aef1:ENTH_OLD,0x15afad:ENTH_OLD,0x15b09a:ENTH_OLD,0x15b13a:ENTH_OLD,0x15b005:ENTH_OLD+0x18,0x15b031:ENTH_OLD+(ENTH_STOCK-1)*ENTH_STRIDE+8}
    if {i.address:t for i,t in enthrefs}!=want:raise ValueError(f'Entity handle reference inventory differs: {[(hex(i.address),hex(t)) for i,t in enthrefs]}')
    for i,t in enthrefs:
        new=ENTH+(ENTH_RECORDS-1)*ENTH_STRIDE+8 if i.address==0x15b031 else ENTH+(t-ENTH_OLD)
        put(i.address,i.bytes,'relocated entity handle records',[(i.disp_offset,4,new,i.address+i.size,True)])
    i=next(x for x in code if x.address==0x15b01c)
    if i.mnemonic!='cmp' or i.operands[1].imm!=ENTH_STOCK or i.imm_size!=4:raise ValueError('Entity handle init bound differs')
    after=bytearray(i.bytes);after[i.imm_offset:i.imm_offset+4]=struct.pack('<I',ENTH_RECORDS)
    put(i.address,after,'entity handle capacity')
    # 7. Relay code and unwind data in the owned region.
    blob=build_relays()
    relay_fixups=[]
    cs=Cs(CS_ARCH_X86,CS_MODE_64);cs.detail=True;cs.skipdata=True
    for i in cs.disasm(blob,REGION):
        if not i.id:continue
        for o in i.operands:
            if o.type==X86_OP_MEM and o.mem.base==X86_REG_RIP:
                relay_fixups.append((i.address-REGION+i.disp_offset,4,i.address+i.size+o.mem.disp,i.address+i.size,True))
            elif o.type==X86_OP_IMM and (i.mnemonic=='call' or i.mnemonic.startswith('j')):
                relay_fixups.append((i.address-REGION+i.imm_offset,i.imm_size,o.imm,i.address+i.size,True))
    rows=[]
    for at,(after,fx,role) in sorted(edits.items()):
        rows.append({'rva':at,'before':orig(at,len(after)).hex(),'after':after.hex(),'fixups':fx,'role':role})
    for f in relay_fixups:
        t=f[2]
        if not(t<0x5000000 or REGION<=t<REGION+REGION_BYTES):raise ValueError(f'Relay reference outside image/region: {t:x}')
    # Branches inside the blob are position independent; everything else is rel32.
    external=[f for f in relay_fixups if not REGION<=f[2]<REGION+CODE_BYTES]
    if any(f[1]!=4 or not f[4] for f in external):raise ValueError('Script VM relay leaves the blob without rel32')
    rows.append({'rva':REGION,'before':'','after':blob.hex(),'fixups':external,'role':'script VM relays'})
    unwind=bytearray(0x200)
    for n,(begin,end,site) in enumerate(RELAYS):
        data=INIT_UNWIND if site is None else MT_INIT_UNWIND if site=='mt_init' else native_unwind(pe,site)
        if len(data)>0x28 or (n+1)*0x28>0x800:raise ValueError('Script VM unwind slot exceeded')
        unwind[n*0x28:n*0x28+len(data)]=data
    rows.append({'rva':UNWIND,'before':'','after':bytes(unwind).hex(),'fixups':[],'role':'script VM relay unwind'})
    meta={'child_pools':POOLS,'stock_child_pools':2,'pool_entries':POOL_STRIDE,'usable_children_per_pool':POOL_STRIDE-2,
          'parent_objects':PARENT_END-1-POOLS,'stock_parent_objects':OLD_PARENT_END-1-2,'child_base':hex(CHILD_BASE),'stock_child_base':hex(OLD_CHILD_BASE),
          'variable_list_entries':ENTRIES,'stock_variable_list_entries':OLD_ENTRIES,
          'region_rva':hex(REGION),'region_bytes':REGION_BYTES,'list_references':len(listrefs),
          'selector_masks':len(selectors),'selector_imul_sites':len(imuls),'parent_head_masks':len(PARENT_HEADS),
          'relays':[{'begin':hex(b),'end':hex(e),'native_frame_site':hex(s) if isinstance(s,int) else s} for b,e,s in RELAYS],
          'list_guard_rva':hex(LIST_GUARD),'struct_relative_map_b_sites':STRUCT_MAP_B_SITES,'object_name_end':hex(OBJECT_NAME_END),'save_id_read':hex(SAVE_ID_READ[0]),'object_name_sites':len(OBJECT_NAME_SITES),'array_index_sites':len(ARRAY_INDEX_SITES),'dobj_rva':hex(DOBJ),'dobj_records':DOBJ_RECORDS,'stock_dobj_records':DOBJ_STOCK,'dobj_references':len(dobjrefs),'entity_handle_records':ENTH_RECORDS-1,'stock_entity_handle_records':ENTH_STOCK-1,'entity_handle_rva':hex(ENTH),'entity_handle_references':ENTH_REFERENCES,
          'save_slot_bytes':SAVE_BYTES,'stock_save_slot_bytes':SAVE_STOCK,'save_lea_sites':len(SAVE_LEAS),'save_size_sites':len(SAVE_SIZES),
          'mt_cells_rva':hex(MT_CELLS),'save_cells_rva':hex(SAVE_CELLS)}
    return rows,meta


CAPACITY_HEADER=Path(__file__).resolve().parents[1]/'include'/'expanded_capacity_patches.hpp'


def check_capacity_disjoint(rows,header=CAPACITY_HEADER):
    """Script VM edits must not touch bytes the actor-capacity module rewrites,
    and no capacity relay may target the relocated list or old counters."""
    import re
    text=header.read_text()
    cap=[(int(m.group(1),16),len(m.group(3))//2,m.group(4)) for m in re.finditer(r'\{0x([0-9a-f]+),"([0-9a-f]*)","([0-9a-f]*)",\{(.*?)\}\},\n',text)]
    if len(cap)<400:raise ValueError(f'Capacity header parse failed ({len(cap)} rows)')
    native=[(r['rva'],len(r['after'])//2) for r in rows if r['rva']<REGION]
    for at,n,_ in cap:
        if at>=0x4000000:continue
        for b,m in native:
            if b<at+n and at<b+m:raise ValueError(f'Script VM edit {b:x} overlaps capacity patch {at:x}')
    for at,n,fx in cap:
        for t in re.findall(r'\{\d+,\d+,0x([0-9a-f]+),',fx):
            t=int(t,16)
            if OLD_LIST<=t<OLD_END or OLD_COUNTS<=t<OLD_COUNTS+8:raise ValueError(f'Capacity patch {at:x} targets script VM storage {t:x}')
    return len(cap)


def check_mod_patterns(pe,rows,root=CAPACITY_HEADER.parents[1]):
    """Every runtime signature the DLL scans for must match the same places
    after these DllMain edits, and no fixed native RVA the sources read may
    fall inside an edited byte range."""
    import re
    from pattern_scan import pattern_matches
    image=bytearray(pe.get_memory_mapped_image());patched=bytearray(image);ranges=[]
    for r in rows:
        if r['rva']>=REGION:continue
        a=bytes.fromhex(r['after']);patched[r['rva']:r['rva']+len(a)]=a;ranges.append((r['rva'],r['rva']+len(a)))
    t=text_section(pe);lo,hi=t.VirtualAddress,t.VirtualAddress+t.Misc_VirtualSize
    files=[f for f in sorted((root/'include').glob('*.hpp'))+sorted((root/'src').glob('*.cpp')) if 'patches' not in f.name]
    source=''.join(f.read_text() for f in files)
    patterns=re.findall(r'(k\w*Pattern)\s*=\s*((?:"[^"]*"\s*)+);',source)
    if len(patterns)<90:raise ValueError('Runtime signature inventory differs')
    for name,literal in patterns:
        text=''.join(re.findall(r'"([^"]*)"',literal))
        if [m.start() for m in pattern_matches(bytes(image[lo:hi]),text)]!=[m.start() for m in pattern_matches(bytes(patched[lo:hi]),text)]:
            raise ValueError(f'Runtime signature {name} changed by script VM edits')
    constants=set(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{5,7})\b',source))
    inside=[hex(c) for c in sorted(constants) if any(a<=c<b for a,b in ranges)]
    if inside:raise ValueError(f'Source reads native bytes edited early: {inside}')
    return len(patterns)


def generate(rows,meta,output):
    def fx(f):return '{%d,%d,0x%x,0x%x,%s}'%(f[0],f[1],f[2],f[3],'true' if f[4] else 'false')
    with open(output,'w') as h:
        h.write('#pragma once\n// Generated by tools/prepare_script_vm.py for the exact supported SP SHA256.\n')
        h.write('#include <array>\n#include <cstdint>\n#include <vector>\nnamespace mod::script_vm {\n')
        h.write('inline constexpr std::uint32_t kRegionRva=0x%x,kRegionBytes=0x%x,kCountsRva=0x%x,kListRva=0x%x,kUnwindRva=0x%x;\n'%(REGION,REGION_BYTES,COUNTS,LIST,UNWIND))
        h.write('inline constexpr std::uint32_t kPools=%d,kPoolStride=0x%x,kChildBase=0x%x,kParentEnd=0x%x,kEntries=0x%x;\n'%(POOLS,POOL_STRIDE,CHILD_BASE,PARENT_END,ENTRIES))
        h.write('inline constexpr std::uint32_t kMapRvas[2]={0x%x,0x%x},kMapBytes=0x%x;\n'%(MAPS[0],MAPS[1],MAP_BYTES))
        h.write('inline constexpr std::uint32_t kListGuardRva=0x%x,kDobjRva=0x%x,kDobjGuardRva=0x%x,kDobjRecords=0x%x,kSaveCellsRva=0x%x,kSaveBytes=0x%x,kMtCellsRva=0x%x;\n'%(LIST_GUARD,DOBJ,DOBJ_GUARD,DOBJ_RECORDS,SAVE_CELLS,SAVE_BYTES,MT_CELLS))
        h.write('inline constexpr std::uint32_t kEntityHandleRva=0x%x,kEntityHandleGuardRva=0x%x,kEntityHandleRecords=0x%x,kEntityHandleHeadRva=0x%x;\n'%(ENTH,ENTH_GUARD,ENTH_RECORDS,ENTH_HEAD))
        h.write('struct Fixup {unsigned offset,width;std::uint32_t target,end;bool relative;};\n')
        h.write('struct Patch {std::uint32_t rva;const char* before;const char* after;std::vector<Fixup> fixups;};\n')
        h.write('struct Relay {std::uint32_t begin,end,unwind;};\n')
        h.write('inline constexpr std::array<Relay,%d> kRelays{{\n'%len(RELAYS))
        for n,(b,e,s) in enumerate(RELAYS):h.write('    {0x%x,0x%x,0x%x},\n'%(b,e,UNWIND+n*0x28))
        h.write('}};\ninline const std::vector<Patch> kPatches{\n')
        for r in rows:h.write('    {0x%x,"%s","%s",{%s}},\n'%(r['rva'],r['before'],r['after'],','.join(fx(f) for f in r['fixups'])))
        h.write('};\n} // namespace mod::script_vm\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path)
    p.add_argument('--header',type=Path);p.add_argument('--manifest',type=Path);a=p.parse_args()
    raw=a.executable.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=EXACT_SHA256:raise ValueError('Wrong executable')
    pe=pefile.PE(data=raw,fast_load=True);rows,meta=prepare(pe,raw)
    meta['capacity_rows_checked_disjoint']=check_capacity_disjoint(rows)
    meta['runtime_signatures_unchanged']=check_mod_patterns(pe,rows)
    if a.header:generate(rows,meta,a.header)
    if a.manifest:a.manifest.write_text(json.dumps({'patches':rows,'metadata':meta},indent=1)+'\n')
    print(json.dumps({'patches':len(rows),'fixups':sum(len(r['fixups']) for r in rows),**{k:meta[k] for k in ('child_pools','list_references','selector_masks','variable_list_entries')}}))
