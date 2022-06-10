#include <stdio.h>
#include <stdint.h>
#include <string.h>

int int_fn(int a, int b) {
    return (a << 2) + b;
}

double my_fn(int64_t a, int64_t b, const char *x) {
    return (double)(a + b) + 0.5 + strlen(x);
}

double double_fn(double x, double y, double z) {
    return (x + y) * z * 3;
}

double double_many(double x, double y, double z, double w, double a, double b) {
    return x + y + z + w + a + b;
}

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

int intint_fn(double x, intint ii) {
    printf("double: %g\n", x);
    return ii.a + ii.b;
}

int intintint_fn(double x, intintint iii) {
    printf("double: %g\n", x);
    return iii.a + iii.b + iii.c;
}

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

big struct_big(int i, double d) {
    big ret;
    ret.a = i;
    ret.b = (int64_t) d;
    ret.c = ret.a + ret.b + 1000;
    return ret;
}

void void_fn(void) {
    printf("void fn ran\n");
}

void void_ret_fn(int x) {
    printf("void fn ran: %d\n", x);
}
