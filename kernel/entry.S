.section .text.boot,"ax"
.global _start
.type _start,@function
.extern kernel_main
.extern kernel_boot_stack_top

_start:
    cli
    cld
    leaq kernel_boot_stack_top(%rip), %rsp
    andq $-16, %rsp
    call kernel_main
1:
    cli
    hlt
    jmp 1b
.size _start, .-_start

.section .text,"ax"
.global isr_default
.type isr_default,@function
isr_default:
    cli
2:
    hlt
    jmp 2b
.size isr_default, .-isr_default

.global isr_timer
.type isr_timer,@function
.extern kernel_timer_tick
isr_timer:
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

    movq %rsp, %rbp
    andq $-16, %rsp
    call kernel_timer_tick
    movq %rbp, %rsp

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
    iretq
.size isr_timer, .-isr_timer

.global isr_keyboard
.type isr_keyboard,@function
.extern keyboard_irq_handler
isr_keyboard:
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

    movq %rsp, %rbp
    andq $-16, %rsp
    call keyboard_irq_handler
    movq %rbp, %rsp

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
    iretq
.size isr_keyboard, .-isr_keyboard

.global gdt_load
.type gdt_load,@function
gdt_load:
    lgdt (%rdi)
    pushq $0x08
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    ret
.size gdt_load, .-gdt_load

.section .bss,"aw",@nobits
.align 16
.global kernel_boot_stack
kernel_boot_stack:
    .skip 65536
.global kernel_boot_stack_top
kernel_boot_stack_top:

.section .note.GNU-stack,"",@progbits
