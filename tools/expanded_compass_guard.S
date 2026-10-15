.intel_syntax noprefix
.text
# Linked at 0x4616200 by prepare_expanded_compass.py.
#
# The native compass/minimap tracker keeps 42 records of 0x2c bytes at
# 0x3fdf50: slots 0..31 for actors (index from the client entity->actor byte
# map), 0x20/0x21 for players and 0x22..0x29 for other tracked entities.
# Actor slots 32..255 wrote past the table: slots 51..63 overwrite static
# console command entries and slots 93..209 overwrite the client glass place
# state table at 0x3fef50, which then requests shatters for absent panes.
# Actors beyond the native 32 compass slots are simply not tracked.

.global compass_update_guard
compass_update_guard:              # 0x92c37 inside 0x92c00 (actor path)
    mov ecx, DWORD PTR [rsp+0x40]
    cmp ecx, 0x20
    jae compass_update_reject
    jmp compass_update_continue
compass_update_reject:
    inc DWORD PTR [rip+compass_rejected]
    mov DWORD PTR [rip+compass_last_slot], ecx
    jmp compass_update_epilogue

.org 0x100, 0x90
.global compass_slot_guard
compass_slot_guard:                # replaces call 0x12c6a0 at 0x92ded
    call compass_actor_slot
    cmp DWORD PTR [rdx], 0x20
    jae compass_slot_reject
    jmp compass_slot_continue
compass_slot_reject:
    inc DWORD PTR [rip+compass_rejected]
    mov eax, DWORD PTR [rdx]
    mov DWORD PTR [rip+compass_last_slot], eax
    mov DWORD PTR [rdx], -1
    xor eax, eax
    jmp compass_slot_epilogue

.org 0x200, 0x90
.global compass_writer_guard
compass_writer_guard:              # 0x925af inside 0x92580, after 0x92dc0
    # Stock code ignores the lookup result and indexes the table with the
    # returned slot even when it is -1. Use only a found slot.
    test al, al
    jz compass_writer_skip
    mov r9d, DWORD PTR [rsp+0x30]
    jmp compass_writer_continue
compass_writer_skip:
    inc DWORD PTR [rip+compass_writer_skips]
    jmp compass_writer_epilogue

.org 0x300, 0x90

.set compass_update_continue, 0x92cf1
.set compass_update_epilogue, 0x92db5
.set compass_actor_slot, 0x12c6a0
.set compass_slot_continue, 0x92df2
.set compass_slot_epilogue, 0x92df4
.set compass_writer_continue, 0x925b4
.set compass_writer_epilogue, 0x9260d
.set compass_rejected, 0x47e0390
.set compass_last_slot, 0x47e0394
.set compass_writer_skips, 0x47e0398
