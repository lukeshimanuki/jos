/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num, dpl)						\
	.text; \
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps; \
	.data; \
	.value name - idt_start; \
	.value GD_KT; \
	.byte 0; \
	.byte 0b1##dpl##01110; \
	.value 0xf011;

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num, dpl)					\
	.text; \
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps; \
	.data; \
	.value name - idt_start; \
	.value GD_KT; \
	.byte 0; \
	.byte 0b1##dpl##01110; \
	.value 0xf011;

#define TRAPINTHANDLER_NOEC(name, num, dpl)					\
	.text; \
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps; \
	.data; \
	.value name - idt_start; \
	.value GD_KT; \
	.byte 0; \
	.byte 0b1##dpl##01110; \
	.value 0xf011;

#define IRQHN(n)					\
	.text; \
	.globl IRQHN_##n;							\
	.type IRQHN_##n, @function;						\
	.align 2;							\
	IRQHN_##n:								\
	pushl $0;							\
	pushl $(IRQ_OFFSET) + n;							\
	jmp _alltraps; \
	.data; \
	.value IRQHN_##n - idt_start; \
	.value GD_KT; \
	.byte 0; \
	.byte 0b10001110; \
	.value 0xf011;

.data
.globl idt
.align 16;
.type idt, @object
.size idt, 256 * 8
idt:

.text
.align 0x10000 // bad hack
idt_start:

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */
#define TRAPH(name) TRAPHANDLER(name, T_##name, 00)
#define TRAPHN(name) TRAPHANDLER_NOEC(name, T_##name, 00)
#define TRAPHUN(name) TRAPHANDLER_NOEC(name, T_##name, 11)
#define TRAPHI(name) TRAPINTHANDLER_NOEC(name, T_##name, 11)

TRAPHN(DIVIDE)
TRAPHN(DEBUG)
TRAPHN(NMI)
TRAPHUN(BRKPT)
TRAPHN(OFLOW)
TRAPHN(BOUND)
TRAPHN(ILLOP)
TRAPHN(DEVICE)
TRAPH(DBLFLT)
.data
.skip 8 * (T_TSS - T_DBLFLT - 1)
TRAPH(TSS)
TRAPH(SEGNP)
TRAPH(STACK)
TRAPH(GPFLT)
TRAPH(PGFLT)
.data
.skip 8 * (T_FPERR - T_PGFLT - 1)
TRAPHN(FPERR)
TRAPH(ALIGN)
TRAPHN(MCHK)
TRAPHN(SIMDERR)
.data
.skip 8 * (IRQ_OFFSET - T_SIMDERR - 1)
IRQHN(0)
IRQHN(1)
IRQHN(2)
IRQHN(3)
IRQHN(4)
IRQHN(5)
IRQHN(6)
IRQHN(7)
IRQHN(8)
IRQHN(9)
IRQHN(10)
IRQHN(11)
IRQHN(12)
IRQHN(13)
IRQHN(14)
IRQHN(15)
.data
.skip 8 * (T_SYSCALL - (IRQ_OFFSET + 15) - 1)
TRAPHI(SYSCALL)
.data
.skip 8 * (256 - T_SYSCALL - 1)

.text
/*
 * Lab 3: Your code here for _alltraps
 */
_alltraps:
	pushw $0
	pushw %ds
	pushw $0
	pushw %es
	pushal

	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %esp, %esi
	pushl %esp
	call trap

