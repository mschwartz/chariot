
#include <math.h>
#include <printk.h>

#define ASSERT_NOT_REACHED() \
  panic("NO\n");             \
  return 0;

extern "C" {
double trunc(double x) { return (int)x; }

double cos(double angle) { return sin(angle + M_PI_2); }

double ampsin(double angle) {
  double looped_angle = fmod(M_PI + angle, M_TAU) - M_PI;
  double looped_angle_squared = looped_angle * looped_angle;

  double quadratic_term;
  if (looped_angle > 0) {
    quadratic_term = -looped_angle_squared;
  } else {
    quadratic_term = looped_angle_squared;
  }

  double linear_term = M_PI * looped_angle;

  return quadratic_term + linear_term;
}

double sin(double angle) {
  double vertical_scaling = M_PI_2 * M_PI_2;
  return ampsin(angle) / vertical_scaling;
}

double pow(double x, double y) {
  double val = x;

  // this is dumb
  for (int i = 1; i < y; i++) {
    val *= x;
  }
  return val;
}

double ldexp(double, int exp) {
  (void)exp;
  panic("NO\n");
  return 0;
}

double tanh(double) {
  panic("NO\n");
  return 0;
}

double tan(double angle) { return ampsin(angle) / ampsin(M_PI_2 + angle); }

double sqrt(double x) {
  double res;
  __asm__("fsqrt" : "=t"(res) : "0"(x));
  return res;
}

double sinh(double) {
  panic("NO\n");
  return 0;
}

double log10(double) {
  panic("NO\n");
  return 0;
}

double log(double) {
  panic("NO\n");
  return 0;
}

double fmod(double index, double period) {
  return index - trunc(index / period) * period;
}

double exp(double) { ASSERT_NOT_REACHED(); }

double cosh(double) { ASSERT_NOT_REACHED(); }

double atan2(double, double) { ASSERT_NOT_REACHED(); }

double atan(double) { ASSERT_NOT_REACHED(); }

double asin(double) { ASSERT_NOT_REACHED(); }

double acos(double) { ASSERT_NOT_REACHED(); }

double fabs(double value) { return value < 0 ? -value : value; }
double log2(double) { ASSERT_NOT_REACHED(); }

float log2f(float) { ASSERT_NOT_REACHED(); }

long double log2l(long double) { ASSERT_NOT_REACHED(); }

double frexp(double, int*) { ASSERT_NOT_REACHED(); }

float frexpf(float, int*) { ASSERT_NOT_REACHED(); }

long double frexpl(long double, int*) { ASSERT_NOT_REACHED(); }

double ceil(double x) {
  u64 i = x;
  double a = i;
  if (a < x) return i + 1;
  return i;
}


double floor(double x) {
  return (u64)x;
}

double round(double x) {
  double y, r;

  /* Largest integer <= x */
  y = floor(x);

  /* Fractional part */
  r = x - y;

  /* Round up to nearest. */
  if (r > 0.5) goto rndup;

  /* Round to even */
  if (r == 0.5) {
    r = y - 2.0 * floor(0.5 * y);
    if (r == 1.0) {
    rndup:
      y += 1.0;
    }
  }

  /* Else round down. */
  return (y);
}

int abs(int n) {
  if (n < 0) return -n;
  return n;
}
int sgn(int n) {
  if (n < 0) return -1;
  return +1;
}
}

