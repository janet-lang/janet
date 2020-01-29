/*
* Copyright (c) 2020 Calvin Rose
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

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "gc.h"
#include "state.h"
#include "util.h"
#include "vector.h"
#endif

/* Implements functionality to build a debugger from within janet.
 * The repl should also be able to serve as pretty featured debugger
 * out of the box. */

/* Add a break point to a function */
void janet_debug_break(JanetFuncDef *def, int32_t pc) {
    if (pc >= def->bytecode_length || pc < 0)
        janet_panic("invalid bytecode offset");
    def->bytecode[pc] |= 0x80;
}

/* Remove a break point from a function */
void janet_debug_unbreak(JanetFuncDef *def, int32_t pc) {
    if (pc >= def->bytecode_length || pc < 0)
        janet_panic("invalid bytecode offset");
    def->bytecode[pc] &= ~((uint32_t)0x80);
}

/*
 * Find a location for a breakpoint given a source file an
 * location.
 */
void janet_debug_find(
    JanetFuncDef **def_out, int32_t *pc_out,
    const uint8_t *source, int32_t sourceLine, int32_t sourceColumn) {
    /* Scan the heap for right func def */
    JanetGCObject *current = janet_vm_blocks;
    /* Keep track of the best source mapping we have seen so far */
    int32_t besti = -1;
    int32_t best_line = -1;
    int32_t best_column = -1;
    JanetFuncDef *best_def = NULL;
    while (NULL != current) {
        if ((current->flags & JANET_MEM_TYPEBITS) == JANET_MEMORY_FUNCDEF) {
            JanetFuncDef *def = (JanetFuncDef *)(current);
            if (def->sourcemap &&
                    def->source &&
                    !janet_string_compare(source, def->source)) {
                /* Correct source file, check mappings. The chosen
                 * pc index is the instruction closest to the given line column, but
                 * not after. */
                int32_t i;
                for (i = 0; i < def->bytecode_length; i++) {
                    int32_t line = def->sourcemap[i].line;
                    int32_t column = def->sourcemap[i].column;
                    if (line <= sourceLine && line >= best_line) {
                        if (column <= sourceColumn &&
                                (line > best_line || column > best_column)) {
                            best_line = line;
                            best_column = column;
                            besti = i;
                            best_def = def;
                        }
                    }
                }
            }
        }
        current = current->next;
    }
    if (best_def) {
        *def_out = best_def;
        *pc_out = besti;
    } else {
        janet_panic("could not find breakpoint");
    }
}

/* Error reporting. This can be emulated from within Janet, but for
 * consitency with the top level code it is defined once. */
void janet_stacktrace(JanetFiber *fiber, Janet err) {
    int32_t fi;
    const char *errstr = (const char *)janet_to_string(err);
    JanetFiber **fibers = NULL;
    int wrote_error = 0;

    int print_color = janet_truthy(janet_dyn("err-color"));
    if (print_color) janet_eprintf("\x1b[31m");

    while (fiber) {
        janet_v_push(fibers, fiber);
        fiber = fiber->child;
    }

    for (fi = janet_v_count(fibers) - 1; fi >= 0; fi--) {
        fiber = fibers[fi];
        int32_t i = fiber->frame;
        while (i > 0) {
            JanetStackFrame *frame = (JanetStackFrame *)(fiber->data + i - JANET_FRAME_SIZE);
            JanetFuncDef *def = NULL;
            i = frame->prevframe;

            /* Print prelude to stack frame */
            if (!wrote_error) {
                JanetFiberStatus status = janet_fiber_status(fiber);
                const char *prefix = status == JANET_STATUS_ERROR ? "" : "status ";
                janet_eprintf("%s%s: %s\n",
                              prefix,
                              janet_status_names[status],
                              errstr);
                wrote_error = 1;
            }

            janet_eprintf("  in");

            if (frame->func) {
                def = frame->func->def;
                janet_eprintf(" %s", def->name ? (const char *)def->name : "<anonymous>");
                if (def->source) {
                    janet_eprintf(" [%s]", (const char *)def->source);
                }
            } else {
                JanetCFunction cfun = (JanetCFunction)(frame->pc);
                if (cfun) {
                    Janet name = janet_table_get(janet_vm_registry, janet_wrap_cfunction(cfun));
                    if (!janet_checktype(name, JANET_NIL))
                        janet_eprintf(" %s", (const char *)janet_to_string(name));
                    else
                        janet_eprintf(" <cfunction>");
                }
            }
            if (frame->flags & JANET_STACKFRAME_TAILCALL)
                janet_eprintf(" (tailcall)");
            if (frame->func && frame->pc) {
                int32_t off = (int32_t)(frame->pc - def->bytecode);
                if (def->sourcemap) {
                    JanetSourceMapping mapping = def->sourcemap[off];
                    janet_eprintf(" on line %d, column %d", mapping.line, mapping.column);
                } else {
                    janet_eprintf(" pc=%d", off);
                }
            }
            janet_eprintf("\n");
        }
    }

    if (print_color) janet_eprintf("\x1b[0m");

    janet_v_free(fibers);
}

