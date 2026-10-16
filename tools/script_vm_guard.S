.intel_syntax noprefix
.text
# Linked at 0x6000000 (script VM region) by prepare_script_vm.py.
#
# Native layout: one 24-byte variable list. Entries 1..0x9000 are parent
# objects in interleaved free lists (stock: 2 lists, heads at entries 1,2).
# Child pool k starts at 0x9002 + k*0xc801 (head + 0xc7ff hashed slots +
# one spare entry); children of parent p live in pool (p & (lists-1)).
# Relative ids are 16-bit, so each pool is capped; the 64-bit port split
# children into two pools for that reason. Test34 uses eight pools/lists,
# keeping pools 0 and 1 at their stock absolute positions.

.set POOLS, 8
.set POOL_STRIDE, 0xc801
# CHILD_BASE, PARENT_END and variable_list come from the generator (--defsym).

# ---------------------------------------------------------------- init
.global script_vm_init
script_vm_init:                 # replaces 0x221600 (InitVariables)
    push rbx
    sub rsp, 0x20
    lea rax, [rip+child_counts]
    xor ecx, ecx
    mov QWORD PTR [rax], rcx
    mov QWORD PTR [rax+8], rcx
    mov QWORD PTR [rax+16], rcx
    mov QWORD PTR [rax+24], rcx
    xor ebx, ebx
init_parent:
    mov ecx, 1
    mov edx, PARENT_END
    mov r8d, ebx
    mov r9d, POOLS
    call init_range
    inc ebx
    cmp ebx, POOLS
    jb init_parent
    xor ebx, ebx
init_child:
    imul ecx, ebx, POOL_STRIDE
    add ecx, CHILD_BASE
    lea edx, [rcx+0xc800]
    xor r8d, r8d
    mov r9d, 1
    call init_range
    inc ebx
    cmp ebx, POOLS
    jb init_child
    add rsp, 0x20
    pop rbx
    ret

# ------------------------------------------------- object list selection
.org 0x100, 0xcc
.global script_vm_alloc_object
script_vm_alloc_object:         # body relay at 0x21b7fa in AllocObject
    lea rbp, [rip+variable_list]
    xor r14d, r14d
    mov r8d, -1
    mov r9d, -1
    lea r10, [rip+child_counts]
    xor ecx, ecx
alloc_scan:
    lea eax, [rcx+1]
    lea rax, [rax+rax*2]
    movzx edx, WORD PTR [rbp+rax*8+8]
    cmp edx, ecx
    je alloc_next               # list k is empty when its head links to itself
    mov edx, DWORD PTR [r10+rcx*4]
    cmp edx, r9d
    jae alloc_next
    mov r9d, edx
    mov r8d, ecx
alloc_next:
    inc ecx
    cmp ecx, POOLS
    jb alloc_scan
    cmp r8d, -1
    je alloc_empty
    mov ebx, r8d
    lea eax, [rbx+1]
    lea rax, [rax+rax*2]
    lea rsi, [rbp+rax*8+8]
    movzx edi, WORD PTR [rsi]
    jmp alloc_continue
alloc_empty:
    xor ebx, ebx
    lea rsi, [rbp+0x20]
    movzx edi, WORD PTR [rsi]
    jmp alloc_error

# ------------------------------------------- per-pool child counters
.org 0x200, 0xcc
.global script_vm_count_alloc
script_vm_count_alloc:          # 0x21b79b in AllocVariable: pool in ebx
    mov ecx, ebx
    sub ecx, CHILD_BASE
    xor edx, edx
count_alloc_loop:
    cmp ecx, POOL_STRIDE
    jb count_alloc_done
    sub ecx, POOL_STRIDE
    add edx, 4
    jmp count_alloc_loop
count_alloc_done:
    mov ecx, edx
    mov rbx, QWORD PTR [rsp+0x30]
    jmp count_alloc_continue

.org 0x280, 0xcc
.global script_vm_count_free
script_vm_count_free:           # 0x21c66c in FreeChildValue: pool in ebx
    lea eax, [rbx-CHILD_BASE]
    xor ecx, ecx
count_free_loop:
    cmp eax, POOL_STRIDE
    jb count_free_done
    sub eax, POOL_STRIDE
    add ecx, 4
    jmp count_free_loop
count_free_done:
    mov eax, ecx
    mov ecx, 4
    jmp count_free_continue

.org 0x300, 0xcc
.global script_vm_count_remove
script_vm_count_remove:         # 0x21c811 in RemoveVariable: pool in edi
    lea eax, [rdi-CHILD_BASE]
    xor ecx, ecx
count_remove_loop:
    cmp eax, POOL_STRIDE
    jb count_remove_done
    sub eax, POOL_STRIDE
    add ecx, 4
    jmp count_remove_loop
count_remove_done:
    mov eax, ecx
    mov ecx, 4
    jmp count_remove_continue

.org 0x380, 0xcc
.global script_vm_count_new
script_vm_count_new:            # 0x21d0fa in GetNewVariable: pool in ebx
    lea eax, [rbx-CHILD_BASE]
    xor r13d, r13d
count_new_loop:
    cmp eax, POOL_STRIDE
    jb count_new_done
    sub eax, POOL_STRIDE
    add r13d, 4
    jmp count_new_loop
count_new_done:
    mov eax, 4
    mov rbx, QWORD PTR [rsp+0x78]
    jmp count_new_continue

# ------------------------------------------- script memory tree (MT)
# The native MT is a 1 MiB bitmap allocator (65,536 16-byte nodes). Script
# string ids are MT node indices and must stay below 0x10000, so the tree
# cannot grow. Pointer-only allocations (thread stacks, arrays, savegame
# buffers) never use indices; they move to an owned, larger heap, leaving
# the whole native tree to strings. MT_Free routes by address.
.org 0x400, 0xcc
.global script_mt_alloc
script_mt_alloc:                # entry of MT_Alloc 0x216ee0 (size, type)
    jmp QWORD PTR [rip+mt_alloc_cell]

.org 0x440, 0xcc
.global script_mt_free
script_mt_free:                 # entry of MT_Free 0x217500 (pointer, size)
    mov rax, rcx
    sub rax, QWORD PTR [rip+mt_heap_base]
    cmp rax, QWORD PTR [rip+mt_heap_bytes]
    jb mt_free_owned
    mov QWORD PTR [rsp+8], rbx
    jmp mt_free_continue
mt_free_owned:
    jmp QWORD PTR [rip+mt_free_cell]

.org 0x480, 0xcc
.global script_mt_init
script_mt_init:                 # entry of MT_Init 0x217780
    sub rsp, 0x28
    call QWORD PTR [rip+mt_reset_cell]
    lea rax, [rip+mt_native_base]
    jmp mt_init_continue

.org 0x500, 0xcc

.set init_range, 0x221510
.set alloc_continue, 0x21b853
.set alloc_error, 0x21b847
.set count_alloc_continue, 0x21b7ab
.set count_free_continue, 0x21c67c
.set count_remove_continue, 0x21c822
.set count_new_continue, 0x21d10e
.set mt_free_continue, 0x217505
.set mt_init_continue, 0x21778b
.set mt_native_base, 0x1466080
.set mt_alloc_cell, 0x6001200
.set mt_free_cell, 0x6001208
.set mt_reset_cell, 0x6001210
.set mt_heap_base, 0x6001218
.set mt_heap_bytes, 0x6001220
.set child_counts, 0x6001000
# Forward-referenced equate: rip-relative uses above get relocations.
.set variable_list, LIST_RVA
