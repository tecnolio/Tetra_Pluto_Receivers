.syntax unified
.arch armv7-a
.fpu neon-vfpv3
.arm
.global _start
.type _start,%function
_start:
    mov r0, sp
    b pt_entry
