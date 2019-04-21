#include <stdlib.h>
#include <janet.h>

typedef struct {
    double *data;
    size_t size;
} num_array;

static num_array *num_array_init(num_array *array, size_t size) {
    array->data = (double *)calloc(size, sizeof(double));
    array->size = size;
    return array;
}

static void num_array_deinit(num_array *array) {
    free(array->data);
}

static int num_array_gc(void *p, size_t s) {
    (void) s;
    num_array *array = (num_array *)p;
    num_array_deinit(array);
    return 0;
}

Janet num_array_get(void *p, Janet key);
void num_array_put(void *p, Janet key, Janet value);

static const JanetAbstractType num_array_type = {
    "numarray",
    num_array_gc,
    NULL,
    num_array_get,
    num_array_put
};

static Janet num_array_new(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    int32_t size = janet_getinteger(argv, 0);
    num_array *array = (num_array *)janet_abstract(&num_array_type, sizeof(num_array));
    num_array_init(array, size);
    return janet_wrap_abstract(array);
}

static Janet num_array_scale(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    num_array *array = (num_array *)janet_getabstract(argv, 0, &num_array_type);
    double factor = janet_getnumber(argv, 1);
    size_t i;
    for (i = 0; i < array->size; i++) {
        array->data[i] *= factor;
    }
    return argv[0];
}

static Janet num_array_sum(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    num_array *array = (num_array *)janet_getabstract(argv, 0, &num_array_type);
    double sum = 0;
    for (size_t i = 0; i < array->size; i++) sum += array->data[i];
    return janet_wrap_number(sum);
}

void num_array_put(void *p, Janet key, Janet value) {
    size_t index;
    num_array *array = (num_array *)p;
    if (!janet_checkint(key))
        janet_panic("expected integer key");
    if (!janet_checktype(value, JANET_NUMBER))
        janet_panic("expected number value");

    index = (size_t)janet_unwrap_integer(key);
    if (index < array->size) {
        array->data[index] = janet_unwrap_number(value);
    }
}

static const JanetMethod methods[] = {
    {"scale", num_array_scale},
    {"sum", num_array_sum},
    {NULL, NULL}
};

Janet num_array_get(void *p, Janet key) {
    size_t index;
    Janet value;
    num_array *array = (num_array *)p;
    if (janet_checktype(key, JANET_KEYWORD))
        return janet_getmethod(janet_unwrap_keyword(key), methods);
    if (!janet_checkint(key))
        janet_panic("expected integer key");
    index = (size_t)janet_unwrap_integer(key);
    if (index >= array->size) {
        value = janet_wrap_nil();
    } else {
        value = janet_wrap_number(array->data[index]);
    }
    return value;
}

static const JanetReg cfuns[] = {
    {
        "new", num_array_new,
        "(numarray/new size)\n\n"
        "Create new numarray"
    },
    {
        "scale", num_array_scale,
        "(numarray/scale numarray factor)\n\n"
        "scale numarray by factor"
    },
    {NULL, NULL, NULL}
};

JANET_MODULE_ENTRY(JanetTable *env) {
    janet_cfuns(env, "numarray", cfuns);
}
