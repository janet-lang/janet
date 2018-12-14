/*
* Copyright (c) 2018 Calvin Rose
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

#include <janet/janet.h>
#include "gc.h"
#include "state.h"

/* Implements functionality to build a debugger from within janet.
 * The repl should also be able to serve as pretty featured debugger
 * out of the box. */

/* Add a break point to a function */
int janet_debug_break(JanetFuncDef *def, int32_t pc) {
    if (pc >= def->bytecode_length || pc < 0)
        return 1;
    def->bytecode[pc] |= 0x80;
    return 0;
}

/* Remove a break point from a function */
int janet_debug_unbreak(JanetFuncDef *def, int32_t pc) {
    if (pc >= def->bytecode_length || pc < 0)
        return 1;
    def->bytecode[pc] &= ~((uint32_t)0x80);
    return 0;
}

/*
 * Find a location for a breakpoint given a source file an
 * location.
 */
int janet_debug_find(
        JanetFuncDef **def_out, int32_t *pc_out,
        const uint8_t *source, int32_t offset) {
    /* Scan the heap for right func def */
    JanetGCMemoryHeader *current = janet_vm_blocks;
    /* Keep track of the best source mapping we have seen so far */
    int32_t besti = -1;
    int32_t best_range = INT32_MAX;
    JanetFuncDef *best_def = NULL;
    while (NULL != current) {
        if ((current->flags & JANET_MEM_TYPEBITS) == JANET_MEMORY_FUNCDEF) {
            JanetFuncDef *def = (JanetFuncDef *)(current + 1);
            if (def->sourcemap &&
                    def->source &&
                    !janet_string_compare(source, def->source)) {
                /* Correct source file, check mappings. The chosen
                 * pc index is the first match with the smallest range. */
                int32_t i;
                for (i = 0; i < def->bytecode_length; i++) {
                    int32_t start = def->sourcemap[i].start;
                    int32_t end = def->sourcemap[i].end;
                    if (end - start < best_range &&
                            start <= offset &&
                            end >= offset) {
                        best_range = end - start;
                        besti = i;
                        best_def = def;
                    }
                }
            }
        }
        current = current->next;
    }
    if (best_def) {
        *def_out = best_def;
        *pc_out = besti;
        return 0;
    } else {
        return 1;
    }
}

/*
 * CFuns
 */

/* Helper to find funcdef and bytecode offset to insert or remove breakpoints.
 * Takes a source file name and byte offset. */
static int helper_find(JanetArgs args, JanetFuncDef **def, int32_t *bytecode_offset) {
    const uint8_t *source;
    int32_t source_offset;
    JANET_FIXARITY(args, 2);
    JANET_ARG_STRING(source, args, 0);
    JANET_ARG_INTEGER(source_offset, args, 1);
    if (janet_debug_find(
                def, bytecode_offset, source, source_offset)) {
        JANET_THROW(args, "could not find breakpoint");
    }
    JANET_RETURN_NIL(args);
}

/* Helper to find funcdef and bytecode offset to insert or remove breakpoints.
 * Takes a function and byte offset*/
static int helper_find_fun(JanetArgs args, JanetFuncDef **def, int32_t *bytecode_offset) {
    JanetFunction *func;
    int32_t offset = 0;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_FUNCTION(func, args, 0);
    if (args.n == 2) {
        JANET_ARG_INTEGER(offset, args, 1);
    }
    *def = func->def;
    *bytecode_offset = offset;
    JANET_RETURN_NIL(args);
}

static int cfun_break(JanetArgs args) {
    JanetFuncDef *def;
    int32_t offset;
    int status = helper_find(args, &def, &offset);
    if (status == 0) janet_debug_break(def, offset);
    return status;
}

static int cfun_unbreak(JanetArgs args) {
    JanetFuncDef *def;
    int32_t offset;
    int status = helper_find(args, &def, &offset);
    if (status == 0) janet_debug_unbreak(def, offset);
    return status;
}

static int cfun_fbreak(JanetArgs args) {
    JanetFuncDef *def;
    int32_t offset;
    int status = helper_find_fun(args, &def, &offset);
    if (status == 0) {
        if (janet_debug_break(def, offset)) {
            JANET_THROW(args, "could not find breakpoint");
        }
    }
    return status;
}

static int cfun_unfbreak(JanetArgs args) {
    JanetFuncDef *def;
    int32_t offset;
    int status = helper_find_fun(args, &def, &offset);
    if (status == 0) {
        if (janet_debug_unbreak(def, offset)) {
            JANET_THROW(args, "could not find breakpoint");
        }
    }
    return status;
}

static int cfun_lineage(JanetArgs args) {
    JanetFiber *fiber;
    JanetArray *array;
    JANET_FIXARITY(args, 1);
    JANET_ARG_FIBER(fiber, args, 0);
    array = janet_array(0);
    while (fiber) {
        janet_array_push(array, janet_wrap_fiber(fiber));
        fiber = fiber->child;
    }
    JANET_RETURN_ARRAY(args, array);
}

