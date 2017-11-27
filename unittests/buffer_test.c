#include "unit.h"
#include <dst/dst.h>

int main() {
    dst_init();
    DstBuffer *buffer = dst_buffer(100);
    assert(buffer->count == 0);
    assert(buffer->capacity == 100);
    dst_buffer_push_u8(buffer, 'h');
    dst_buffer_push_u8(buffer, 'e');
    dst_buffer_push_u8(buffer, 'l');
    dst_buffer_push_u8(buffer, 'l');
    dst_buffer_push_u8(buffer, 'o');
    dst_buffer_push_cstring(buffer, " world!");
    assert(dst_equals(
        dst_wrap_string(dst_cstring("hello world!")),
        dst_wrap_string(dst_string(buffer->data, buffer->count))
    ));
    return 0;
}
