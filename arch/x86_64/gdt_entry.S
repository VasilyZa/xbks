.section .text
.global xbks_x86_64_gdt_load
.type xbks_x86_64_gdt_load, @function

xbks_x86_64_gdt_load:
    lgdt (%rdi)

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    pushq $0x08
    leaq 1f(%rip), %rax
    pushq %rax
    lretq

1:
    ret

.size xbks_x86_64_gdt_load, . - xbks_x86_64_gdt_load
