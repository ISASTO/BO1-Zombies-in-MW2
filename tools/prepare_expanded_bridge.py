"""Keep packed 16-bit draw IDs while bridging expanded animated records.

Stock rigid quarter-offsets use 0..32767. Animated records use 32768..65535,
with per-native-frame pointer tables and a sparse reverse map. Consumers keep
the original packed draw format, material bits, flags and scalar/vector loads.
"""
from pathlib import Path
import struct,subprocess,tempfile
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from capstone.x86_const import X86_OP_MEM,X86_REG_RIP,X86_REG_RSP
from prepare_expanded_crowd import native_unwind

CODE_BASE=0x4620000
UNWIND_BASE=0x4630000
SLOT=0x200
# Every direct indexed reader of the native record fields. The special cases
# below cover precomputed bases/pointers as well as the two packed producers.
READERS=(0x5e686,0x5eb46,0x5ece9,0x5ed60,0x5ed6d,0x5ed7d,0x5ed91,
         0x5efb3,0x5efce,0x5efe0,0x5efee,0x5f0e2,0x60614,0x6063a,
         0x64f84,0x65004,0x65115)
# (start, end, native frame register, original ID register, pointer output,
# extra native field output). These add the pool origin in quarter units
# before their final scale-by-four memory operation.
QUARTER_READERS=(
    (0x5dfa2,0x5dfb2,'r14','rax','r12','movzx r15d, WORD PTR [r10+0x12]'),
    (0x5e2da,0x5e2e5,'r14','r12','r12',''),
    (0x5e3a9,0x5e3b4,'r14','rdx','rdx',''),
    (0x5e5a5,0x5e5b0,'r14','rdx','rdx',''),
    (0x643d3,0x643e4,'r8','rsi','rsi','movzx ecx, WORD PTR [r10+0x12]'),
    (0x648cf,0x648da,'r8','rsi','rsi',''),
)

SAVE='''
    movdqu XMMWORD PTR [rsp], xmm0
    mov QWORD PTR [rsp+0x10], r10
    mov QWORD PTR [rsp+0x18], r11
'''
RESTORE='''
    mov r10, QWORD PTR [rsp+0x10]
    mov r11, QWORD PTR [rsp+0x18]
'''

def reg32(reg):
    return {'rax':'eax','rbx':'ebx','rcx':'ecx','rdx':'edx','rsi':'esi','rdi':'edi','rbp':'ebp'} .get(reg,reg+'d')

def select_frame(base):
    # All operations preserve EFLAGS, including SUB/branch-dependent readers.
    # The native frame pointers differ by 0x13d700, so their low DWORDs are
    # distinct under every ASLR base. Only those two native frames are used.
    return f'''
    movd xmm0, {reg32(base)}
    pcmpeqd xmm0, XMMWORD PTR [rip+second_frame_low]
    psrld xmm0, 31
    movd r11d, xmm0
    lea r10, [rip+frame_pointer_slots]
    mov r10, QWORD PTR [r10+r11*8]
'''

def resolve(base,index):
    return SAVE+select_frame(base)+f'''
    mov r10, QWORD PTR [r10+24]
    mov r10, QWORD PTR [r10+{index}*8]
    movdqu xmm0, XMMWORD PTR [rsp]
'''