/*
 * CFuns
 */

/* Helper to find funcdef and bytecode offset to insert or remove breakpoints.
 * Takes a source file name and byte offset. */
static void helper_find(int32_t argc, Janet *argv, JanetFuncDef **def, int32_t *bytecode_offset) {
    janet_fixarity(argc, 3);
    const uint8_t *source = janet_getstring(argv, 0);
    int32_t line = janet_getinteger(argv, 1);
    int32_t col = janet_getinteger(argv, 2);
    janet_debug_find(def, bytecode_offset, source, line, col);
}

/* Helper to find funcdef and bytecode offset to insert or remove breakpoints.
 * Takes a function and byte offset*/
static void helper_find_fun(int32_t argc, Janet *argv, JanetFuncDef **def, int32_t *bytecode_offset) {
    janet_arity(argc, 1, 2);
    JanetFunction *func = janet_getfunction(argv, 0);
    int32_t offset = (argc == 2) ? janet_getinteger(argv, 1) : 0;
    *def = func->def;
    *bytecode_offset = offset;
}

static Janet cfun_debug_break(int32_t argc, Janet *argv) {
    JanetFuncDef *def;
    int32_t offset;
    helper_find(argc, argv, &def, &offset);
    janet_debug_break(def, offset);
    return janet_wrap_nil();
}

static Janet cfun_debug_unbreak(int32_t argc, Janet *argv) {
    JanetFuncDef *def;
    int32_t offset = 0;
    helper_find(argc, argv, &def, &offset);
    janet_debug_unbreak(def, offset);
    return janet_wrap_nil();
}

static Janet cfun_debug_fbreak(int32_t argc, Janet *argv) {
    JanetFuncDef *def;
    int32_t offset = 0;
    helper_find_fun(argc, argv, &def, &offset);
    janet_debug_break(def, offset);
    return janet_wrap_nil();
}

static Janet cfun_debug_unfbreak(int32_t argc, Janet *argv) {
    JanetFuncDef *def;
    int32_t offset;
    helper_find_fun(argc, argv, &def, &offset);
    janet_debug_unbreak(def, offset);
    return janet_wrap_nil();
}

static Janet cfun_debug_lineage(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    JanetArray *array = janet_array(0);
    while (fiber) {
        janet_array_push(array, janet_wrap_fiber(fiber));
        fiber = fiber->child;
    }
    return janet_wrap_array(array);
}

/* Extract info from one stack frame */
static Janet doframe(JanetStackFrame *frame) {
    int32_t off;
    JanetTable *t = janet_table(3);
    JanetFuncDef *def = NULL;
    if (frame->func) {
        janet_table_put(t, janet_ckeywordv("function"), janet_wrap_function(frame->func));
        def = frame->func->def;
        if (def->name) {
            janet_table_put(t, janet_ckeywordv("name"), janet_wrap_string(def->name));
        }
    } else {
        JanetCFunction cfun = (JanetCFunction)(frame->pc);
        if (cfun) {
            Janet name = janet_table_get(janet_vm_registry, janet_wrap_cfunction(cfun));
            if (!janet_checktype(name, JANET_NIL)) {
                janet_table_put(t, janet_ckeywordv("name"), name);
            }
        }
        janet_table_put(t, janet_ckeywordv("c"), janet_wrap_true());
    }
    if (frame->flags & JANET_STACKFRAME_TAILCALL) {
        janet_table_put(t, janet_ckeywordv("tail"), janet_wrap_true());
    }
    if (frame->func && frame->pc) {
        Janet *stack = (Janet *)frame + JANET_FRAME_SIZE;
        JanetArray *slots;
        off = (int32_t)(frame->pc - def->bytecode);
        janet_table_put(t, janet_ckeywordv("pc"), janet_wrap_integer(off));
        if (def->sourcemap) {
            JanetSourceMapping mapping = def->sourcemap[off];
            janet_table_put(t, janet_ckeywordv("source-line"), janet_wrap_integer(mapping.line));
            janet_table_put(t, janet_ckeywordv("source-column"), janet_wrap_integer(mapping.column));
        }
        if (def->source) {
            janet_table_put(t, janet_ckeywordv("source"), janet_wrap_string(def->source));
        }
        /* Add stack arguments */
        slots = janet_array(def->slotcount);
        safe_memcpy(slots->data, stack, sizeof(Janet) * def->slotcount);
        slots->count = def->slotcount;
        janet_table_put(t, janet_ckeywordv("slots"), janet_wrap_array(slots));
    }
    return janet_wrap_table(t);
}

