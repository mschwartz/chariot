#include <errno.h>
#include <stdarg.h>
#include <sys/syscall.h>

extern long __syscall(int num, unsigned long long a, unsigned long long b,
                      unsigned long long c, unsigned long long d,
                      unsigned long long e, unsigned long long f);

long syscall(long num, ...) {
  va_list ap;
  unsigned long long a, b, c, d, e, f;
  va_start(ap, num);
  a = va_arg(ap, unsigned long long);
  b = va_arg(ap, unsigned long long);
  c = va_arg(ap, unsigned long long);
  d = va_arg(ap, unsigned long long);
  e = va_arg(ap, unsigned long long);
  f = va_arg(ap, unsigned long long);
  va_end(ap);
  return __syscall(num, a, b, c, d, e, f);
}

long errno_syscall(long num, ...) {
  va_list ap;
  unsigned long long a, b, c, d, e, f;
  va_start(ap, num);
  a = va_arg(ap, unsigned long long);
  b = va_arg(ap, unsigned long long);
  c = va_arg(ap, unsigned long long);
  d = va_arg(ap, unsigned long long);
  e = va_arg(ap, unsigned long long);
  f = va_arg(ap, unsigned long long);
  va_end(ap);
  return __syscall_ret(__syscall(num, a, b, c, d, e, f));
}

long __syscall_ret(long r) {
  if (r < 0) {
    errno = -r;
    return -1;
  }
  return r;
}