def hook_specs(pe):
    decoder=Cs(CS_ARCH_X86,CS_MODE_64);decoder.detail=True
    specs=[]
    def add(at,end,body,role):specs.append({'at':at,'end':end,'body':body,'role':role})
    # Both full-pointer-to-packed-ID producers. Preserve every other packed
    # material/light/type bit; only the existing low 16-bit geometry ID changes.
    for at,end,out,extra in ((0x419a,0x41b5,'rcx','or r8, rdx'),(0x438d,0x43a1,'rdx','')):
        body=SAVE+extra+'\n'+'''mov r10, QWORD PTR [rip+front_end]\n'''+select_frame('r10')+f'''
    mov {out}, rdi
    sub {out}, QWORD PTR [r10]
    cmp {out}, 0x200000
    jae stock_{at:x}
    shr {out}, 2
    mov r10, QWORD PTR [r10+16]
    movzx {reg32(out)}, WORD PTR [r10+{out}*2]
    or {reg32(out)}, 0x8000
    jmp encoded_{at:x}
stock_{at:x}:
    mov {out}, rdi
    sub {out}, QWORD PTR [rip+front_end]
    sub {out}, 0x2980
    shr {out}, 2
    movzx {reg32(out)}, {out[1:] if out in ('rcx','rdx') else out}
encoded_{at:x}:
    movdqu xmm0, XMMWORD PTR [rsp]
'''+RESTORE
        add(at,end,body,'packed animated surface ID producer')
    for at in READERS:
        i=next(decoder.disasm(pe.get_data(at,16),at))
        mem=[o.mem for o in i.operands if o.type==X86_OP_MEM]
        if len(mem)!=1 or mem[0].scale!=4 or not 0x42980<=mem[0].disp<=0x429c4:
            raise ValueError(f'Indexed native surface reader differs at {at:x}')
        m=mem[0];base=decoder.reg_name(m.base);index=decoder.reg_name(m.index)
        if base in ('r10','r11') or index in ('r10','r11'):raise ValueError('Reader aliases bridge scratch register')
        left=i.op_str.index('[');right=i.op_str.index(']',left)
        op=i.op_str[:left]+f'[r10+{m.disp-0x42980}]'+i.op_str[right+1:]
        add(at,at+i.size,resolve(base,index)+f'{i.mnemonic} {op}\n'+RESTORE,'native packed surface scalar/vector reader')
    add(0x3c9e,0x3cad,resolve('rsi','rbp')+'mov BYTE PTR [rsp+0x70], al\nmov rsi, r10\n'+RESTORE,'native rigid surface pointer reader')
    add(0x3e77,0x3e88,resolve('r14','rbp')+'mov rcx, rdi\nmovsxd r12, r8d\nmov r14, r10\n'+RESTORE,'native rigid surface pointer reader')
    add(0x64a71,0x64a76,resolve('r13','rcx')+'movzx eax, WORD PTR [r10+0x12]\nmov rdi, r10\n'+RESTORE,'native skinned surface pointer and light index reader')
    add(0x64dd6,0x64de1,resolve('r13','rcx')+'mov rdi, r10\n'+RESTORE,'native next skinned surface pointer reader')
    add(0x65016,0x65027,resolve('r15','r14')+'lea rsi, [r10+0x44]\nmov DWORD PTR [rbp+0xa0], ecx\n'+RESTORE,'native CPU skinned matrix record pointer reader')
    # Resolve before the native ADD, then execute that ADD to retain its exact
    # output flags and any still-live index-register result. Field outputs and
    # full pointer outputs match the corresponding original consumer.
    for at,end,base,index,out,extra in QUARTER_READERS:
        body=resolve(base,index)+f'add {index}, 0x10a60\n'+extra+f'\nmov {out}, r10\n'+RESTORE
        add(at,end,body,'native quarter-unit surface record reader')
    # Publish handle mappings after the native copy, before the worker command
    # or DObj's ready state becomes visible to another thread.
    body='''
    mov rax, QWORD PTR [rip+front_end]
    lea r10, [rip+native_frames]
    lea r11, [rip+frame_states]
    lea r9, [rip+handle_counts]
    cmp rax, r10
    je publish_slot
    add r10, 0x13d700
    add r11, 32
    add r9, 4
    cmp rax, r10
    jne publish_bad_frame
publish_slot:
    mov rax, rdi
    sub rax, QWORD PTR [r11]
    cmp rax, 0x200000
    jae publish_bad_record
    lea r10, [rax+rbx]
    cmp r10, 0x200000
    ja publish_bad_record
    cmp r14d, 0x8000
    ja publish_handle_fail
    mov eax, DWORD PTR [r9]
publish_retry:
    lea edx, [rax+r14]
    cmp edx, eax
    jb publish_handle_fail
    cmp edx, 0x8000
    ja publish_handle_fail
    lock cmpxchg DWORD PTR [r9], edx
    jne publish_retry
    mov DWORD PTR [rsp+0x18], edx
    mov r9d, eax
    mov r8, rdi
    mov ecx, ebx
    test ecx, ecx
    jz publish_end
publish_record:
    cmp r9d, DWORD PTR [rsp+0x18]
    jae publish_bad_record
    cmp ecx, 4
    jb publish_bad_record
    mov edx, DWORD PTR [r8]
    cmp edx, -4
    jle publish_variable
    cmp edx, -3
    je publish_empty
    mov edx, 48
    jmp publish_size
publish_variable:
    mov eax, -3
    sub eax, edx
    cmp eax, 255
    ja publish_bad_record
    shl eax, 5
    lea edx, [rax+44]
    jmp publish_size
publish_empty:
    mov edx, 4
publish_size:
    cmp edx, ecx
    ja publish_bad_record
    mov rax, r8
    sub rax, QWORD PTR [r11]
    cmp rax, 0x200000
    jae publish_bad_record
    shr rax, 2
    mov r10, QWORD PTR [r11+16]
    mov WORD PTR [r10+rax*2], r9w
    mov r10, QWORD PTR [r11+24]
    mov QWORD PTR [r10+r9*8+0x40000], r8
    add r8, rdx
    sub ecx, edx
    inc r9d
    test ecx, ecx
    jnz publish_record
publish_end:
    cmp r9d, DWORD PTR [rsp+0x18]
    jne publish_bad_record
    cmp DWORD PTR [rsp+0x34], 0
    je native_no_worker
    jmp native_worker
publish_bad_frame:
    lock inc DWORD PTR [rip+bad_frames]
publish_bad_record:
    lock inc DWORD PTR [rip+map_failures]
    jmp publish_fail
publish_handle_fail:
    lock inc DWORD PTR [rip+handle_failures]
publish_fail:
    mov QWORD PTR [r13+0x48], 0
    jmp native_surface_fail
'''
    add(0x532e2,0x532e9,body,'bounded animated record handle publication')
    return specs

