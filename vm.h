#ifndef VM_H_C4OZU8CQ
#define VM_H_C4OZU8CQ

#include "datatypes.h"

/* Exit from the VM normally */
#define VMExit(vm, r) ((vm)->tempRoot = (r), longjmp((vm)->jump, 1))

/* Bail from the VM with an error. */
#define VMError(vm, e) ((vm)->error = (e), longjmp((vm)->jump, 2))

/* Crash. Not catchable, unlike error. */
#define VMCrash(vm, e) ((vm)->error = (e), longjmp((vm)->jump, 3))

/* Error if the condition is false */
#define VMAssert(vm, cond, e) do \
    { if (!(cond)) { VMError((vm), (e)); } } while (0)

/* Initialize the VM */
void VMInit(VM * vm);

/* Deinitialize the VM */
void VMDeinit(VM * vm);

/* Load a function to be run on the VM */
void VMLoad(VM * vm, Func * func);

/* Start running the VM */
int VMStart(VM * vm);

/* Get the result after VMStart returns */
#define VMResult(vm) ((vm)->tempRoot)

/* Run garbage collection */
void VMCollect(VM * vm);

/* Collect garbage if enough memory has been allocated since
 * the previous collection */
void VMMaybeCollect(VM * vm);

/* Allocate memory */
void * VMAlloc(VM * vm, uint32_t amount);

/* Allocate zeroed memory */
void * VMZalloc(VM * vm, uint32_t amount);

#endif /* end of include guard: VM_H_C4OZU8CQ */
