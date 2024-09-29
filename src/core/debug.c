/*
* Copyright (c) 2024 Calvin Rose
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
    JanetGCObject *current = janet_vm.blocks;
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
        current = current->data.next;
    }
    if (best_def) {
        *def_out = best_def;
        *pc_out = besti;
    } else {
        janet_panic("could not find breakpoint");
    }
}

void janet_stacktrace(JanetFiber *fiber, Janet err) {
    const char *prefix = janet_checktype(err, JANET_NIL) ? NULL : "";
    janet_stacktrace_ext(fiber, err, prefix);
}

/* Error reporting. This can be emulated from within Janet, but for
 * consistency with the top level code it is defined once. */
void janet_stacktrace_ext(JanetFiber *fiber, Janet err, const char *prefix) {

    int32_t fi;
    const char *errstr = (const char *)janet_to_string(err);
    JanetFiber **fibers = NULL;
    int wrote_error = !prefix;

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
            JanetCFunRegistry *reg = NULL;
            JanetStackFrame *frame = (JanetStackFrame *)(fiber->data + i - JANET_FRAME_SIZE);
            JanetFuncDef *def = NULL;
            i = frame->prevframe;

            /* Print prelude to stack frame */
            if (!wrote_error) {
                JanetFiberStatus status = janet_fiber_status(fiber);
                janet_eprintf("%s%s: %s\n",
                              prefix ? prefix : "",
                              janet_status_names[status],
                              errstr ? errstr : janet_status_names[status]);
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
                    reg = janet_registry_get(cfun);
                    if (NULL != reg && NULL != reg->name) {
                        if (reg->name_prefix) {
                            janet_eprintf(" %s/%s", reg->name_prefix, reg->name);
                        } else {
                            janet_eprintf(" %s", reg->name);
                        }
                        if (NULL != reg->source_file) {
                            janet_eprintf(" [%s]", reg->source_file);
                        }
                    } else {
                        janet_eprintf(" <cfunction>");
                    }
                }
            }
            if (frame->flags & JANET_STACKFRAME_TAILCALL)
                janet_eprintf(" (tail call)");
            if (frame->func && frame->pc) {
                int32_t off = (int32_t)(frame->pc - def->bytecode);
                if (def->sourcemap) {
                    JanetSourceMapping mapping = def->sourcemap[off];
                    janet_eprintf(" on line %d, column %d", mapping.line, mapping.column);
                } else {
                    janet_eprintf(" pc=%d", off);
                }
            } else if (NULL != reg) {
                /* C Function */
                if (reg->source_line > 0) {
                    janet_eprintf(" on line %d", (long) reg->source_line);
                }
            }
            janet_eprintf("\n");
            /* Print fiber points optionally. Clutters traces but provides info
            if (i <= 0 && fi > 0) {
                janet_eprintf("  in parent fiber\n");
            }
            */
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

JANET_CORE_FN(cfun_debug_break,
              "(debug/break source line col)",
              "Sets a breakpoint in `source` at a given line and column. "
              "Will throw an error if the breakpoint location "
              "cannot be found. For example\n\n"
              "\t(debug/break \"core.janet\" 10 4)\n\n"
              "will set a breakpoint at line 10, 4th column of the file core.janet.") {
    JanetFuncDef *def;
    int32_t offset;
    helper_find(argc, argv, &def, &offset);
    janet_debug_break(def, offset);
    return janet_wrap_nil();
}

JANET_CORE_FN(cfun_debug_unbreak,
              "(debug/unbreak source line column)",
              "Remove a breakpoint with a source key at a given line and column. "
              "Will throw an error if the breakpoint "
              "cannot be found.") {
    JanetFuncDef *def;
    int32_t offset = 0;
    helper_find(argc, argv, &def, &offset);
    janet_debug_unbreak(def, offset);
    return janet_wrap_nil();
}

JANET_CORE_FN(cfun_debug_fbreak,
              "(debug/fbreak fun &opt pc)",
              "Set a breakpoint in a given function. pc is an optional offset, which "
              "is in bytecode instructions. fun is a function value. Will throw an error "
              "if the offset is too large or negative.") {
    JanetFuncDef *def;
    int32_t offset = 0;
    helper_find_fun(argc, argv, &def, &offset);
    janet_debug_break(def, offset);
    return janet_wrap_nil();
}

JANET_CORE_FN(cfun_debug_unfbreak,
              "(debug/unfbreak fun &opt pc)",
              "Unset a breakpoint set with debug/fbreak.") {
    JanetFuncDef *def;
    int32_t offset;
    helper_find_fun(argc, argv, &def, &offset);
    janet_debug_unbreak(def, offset);
    return janet_wrap_nil();
}

