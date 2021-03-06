#pragma once

#include <lock.h>
#include <sched.h>
#include <types.h>

struct cpu_t {
  void *local;
  int ncli;
  size_t ticks;

  uint16_t preemption_depth;

  u32 speed_khz;
  struct thread *current_thread;
  struct thread_context *sched_ctx;
};

extern int cpunum;
extern cpu_t cpus[16];

// Nice macros to allow cleaner access to the current task and proc
#define curthd cpu::thread()
#define curproc cpu::proc()

namespace cpu {

// return the current cpu struct
cpu_t *get(void);

struct process *proc(void);

struct thread *thread(void);
bool in_thread(void);

void calc_speed_khz(void);

void pushcli();
void popcli();

void preempt_enable(void);
void preempt_disable(void);
void preempt_reset(void);
int preempt_disabled(void);

/**
 * These functions are all implemented in the arch directory
 */
cpu_t &current(void);
// setup CPU segment descriptors, run once per cpu
void seginit(void *local = nullptr);
void switch_vm(struct thread *);

inline int ncli(void) { return current().ncli; }
static inline u64 get_ticks(void) { return current().ticks; }

}  // namespace cpu
