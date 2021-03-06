#include <arch.h>
#include <cpu.h>
#include <paging.h>
#include <pit.h>
#include <printk.h>
#include <syscall.h>
#include <sched.h>
#include "arch.h"
#include "smp.h"

// implementation of the x86 interrupt request handling system
extern u32 idt_block[];
extern void *vectors[];  // in vectors.S: array of 256 entry pointers

// Set up a normal interrupt/trap gate descriptor.
// - istrap: 1 for a trap (= exception) gate, 0 for an interrupt gate.
//   interrupt gate clears FL_IF, trap gate leaves FL_IF alone
// - sel: Code segment selector for interrupt/trap handler
// - off: Offset in code segment for interrupt/trap handler
// - dpl: Descriptor Privilege Level -
//        the privilege level required for software to invoke
//        this interrupt/trap gate explicitly using an int instruction.
#define SETGATE(gate, istrap, sel, off, d)        \
  {                                               \
    (gate).off_15_0 = (u32)(off)&0xffff;          \
    (gate).cs = (sel);                            \
    (gate).args = 0;                              \
    (gate).rsv1 = 0;                              \
    (gate).type = (istrap) ? STS_TG32 : STS_IG32; \
    (gate).s = 0;                                 \
    (gate).dpl = (d);                             \
    (gate).p = 1;                                 \
    (gate).off_31_16 = (u32)(off) >> 16;          \
  }

// Processor-defined:
#define TRAP_DIVIDE 0  // divide error
#define TRAP_DEBUG 1   // debug exception
#define TRAP_NMI 2     // non-maskable interrupt
#define TRAP_BRKPT 3   // breakpoint
#define TRAP_OFLOW 4   // overflow
#define TRAP_BOUND 5   // bounds check
#define TRAP_ILLOP 6   // illegal opcode
#define TRAP_DEVICE 7  // device not available
#define TRAP_DBLFLT 8  // double fault
#define TRAP_TSS 10    // invalid task switch segment
#define TRAP_SEGNP 11  // segment not present
#define TRAP_STACK 12  // stack exception
#define TRAP_GPFLT 13  // general protection fault
#define TRAP_PGFLT 14  // page fault
// #define TRAP_RES        15      // reserved
#define TRAP_FPERR 16    // floating point error
#define TRAP_ALIGN 17    // aligment check
#define TRAP_MCHK 18     // machine check
#define TRAP_SIMDERR 19  // SIMD floating point error

#define EXCP_NAME 0
#define EXCP_MNEMONIC 1
const char *excp_codes[128][2] = {
    {"Divide By Zero", "#DE"},
    {"Debug", "#DB"},
    {"Non-maskable Interrupt", "N/A"},
    {"Breakpoint Exception", "#BP"},
    {"Overflow Exception", "#OF"},
    {"Bound Range Exceeded", "#BR"},
    {"Invalid Opcode", "#UD"},
    {"Device Not Available", "#NM"},
    {"Double Fault", "#DF"},
    {"Coproc Segment Overrun", "N/A"},
    {"Invalid TSS", "#TS"},
    {"Segment Not Present", "#NP"},
    {"Stack Segment Fault", "#SS"},
    {"General Protection Fault", "#GP"},
    {"Page Fault", "#PF"},
    {"Reserved", "N/A"},
    {"x86 FP Exception", "#MF"},
    {"Alignment Check", "#AC"},
    {"Machine Check", "#MC"},
    {"SIMD FP Exception", "#XM"},
    {"Virtualization Exception", "#VE"},
    {"Reserved", "N/A"},
    {"Reserved", "N/A"},
    {"Reserved", "N/A"},
    {"Reserved", "N/A"},
    {"Reserved", "N/A"},
    {"Reserved", "N/A"},
    {"Reserved", "N/A"},
    {"Reserved", "N/A"},
    {"Reserved", "N/A"},
    {"Security Exception", "#SX"},
    {"Reserved", "N/A"},
};

