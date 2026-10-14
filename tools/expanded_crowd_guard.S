.intel_syntax noprefix
.text
# Linked at 0x4612000. Every relay preserves the original native frame.
# Storage layout is asserted by include/crowd_resources.hpp.
.global crowd_gpu
crowd_gpu:
    mov rax, QWORD PTR [rip+front_end]
    cmp edx, 0x240000
    ja gpu_fail
    shl edx, 5
    mov rcx, QWORD PTR [rax+0x419c0]
    mov eax, DWORD PTR [rcx]
gpu_retry:
    lea r11d, [rax+rdx]
    cmp r11d, eax
    jb gpu_fail
    cmp r11d, 0x4800000
    ja gpu_fail
    lock cmpxchg DWORD PTR [rcx], r11d
    jne gpu_retry
    mov edi, eax
    mov eax, DWORD PTR [rip+gpu_peak]
gpu_peak_retry:
    cmp eax, r11d
    jae gpu_peak_done
    lock cmpxchg DWORD PTR [rip+gpu_peak], r11d
    jne gpu_peak_retry
gpu_peak_done:
    jmp gpu_resume
gpu_fail:
    lock inc DWORD PTR [rip+gpu_failures]
    jmp gpu_exhausted

.org 0x100, 0x90
.global crowd_cpu
crowd_cpu:
    mov rax, QWORD PTR [rip+front_end]
    cmp edx, 0x240000
    ja cpu_fail
    shl edx, 5
    lea r10, [rax+0x419d8]
    mov eax, DWORD PTR [r10]
cpu_retry:
    lea r11d, [rax+rdx]
    cmp r11d, eax
    jb cpu_fail
    cmp r11d, 0x4800000
    ja cpu_fail
    lock cmpxchg DWORD PTR [r10], r11d
    jne cpu_retry
    mov ecx, eax
    mov eax, DWORD PTR [rip+cpu_peak]
cpu_peak_retry:
    cmp eax, r11d
    jae cpu_peak_done
    lock cmpxchg DWORD PTR [rip+cpu_peak], r11d
    jne cpu_peak_retry
cpu_peak_done:
    jmp cpu_resume
cpu_fail:
    lock inc DWORD PTR [rip+cpu_failures]
    jmp cpu_exhausted

.org 0x200, 0x90
.global crowd_surfaces
crowd_surfaces:
    lea rax, [rbp]
    sub ebx, eax
    mov r10, QWORD PTR [rip+front_end]
    lea rax, [rip+native_frames]
    lea r11, [rip+frame_states]
    cmp r10, rax
    je surface_slot
    add rax, 0x13d700
    add r11, 32
    cmp r10, rax
    jne surface_bad_frame
surface_slot:
    mov eax, DWORD PTR [r11+8]
surface_retry:
    lea edx, [rax+rbx]
    cmp edx, eax
    jb surface_fail
    cmp edx, 0x200000
    ja surface_fail
    lock cmpxchg DWORD PTR [r11+8], edx
    jne surface_retry
    mov edx, eax
    mov rdi, QWORD PTR [r11]
    # Native continuation stores a full pointer in sceneDObj+0x48. Rigid
    # packed quarter-offsets keep their unchanged 128 KiB native pool.
    jmp surface_resume
surface_bad_frame:
    lock inc DWORD PTR [rip+bad_frames]
surface_fail:
    lock inc DWORD PTR [rip+surface_failures]
    jmp surface_exhausted

.org 0x400, 0x90
.global crowd_reset
crowd_reset:
    # RCX/RDX/R8 and all nonvolatile registers must survive this boundary.
    lea r10, [rip+native_frames]
    lea r11, [rip+frame_states]
    cmp rcx, r10
    je reset_first_slot
    add r10, 0x13d700
    add r11, 32
    cmp rcx, r10
    jne reset_bad_frame
    mov DWORD PTR [rip+second_handle_count], esi
    jmp reset_slot
reset_first_slot:
    mov DWORD PTR [rip+first_handle_count], esi
reset_slot:
    mov r10d, DWORD PTR [r11+8]
    cmp r10d, DWORD PTR [r11+12]
    jbe reset_clear
    mov DWORD PTR [r11+12], r10d
reset_clear:
    mov DWORD PTR [r11+8], esi
    lock inc DWORD PTR [rip+frame_resets]
    mov DWORD PTR [rcx+0x41994], esi
    jmp reset_resume
reset_bad_frame:
    lock inc DWORD PTR [rip+bad_frames]
    mov DWORD PTR [rcx+0x41994], esi
    jmp reset_resume

.org 0x600, 0x90
.global crowd_warning
crowd_warning:
    cmp ecx, 64
    jae warning_resume
    lea r10, [rip+warning_counts]
    mov r11d, ecx
    lock inc DWORD PTR [r10+r11*4]
warning_resume:
    mov DWORD PTR [rsp+8], ecx
    mov QWORD PTR [rsp+0x10], rdx
    jmp native_warning_resume

.org 0x1000, 0x90
.global crowd_glass_query
crowd_glass_query:
    movdqa xmm1, XMMWORD PTR [rip+exponent_mask]
    # +0x90 is a pointer in this source descriptor and must not be tested as
    # a float. Only native numeric center/extents/axes/points are inspected.
    .irp off,0x18,0x28,0x38,0x48,0x58,0x68,0x78,0xa0
        movdqu xmm0, XMMWORD PTR [r9+\off]
        pand xmm0, xmm1
        pcmpeqd xmm0, xmm1
        pmovmskb eax, xmm0
        test eax, eax
        jne invalid_glass_query
    .endr
    mov eax, DWORD PTR [r9+0x88]
    and eax, 0x7f800000
    cmp eax, 0x7f800000
    je invalid_glass_query
    movups xmm0, XMMWORD PTR [r9]
    mov rax, QWORD PTR [r9+0xb0]
    jmp glass_query_resume
