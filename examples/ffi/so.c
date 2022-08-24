#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#define EXPORTER __declspec(dllexport)
#else
#define EXPORTER
#endif

EXPORTER
int int_fn(int a, int b) {
    return (a << 2) + b;
}

EXPORTER
double my_fn(int64_t a, int64_t b, const char *x) {
    return (double)(a + b) + 0.5 + strlen(x);
}

EXPORTER
double double_fn(double x, double y, double z) {
    return (x + y) * z * 3;
}

EXPORTER
double double_many(double x, double y, double z, double w, double a, double b) {
    return x + y + z + w + a + b;
}

EXPORTER
double double_lots(
    double a,
    double b,
    double c,
    double d,
    double e,
    double f,
    double g,
    double h,
    double i,
    double j) {
    return i + j;
}


EXPORTER
double double_lots_2(
    double a,
    double b,
    double c,
    double d,
    double e,
    double f,
    double g,
    double h,
    double i,
    double j) {
    return a +
           10.0 * b +
           100.0 * c +
           1000.0 * d +
           10000.0 * e +
           100000.0 * f +
           1000000.0 * g +
           10000000.0 * h +
           100000000.0 * i +
           1000000000.0 * j;
}

EXPORTER
double float_fn(float x, float y, float z) {
    return (x + y) * z;
}

typedef struct {
    int a;
    int b;
} intint;

typedef struct {
    int a;
    int b;
    int c;
} intintint;

EXPORTER
int intint_fn(double x, intint ii) {
    printf("double: %g\n", x);
    return ii.a + ii.b;
}

EXPORTER
int intintint_fn(double x, intintint iii) {
    printf("double: %g\n", x);
    return iii.a + iii.b + iii.c;
}

EXPORTER
intint return_struct(int i) {
    intint ret;
    ret.a = i;
    ret.b = i * i;
    return ret;
}

typedef struct {
    int64_t a;
    int64_t b;
    int64_t c;
} big;

EXPORTER
big struct_big(int i, double d) {
    big ret;
    ret.a = i;
    ret.b = (int64_t) d;
    ret.c = ret.a + ret.b + 1000;
    return ret;
}

EXPORTER
void void_fn(void) {
    printf("void fn ran\n");
}

EXPORTER
void void_ret_fn(int x) {
    printf("void fn ran: %d\n", x);
}
