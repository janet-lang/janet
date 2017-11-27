#include "unit.h"
#include <dst/dst.h>


int main() {
    int64_t i;
    dst_init();
    DstArray *array = dst_array(10);
    assert(array->capacity == 10);
    assert(array->count == 0);
    for (i = 0; i < 500; ++i)
        dst_array_push(array, dst_wrap_integer(i));
    for (i = 0; i < 500; ++i)
        assert(array->data[i].type == DST_INTEGER && array->data[i].as.integer == i);
    for (i = 0; i < 200; ++i)
        dst_array_pop(array);
    assert(array->count == 300);
    return 0;
}
