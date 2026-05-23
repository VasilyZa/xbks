.section .text
.global xbks_x86_64_syscall_entry
.type xbks_x86_64_syscall_entry, @function

xbks_x86_64_syscall_entry:
    movq %rsp, xbks_syscall_saved_user_rsp(%rip)
    leaq xbks_syscall_stack_top(%rip), %rsp

    pushq xbks_syscall_saved_user_rsp(%rip)
    pushq %rcx
    pushq %r11

    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi
    call xbks_syscall_dispatch

    movq %rax, 112(%rsp)

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    popq %r11
    popq %rcx
    popq %rsp

    sysretq

.size xbks_x86_64_syscall_entry, . - xbks_x86_64_syscall_entry

.section .bss
.align 16
xbks_syscall_saved_user_rsp:
    .quad 0

.align 16
xbks_syscall_stack:
    .skip 16384
xbks_syscall_stack_top:
