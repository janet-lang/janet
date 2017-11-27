#include <dst/dst.h>
#include <stdbool.h>
#include <math.h>
#include "unit.h"

/* Required interface for DstValue */
/* wrap and unwrap for all types */
/* Get type quickly */
/* Check against type quickly */
/* Small footprint */
/* 64 bit integer support undecided */

/* dst_type(x)
 * dst_checktype(x, t)
 * dst_wrap_##TYPE(x)
 * dst_unwrap_##TYPE(x)
 * dst_truthy(x)
 * dst_memclear(p, n) - clear memory for hash tables to nils
 * dst_isnan(x) - check if value is nan
 */

typedef union dst_t dst_t;
union dst_t {
    uint64_t u64;
    int64_t i64;
    void *pointer;
    double real;
};

/* dst_t */

/* This representation uses 48 bit pointers. The trade off vs. the LuaJIT style
 * 47 bit payload representaion is that the type bits are no long contiguous. Type
 * checking can still be fast, but typewise polymorphism takes a bit longer. However, 
 * hopefully we can avoid some annoying problems that occur when trying to use 47 bit pointers
 * in a 48 bit address space (Linux on ARM) */

enum dst_t_tag {
    DST_T_NIL,
    DST_T_TRUE,
    DST_T_FALSE,
    DST_T_INTEGER,
    DST_T_FIBER,
    DST_T_STRUCT,
    DST_T_TUPLE,
    DST_T_ARRAY,
    DST_T_BUFFER,
    DST_T_TABLE,
    DST_T_USERDATA,
    DST_T_FUNCTION,
    DST_T_CFUNCTION,
    DST_T_STRING,
    DST_T_SYMBOL,
    DST_T_REAL
};

/*                    |.......Tag.......|.......................Payload..................| */
/* Non-double:        t|11111111111|1ttt|xxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx */
/* Types of NIL, TRUE, and FALSE must have payload set to all 1s. */

/* Double (no NaNs):   x xxxxxxxxxxx xxxx xxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx */

/* A simple scheme for nan boxed values */
/* normal doubles, denormalized doubles, and infinities are doubles */
/* Quiet nan is nil. Sign bit should be 0. */

#define DST_NANBOX_TYPEBITS    0x0007000000000000lu

#ifdef DST_64
#define DST_NANBOX_POINTERBITS 0x0000FFFFFFFFFFFFlu
#else
#define DST_NANBOX_POINTERBITS 0x00000000FFFFFFFFlu
#endif

#define DST_NANBOX_QUIET_BIT   0x0008000000000000lu

#define dst_nanbox_isreal(x) (!isnan((x).real))

#define dst_nanbox_tag(type) \
    ((((uint64_t)(type) & 0x8) << 12) | 0x7FF8 | (type))

#define dst_nanbox_tagbits(x) \
    ((x).u64 & 0xFFFF000000000000lu)

#define dst_nanbox_payloadbits(x) \
    ((x).u64 & 0x0000FFFFFFFFFFFFlu)

#define dst_nanbox_type(x) \
    (dst_nanbox_isreal(x) \
        ? DST_T_REAL \
        : (((x).u64 & DST_NANBOX_TYPEBITS) >> 48) | (((x).u64 >> 60) & 0x8))

#define dst_nanbox_checktype(x, t) \
    (((t) == DST_T_REAL) \
        ? dst_nanbox_isreal(x) \
        : (!dst_nanbox_isreal(x) && (((x).u64 >> 48) == dst_nanbox_tag(t))))

static inline void *dst_nanbox_to_pointer(dst_t x) {
    /* We need to do this shift to keep the higher bits of the pointer
     * the same as bit 47 as required by the x86 architecture. We may save
     * an instruction if we do x.u64 & DST_NANBOX_POINTERBITS, but this 0s
     * th high bits. */
    x.i64 = (x.i64 << 16) >> 16;
    return x.pointer;
}

static inline dst_t dst_nanbox_from_pointer(void *p, uint64_t tagmask) {
    dst_t ret;
    ret.pointer = p;
    ret.u64 &= DST_NANBOX_POINTERBITS;
    ret.u64 |= tagmask;
    return ret;
}

