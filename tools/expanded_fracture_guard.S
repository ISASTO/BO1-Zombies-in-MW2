.intel_syntax noprefix
.text
# Native glass triangulation writes into a 231-DWORD piece geometry array.
# The returned Test30 stack contains hundreds of repeated two-vertex records.
# Relays retain native frames, coordinates, edge construction and publication.
.global fracture_entry
fracture_entry:
    mov r8d, DWORD PTR [rdx+0x430]
    cmp r8d, 0xe7
    ja entry_invalid
    cmp DWORD PTR [rcx+0xbf4], 0xff
    ja entry_invalid
    cmp DWORD PTR [rdx+0x14], 0x20
    ja entry_invalid
    cmp BYTE PTR [rdx+0x20], 0x80
    ja entry_invalid
    lea rax, [rdx+0x434]
    jmp entry_continue
entry_invalid:
    mov r13, rdx
    inc DWORD PTR [rip+invalid_inputs]
    mov DWORD PTR [rip+last_vertices], 0
    mov DWORD PTR [rip+last_bytes], 0
    jmp fracture_reject
.org 0x100, 0x90
fracture_edges:
    cmp eax, 0x200
    jae edges_invalid
    movzx edx, BYTE PTR [r8+1]
    inc r9d
    jmp edges_continue
edges_invalid:
    inc DWORD PTR [rip+invalid_inputs]
    mov DWORD PTR [rip+last_vertices], eax
    mov DWORD PTR [rip+last_bytes], 0
    jmp fracture_reject
.org 0x180, 0x90
fracture_reject:
    # Never publish a partially generated or invalid native piece.
    mov DWORD PTR [r13+0x430], -1
    jmp fracture_epilogue
.org 0x200, 0x90
polygon_first:
    mov ecx, DWORD PTR [rsi+0x1100]
    add ecx, 2
    cmp ecx, DWORD PTR [rsi+0x1104]
    ja polygon_full
    mov BYTE PTR [r14], al
    mov rax, QWORD PTR [rsp+0x40]
    jmp first_continue
polygon_full:
    xor ebp, ebp
    jmp polygon_epilogue
.org 0x300, 0x90
polygon_next:
    mov ecx, DWORD PTR [rsi+0x1100]
    add ecx, ebp
    cmp ecx, DWORD PTR [rsi+0x1104]
    jae polygon_full
    mov ecx, ebp
    mov r9, r8
    inc ebp
    jmp next_continue
.org 0x400, 0x90
fracture_polygon:
    mov DWORD PTR [rip+last_vertices], eax
    mov ecx, DWORD PTR [rbp+0x1020]
    mov DWORD PTR [rip+last_bytes], ecx
    test eax, eax
    jz output_full
    cmp eax, 3
    jb polygon_invalid
    cmp eax, 0x80
    ja polygon_invalid
    # Include the terminator and up to three alignment bytes in the bound.
    lea ecx, [rcx+rax+4]
    and ecx, -4
    cmp ecx, DWORD PTR [rbp+0x1024]
    ja output_full
    mov r8d, DWORD PTR [rbp+0x1020]
    mov r11, QWORD PTR [rbp+0x1028]
    jmp polygon_continue
polygon_invalid:
    inc DWORD PTR [rip+invalid_fragments]
    jmp fracture_reject
output_full:
    inc DWORD PTR [rip+output_rejections]
    jmp fracture_reject
.org 0x600, 0x90
fracture_publish:
    cmp DWORD PTR [rsp+0x450], 0xe7
    ja producer_epilogue
    lea rdx, [rsp+0x20]
    mov rcx, rbx
    jmp publish_continue
.org 0x700, 0x90

.set entry_continue, 0x10a8e5
.set edges_continue, 0x10a968
.set fracture_epilogue, 0x10acdd
.set first_continue, 0x109ea4
.set next_continue, 0x109f12
.set polygon_epilogue, 0x109f20
.set polygon_continue, 0x10aa67
.set producer_epilogue, 0x1095c9
.set publish_continue, 0x1095c4
.set invalid_fragments, 0x47e02dc
.set output_rejections, 0x47e02e0
.set invalid_inputs, 0x47e02e4
.set last_vertices, 0x47e02e8
.set last_bytes, 0x47e02ec
