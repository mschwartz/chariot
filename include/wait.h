#pragma once

#include <lock.h>
#include <single_list.h>
#include <types.h>


#define WAIT_NOINT 1
/**
 * implemented in sched.cpp
 */
class waitqueue {
 public:
  waitqueue(const char * = "generic_waitqueue");

  // wait on the queue, interruptable. Returns if it was interrupted or not
  int wait(u32 on = 0);

  // wait, but not interruptable
  void wait_noint(u32 on = 0);
  void notify();

  bool should_notify(u32 val);

 private:

  int do_wait(u32 on, int flags);
  // navail is the number of unhandled notifications
  int navail = 0;
  const char *name;

  spinlock lock = spinlock("waitqueue.lock");

  // next to pop on notify
  struct task *front = NULL;
  // where to put new tasks
  struct task *back = NULL;
};
