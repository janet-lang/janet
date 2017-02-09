#ifndef BUFFER_H_OEA9T4DJ
#define BUFFER_H_OEA9T4DJ

#include "datatypes.h"

void BufferInit(GC * gc, Buffer * buffer, uint32_t capacity);

Buffer * BufferNew(GC * gc, uint32_t capacity);

void BufferEnsure(GC * gc, Buffer * buffer, uint32_t capacity);

int32_t BufferGet(Buffer * buffer, uint32_t index);

void BufferPush(GC * gc, Buffer * buffer, uint8_t c);

void BufferAppendData(GC * gc, Buffer * buffer, uint8_t * string, uint32_t length);

uint8_t * BufferToString(GC * gc, Buffer * buffer);

#define BufferDefine(name, type) \
static void BufferPush##name (GC * gc, Buffer * buffer, type x) { \
    union { type t; uint8_t bytes[sizeof(type)]; } u; \
    u.t = x; return BufferAppendData(gc, buffer, u.bytes, sizeof(type)); \
}

#endif /* end of include guard: BUFFER_H_OEA9T4DJ */