JANET_CORE_FN(cfun_debug_lineage,
              "(debug/lineage fib)",
              "Returns an array of all child fibers from a root fiber. This function "
              "is useful when a fiber signals or errors to an ancestor fiber. Using this function, "
              "the fiber handling the error can see which fiber raised the signal. This function should "
              "be used mostly for debugging purposes.") {
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
            JanetCFunRegistry *reg = janet_registry_get(cfun);
            if (NULL != reg->name) {
                if (NULL != reg->name_prefix) {
                    janet_table_put(t, janet_ckeywordv("name"), janet_wrap_string(janet_formatc("%s/%s", reg->name_prefix, reg->name)));
                } else {
                    janet_table_put(t, janet_ckeywordv("name"), janet_cstringv(reg->name));
                }
                if (NULL != reg->source_file) {
                    janet_table_put(t, janet_ckeywordv("source"), janet_cstringv(reg->source_file));
                }
                if (reg->source_line > 0) {
                    janet_table_put(t, janet_ckeywordv("source-line"), janet_wrap_integer(reg->source_line));
                    janet_table_put(t, janet_ckeywordv("source-column"), janet_wrap_integer(1));
                }
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
        janet_assert(def != NULL, "def != NULL");
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
        /* Add local bindings */
        if (def->symbolmap) {
            JanetTable *local_bindings = janet_table(0);
            for (int32_t i = def->symbolmap_length - 1; i >= 0; i--) {
                JanetSymbolMap jsm = def->symbolmap[i];
                Janet value = janet_wrap_nil();
                uint32_t pc = (uint32_t)(frame->pc - def->bytecode);
                if (jsm.birth_pc == UINT32_MAX) {
                    JanetFuncEnv *env = frame->func->envs[jsm.death_pc];
                    if (env->offset > 0) {
                        value = env->as.fiber->data[env->offset + jsm.slot_index];
                    } else {
                        value = env->as.values[jsm.slot_index];
                    }
                } else if (pc >= jsm.birth_pc && pc < jsm.death_pc) {
                    value = stack[jsm.slot_index];
                }
                janet_table_put(local_bindings, janet_wrap_symbol(jsm.symbol), value);
            }
            janet_table_put(t, janet_ckeywordv("locals"), janet_wrap_table(local_bindings));
        }
    }
    return janet_wrap_table(t);
}

JANET_CORE_FN(cfun_debug_stack,
              "(debug/stack fib)",
              "Gets information about the stack as an array of tables. Each table "
              "in the array contains information about a stack frame. The top-most, current "
              "stack frame is the first table in the array, and the bottom-most stack frame "
              "is the last value. Each stack frame contains some of the following attributes:\n\n"
              "* :c - true if the stack frame is a c function invocation\n\n"
              "* :source-column - the current source column of the stack frame\n\n"
              "* :function - the function that the stack frame represents\n\n"
              "* :source-line - the current source line of the stack frame\n\n"
              "* :name - the human-friendly name of the function\n\n"
              "* :pc - integer indicating the location of the program counter\n\n"
              "* :source - string with the file path or other identifier for the source code\n\n"
              "* :slots - array of all values in each slot\n\n"
              "* :tail - boolean indicating a tail call") {
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

JANET_CORE_FN(cfun_debug_stacktrace,
              "(debug/stacktrace fiber &opt err prefix)",
              "Prints a nice looking stacktrace for a fiber. Can optionally provide "
              "an error value to print the stack trace with. If `prefix` is nil or not "
              "provided, will skip the error line. Returns the fiber.") {
    janet_arity(argc, 1, 3);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    Janet x = argc == 1 ? janet_wrap_nil() : argv[1];
    const char *prefix = janet_optcstring(argv, argc, 2, NULL);
    janet_stacktrace_ext(fiber, x, prefix);
    return argv[0];
}

JANET_CORE_FN(cfun_debug_argstack,
              "(debug/arg-stack fiber)",
              "Gets all values currently on the fiber's argument stack. Normally, "
              "this should be empty unless the fiber signals while pushing arguments "
              "to make a function call. Returns a new array.") {
    janet_fixarity(argc, 1);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    JanetArray *array = janet_array(fiber->stacktop - fiber->stackstart);
    memcpy(array->data, fiber->data + fiber->stackstart, array->capacity * sizeof(Janet));
    array->count = array->capacity;
    return janet_wrap_array(array);
}

JANET_CORE_FN(cfun_debug_step,
              "(debug/step fiber &opt x)",
              "Run a fiber for one virtual instruction of the Janet machine. Can optionally "
              "pass in a value that will be passed as the resuming value. Returns the signal value, "
              "which will usually be nil, as breakpoints raise nil signals.") {
    janet_arity(argc, 1, 2);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    Janet out = janet_wrap_nil();
    janet_step(fiber, argc == 1 ? janet_wrap_nil() : argv[1], &out);
    return out;
}

/* Module entry point */
void janet_lib_debug(JanetTable *env) {
    JanetRegExt debug_cfuns[] = {
        JANET_CORE_REG("debug/break", cfun_debug_break),
        JANET_CORE_REG("debug/unbreak", cfun_debug_unbreak),
        JANET_CORE_REG("debug/fbreak", cfun_debug_fbreak),
        JANET_CORE_REG("debug/unfbreak", cfun_debug_unfbreak),
        JANET_CORE_REG("debug/arg-stack", cfun_debug_argstack),
        JANET_CORE_REG("debug/stack", cfun_debug_stack),
        JANET_CORE_REG("debug/stacktrace", cfun_debug_stacktrace),
        JANET_CORE_REG("debug/lineage", cfun_debug_lineage),
        JANET_CORE_REG("debug/step", cfun_debug_step),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, debug_cfuns);
}
