/**
 * This file implements all the thread:: functions and methods
 */
#include <cpu.h>
#include <mmap_flags.h>
#include <sched.h>
#include <syscall.h>
#include <util.h>

/**
 * A thread needs to bootstrap itself somehow, and it uses this function to do
 * so. Both kinds of threads (user and kernel) start by executing this function.
 * User threads will iret to userspace, and kernel threads will simply call it's
 * function then exit when the function returns
 */
static void thread_create_callback(void *);
// implemented in arch/$ARCH/trap.asm most likely
extern "C" void trapret(void);

static rwlock thread_table_lock;
static map<pid_t, struct thread *> thread_table;

thread::thread(pid_t tid, struct process &proc) : proc(proc) {
  this->tid = tid;

  this->pid = proc.pid;

  fpu.initialized = false;
  fpu.state = kmalloc(512);

  sched.priority = PRIORITY_HIGH;

  stack_size = PGSIZE * 16;
  stack = kmalloc(stack_size);

  auto sp = (off_t)stack + stack_size;

  // get a pointer to the trapframe on the stack
  sp -= arch::trapframe_size();
  trap_frame = (reg_t *)sp;

  // 'return' to trapret()
  sp -= sizeof(void *);
  *(void **)sp = (void *)trapret;

  // initial kernel context
  sp -= sizeof(struct thread_context);
  kern_context = (struct thread_context *)sp;
  memset(kern_context, 0, sizeof(struct thread_context));

  state = PS_EMBRYO;

  arch::reg(REG_SP, trap_frame) = sp;
  arch::reg(REG_PC, trap_frame) = -1;

  // set the initial context to the creation boostrap function
  kern_context->eip = (u64)thread_create_callback;

  arch::initialize_trapframe(proc.ring == RING_USER, trap_frame);

  thread_table_lock.write_lock();
  assert(!thread_table.contains(tid));
  // printk("inserting %d\n", tid);
  thread_table.set(tid, this);
  thread_table_lock.write_unlock();

  // push the tid into the proc's tid list
  proc.threads.push(tid);
}

bool thread::kickoff(void *rip, int initial_state) {
  arch::reg(REG_PC, trap_frame) = (unsigned long)rip;

  this->state = initial_state;

  sched::add_task(this);
  return true;
}

thread::~thread(void) {
  sched::remove_task(this);
  kfree(stack);
  kfree(fpu.state);
}

bool thread::awaken(bool rudely) {
  if (!(wq.flags & WAIT_NOINT)) {
    if (rudely) {
      return false;
    }
  }

  // TODO: this should be more complex
  wq.rudely_awoken = rudely;

  // fix up the wq double linked list
  wq.current_wq = NULL;

  assert(state == PS_BLOCKED);
  state = PS_RUNNABLE;

  return true;
}

static void thread_create_callback(void *) {
  auto thd = curthd;

  auto tf = thd->trap_frame;

  if (thd->proc.ring == RING_KERN) {
    using fn_t = int (*)(void *);
    auto fn = (fn_t)arch::reg(REG_PC, tf);
    cpu::popcli();
    // run the kernel thread
    int res = fn(NULL);
    // exit the thread with the return code of the func
    sys::exit_thread(res);
  } else {
    if (thd->pid == thd->tid) {
      auto sp = arch::reg(REG_SP, tf);
#define round_up(x, y) (((x) + (y)-1) & ~((y)-1))
#define STACK_ALLOC(T, n)               \
  ({                                    \
    sp -= round_up(sizeof(T) * (n), 8); \
    (T *)(void *) sp;                   \
  })
      auto argc = (u64)thd->proc.args.size();

      size_t sz = 0;
      sz += thd->proc.args.size() * sizeof(char *);
      for (auto &a : thd->proc.args) sz += a.size() + 1;

      auto region = (void *)STACK_ALLOC(char, sz);

      auto argv = (char **)region;

      auto *arg = (char *)((char **)argv + argc);

      int i = 0;
      for (auto &a : thd->proc.args) {
        int len = a.size() + 1;
        memcpy(arg, a.get(), len);
        argv[i++] = arg;
        arg += len;
      }

      auto envc = thd->proc.env.size();
      auto envp = STACK_ALLOC(char *, envc + 1);

      for (int i = 0; i < envc; i++) {
        auto &e = thd->proc.env[i];
        envp[i] = STACK_ALLOC(char, e.len() + 1);
        memcpy(envp[i], e.get(), e.len() + 1);
      }

      envp[envc] = NULL;

      // align the stack to 16 bytes. (this is what intel wants, so it what I
      // will give them)
      arch::reg(REG_SP, tf) = sp & ~0xF;
      tf[1] = argc;
      tf[2] = (unsigned long)argv;
      tf[3] = (unsigned long)envp;
    }
    cpu::popcli();
    return;
  }

  sys::exit_proc(-1);
  while (1) {
  }
}

struct thread *thread::lookup(pid_t tid) {
  thread_table_lock.read_lock();
  assert(thread_table.contains(tid));
  auto t = thread_table.get(tid);
  thread_table_lock.read_unlock();
  return t;
}

bool thread::teardown(thread *t) {
  thread_table_lock.write_lock();
  assert(thread_table.contains(t->tid));
  thread_table.remove(t->tid);
  thread_table_lock.write_unlock();

  delete t;
  return true;
}