static inline dst_t dst_nanbox_from_double(double d) {
    dst_t ret;
    ret.real = d;
    /* Normalize NaNs to nil */
    if (d != d)
        ret.u64 = dst_nanbox_tag(DST_T_NIL) << 48;
    return ret;
}

static inline dst_t dst_nanbox_from_bits(uint64_t bits) {
    dst_t ret;
    ret.u64 = bits;
    return ret;
}

#define dst_nanbox_truthy(x) \
    (~(dst_nanbox_checktype((x), DST_NIL) || dst_nanbox_checktype((x), DST_FALSE)))

#define dst_nanbox_from_payload(t, p) \
    dst_nanbox_from_bits((dst_nanbox_tag(t) << 48) | (p))

#define dst_nanbox_wrap_(p, t) \
    dst_nanbox_from_pointer((p), (dst_nanbox_tag(t) << 48) | 0x7FF8000000000000lu)

/* Wrap the simple types */
#define dst_nanbox_wrap_nil() dst_nanbox_from_payload(DST_T_NIL, 0xFFFFFFFFFFFFlu)
#define dst_nanbox_wrap_true() dst_nanbox_from_payload(DST_T_TRUE, 0xFFFFFFFFFFFFlu)
#define dst_nanbox_wrap_false() dst_nanbox_from_payload(DST_T_FALSE, 0xFFFFFFFFFFFFlu)
#define dst_nanbox_wrap_boolean(b) dst_nanbox_from_payload((b) ? DST_T_TRUE : DST_T_FALSE, 0xFFFFFFFFFFFFlu)
#define dst_nanbox_wrap_integer(i) dst_nanbox_from_payload(DST_T_INTEGER, (uint32_t)(i))
#define dst_nanbox_wrap_real(r) dst_nanbox_from_double(r)

#define dst_nanbox_unwrap_boolean(x) \
    (((x).u64 >> 48) == dst_nanbox_tag(DST_T_TRUE))

#define dst_nanbox_unwrap_integer(x) \
    ((int32_t)((x).u64 & 0xFFFFFFFFlu))
    
#define dst_nanbox_unwrap_real(x) ((x).real)

/* Wrap the pointer types */
#define dst_nanbox_wrap_struct(s) dst_nanbox_wrap_((s), DST_T_STRUCT)
#define dst_nanbox_wrap_tuple(s) dst_nanbox_wrap_((s), DST_T_TUPLE)
#define dst_nanbox_wrap_fiber(s) dst_nanbox_wrap_((s), DST_T_FIBER)
#define dst_nanbox_wrap_array(s) dst_nanbox_wrap_((s), DST_T_ARRAY)
#define dst_nanbox_wrap_table(s) dst_nanbox_wrap_((s), DST_T_TABLE)
#define dst_nanbox_wrap_buffer(s) dst_nanbox_wrap_((s), DST_T_BUFFER)
#define dst_nanbox_wrap_string(s) dst_nanbox_wrap_((s), DST_T_STRING)
#define dst_nanbox_wrap_symbol(s) dst_nanbox_wrap_((s), DST_T_SYMBOL)
#define dst_nanbox_wrap_userdata(s) dst_nanbox_wrap_((s), DST_T_USERDATA)
#define dst_nanbox_wrap_function(s) dst_nanbox_wrap_((s), DST_T_FUNCTION)
#define dst_nanbox_wrap_cfunction(s) dst_nanbox_wrap_((s), DST_T_CFUNCTION)

/* Unwrap the pointer types */
#define dst_nanbox_unwrap_struct(x) ((const DstValue *)dst_nanbox_to_pointer(x))
#define dst_nanbox_unwrap_tuple(x) ((const DstValue *)dst_nanbox_to_pointer(x))
#define dst_nanbox_unwrap_fiber(x) ((DstFiber *)dst_nanbox_to_pointer(x))
#define dst_nanbox_unwrap_array(x) ((DstArray *)dst_nanbox_to_pointer(x))
#define dst_nanbox_unwrap_table(x) ((DstTable *)dst_nanbox_to_pointer(x))
#define dst_nanbox_unwrap_buffer(x) ((DstBuffer *)dst_nanbox_to_pointer(x))
#define dst_nanbox_unwrap_string(x) ((const uint8_t *)dst_nanbox_to_pointer(x))
#define dst_nanbox_unwrap_symbol(x) ((const uint8_t *)dst_nanbox_to_pointer(x))
#define dst_nanbox_unwrap_userdata(x) (dst_nanbox_to_pointer(x))
#define dst_nanbox_unwrap_function(x) ((DstFunction *)dst_nanbox_to_pointer(x))
#define dst_nanbox_unwrap_cfunction(x) ((DstCFunction)dst_nanbox_to_pointer(x))