/* Extract info from one stack frame */
static Janet doframe(JanetStackFrame *frame) {
    int32_t off;
    JanetTable *t = janet_table(3);
    JanetFuncDef *def = NULL;
    if (frame->func) {
        janet_table_put(t, janet_csymbolv(":function"), janet_wrap_function(frame->func));
        def = frame->func->def;
        if (def->name) {
            janet_table_put(t, janet_csymbolv(":name"), janet_wrap_string(def->name));
        }
    } else {
        JanetCFunction cfun = (JanetCFunction)(frame->pc);
        if (cfun) {
            Janet name = janet_table_get(janet_vm_registry, janet_wrap_cfunction(cfun));
            if (!janet_checktype(name, JANET_NIL)) {
                janet_table_put(t, janet_csymbolv(":name"), name);
            }
        }
        janet_table_put(t, janet_csymbolv(":c"), janet_wrap_true());
    }
    if (frame->flags & JANET_STACKFRAME_TAILCALL) {
        janet_table_put(t, janet_csymbolv(":tail"), janet_wrap_true());
    }
    if (frame->func && frame->pc) {
        Janet *stack = (Janet *)frame + JANET_FRAME_SIZE;
        JanetArray *slots;
        off = (int32_t) (frame->pc - def->bytecode);
        janet_table_put(t, janet_csymbolv(":pc"), janet_wrap_integer(off));
        if (def->sourcemap) {
            JanetSourceMapping mapping = def->sourcemap[off];
            janet_table_put(t, janet_csymbolv(":source-start"), janet_wrap_integer(mapping.start));
            janet_table_put(t, janet_csymbolv(":source-end"), janet_wrap_integer(mapping.end));
        }
        if (def->source) {
            janet_table_put(t, janet_csymbolv(":source"), janet_wrap_string(def->source));
        }
        /* Add stack arguments */
        slots = janet_array(def->slotcount);
        memcpy(slots->data, stack, sizeof(Janet) * def->slotcount);
        slots->count = def->slotcount;
        janet_table_put(t, janet_csymbolv(":slots"), janet_wrap_array(slots));
    }
    return janet_wrap_table(t);
}

static int cfun_stack(JanetArgs args) {
    JanetFiber *fiber;
    JanetArray *array;
    JANET_FIXARITY(args, 1);
    JANET_ARG_FIBER(fiber, args, 0);
    array = janet_array(0);
    {
        int32_t i = fiber->frame;
        JanetStackFrame *frame;
        while (i > 0) {
            frame = (JanetStackFrame *)(fiber->data + i - JANET_FRAME_SIZE);
            janet_array_push(array, doframe(frame));
            i = frame->prevframe;
        }
    }
    JANET_RETURN_ARRAY(args, array);
}

static int cfun_argstack(JanetArgs args) {
    JanetFiber *fiber;
    JanetArray *array;
    JANET_FIXARITY(args, 1);
    JANET_ARG_FIBER(fiber, args, 0);
    array = janet_array(fiber->stacktop - fiber->stackstart);
    memcpy(array->data, fiber->data + fiber->stackstart, array->capacity * sizeof(Janet));
    array->count = array->capacity;
    JANET_RETURN_ARRAY(args, array);
}

static const JanetReg cfuns[] = {
    {"debug/break", cfun_break,
        "(debug/break source byte-offset)\n\n"
        "Sets a breakpoint with source a key at a given byte offset. An offset "
        "of 0 is the first byte in a file. Will throw an error if the breakpoint location "
        "cannot be found. For example\n\n"
        "\t(debug/break \"core.janet\" 1000)\n\n"
        "wil set a breakpoint at the 1000th byte of the file core.janet."},
    {"debug/unbreak", cfun_unbreak,
        "(debug/unbreak source byte-offset)\n\n"
        "Remove a breakpoint with a source key at a given byte offset. An offset "
        "of 0 is the first byte in a file. Will throw an error if the breakpoint "
        "cannot be found."},
    {"debug/fbreak", cfun_fbreak,
        "(debug/fbreak fun [,pc=0])\n\n"
        "Set a breakpoint in a given function. pc is an optional offset, which "
        "is in bytecode instructions. fun is a function value. Will throw an error "
        "if the offset is too large or negative."},
    {"debug/funbreak", cfun_unfbreak,
        "(debug/fbreak fun [,pc=0])\n\n"
        "Unset a breakpoint set with debug/fbreak."},
    {"debug/arg-stack", cfun_argstack,
        "(debug/arg-stack fiber)\n\n"
        "Gets all values currently on the fiber's argument stack. Normally, "
        "this should be empty unless the fiber signals while pushing arguments "
        "to make a function call. Returns a new array."},
    {"debug/stack", cfun_stack,
        "(debug/stack fib)\n\n"
        "Gets information about the stack as an array of tables. Each table "
        "in the array contains information about a stack frame. The top most, current "
        "stack frame is the first table in the array, and the bottom most stack frame "
        "is the last value. Each stack frame contains some of the following attributes:\n\n"
        "\t:c - true if the stack frame is a c function invocation\n"
        "\t:column - the current source column of the stack frame\n"
        "\t:function - the function that the stack frame represents\n"
        "\t:line - the current source line of the stack frame\n"
        "\t:name - the human friendly name of the function\n"
        "\t:pc - integer indicating the location of the program counter\n"
        "\t:source - string with filename or other identifier for the source code\n"
        "\t:slots - array of all values in each slot\n"
        "\t:tail - boolean indicating a tail call"
    },
    {"debug/lineage", cfun_lineage,
        "(debug/lineage fib)\n\n"
        "Returns an array of all child fibers from a root fiber. This function "
        "is useful when a fiber signals or errors to an ancestor fiber. Using this function, "
        "the fiber handling the error can see which fiber raised the signal. This function should "
        "be used mostly for debugging purposes."
    },
    {NULL, NULL, NULL}
};

/* Module entry point */
int janet_lib_debug(JanetArgs args) {
    JanetTable *env = janet_env(args);
    janet_cfuns(env, NULL, cfuns);
    return 0;
}