invalid_glass_query:
    lock inc DWORD PTR [rip+invalid_queries]
    xor eax, eax
    mov r10d, 1
    lock cmpxchg DWORD PTR [rip+query_claim], r10d
    jne glass_query_skip
    mov QWORD PTR [rip+first_query], r9
    mov QWORD PTR [rip+first_vertex_data], rdx
    lea r10, [rip+query_copy]
    xor r11d, r11d
copy_query:
    mov rax, QWORD PTR [r9+r11]
    mov QWORD PTR [r10+r11], rax
    add r11d, 8
    cmp r11d, 0xb8
    jb copy_query
    mov DWORD PTR [rip+query_stage], 2
    mov DWORD PTR [rip+query_claim], 2
glass_query_skip:
    jmp glass_query_epilogue
.balign 16, 0x90
exponent_mask:
    .long 0x7f800000,0x7f800000,0x7f800000,0x7f800000

.org 0x1400, 0x90
.global crowd_feature
crowd_feature:
    mov eax, DWORD PTR [rsp+0x54]
    test eax, eax
    jz invalid_feature
    mov r10d, DWORD PTR [rbx+8]
    test r10d, r10d
    jz invalid_feature
    lea r10, [r10*4+2]
    cmp rax, r10
    ja invalid_feature
    cmp eax, 1
    jmp feature_resume
invalid_feature:
    lock inc DWORD PTR [rip+invalid_features]
    mov DWORD PTR [rip+last_feature], eax
    jmp feature_epilogue

.org 0x1800, 0x90
.global crowd_glass_pose
crowd_glass_pose:
    lea r8, [rsp+0x20]
    lea rdx, [rsp+0x38]
    lea rcx, [rsp+0x50]
    cmp DWORD PTR [rcx+0x34], 7
    jne glass_pose_resume
    movdqa xmm1, XMMWORD PTR [rip+exponent_mask]
    .irp off,0x38,0x48,0x58
        movdqu xmm0, XMMWORD PTR [rcx+\off]
        pand xmm0, xmm1
        pcmpeqd xmm0, xmm1
        pmovmskb eax, xmm0
        test eax, eax
        jne invalid_glass_pose
    .endr
    movdqu xmm0, XMMWORD PTR [r8]
    pand xmm0, xmm1
    pcmpeqd xmm0, xmm1
    pmovmskb eax, xmm0
    test eax, eax
    jne invalid_glass_pose
    movq xmm0, QWORD PTR [r8+16]
    pand xmm0, xmm1
    pcmpeqd xmm0, xmm1
    pmovmskb eax, xmm0
    test eax, eax
    jne invalid_glass_pose
glass_pose_resume:
    jmp native_glass_pose_resume
invalid_glass_pose:
    lock inc DWORD PTR [rip+invalid_queries]
    xor eax, eax
    mov r10d, 1
    lock cmpxchg DWORD PTR [rip+query_claim], r10d
    jne glass_pose_skip
    mov QWORD PTR [rip+first_query], rdi
    mov QWORD PTR [rip+first_vertex_data], rcx
    lea r10, [rip+query_copy]
    xor r11d, r11d
copy_pose:
    mov rax, QWORD PTR [rcx+r11]
    mov QWORD PTR [r10+r11], rax
    add r11d, 8
    cmp r11d, 0xb8
    jb copy_pose
    mov DWORD PTR [rip+query_stage], 1
    mov DWORD PTR [rip+query_claim], 2
glass_pose_skip:
    jmp native_glass_pose_skip

.org 0x1a00, 0x90
.set front_end, 0x410fd00
.set native_frames, 0x458fb00
.set frame_states, 0x47e0040
.set gpu_failures, 0x47e0080
.set cpu_failures, 0x47e0084
.set surface_failures, 0x47e0088
.set bad_frames, 0x47e008c
.set frame_resets, 0x47e0090
.set gpu_peak, 0x47e0094
.set cpu_peak, 0x47e0098
.set first_handle_count, 0x47e009c
.set second_handle_count, 0x47e00a0
.set warning_counts, 0x47e0100
.set invalid_queries, 0x47e0200
.set invalid_features, 0x47e0204
.set query_claim, 0x47e0208
.set last_feature, 0x47e020c
.set first_query, 0x47e0210
.set first_vertex_data, 0x47e0218
.set query_copy, 0x47e0220
.set query_stage, 0x47e02d8
.set gpu_resume, 0x530cc
.set gpu_exhausted, 0x531ce
.set cpu_resume, 0x53224
.set cpu_exhausted, 0x53208
.set surface_resume, 0x532cc
.set surface_exhausted, 0x531bc
.set reset_resume, 0xcc1e
.set native_warning_resume, 0x2acf9
.set glass_query_resume, 0x1dfaa4
.set glass_query_epilogue, 0x1dfba8
.set feature_resume, 0x1dcf66
.set feature_epilogue, 0x1dd183
.set native_glass_pose_resume, 0x1ecffa
.set native_glass_pose_skip, 0x1ed005
