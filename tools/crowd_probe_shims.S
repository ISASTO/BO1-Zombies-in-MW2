.intel_syntax noprefix
.text
# Linux-only test harness. The shipped relays do not use these functions.
# Restore a complete synthetic native context, run one exact fragment, capture
# it before the following engine code, then restore the SysV fixture caller.
.global crowd_execute
crowd_execute:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov QWORD PTR [rip+crowd_saved_stack], rsp
    mov QWORD PTR [rip+crowd_entry], rdi
    mov r15, rsi
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
        movdqu xmm\n, XMMWORD PTR [r15+128+\n*16]
    .endr
    push QWORD PTR [r15+384]
    popfq
    mov rax, QWORD PTR [r15]
    mov rcx, QWORD PTR [r15+8]
    mov rdx, QWORD PTR [r15+16]
    mov rbx, QWORD PTR [r15+24]
    mov rsp, QWORD PTR [r15+32]
    mov rbp, QWORD PTR [r15+40]
    mov rsi, QWORD PTR [r15+48]
    mov rdi, QWORD PTR [r15+56]
    mov r8, QWORD PTR [r15+64]
    mov r9, QWORD PTR [r15+72]
    mov r10, QWORD PTR [r15+80]
    mov r11, QWORD PTR [r15+88]
    mov r12, QWORD PTR [r15+96]
    mov r13, QWORD PTR [r15+104]
    mov r14, QWORD PTR [r15+112]
    mov r15, QWORD PTR [r15+120]
    jmp QWORD PTR [rip+crowd_entry]

.global crowd_snapshot
crowd_snapshot:
    mov QWORD PTR [rip+crowd_result], rax
    mov QWORD PTR [rip+crowd_result+8], rcx
    mov QWORD PTR [rip+crowd_result+16], rdx
    mov QWORD PTR [rip+crowd_result+24], rbx
    mov QWORD PTR [rip+crowd_result+32], rsp
    mov QWORD PTR [rip+crowd_result+40], rbp
    mov QWORD PTR [rip+crowd_result+48], rsi
    mov QWORD PTR [rip+crowd_result+56], rdi
    mov QWORD PTR [rip+crowd_result+64], r8
    mov QWORD PTR [rip+crowd_result+72], r9
    mov QWORD PTR [rip+crowd_result+80], r10
    mov QWORD PTR [rip+crowd_result+88], r11
    mov QWORD PTR [rip+crowd_result+96], r12
    mov QWORD PTR [rip+crowd_result+104], r13
    mov QWORD PTR [rip+crowd_result+112], r14
    mov QWORD PTR [rip+crowd_result+120], r15
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
        movdqu XMMWORD PTR [rip+crowd_result+128+\n*16], xmm\n
    .endr
    pushfq
    pop QWORD PTR [rip+crowd_result+384]
    mov rsp, QWORD PTR [rip+crowd_saved_stack]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

# Thread-safe Windows-ABI entry shim for the new post-copy publication gate.
.global crowd_publish
crowd_publish:
    push rbx
    push rdi
    push r13
    push r14
    sub rsp, 0x48
    mov rdi, rcx
    mov r14d, edx
    mov ebx, r8d
    mov r13, r9
    mov DWORD PTR [rsp+0x34], 0
    jmp QWORD PTR [rip+crowd_publish_entry]
.global crowd_publish_success
crowd_publish_success:
    mov eax, 1
    jmp publish_return
.global crowd_publish_failure
crowd_publish_failure:
    xor eax, eax
publish_return:
    add rsp, 0x48
    pop r14
    pop r13
    pop rdi
    pop rbx
    ret
.section .note.GNU-stack,"",@progbits
