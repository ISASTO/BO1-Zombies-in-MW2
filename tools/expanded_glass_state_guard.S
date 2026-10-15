.intel_syntax noprefix
.text
# Linked at 0x4616000 by prepare_expanded_glass_state.py.
#
# Client glass state 2 ("shatter") is applied by 0x100870. It scans the active
# pieces for one owned by the place and records the match in a stack flag at
# [rsp+0x40] and the index in a local that is never initialized. Stock code
# then calls the fracture routine unconditionally. The returned Test31 dumps
# show the flag clear and the uninitialized local (0x7ff7, 0) used as a piece
# index, so native fracture ran on an inactive or foreign piece record.
.global place_lookup_guard
place_lookup_guard:
    cmp BYTE PTR [rsp+0x40], 0
    je place_missing
    mov r9, r15
    mov QWORD PTR [rsp+0x20], r14
    jmp lookup_continue
place_missing:
    # Nothing exists to shatter. Skip only the fracture call; the caller still
    # records the applied state exactly as it does after a native fracture.
    inc DWORD PTR [rip+missing_places]
    mov DWORD PTR [rip+last_missing_place], esi
    jmp lookup_skip

.org 0x100, 0x90
# Server glass damage receives a place index derived from a trace hit id and
# indexes 12-byte place records with no bound. Reject an index outside the
# loaded place count and record its native caller for diagnosis.
.global glass_damage_guard
glass_damage_guard:
    mov rax, QWORD PTR [rip+server_glass]
    test rax, rax
    jz damage_reject
    cmp ecx, DWORD PTR [rax+0x8]
    jae damage_reject
    mov QWORD PTR [rsp+0x8], rbx
    mov QWORD PTR [rsp+0x10], rbp
    jmp damage_continue
damage_reject:
    inc DWORD PTR [rip+rejected_damage]
    mov DWORD PTR [rip+last_rejected_place], ecx
    mov rax, QWORD PTR [rsp]
    mov QWORD PTR [rip+last_rejected_caller], rax
    ret

.org 0x200, 0x90

.set lookup_continue, 0x1009de
.set lookup_skip, 0x1009f0
.set damage_continue, 0x16a06a
.set server_glass, 0xcf75a8
.set missing_places, 0x47e02f0
.set last_missing_place, 0x47e02f4
.set rejected_damage, 0x47e02f8
.set last_rejected_place, 0x47e02fc
.set last_rejected_caller, 0x47e0300