static Janet cfun_debug_stack(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    JanetArray *array = janet_array(0);
    {
        int32_t i = fiber->frame;
        JanetStackFrame *frame;
        while (i > 0) {
            frame = (JanetStackFrame *)(fiber->data + i - JANET_FRAME_SIZE);
            janet_array_push(array, doframe(frame));
            i = frame->prevframe;
        }
    }
    return janet_wrap_array(array);
}

static Janet cfun_debug_stacktrace(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    janet_stacktrace(fiber, argv[1]);
    return argv[0];
}

static Janet cfun_debug_argstack(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    JanetArray *array = janet_array(fiber->stacktop - fiber->stackstart);
    memcpy(array->data, fiber->data + fiber->stackstart, array->capacity * sizeof(Janet));
    array->count = array->capacity;
    return janet_wrap_array(array);
}

static Janet cfun_debug_step(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    Janet out = janet_wrap_nil();
    janet_step(fiber, argc == 1 ? janet_wrap_nil() : argv[1], &out);
    return out;
}

static const JanetReg debug_cfuns[] = {
    {
        "debug/break", cfun_debug_break,
        JDOC("(debug/break source byte-offset)\n\n"
             "Sets a breakpoint with source a key at a given line and column. "
             "Will throw an error if the breakpoint location "
             "cannot be found. For example\n\n"
             "\t(debug/break \"core.janet\" 1000)\n\n"
             "wil set a breakpoint at the 1000th byte of the file core.janet.")
    },
    {
        "debug/unbreak", cfun_debug_unbreak,
        JDOC("(debug/unbreak source line column)\n\n"
             "Remove a breakpoint with a source key at a given line and column. "
             "Will throw an error if the breakpoint "
             "cannot be found.")
    },
    {
        "debug/fbreak", cfun_debug_fbreak,
        JDOC("(debug/fbreak fun &opt pc)\n\n"
             "Set a breakpoint in a given function. pc is an optional offset, which "
             "is in bytecode instructions. fun is a function value. Will throw an error "
             "if the offset is too large or negative.")
    },
    {
        "debug/unfbreak", cfun_debug_unfbreak,
        JDOC("(debug/unfbreak fun &opt pc)\n\n"
             "Unset a breakpoint set with debug/fbreak.")
    },
    {
        "debug/arg-stack", cfun_debug_argstack,
        JDOC("(debug/arg-stack fiber)\n\n"
             "Gets all values currently on the fiber's argument stack. Normally, "
             "this should be empty unless the fiber signals while pushing arguments "
             "to make a function call. Returns a new array.")
    },
    {
        "debug/stack", cfun_debug_stack,
        JDOC("(debug/stack fib)\n\n"
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
             "\t:source - string with the file path or other identifier for the source code\n"
             "\t:slots - array of all values in each slot\n"
             "\t:tail - boolean indicating a tail call")
    },
    {
        "debug/stacktrace", cfun_debug_stacktrace,
        JDOC("(debug/stacktrace fiber err)\n\n"
             "Prints a nice looking stacktrace for a fiber. The error message "
             "err must be passed to the function as fiber's do not keep track of "
             "the last error they have thrown. Returns the fiber.")
    },
    {
        "debug/lineage", cfun_debug_lineage,
        JDOC("(debug/lineage fib)\n\n"
             "Returns an array of all child fibers from a root fiber. This function "
             "is useful when a fiber signals or errors to an ancestor fiber. Using this function, "
             "the fiber handling the error can see which fiber raised the signal. This function should "
             "be used mostly for debugging purposes.")
    },
    {
        "debug/step", cfun_debug_step,
        JDOC("(debug/step fiber &opt x)\n\n"
             "Run a fiber for one virtual instruction of the Janet machine. Can optionally "
             "pass in a value that will be passed as the resuming value. Returns the signal value, "
             "which will usually be nil, as breakpoints raise nil signals.")
    },
    {NULL, NULL, NULL}
};

/* Module entry point */
void janet_lib_debug(JanetTable *env) {
    janet_core_cfuns(env, NULL, debug_cfuns);
}