def prepare(pe,image):
    specs=hook_specs(pe);edits=[];decoder=Cs(CS_ARCH_X86,CS_MODE_64);decoder.detail=True
    def put(at,b,role,data=False):
        image[at:at+len(b)]=b;edits.append({'rva':hex(at),'bytes':len(b),'role':role,'data':data})
    source=['.intel_syntax noprefix','.text']
    for n,row in enumerate(specs):
        source+= [f'.org {n*SLOT},0x90',f'.global bridge_{n}',f'bridge_{n}:',row['body'],f'jmp {row["end"]}']
    source+=[f'.org {len(specs)*SLOT},0x90',
             '.set front_end,0x410fd00','.set native_frames,0x458fb00','.set frame_states,0x47e0040',
             '.set handle_counts,0x47e009c','.set handle_failures,0x47e00a4','.set map_failures,0x47e00a8',
             '.set second_frame_low,0x47e00b0','.set frame_pointer_slots,0x47e00c0','.set bad_frames,0x47e008c',
             '.set native_no_worker,0x5330f','.set native_worker,0x532e9','.set native_surface_fail,0x531bc']
    with tempfile.TemporaryDirectory(prefix='bo1mw2-surface-bridge-') as name:
        root=Path(name);asm=root/'bridge.S';obj=root/'bridge.o';binary=root/'bridge.bin'
        asm.write_text('\n'.join(source)+'\n')
        subprocess.run(['as','--64',str(asm),'-o',str(obj)],check=True,capture_output=True)
        subprocess.run(['ld','-Ttext',hex(CODE_BASE),'--oformat=binary','-o',str(binary),str(obj)],check=True,capture_output=True)
        blob=binary.read_bytes()
    if len(blob)!=len(specs)*SLOT:raise ValueError('Packed surface relay slots differ')
    relays=[]
    for n,row in enumerate(specs):
        at=row['at'];end=row['end'];where=CODE_BASE+n*SLOT;unwind=UNWIND_BASE+n*0x80
        code=list(decoder.disasm(pe.get_data(at,end-at),at))
        if sum(i.size for i in code)!=end-at or end-at<5:raise ValueError(f'Partial packed surface hook at {at:x}')
        put(at,b'\xe9'+struct.pack('<i',where-at-5)+b'\x90'*(end-at-5),row['role'])
        put(where,blob[n*SLOT:(n+1)*SLOT],row['role']+' relay')
        raw=native_unwind(pe,at)
        if len(raw)>0x80:raise ValueError('Packed surface unwind slot exceeded')
        put(unwind,raw,'native packed surface relay unwind',True)
        relays.append((where,where+SLOT,unwind))
    # Its full pointer is now selected by the preceding light-index lookup.
    if pe.get_data(0x64a86,4)!=bytes.fromhex('488d3c8f'):raise ValueError('Following native surface pointer increment differs')
    put(0x64a86,b'\x90'*4,'remove superseded packed surface pointer index addition')
    return edits,{'packed_draw_id_bits':16,'stock_quarter_offset_ids':32768,'animated_record_handles':32768,
                  'native_record_readers':len(READERS)+5+len(QUARTER_READERS),'packed_producers':2,'record_publication_gate':1,
                  'quarter_unit_reader_sites':[hex(row[0]) for row in QUARTER_READERS],
                  'pointer_table_entries_per_frame':65536,'reverse_map_bytes_per_frame':0x100000,
                  'relay_ranges':relays,'source_sites':[hex(r['at']) for r in specs],
                  'native_registers_and_flags':'32-byte outgoing home scratch; R10/R11/XMM0 restored, no RSP changes',
                  'native_surface_representation':'lower rigid quarter-offsets retained; upper animated IDs use per-frame indirection'}

def generate_layout(metadata,path):
    rows=metadata['relay_ranges'];s=['#pragma once','// Generated by the checked packed-surface bridge plan.','namespace mod::expansion {',
        f'inline constexpr std::array<CrowdRelay,{len(rows)}> kCrowdBridgeRelays{{{{']
    s+=['    {0x%x,0x%x,0x%x},'%tuple(row) for row in rows]
    s+=['}};','} // namespace mod::expansion',''];path.write_text('\n'.join(s))
