#ifndef VM_H_C4OZU8CQ
#define VM_H_C4OZU8CQ

#include "datatypes.h"

/* Exit from the VM normally */
#define VMExit(vm, r) ((vm)->ret = (r), longjmp((vm)->jump, 1))

/* Bail from the VM with an error. */
#define VMError(vm, e) ((vm)->error = (e), longjmp((vm)->jump, 2))

/* Crash. Not catchable, unlike error. */
#define VMCrash(vm, e) ((vm)->error = (e), longjmp((vm)->jump, 3))

/* Error if the condition is false */
#define VMAssert(vm, cond, e) do \
    { if (!(cond)) { VMError((vm), (e)); } } while (0)

/* Type assertion */
#define VMAssertType(vm, f, t) \
    VMAssert((vm), (f).type == (t), "Expected type,")

/* Initialize the VM */
void VMInit(VM * vm);

/* Deinitialize the VM */
void VMDeinit(VM * vm);

/* Load a function to be run on the VM */
void VMLoad(VM * vm, Value func);

/* Start running the VM */
int VMStart(VM * vm);

/* Run garbage collection */
void VMCollect(VM * vm);

/* Collect garbage if enough memory has been allocated since
 * the previous collection */
void VMMaybeCollect(VM * vm);

/* Allocate memory */
void * VMAlloc(VM * vm, uint32_t amount);

/* Allocate zeroed memory */
void * VMZalloc(VM * vm, uint32_t amount);

/* Get an argument from the stack */
Value VMGetArg(VM * vm, uint16_t index);

/* Put a value on the stack */
void VMSetArg(VM * vm, uint16_t index, Value x);

/* Get the number of arguments on the stack */
uint16_t VMCountArgs(VM * vm);

#endif /* end of include guard: VM_H_C4OZU8CQ */
