#ifndef VM_H_C4OZU8CQ
#define VM_H_C4OZU8CQ

#include "datatypes.h"
#include "value.h"

/* Initialize the VM */
void gst_init(Gst * vm);

/* Deinitialize the VM */
void gst_deinit(Gst * vm);

/* Start running the VM with a given entry point */
int gst_run(Gst *vm, GstValue func);

/* Start running the VM from where it left off. */
int gst_continue(Gst *vm);

/* Call a gst value */
int gst_call(Gst *vm, GstValue callee, uint32_t arity, GstValue *args);

/* Run garbage collection */
void gst_collect(Gst * vm);

/* Collect garbage if enough memory has been allocated since
 * the previous collection */
void gst_maybe_collect(Gst * vm);

/* Allocate memory */
void * gst_alloc(Gst * vm, uint32_t amount);

/* Allocate zeroed memory */
void * gst_zalloc(Gst * vm, uint32_t amount);

/* Get an argument from the stack */
GstValue gst_arg(Gst * vm, uint16_t index);

/* Put a value on the stack */
void gst_set_arg(Gst * vm, uint16_t index, GstValue x);

/* Get the number of arguments on the stack */
uint16_t gst_count_args(Gst * vm);

#endif /* end of include guard: VM_H_C4OZU8CQ */
