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
