/* See COPYRIGHT for copyright information. */

#ifndef JOS_KERN_TRAP_H
#define JOS_KERN_TRAP_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#include <inc/trap.h>
#include <inc/mmu.h>

/* The kernel's interrupt descriptor table */
extern struct Gatedesc idt[];
extern struct Pseudodesc idt_pd;

void trap_init(void);
void trap_init_percpu(void);
void print_regs(struct PushRegs *regs);
void print_trapframe(struct Trapframe *tf);
void page_fault_handler(struct Trapframe *);
void backtrace(struct Trapframe *);


/*
  awk code to generate these definitions:
  awk '{print "void "tolower($2)"();"}'
*/

void t_divide();
void t_debug();
void t_nmi();
void t_brkpt();
void t_oflow();
void t_bound();
void t_illop();
void t_device();
void t_dblflt();
void t_tss();
void t_segnp();
void t_stack();
void t_gpflt();
void t_pgflt();
void t_fperr();
void t_align();
void t_mchk();
void t_simderr();

void t_syscall();
void t_default();

void irq_timer();
void irq_kbd();
void irq_serial();
void irq_spurious();
void irq_error();

#endif /* JOS_KERN_TRAP_H */
