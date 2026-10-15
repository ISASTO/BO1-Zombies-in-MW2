"""Bound native glass fragment construction before publication or stack damage."""
from pathlib import Path
import struct, subprocess, tempfile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_IMM
from prepare_expanded_crowd import native_unwind

BASE=0x4615000
RANGES=((BASE,BASE+0x200,BASE+0x700,0x10a8d7),
        (BASE+0x200,BASE+0x400,BASE+0x740,0x109e9c),
        (BASE+0x400,BASE+0x600,BASE+0x780,0x10aa59),
        (BASE+0x600,BASE+0x700,BASE+0x7c0,0x1095bc))
SITES=((0x10a8d7,0x10a8e5,BASE), (0x10a960,0x10a968,BASE+0x100),
       (0x109e9c,0x109ea4,BASE+0x200), (0x109f0b,0x109f12,BASE+0x300),
       (0x10aa59,0x10aa67,BASE+0x400), (0x1095bc,0x1095c4,BASE+0x600))

def prepare(pe,image):
    decoder=Cs(CS_ARCH_X86,CS_MODE_64);decoder.detail=True
    # The sole producer is deliberately gated before the native allocator.
    decoder.skipdata=True;callers=[]
    for section in pe.sections:
        if section.Characteristics&0x20000000:
            for i in decoder.disasm(section.get_data(),section.VirtualAddress):
                if i.id and i.mnemonic=='call' and i.operands[0].type==X86_OP_IMM and i.operands[0].imm==0x10a8b0:callers.append(i.address)
    if callers!=[0x1095b7]:raise ValueError('Additional glass-fragment publisher requires review')
    decoder.skipdata=False;edits=[]
    def put(at,blob,role,data=False):
        image[at:at+len(blob)]=blob
        edits.append({'rva':hex(at),'bytes':len(blob),'role':role,'data':data})
    with tempfile.TemporaryDirectory(prefix='bo1mw2-fracture-') as directory:
        root=Path(directory);obj=root/'guard.o';binary=root/'guard.bin'
        subprocess.run(['as','--64',str(Path(__file__).with_name('expanded_fracture_guard.S')),'-o',str(obj)],check=True,capture_output=True)
        subprocess.run(['ld','-Ttext',hex(BASE),'--oformat=binary','-o',str(binary),str(obj)],check=True,capture_output=True)
        blob=binary.read_bytes()
    if len(blob)!=0x700:raise ValueError('Glass-fragment relay layout differs')
    for begin,end,unwind,site in RANGES:
        put(begin,blob[begin-BASE:end-BASE],'bounded native glass fragment construction')
        raw=native_unwind(pe,site)
        if len(raw)>0x40:raise ValueError('Glass-fragment unwind slot exceeded')
        put(unwind,raw,'standalone native glass-fragment unwind',True)
    for begin,end,target in SITES:
        code=list(decoder.disasm(pe.get_data(begin,end-begin),begin))
        if sum(i.size for i in code)!=end-begin:raise ValueError('Partial glass-fragment hook instruction')
        put(begin,b'\xe9'+struct.pack('<i',target-begin-5)+b'\x90'*(end-begin-5),'complete native glass-fragment guard entry')
    return edits,{'piece_geometry_dwords':231,'piece_geometry_bytes':924,
                  'maximum_piece_vertices':128,'maximum_work_vertices':255,'maximum_boundary_edges':512,
                  'minimum_fragment_vertices':3,'bounded_polygon_write_sites':2,
                  'publication_gate':'reject sentinel count before native piece allocation',
                  'standalone_unwind_entries':4,'native_producer_callers':[hex(a) for a in callers],
                  'scope':'Native glass triangulation and publication only; actor and renderer capacities unchanged.',
                  'causal_limit':'The dump proves repeated degenerate output overwrote the stack. The earlier geometry update that created the malformed topology is not captured.'}
