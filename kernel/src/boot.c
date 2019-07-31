#define __packed __attribute__((packed))
#define __align(n) __attribute__((aligned(n)))

#include <asm.h>
#include <idt.h>
#include <mem.h>
#include <paging.h>
#include <printk.h>
#include <serial.h>
#include <types.h>
#include <posix.h>

extern int kernel_end;

u64 fib(u64 n) {
  if (n < 2) return 1;

  return fib(n - 1) + fib(n - 2);
}

static u64 strlen(char *str) {
  u64 i = 0;
  for (i = 0; str[i] != '\0'; i++)
    ;
  return i;
}


// in src/arch/x86/sse.asm
extern void enable_sse();

int kmain(void) {
  serial_install();
  init_idt();

  // at this point, we are still mapped with the 2mb large pages.
  // init_mem will replace this with a more fine-grained 4k page system by
  // mapping kernel memory 1:1
  init_mem();

  // now that we have interupts working, enable sse! (fpu)
  enable_sse();

  // finally, enable interrupts
  sti();

  printk("hello, from the kernel\n");


  // simply hltspin
  while (1) halt();
  return 0;
}