void arch::irq::eoi(int i) {
  if (i >= 32) {
    int pic_irq = i - 32;
    if (pic_irq >= 8) {
      outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
  }

  smp::lapic_eoi();
}

void arch::irq::enable(int num) {
  // if the interrupt is larger than 32, enable in the ioapic
  if (num >= 32) {
    smp::ioapicenable(num - 32, /* TODO */ 0);
    pic_enable(num - 32);
  }
}

void arch::irq::disable(int num) {
  // if the interrupt is larger than 32, disable in the ioapic
  if (num >= 32) {
    // smp::ioapicdisable(num);
    pic_disable(num - 32);
  }
}

static void mkgate(u32 *idt, u32 n, void *kva, u32 pl, u32 trap) {
  u64 addr = (u64)kva;
  n *= 4;
  trap = trap ? 0x8F00 : 0x8E00;  // TRAP vs INTERRUPT gate;
  idt[n + 0] = (addr & 0xFFFF) | ((SEG_KCODE << 3) << 16);
  idt[n + 1] = (addr & 0xFFFF0000) | trap | ((pl & 0b11) << 13);  // P=1 DPL=pl
  idt[n + 2] = addr >> 32;
  idt[n + 3] = 0;
}

static u64 last_tsc = 0;

// TODO: move to sched.cpp
static void tick_handle(int i, reg_t *tf) {
  auto &cpu = cpu::current();

  u64 now = arch::read_timestamp();

  last_tsc = now;

  // increment the number of ticks
  cpu.ticks++;
  sched::handle_tick(cpu.ticks);
  return;
}

static void 
unknown_exception(int i, reg_t *regs) {
  auto *tf = (struct x86_64regs*)regs;

  KERR("KERNEL PANIC\n");
  KERR("CPU EXCEPTION: %s\n", excp_codes[tf->trapno][EXCP_NAME]);
  KERR("the system was running for %d ticks\n");

  KERR("\n");
  KERR("Stats for nerds:\n");
  KERR("INT=%016zx  ERR=%016zx\n", tf->trapno, tf->err);
  KERR("ESP=%016zx  EIP=%016zx\n", tf->rsp, tf->rip);
  KERR("CR2=%016zx  CR3=%016zx\n", read_cr2(), read_cr3());
  KERR("\n");
  KERR("SYSTEM HALTED. File a bug report please:\n");
  KERR("  repo: github.com/nickwanninger/chariot\n");

  KERR("\n");
  KERR("git = %s\n", GIT_REVISION);

  lidt(0, 0);  // die
  while (1) {
  };
}

static void dbl_flt_handler(int i, reg_t *tf) {
  printk("DOUBLE FAULT!\n");
  printk("&i=%p\n", &i);

  unknown_exception(i, tf);
  while (1) {
  }
}

/*
static void unknown_hardware(int i, reg_t *tf) {
  printk("unknown! %d\n", i);
}
*/

extern const char *ksym_find(off_t);

void dump_backtrace(off_t ebp) {
  printk("Backtrace (ebp=%p):\n", ebp);

  off_t stk_end = (off_t)curthd->stack + curthd->stack_size;
  int i = 0;
  for (off_t *stack_ptr = (off_t *)ebp;
       (off_t)stack_ptr < stk_end && (off_t)stack_ptr >= KERNEL_VIRTUAL_BASE;
       stack_ptr = (off_t *)*stack_ptr) {
    off_t retaddr = stack_ptr[1];
    printk("%3d: %p\n", i++, retaddr);
  }
}


/* eflags masks */
#define CC_C 0x0001
#define CC_P 0x0004
#define CC_A 0x0010
#define CC_Z 0x0040
#define CC_S 0x0080
#define CC_O 0x0800

#define TF_SHIFT 8
#define IOPL_SHIFT 12
#define VM_SHIFT 17
#define TF_MASK 0x00000100
#define IF_MASK 0x00000200
#define DF_MASK 0x00000400
#define IOPL_MASK 0x00003000
#define NT_MASK 0x00004000
#define RF_MASK 0x00010000
#define VM_MASK 0x00020000
#define AC_MASK 0x00040000
#define VIF_MASK 0x00080000
#define VIP_MASK 0x00100000
#define ID_MASK 0x00200000


static void gpf_handler(int i, reg_t *regs) {

  auto *tf = (struct x86_64regs*)regs;
  // TODO: die
  KERR("pid %d, tid %d died from GPF @ %p (err=%p)\n", curthd->pid, curthd->tid,
       tf->rip, tf->err);

  unsigned int eflags = tf->rflags;
#define GET(name) (tf->name)

#define REGFMT "%016p"
  printk("RAX=" REGFMT " RBX=" REGFMT " RCX=" REGFMT " RDX=" REGFMT
         "\n"
         "RSI=" REGFMT " RDI=" REGFMT " RBP=" REGFMT " RSP=" REGFMT
         "\n"
         "R8 =" REGFMT " R9 =" REGFMT " R10=" REGFMT " R11=" REGFMT
         "\n"
         "R12=" REGFMT " R13=" REGFMT " R14=" REGFMT " R15=" REGFMT
         "\n"
         "RIP=" REGFMT " RFL=%08x [%c%c%c%c%c%c%c]\n",

         GET(rax), GET(rbx), GET(rcx), GET(rdx), GET(rsi), GET(rdi), GET(rbp),
         GET(rsp), GET(r8), GET(r9), GET(r10), GET(r11), GET(r12), GET(r13),
         GET(r14), GET(r15), GET(rip), eflags, eflags & DF_MASK ? 'D' : '-',
         eflags & CC_O ? 'O' : '-', eflags & CC_S ? 'S' : '-',
         eflags & CC_Z ? 'Z' : '-', eflags & CC_A ? 'A' : '-',
         eflags & CC_P ? 'P' : '-', eflags & CC_C ? 'C' : '-');

  dump_backtrace(tf->rbp);

  sys::exit_proc(-1);

  sched::exit();
}

static void illegal_instruction_handler(int i, reg_t *regs) {

  auto *tf = (struct x86_64regs*)regs;
  auto pa = paging::get_physical(tf->rip & ~0xFFF);
  if (pa == 0) {
    return;
  }

  KERR("pid %d, tid %d died from illegal instruction @ %p\n", curthd->pid,
       curthd->tid, tf->rip);
  KERR("  ESP=%p\n", tf->rsp);
  sys::exit_proc(-1);
}

extern "C" void syscall_handle(int i, reg_t *tf);

#define PGFLT_PRESENT (1 << 0)
#define PGFLT_WRITE (1 << 1)
#define PGFLT_USER (1 << 2)
#define PGFLT_RESERVED (1 << 3)
#define PGFLT_INSTR (1 << 4)

static void pgfault_handle(int i, reg_t *regs) {
  auto *tf = (struct x86_64regs*)regs;
  void *page = (void *)(read_cr2() & ~0xFFF);

  auto proc = curproc;
  if (curproc == NULL) {
    KERR("not in a proc while pagefaulting (rip=%p, addr=%p)\n", tf->rip,
         read_cr2());
    // lookup the kernel proc if we aren't in one!
    proc = sched::proc::kproc();
  }

  curthd->trap_frame = regs;

  if (proc) {
    int err = 0;

    // TODO: read errors
    if (tf->err & PGFLT_USER) err |= FAULT_PERM;
    if (tf->err & PGFLT_WRITE) err |= FAULT_WRITE;
    if (tf->err & PGFLT_INSTR) err |= FAULT_EXEC;

    if ((tf->err & PGFLT_USER) == 0) {
      // TODO
    }

    int res = proc->mm->pagefault((off_t)page, err);
    if (res == -1) {
      // TODO:
      KERR("pid %d, tid %d segfaulted @ %p\n", curthd->pid, curthd->tid,
           tf->rip);
      KERR("       bad address = %p\n", read_cr2());
      KERR("               err = %p\n", tf->err);

      dump_backtrace(tf->rbp);

      sys::exit_proc(-1);

      // XXX: just block, cause its an easy way to get the proc to stop running
      sched::block();
    }

  } else {
    panic("page fault in kernel code (no proc)\n");
  }
}

int arch::irq::init(void) {
  u32 *idt = (u32 *)&idt_block;

  // fill up the idt with the correct trap vectors.
  for (int n = 0; n < 130; n++) mkgate(idt, n, vectors[n], 0, 0);

  int i;
  for (i = 32; i < 48; i++) irq::disable(i);

  // handle all the <32 irqs as unknown
  for (i = 0; i < 32; i++) {
    ::irq::install(i, unknown_exception, "Unknown Exception");
  }

  for (i = 32; i < 48; i++) {
    // ::irq::install(i, unknown_hardware, "Unknown Hardware");
  }

  ::irq::install(TRAP_DBLFLT, dbl_flt_handler, "Double Fault");
  ::irq::install(TRAP_PGFLT, pgfault_handle, "Page Fault");
  ::irq::install(TRAP_GPFLT, gpf_handler, "General Protection Fault");
  ::irq::install(TRAP_ILLOP, illegal_instruction_handler,
                 "Illegal Instruction Handler");

  pic_disable(34);

  mkgate(idt, 32, vectors[32], 0, 0);
  ::irq::install(32, tick_handle, "Preemption Tick");

  mkgate(idt, 0x80, vectors[0x80], 3, 0);
  ::irq::install(0x80, syscall_handle, "System Call");

  KINFO("Registered basic interrupt handlers\n");

  // and load the idt into the processor. It is a page in memory
  lidt(idt, 4096);

  return 0;
}

// just forward the trap on to the irq subsystem
// This function is called from arch/x86/trap.asm
extern "C" void trap(reg_t *regs) {
  /**
   * TODO: why do some traps disable interrupts?
   * it seems like its only with irq 32 (ticks)
   *
   * Honestly, I have no idea why this is needed...
   */
  arch::sti();

  auto *tf = (struct x86_64regs*)regs;
  irq::dispatch(tf->trapno, regs);

  irq::eoi(tf->trapno);

  // TODO: generalize
  bool to_userspace = tf->cs == 0x23;
  sched::before_iret(to_userspace);
}
