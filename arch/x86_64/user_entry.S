.section .text
.global xbks_x86_64_enter_user
.type xbks_x86_64_enter_user, @function

xbks_x86_64_enter_user:
    movq %rsi, %rsp
    movq $0x202, %r11
    movq %rdi, %rcx
    xorq %rax, %rax
    xorq %rbx, %rbx
    xorq %rdx, %rdx
    xorq %rsi, %rsi
    xorq %rdi, %rdi
    xorq %rbp, %rbp
    xorq %r8, %r8
    xorq %r9, %r9
    xorq %r10, %r10
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15
    sysretq

.size xbks_x86_64_enter_user, . - xbks_x86_64_enter_user
