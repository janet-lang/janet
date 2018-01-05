#include "unit.h"
#include "../core/gc.h"
#include <dst/dst.h>
#include <stdio.h>

/* Create dud funcdef and function */
static DstFunction *dud_func(uint32_t slotcount, uint32_t arity, int varargs) {
    DstFuncDef *def = dst_gcalloc(DST_MEMORY_FUNCDEF, sizeof(DstFuncDef));
    def->environments_length = 0;
    def->constants_length = 0;
    def->bytecode_length = 0;
    def->environments = NULL;
    def->constants = NULL;
    def->bytecode = NULL;
    def->flags = varargs ? DST_FUNCDEF_FLAG_VARARG : 0;
    def->arity = arity;
    def->slotcount = slotcount;
    DstFunction *f = dst_gcalloc(DST_MEMORY_FUNCTION, sizeof(DstFunction));
    f->envs = NULL;
    f->def = def;
    return f;
}

/* Print debug information for a fiber */
static void debug_print_fiber(DstFiber *fiber, int showslots) {
    uint32_t frameindex = fiber->frame;
    uint32_t frametopindex = fiber->frametop;
    const char *statusname =
        fiber->status == DST_FIBER_ALIVE ? "alive" :
        fiber->status == DST_FIBER_PENDING ? "pending" :
        fiber->status == DST_FIBER_ERROR ? "error" :
        "dead";

    printf("fiber at %p\n", fiber);
    printf("  frame = %d\n", fiber->frame);
    printf("  frametop = %d\n", fiber->frametop);
    printf("  stacktop = %d\n", fiber->stacktop);
    printf("  capacity = %d\n", fiber->capacity);
    printf("  status = %s\n  -----\n", statusname);

    while (frameindex > 0) {
        DstValue *stack = fiber->data + frameindex;
        DstStackFrame *frame = dst_stack_frame(stack);
        uint32_t slots = frametopindex - frameindex;
        const uint8_t *str;
        /* Print the stack frame */
        if (frame->func != NULL)
            str = dst_to_string(dst_wrap_function(frame->func));
        else
            str = dst_cstring("<anonymous>");
        printf("  at %.*s (slots: %d)\n", 
            dst_string_length(str),
            (const char *)str,
            slots);
        /* Optionally print all values in the stack */
        if (showslots) {
            for (uint32_t j = 0; j < slots; j++) {
                const uint8_t *vstr = dst_to_string(stack[j]);
                printf("    [%d]: %.*s\n", 
                        j,
                        dst_string_length(vstr),
                        (const char *)vstr);
            }
        }
        frametopindex = frameindex - DST_FRAME_SIZE;
        frameindex = frame->prevframe;
    } 

}

int main() {
    dst_init();

    DstFunction *f1 = dud_func(5, 0, 1);
    DstFiber *fiber1 = dst_fiber(10);
    for (int i = 0; i < 2; i++) {
        dst_fiber_funcframe(fiber1, f1);
    }
    for (int i = 0; i < 13; i++) {
        dst_fiber_push(fiber1, dst_wrap_integer(i));
    }
    for (int i = 0; i < 10; i++) {
        dst_fiber_popvalue(fiber1);
    }
    dst_fiber_funcframe_tail(fiber1, dud_func(20, 0, 0));
    debug_print_fiber(fiber1, 1);

    //dst_deinit();

    return 0;
}