void dst_nanbox_print(dst_t x) {
    assert(dst_nanbox_checktype(x, dst_nanbox_type(x)));
    printf("hex: 0x%llx, "
           "description: ", x.u64);
    switch (dst_nanbox_type(x)) {
        case DST_T_NIL:
            printf("nil\n");
            break;
        case DST_T_TRUE:
            printf("true\n");
            break;
        case DST_T_FALSE:
            printf("false\n");
            break;
        case DST_T_INTEGER:
            printf("%dI\n", dst_nanbox_unwrap_integer(x));
            break;
        case DST_T_STRUCT:
            printf("<struct %p>\n", dst_nanbox_unwrap_struct(x));
            break;
        case DST_T_TUPLE:
            printf("<tuple %p>\n", dst_nanbox_unwrap_tuple(x));
            break;
        case DST_T_FIBER:
            printf("<fiber %p>\n", dst_nanbox_unwrap_fiber(x));
            break;
        case DST_T_ARRAY:
            printf("<array %p>\n", dst_nanbox_unwrap_array(x));
            break;
        case DST_T_TABLE:
            printf("<table %p>\n", dst_nanbox_unwrap_table(x));
            break;
        case DST_T_STRING:
            printf("<string %p>\n", dst_nanbox_unwrap_string(x));
            break;
        case DST_T_SYMBOL:
            printf("<symbol %p>\n", dst_nanbox_unwrap_symbol(x));
            break;
        case DST_T_USERDATA:
            printf("<userdata %p>\n", dst_nanbox_unwrap_userdata(x));
            break;
        case DST_T_FUNCTION:
            printf("<function %p>\n", dst_nanbox_unwrap_function(x));
            break;
        case DST_T_CFUNCTION:
            printf("<cfunction %p>\n", dst_nanbox_unwrap_cfunction(x));
            break;
        case DST_T_BUFFER:
            printf("<buffer %p>\n", dst_nanbox_unwrap_buffer(x));
            break;
        default:
            printf("unknown type 0x%llu\n", dst_nanbox_type(x));
        case DST_T_REAL:
            printf("%.21g\n", dst_nanbox_unwrap_real(x));
            break;
    }
}

int main() {
    printf("--- nan box test ---\n");
    printf("sizeof(dst_t) = %lu\n", sizeof(dst_t));

    DstArray array;

    dst_nanbox_print(dst_nanbox_wrap_real(0.125));     
    dst_nanbox_print(dst_nanbox_wrap_real(19236910.125));     
    dst_nanbox_print(dst_nanbox_wrap_real(123120.125));     
    dst_nanbox_print(dst_nanbox_wrap_real(0.0));     
    dst_nanbox_print(dst_nanbox_wrap_real(1.0/0.0));     
    dst_nanbox_print(dst_nanbox_wrap_real(1.0/-0.0));     
    dst_nanbox_print(dst_nanbox_wrap_real(0.0/-0.0));     
    dst_nanbox_print(dst_nanbox_wrap_real(0.0/0.0));     
    dst_nanbox_print(dst_nanbox_wrap_true());     
    dst_nanbox_print(dst_nanbox_wrap_false());     
    dst_nanbox_print(dst_nanbox_wrap_integer(123));     
    dst_nanbox_print(dst_nanbox_wrap_integer(-123));     
    dst_nanbox_print(dst_nanbox_wrap_integer(0));     

    dst_nanbox_print(dst_nanbox_wrap_array(&array));
    dst_nanbox_print(dst_nanbox_wrap_table(&array));
    dst_nanbox_print(dst_nanbox_wrap_string(&array));
    dst_nanbox_print(dst_nanbox_wrap_buffer(&array));

    printf("--- nan box test end ---\n");
}
