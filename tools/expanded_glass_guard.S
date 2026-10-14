.intel_syntax noprefix
.text
# Linked at 0x4611000 by prepare_expanded_resources.py. This runs after the
# original allocator prologue; its saved request arguments remain untouched.
.global glass_allocate_guard
glass_allocate_guard:
    mov eax, DWORD PTR [r11+0xc]
    test eax, eax
    jz allocate_fail
    cmp eax, 0xffff
    ja allocate_fail
    add eax, 31
    shr eax, 5
    cmp eax, DWORD PTR [r11+0x10]
    jne allocate_fail
    mov r8, QWORD PTR [r11+0x58]
    mov r9, QWORD PTR [r11+0x38]
    test r8, r8
    jz allocate_fail
    test r9, r9
    jz allocate_fail
    cmp ebx, 0xffff
    je allocate_fail
    cmp ebx, DWORD PTR [r11+0xc]
    jae rebuild
    mov edx, ebx
    shr edx, 5
    mov eax, DWORD PTR [r8+rdx*4]
    mov ecx, ebx
    not ecx
    and ecx, 31
    bt eax, ecx
    jc rebuild
    mov edx, ebx
    shl rdx, 5
    mov edx, DWORD PTR [r9+rdx]
    cmp edx, 0xffff
    je allocate_resume
    cmp edx, DWORD PTR [r11+0xc]
    jae rebuild
    cmp edx, ebx
    je rebuild
    mov ecx, edx
    shr ecx, 5
    mov eax, DWORD PTR [r8+rcx*4]
    mov ecx, edx
    not ecx
    and ecx, 31
    bt eax, ecx
    jnc allocate_resume
rebuild:
    # Only inactive piece-place records can hold free links. An active place
    # begins with a quaternion, exactly the float bits in the returned dumps.
    # The native occupancy map is authoritative; never overwrite active glass.
    mov edx, DWORD PTR [r11+0xc]
    mov ebx, 0xffff
    xor r10d, r10d
rebuild_loop:
    dec edx
    mov ecx, edx
    shr ecx, 5
    mov eax, DWORD PTR [r8+rcx*4]
    mov ecx, edx
    not ecx
    and ecx, 31
    bt eax, ecx
    jc rebuild_active
    mov ecx, edx
    shl rcx, 5
    mov DWORD PTR [r9+rcx], ebx
    mov ebx, edx
    jmp rebuild_next
rebuild_active:
    inc r10d
rebuild_next:
    test edx, edx
    jnz rebuild_loop
    mov DWORD PTR [r11+0x1c], r10d
    mov DWORD PTR [r11+0x20], ebx
    inc DWORD PTR [rip+glass_rebuilds]
    cmp ebx, 0xffff
    je allocate_fail
allocate_resume:
    mov rcx, r11
    jmp allocator_continue
allocate_fail:
    mov rcx, r11
    jmp allocator_exhausted

.org 0x300, 0x90
# Standalone unwind for already-executed RBX/RBP/RSI/RDI saves and R14/R15
# pushes. Code offsets zero mean all apply throughout this body-only relay.
.byte 1,0,10,0, 0,0x74,6,0, 0,0x64,5,0, 0,0x54,4,0, 0,0x34,3,0, 0,0xf0,0,0xe0

.org 0x400, 0x90
.global glass_free_guard
glass_free_guard:
    mov r11, rcx
    cmp edx, DWORD PTR [r11+0xc]
    jae free_ignore
    mov eax, DWORD PTR [r11+0xc]
    test eax, eax
    jz free_ignore
    cmp eax, 0xffff
    ja free_ignore
    add eax, 31
    shr eax, 5
    cmp eax, DWORD PTR [r11+0x10]
    jne free_ignore
    mov r8, QWORD PTR [r11+0x58]
    test r8, r8
    jz free_ignore
    mov r9d, edx
    shr r9d, 5
    mov eax, DWORD PTR [r8+r9*4]
    mov ecx, edx
    not ecx
    and ecx, 31
    bt eax, ecx
    jnc free_ignore
    mov rcx, r11
    mov eax, DWORD PTR [rcx+0x20]
    mov rbx, rcx
    jmp free_continue
free_ignore:
    inc DWORD PTR [rip+glass_ignored_frees]
    mov rcx, r11
    jmp free_epilogue

.org 0x500, 0x90
.byte 1,0,6,0, 0,0x64,7,0, 0,0x34,6,0, 0,0x32,0,0x70

.set allocator_continue, 0x100445
.set allocator_exhausted, 0x1004c5
.set free_continue, 0x100ee5
.set free_epilogue, 0x100f69
.set glass_rebuilds, 0x47e0010
.set glass_ignored_frees, 0x47e0014
