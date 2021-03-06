#include "glue.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void *__dso_handle;
unsigned __atexit_func_count = 0;
atexit_func_entry_t __atexit_funcs[ATEXIT_MAX_FUNCS];

template <typename... T>
inline void do_panic(const char *fmt, T &&... args) {
  // disable interrupts
  printf(fmt, args...);
  while (1) {
    exit(-1);
  }
}

#define panic(fmt, args...)                     \
  do {                                          \
    printf("PANIC: %s\n", __PRETTY_FUNCTION__); \
    do_panic(fmt, ##args);                      \
  } while (0);

#define BAD() panic("Undefined C++ function (%s)\n", __func__);

// Called when a pure virtual function call is attempted
void __cxa_pure_virtual(void) { BAD(); }

int __cxa_atexit(void (*destructor)(void *), void *arg, void *dso_handle) {
  if (__atexit_func_count >= ATEXIT_MAX_FUNCS) {
    return -1;
  }

  __atexit_funcs[__atexit_func_count].destructor_func = destructor;
  __atexit_funcs[__atexit_func_count].obj_ptr = arg;
  __atexit_funcs[__atexit_func_count].dso_handle = dso_handle;
  __atexit_func_count++;
  return 0;
}

void __cxa_finalize(void *f) {
  unsigned i = __atexit_func_count;
  /* "If f is NULL, it shall call all the termination funtions." */
  if (!f) {
    while (i--) {
      if (__atexit_funcs[i].destructor_func) {
        (*__atexit_funcs[i].destructor_func)(__atexit_funcs[i].obj_ptr);
      }
    }
  } else {
    for (; i != 0; --i) {
      if (__atexit_funcs[i].destructor_func == f) {
        (*__atexit_funcs[i].destructor_func)(__atexit_funcs[i].obj_ptr);
        __atexit_funcs[i].destructor_func = NULL;
      }
    }
  }
}

// Not really needed since we will compile with -fno-rtti and -fno-exceptions
void _Unwind_Resume(void) { BAD(); }

extern "C" _Unwind_Reason_Code _Unwind_Resume_or_Rethrow(
    struct _Unwind_Exception *exception_object) {
  BAD();
  return _URC_NO_REASON;
}

extern "C" void _Unwind_SetGR(struct _Unwind_Context *context, int index,
                              uint64_t new_value) {
  BAD();
}

extern "C" uint64_t _Unwind_GetIP(struct _Unwind_Context *context) {
  BAD();
  return 0;
}

extern "C" void _Unwind_SetIP(struct _Unwind_Context *context,
                              uint64_t new_value) {
  BAD();
}

extern "C" uint64_t _Unwind_GetIPInfo(struct _Unwind_Context *context,
                                      int *ip_before_insn) {
  BAD();
  return 0;
}

extern "C" _Unwind_Reason_Code _Unwind_RaiseException(
    struct _Unwind_Exception *exception_object) {
  BAD();
  return _URC_NO_REASON;
}

extern "C" void _Unwind_DeleteException(
    struct _Unwind_Exception *exception_object) {
  BAD();
}

extern "C" uint64_t _Unwind_GetLanguageSpecificData(
    struct _Unwind_Context *context) {
  BAD();
  return 0;
}

extern "C" uint64_t _Unwind_GetRegionStart(struct _Unwind_Context *context) {
  BAD();
  return 0;
}

extern "C" uint64_t _Unwind_GetTextRelBase(struct _Unwind_Context *context) {
  BAD();
  return 0;
}

extern "C" uint64_t _Unwind_GetDataRelBase(struct _Unwind_Context *context) {
  BAD();
  return 0;
}
/*

void *operator new(size_t size) { return malloc(size); }

void *operator new[](size_t size) { return malloc(size); }

template <typename T>
void *operator new(size_t size, T *&dst) {
  dst = malloc(size);
  return dst;
}

void operator delete(void *ptr) { free(ptr); }

void operator delete[](void *ptr) { free(ptr); }

void operator delete(void *ptr, size_t s) { free(ptr); }

void operator delete[](void *ptr, size_t s) { free(ptr); }

extern "C" void __stack_chk_fail(void) { panic("stack pointer smashed!\n"); }

*/
/*
namespace std {

    void __throw_bad_alloc(void) {
        BAD();
    }

    void __throw_length_error(char const * c) {
        BAD();
    }
}
*/

extern "C" void __cxa_guard_acquire(void *p) {
  // printk("a: %p\n", p);
}

extern "C" void __cxa_guard_release(void *p) {
  // printk("r: %p\n", p);
}
