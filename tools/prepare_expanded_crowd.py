"""Coordinate native render storage and guard the returned non-finite query.

Skinned storage and worker commands use full native pointers. The separate
surface bridge preserves packed draw IDs used by later builders and readers;
rigid packed quarter-offsets retain their original pool and bound.
"""
from pathlib import Path
import struct,subprocess,tempfile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64

BASE=0x4612000
SKIN_BYTES=0x4800000
SURFACE_BYTES=0x200000
RELAY_SITES=(0x530a7,0x5318c,0x531ea,0xcc18,0x1dfa99,0x1dcf5f,0x1ecfeb)
UNWINDS=(0x4613a00,0x4613a80,0x4613a40,0x4613ac0,0x4613b00,0x4613b40,0x4613b80)
RELAY_RANGES=((0x4612000,0x4612100,0x4613a00),(0x4612100,0x4612200,0x4613a40),
              (0x4612200,0x4612400,0x4613a80),(0x4612400,0x4612600,0x4613ac0),
              (0x4613000,0x4613400,0x4613b00),(0x4613400,0x4613600,0x4613b40),
              (0x4613800,0x4613a00,0x4613b80))

def native_unwind(pe,pc):
    pe.parse_data_directories(directories=[3])
    entry=next(x.struct for x in pe.DIRECTORY_ENTRY_EXCEPTION if x.struct.BeginAddress<=pc<x.struct.EndAddress)
    codes=bytearray();frame=0
    while True:
        raw=pe.get_data(entry.UnwindData,256)
        if raw[0]&7!=1 or raw[0]>>3 not in (0,4):raise ValueError('Unsupported native relay unwind')
        if frame and raw[3] and frame!=raw[3]:raise ValueError('Chained native frame differs')
        frame=frame or raw[3];n=raw[2];slot=0
        while slot<n:
            off,encoded=raw[4+slot*2:6+slot*2];op=encoded&15;info=encoded>>4
            slots=2 if op in (4,8) or (op==1 and info==0) else 3 if op in (5,9) or (op==1 and info==1) else 1
            if op not in (0,1,2,3,4,5,8,9) or pc-entry.BeginAddress<off:
                raise ValueError('Relay precedes a required native save or uses unsupported unwind')
            codes+=bytes((0,encoded))+raw[6+slot*2:4+(slot+slots)*2];slot+=slots
        if not raw[0]&0x20:break
        a,z,u=struct.unpack_from('<III',raw,4+((n+1)&~1)*2)
        class Chain:pass
        entry=Chain();entry.BeginAddress=a;entry.EndAddress=z;entry.UnwindData=u
    if len(codes)//2>255:raise ValueError('Too many native unwind codes')
    result=bytes((1,0,len(codes)//2,frame))+codes
    return result+bytes((-len(result))%4)

def prepare(pe,image):
    edits=[];dec=Cs(CS_ARCH_X86,CS_MODE_64);dec.detail=True
    def put(at,data,role,is_data=False):
        image[at:at+len(data)]=data
        edits.append({'rva':hex(at),'bytes':len(data),'role':role,'data':is_data})
    for at in (0x24fda,0x24ff6,0x25028,0x25057):
        i=next(dec.disasm(pe.get_data(at,16),at))
        if i.mnemonic!='mov' or i.imm_size!=4 or i.operands[-1].imm!=0x480000:
            raise ValueError(f'Native skin-cache allocation differs at {at:x}')
        b=bytearray(i.bytes);struct.pack_into('<I',b,i.imm_offset,SKIN_BYTES)
        put(at,b,'matching native GPU capacity/CreateVertexBuffer/error and CPU virtual reservation')
    with tempfile.TemporaryDirectory(prefix='bo1mw2-crowd-') as name:
        root=Path(name);obj=root/'crowd.o';binary=root/'crowd.bin'
        subprocess.run(['as','--64',str(Path(__file__).with_name('expanded_crowd_guard.S')),'-o',str(obj)],check=True,capture_output=True)
        subprocess.run(['ld','-Ttext',hex(BASE),'--oformat=binary','-o',str(binary),str(obj)],check=True,capture_output=True)
        data=binary.read_bytes()
    if len(data)!=0x1a00:raise ValueError('Crowd relay layout differs')
    for a,z in ((0,0x100),(0x100,0x200),(0x200,0x400),(0x400,0x600),(0x600,0x700),
                (0x1000,0x1400),(0x1400,0x1600),(0x1800,0x1a00)):
        # The exponent mask is immutable data inside the glass relay; it has
        # its own descriptor so the patch planner never decodes it as code.
        if a==0x1000:
            mask=bytes.fromhex('0000807f'*4);where=data.index(mask,a,z)
            put(BASE+a,data[a:where],'finite native glass collision query guard')
            put(BASE+where,data[where:where+16],'glass query exponent mask',True)
            if where+16<z:put(BASE+where+16,data[where+16:z],'glass query guard padding')
        else:put(BASE+a,data[a:z],'bounded native crowd resource relay')
    for site,unwind in zip(RELAY_SITES,UNWINDS):
        raw=native_unwind(pe,site)
        if len(raw)>0x40:raise ValueError('Crowd unwind slot exceeded')
        put(unwind,raw,'standalone native crowd relay unwind',True)
    entries=((0x530a7,0x530cc,0x4612000),(0x531ea,0x53208,0x4612100),
             (0x5318c,0x531b2,0x4612200),(0xcc18,0xcc1e,0x4612400),
             (0x2acf0,0x2acf9,0x4612600),(0x1dfa99,0x1dfaa4,0x4613000),
             (0x1dcf5f,0x1dcf66,0x4613400),(0x1ecfeb,0x1ecffa,0x4613800))
    for at,end,target in entries:
        code=list(dec.disasm(pe.get_data(at,end-at),at))
        if sum(i.size for i in code)!=end-at:raise ValueError(f'Partial crowd hook instruction at {at:x}')
        put(at,b'\xe9'+struct.pack('<i',target-at-5)+b'\x90'*(end-at-5),'complete native crowd hook entry')
    return edits,{'skin_cache_bytes':SKIN_BYTES,'skin_cache_vertices':SKIN_BYTES//32,
                  'native_skin_allocation_sites':4,'bounded_atomic_skin_reservations':2,
                  'skinned_scene_surface_bytes_per_frame':SURFACE_BYTES,'frame_slots':2,
                  'rigid_scene_surface_bytes':0x20000,'rigid_packed_offsets':'unchanged',
                  'warning_counters':64,'standalone_unwind_entries':7,
                  'early_glass_pose':'reject non-finite native glass origin/rotation/bounds before map traversal',
                  'glass_query':'reject non-finite center/extents/axes before triangle work; preserve first invalid descriptor',
                  'collision_feature':'reject zero/out-of-range feature before unsigned vertex-index arithmetic',
                  'causal_limit':'The returned dump contains invalid glass geometry before narrowphase. Its first corrupting physics update is not captured.'}
