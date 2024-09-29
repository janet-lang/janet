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
#include "compile.h"
#include "util.h"
#include "vector.h"
#include "emit.h"
#endif

static JanetSlot janetc_quote(JanetFopts opts, int32_t argn, const Janet *argv) {
    if (argn != 1) {
        janetc_cerror(opts.compiler, "expected 1 argument to quote");
        return janetc_cslot(janet_wrap_nil());
    }
    return janetc_cslot(argv[0]);
}

static JanetSlot janetc_splice(JanetFopts opts, int32_t argn, const Janet *argv) {
    JanetSlot ret;
    if (!(opts.flags & JANET_FOPTS_ACCEPT_SPLICE)) {
        janetc_cerror(opts.compiler, "splice can only be used in function parameters and data constructors, it has no effect here");
        return janetc_cslot(janet_wrap_nil());
    }
    if (argn != 1) {
        janetc_cerror(opts.compiler, "expected 1 argument to splice");
        return janetc_cslot(janet_wrap_nil());
    }
    ret = janetc_value(opts, argv[0]);
    ret.flags |= JANET_SLOT_SPLICED;
    return ret;
}

static JanetSlot qq_slots(JanetFopts opts, JanetSlot *slots, int makeop) {
    JanetSlot target = janetc_gettarget(opts);
    janetc_pushslots(opts.compiler, slots);
    janetc_freeslots(opts.compiler, slots);
    janetc_emit_s(opts.compiler, makeop, target, 1);
    return target;
}

static JanetSlot quasiquote(JanetFopts opts, Janet x, int depth, int level) {
    if (depth == 0) {
        janetc_cerror(opts.compiler, "quasiquote too deeply nested");
        return janetc_cslot(janet_wrap_nil());
    }
    JanetSlot *slots = NULL;
    JanetFopts subopts = opts;
    subopts.flags &= ~JANET_FOPTS_HINT;
    switch (janet_type(x)) {
        default:
            return janetc_cslot(x);
        case JANET_TUPLE: {
            int32_t i, len;
            const Janet *tup = janet_unwrap_tuple(x);
            len = janet_tuple_length(tup);
            if (len > 1 && janet_checktype(tup[0], JANET_SYMBOL)) {
                const uint8_t *head = janet_unwrap_symbol(tup[0]);
                if (!janet_cstrcmp(head, "unquote")) {
                    if (level == 0) {
                        JanetFopts subopts = janetc_fopts_default(opts.compiler);
                        subopts.flags |= JANET_FOPTS_ACCEPT_SPLICE;
                        return janetc_value(subopts, tup[1]);
                    } else {
                        level--;
                    }
                } else if (!janet_cstrcmp(head, "quasiquote")) {
                    level++;
                }
            }
            for (i = 0; i < len; i++)
                janet_v_push(slots, quasiquote(subopts, tup[i], depth - 1, level));
            return qq_slots(opts, slots, (janet_tuple_flag(tup) & JANET_TUPLE_FLAG_BRACKETCTOR)
                            ? JOP_MAKE_BRACKET_TUPLE
                            : JOP_MAKE_TUPLE);
        }
        case JANET_ARRAY: {
            int32_t i;
            JanetArray *array = janet_unwrap_array(x);
            for (i = 0; i < array->count; i++)
                janet_v_push(slots, quasiquote(subopts, array->data[i], depth - 1, level));
            return qq_slots(opts, slots, JOP_MAKE_ARRAY);
        }
        case JANET_TABLE:
        case JANET_STRUCT: {
            const JanetKV *kv = NULL, *kvs = NULL;
            int32_t len, cap = 0;
            janet_dictionary_view(x, &kvs, &len, &cap);
            while ((kv = janet_dictionary_next(kvs, cap, kv))) {
                JanetSlot key = quasiquote(subopts, kv->key, depth - 1, level);
                JanetSlot value =  quasiquote(subopts, kv->value, depth - 1, level);
                key.flags &= ~JANET_SLOT_SPLICED;
                value.flags &= ~JANET_SLOT_SPLICED;
                janet_v_push(slots, key);
                janet_v_push(slots, value);
            }
            return qq_slots(opts, slots,
                            janet_checktype(x, JANET_TABLE) ? JOP_MAKE_TABLE : JOP_MAKE_STRUCT);
        }
    }
}

static JanetSlot janetc_quasiquote(JanetFopts opts, int32_t argn, const Janet *argv) {
    if (argn != 1) {
        janetc_cerror(opts.compiler, "expected 1 argument to quasiquote");
        return janetc_cslot(janet_wrap_nil());
    }
    return quasiquote(opts, argv[0], JANET_RECURSION_GUARD, 0);
}

static JanetSlot janetc_unquote(JanetFopts opts, int32_t argn, const Janet *argv) {
    (void) argn;
    (void) argv;
    janetc_cerror(opts.compiler, "cannot use unquote here");
    return janetc_cslot(janet_wrap_nil());
}

/* Perform destructuring. Be careful to
 * keep the order registers are freed.
 * Returns if the slot 'right' can be freed. */
static int destructure(JanetCompiler *c,
                       Janet left,
                       JanetSlot right,
                       int (*leaf)(JanetCompiler *c,
                                   const uint8_t *sym,
                                   JanetSlot s,
                                   JanetTable *attr),
                       JanetTable *attr) {
    switch (janet_type(left)) {
        default:
            janetc_error(c, janet_formatc("unexpected type in destructuring, got %v", left));
            return 1;
        case JANET_SYMBOL:
            /* Leaf, assign right to left */
            return leaf(c, janet_unwrap_symbol(left), right, attr);
        case JANET_TUPLE:
        case JANET_ARRAY: {
            int32_t len = 0;
            const Janet *values = NULL;
            janet_indexed_view(left, &values, &len);
            for (int32_t i = 0; i < len; i++) {
                JanetSlot nextright = janetc_farslot(c);
                Janet subval = values[i];

                if (janet_checktype(subval, JANET_SYMBOL) && !janet_cstrcmp(janet_unwrap_symbol(subval), "&")) {
                    if (i + 1 >= len) {
                        janetc_cerror(c, "expected symbol following '& in destructuring pattern");
                        return 1;
                    }

                    if (i + 2 < len) {
                        int32_t num_extra = len - i - 1;
                        Janet *extra = janet_tuple_begin(num_extra);
                        janet_tuple_flag(extra) |= JANET_TUPLE_FLAG_BRACKETCTOR;

                        for (int32_t j = 0; j < num_extra; ++j) {
                            extra[j] = values[j + i + 1];
                        }

                        janetc_error(c, janet_formatc("expected a single symbol follow '& in destructuring pattern, found %q", janet_wrap_tuple(janet_tuple_end(extra))));
                        return 1;
                    }

                    if (!janet_checktype(values[i + 1], JANET_SYMBOL)) {
                        janetc_error(c, janet_formatc("expected symbol following '& in destructuring pattern, found %q", values[i + 1]));
                        return 1;
                    }

                    JanetSlot argi = janetc_farslot(c);
                    JanetSlot arg  = janetc_farslot(c);
                    JanetSlot len  = janetc_farslot(c);

                    janetc_emit_si(c, JOP_LOAD_INTEGER, argi, i, 0);
                    janetc_emit_ss(c, JOP_LENGTH, len, right, 0);

                    /* loop condition - reuse arg slot for the condition result */
                    int32_t label_loop_start = janetc_emit_sss(c, JOP_LESS_THAN, arg, argi, len, 0);
                    int32_t label_loop_cond_jump = janetc_emit_si(c, JOP_JUMP_IF_NOT, arg, 0, 0);

                    /* loop body */
                    janetc_emit_sss(c, JOP_GET, arg, right, argi, 0);
                    janetc_emit_s(c, JOP_PUSH, arg, 0);
                    janetc_emit_ssi(c, JOP_ADD_IMMEDIATE, argi, argi, 1, 0);

                    /* loop - jump back to the start of the loop */
                    int32_t label_loop_loop = janet_v_count(c->buffer);
                    janetc_emit(c, JOP_JUMP);
                    int32_t label_loop_exit = janet_v_count(c->buffer);

                    /* avoid shifting negative numbers */
                    c->buffer[label_loop_cond_jump] |= (uint32_t)(label_loop_exit - label_loop_cond_jump) << 16;
                    c->buffer[label_loop_loop] |= (uint32_t)(label_loop_start - label_loop_loop) << 8;

                    janetc_freeslot(c, argi);
                    janetc_freeslot(c, arg);
                    janetc_freeslot(c, len);

                    janetc_emit_s(c, JOP_MAKE_TUPLE, nextright, 1);

                    leaf(c, janet_unwrap_symbol(values[i + 1]), nextright, attr);
                    janetc_freeslot(c, nextright);
                    break;
                }

                if (i < 0x100) {
                    janetc_emit_ssu(c, JOP_GET_INDEX, nextright, right, (uint8_t) i, 1);
                } else {
                    JanetSlot k = janetc_cslot(janet_wrap_integer(i));
                    janetc_emit_sss(c, JOP_IN, nextright, right, k, 1);
                }
                if (destructure(c, subval, nextright, leaf, attr))
                    janetc_freeslot(c, nextright);
            }
        }
        return 1;
        case JANET_TABLE:
        case JANET_STRUCT: {
            const JanetKV *kvs = NULL;
            int32_t cap = 0, len = 0;
            janet_dictionary_view(left, &kvs, &len, &cap);
            for (int32_t i = 0; i < cap; i++) {
                if (janet_checktype(kvs[i].key, JANET_NIL)) continue;
                JanetSlot nextright = janetc_farslot(c);
                JanetSlot k = janetc_value(janetc_fopts_default(c), kvs[i].key);
                janetc_emit_sss(c, JOP_IN, nextright, right, k, 1);
                if (destructure(c, kvs[i].value, nextright, leaf, attr))
                    janetc_freeslot(c, nextright);
            }
        }
        return 1;
    }
}

/* Create a source map for definitions. */
static const Janet *janetc_make_sourcemap(JanetCompiler *c) {
    Janet *tup = janet_tuple_begin(3);
    tup[0] = c->source ? janet_wrap_string(c->source) : janet_wrap_nil();
    tup[1] = janet_wrap_integer(c->current_mapping.line);
    tup[2] = janet_wrap_integer(c->current_mapping.column);
    return janet_tuple_end(tup);
}

static JanetSlot janetc_varset(JanetFopts opts, int32_t argn, const Janet *argv) {
    if (argn != 2) {
        janetc_cerror(opts.compiler, "expected 2 arguments to set");
        return janetc_cslot(janet_wrap_nil());
    }
    JanetFopts subopts = janetc_fopts_default(opts.compiler);
    if (janet_checktype(argv[0], JANET_SYMBOL)) {
        /* Normal var - (set a 1) */
        const uint8_t *sym = janet_unwrap_symbol(argv[0]);
        JanetSlot dest = janetc_resolve(opts.compiler, sym);
        if (!(dest.flags & JANET_SLOT_MUTABLE)) {
            janetc_cerror(opts.compiler, "cannot set constant");
            return janetc_cslot(janet_wrap_nil());
        }
        subopts.flags = JANET_FOPTS_HINT;
        subopts.hint = dest;
        JanetSlot ret = janetc_value(subopts, argv[1]);
        janetc_copy(opts.compiler, dest, ret);
        return ret;
    } else if (janet_checktype(argv[0], JANET_TUPLE)) {
        /* Set a field (setf behavior) - (set (tab :key) 2) */
        const Janet *tup = janet_unwrap_tuple(argv[0]);
        /* Tuple must have 2 elements */
        if (janet_tuple_length(tup) != 2) {
            janetc_cerror(opts.compiler, "expected 2 element tuple for l-value to set");
            return janetc_cslot(janet_wrap_nil());
        }
        JanetSlot ds = janetc_value(subopts, tup[0]);
        JanetSlot key = janetc_value(subopts, tup[1]);
        /* Can't be tail position because we will emit a PUT instruction afterwards */
        /* Also can't drop either */
        opts.flags &= ~(JANET_FOPTS_TAIL | JANET_FOPTS_DROP);
        JanetSlot rvalue = janetc_value(opts, argv[1]);
        /* Emit the PUT instruction */
        janetc_emit_sss(opts.compiler, JOP_PUT, ds, key, rvalue, 0);
        return rvalue;
    } else {
        /* Error */
        janetc_cerror(opts.compiler, "expected symbol or tuple for l-value to set");
        return janetc_cslot(janet_wrap_nil());
    }
}

/* Add attributes to a global def or var table */
static JanetTable *handleattr(JanetCompiler *c, const char *kind, int32_t argn, const Janet *argv) {
    int32_t i;
    JanetTable *tab = janet_table(2);
    const char *binding_name = janet_type(argv[0]) == JANET_SYMBOL
                               ? ((const char *)janet_unwrap_symbol(argv[0]))
                               : "<multiple bindings>";
    if (argn < 2) {
        janetc_error(c, janet_formatc("expected at least 2 arguments to %s", kind));
        return NULL;
    }
    for (i = 1; i < argn - 1; i++) {
        Janet attr = argv[i];
        switch (janet_type(attr)) {
            case JANET_TUPLE:
                janetc_cerror(c, "unexpected form - did you intend to use defn?");
                break;
            default:
                janetc_error(c, janet_formatc("cannot add metadata %v to binding %s", attr, binding_name));
                break;
            case JANET_KEYWORD:
                janet_table_put(tab, attr, janet_wrap_true());
                break;
            case JANET_STRING:
                janet_table_put(tab, janet_ckeywordv("doc"), attr);
                break;
            case JANET_STRUCT:
                janet_table_merge_struct(tab, janet_unwrap_struct(attr));
                break;
        }
    }
    return tab;
}

typedef struct SlotHeadPair {
    Janet lhs;
    JanetSlot rhs;
} SlotHeadPair;

SlotHeadPair *dohead_destructure(JanetCompiler *c, SlotHeadPair *into, JanetFopts opts, Janet lhs, Janet rhs) {

    /* Detect if we can do an optimization to avoid some allocations */
    int can_destructure_lhs = janet_checktype(lhs, JANET_TUPLE)
                              || janet_checktype(lhs, JANET_ARRAY);
    int rhs_is_indexed = janet_checktype(rhs, JANET_ARRAY)
                         || (janet_checktype(rhs, JANET_TUPLE) && (janet_tuple_flag(janet_unwrap_tuple(rhs)) & JANET_TUPLE_FLAG_BRACKETCTOR));
    uint32_t has_drop = opts.flags & JANET_FOPTS_DROP;

    JanetFopts subopts = janetc_fopts_default(c);
    subopts.flags = opts.flags & ~(JANET_FOPTS_TAIL | JANET_FOPTS_DROP);

    if (has_drop && can_destructure_lhs && rhs_is_indexed) {
        /* Code is of the form (def [a b] [1 2]), avoid the allocation of two tuples */
        JanetView view_lhs = {0};
        JanetView view_rhs = {0};
        janet_indexed_view(lhs, &view_lhs.items, &view_lhs.len);
        janet_indexed_view(rhs, &view_rhs.items, &view_rhs.len);
        int found_amp = 0;
        for (int32_t i = 0; i < view_lhs.len; i++) {
            if (janet_symeq(view_lhs.items[i], "&")) {
                found_amp = 1;
                /* Good error will be generated later. */
                break;
            }
        }
        if (!found_amp) {
            for (int32_t i = 0; i < view_lhs.len; i++) {
                Janet sub_rhs = view_rhs.len <= i ? janet_wrap_nil() : view_rhs.items[i];
                into = dohead_destructure(c, into, subopts, view_lhs.items[i], sub_rhs);
            }
            return into;
        }
    }

    /* No optimization, do the simple way */
    subopts.hint = opts.hint;
    JanetSlot ret = janetc_value(subopts, rhs);
    SlotHeadPair shp = {lhs, ret};
    janet_v_push(into, shp);
    return into;
}

/* Def or var a symbol in a local scope */
static int namelocal(JanetCompiler *c, const uint8_t *head, int32_t flags, JanetSlot ret) {
    int isUnnamedRegister = !(ret.flags & JANET_SLOT_NAMED) &&
                            ret.index > 0 &&
                            ret.envindex >= 0;
    /* optimization for `(def x my-def)` - don't emit a movn/movf instruction, we can just alias my-def */
    /* TODO - implement optimization for `(def x my-var)` correctly as well w/ de-aliasing */
    int canAlias = !(flags & JANET_SLOT_MUTABLE) &&
                   !(ret.flags & JANET_SLOT_MUTABLE) &&
                   (ret.flags & JANET_SLOT_NAMED) &&
                   (ret.index >= 0) &&
                   (ret.envindex == -1);
    if (canAlias) {
        ret.flags &= ~JANET_SLOT_MUTABLE;
        isUnnamedRegister = 1; /* don't free slot after use - is an alias for another slot */
    } else if (!isUnnamedRegister) {
        /* Slot is not able to be named */
        JanetSlot localslot = janetc_farslot(c);
        janetc_copy(c, localslot, ret);
        ret = localslot;
    }
    ret.flags |= flags;
    janetc_nameslot(c, head, ret);
    return !isUnnamedRegister;
}

static int varleaf(
    JanetCompiler *c,
    const uint8_t *sym,
    JanetSlot s,
    JanetTable *reftab) {
    if (c->scope->flags & JANET_SCOPE_TOP) {
        /* Global var, generate var */
        JanetSlot refslot;
        JanetTable *entry = janet_table_clone(reftab);

        Janet redef_kw = janet_ckeywordv("redef");
        int is_redef = janet_truthy(janet_table_get(c->env, redef_kw));

        JanetArray *ref;
        JanetBinding old_binding;
        if (is_redef && (old_binding = janet_resolve_ext(c->env, sym),
                         old_binding.type == JANET_BINDING_VAR)) {
            ref = janet_unwrap_array(old_binding.value);
        } else {
            ref = janet_array(1);
            janet_array_push(ref, janet_wrap_nil());
        }

        janet_table_put(entry, janet_ckeywordv("ref"), janet_wrap_array(ref));
        janet_table_put(entry, janet_ckeywordv("source-map"),
                        janet_wrap_tuple(janetc_make_sourcemap(c)));
        janet_table_put(c->env, janet_wrap_symbol(sym), janet_wrap_table(entry));
        refslot = janetc_cslot(janet_wrap_array(ref));
        janetc_emit_ssu(c, JOP_PUT_INDEX, refslot, s, 0, 0);
        return 1;
    } else {
        return namelocal(c, sym, JANET_SLOT_MUTABLE, s);
    }
}

static JanetSlot janetc_var(JanetFopts opts, int32_t argn, const Janet *argv) {
    JanetCompiler *c = opts.compiler;
    JanetTable *attr_table = handleattr(c, "var", argn, argv);
    if (c->result.status == JANET_COMPILE_ERROR) {
        return janetc_cslot(janet_wrap_nil());
    }
    SlotHeadPair *into = NULL;
    into = dohead_destructure(c, into, opts, argv[0], argv[argn - 1]);
    if (c->result.status == JANET_COMPILE_ERROR) {
        janet_v_free(into);
        return janetc_cslot(janet_wrap_nil());
    }
    JanetSlot ret;
    janet_assert(janet_v_count(into) > 0, "bad destructure");
    for (int32_t i = 0; i < janet_v_count(into); i++) {
        destructure(c, into[i].lhs, into[i].rhs, varleaf, attr_table);
        ret = into[i].rhs;
    }
    janet_v_free(into);
    return ret;
}

static int defleaf(
    JanetCompiler *c,
    const uint8_t *sym,
    JanetSlot s,
    JanetTable *tab) {
    if (c->scope->flags & JANET_SCOPE_TOP) {
        JanetTable *entry = janet_table_clone(tab);
        janet_table_put(entry, janet_ckeywordv("source-map"),
                        janet_wrap_tuple(janetc_make_sourcemap(c)));

        Janet redef_kw = janet_ckeywordv("redef");
        int is_redef = janet_truthy(janet_table_get(c->env, redef_kw));
        if (is_redef) janet_table_put(entry, redef_kw, janet_wrap_true());

        if (is_redef) {
            JanetBinding binding = janet_resolve_ext(c->env, sym);
            JanetArray *ref;
            if (binding.type == JANET_BINDING_DYNAMIC_DEF || binding.type == JANET_BINDING_DYNAMIC_MACRO) {
                ref = janet_unwrap_array(binding.value);
            } else {
                ref = janet_array(1);
                janet_array_push(ref, janet_wrap_nil());
            }
            janet_table_put(entry, janet_ckeywordv("ref"), janet_wrap_array(ref));
            JanetSlot refslot = janetc_cslot(janet_wrap_array(ref));
            janetc_emit_ssu(c, JOP_PUT_INDEX, refslot, s, 0, 0);
        } else {
            JanetSlot valsym = janetc_cslot(janet_ckeywordv("value"));
            JanetSlot tabslot = janetc_cslot(janet_wrap_table(entry));
            janetc_emit_sss(c, JOP_PUT, tabslot, valsym, s, 0);
        }

        /* Add env entry to env */
        janet_table_put(c->env, janet_wrap_symbol(sym), janet_wrap_table(entry));
    }
    return namelocal(c, sym, 0, s);
}

static JanetSlot janetc_def(JanetFopts opts, int32_t argn, const Janet *argv) {
    JanetCompiler *c = opts.compiler;
    JanetTable *attr_table = handleattr(c, "def", argn, argv);
    if (c->result.status == JANET_COMPILE_ERROR) {
        return janetc_cslot(janet_wrap_nil());
    }
    opts.flags &= ~JANET_FOPTS_HINT;
    SlotHeadPair *into = NULL;
    into = dohead_destructure(c, into, opts, argv[0], argv[argn - 1]);
    if (c->result.status == JANET_COMPILE_ERROR) {
        janet_v_free(into);
        return janetc_cslot(janet_wrap_nil());
    }
    JanetSlot ret;
    janet_assert(janet_v_count(into) > 0, "bad destructure");
    for (int32_t i = 0; i < janet_v_count(into); i++) {
        destructure(c, into[i].lhs, into[i].rhs, defleaf, attr_table);
        ret = into[i].rhs;
    }
    janet_v_free(into);
    return ret;
}

/* Check if a form matches the pattern (= nil _) or (not= nil _) */
static int janetc_check_nil_form(Janet x, Janet *capture, uint32_t fun_tag) {
    if (!janet_checktype(x, JANET_TUPLE)) return 0;
    JanetTuple tup = janet_unwrap_tuple(x);
    if (3 != janet_tuple_length(tup)) return 0;
    Janet op1 = tup[0];
    if (!janet_checktype(op1, JANET_FUNCTION)) return 0;
    JanetFunction *fun = janet_unwrap_function(op1);
    uint32_t tag = fun->def->flags & JANET_FUNCDEF_FLAG_TAG;
    if (tag != fun_tag) return 0;
    if (janet_checktype(tup[1], JANET_NIL)) {
        *capture = tup[2];
        return 1;
    } else if (janet_checktype(tup[2], JANET_NIL)) {
        *capture = tup[1];
        return 1;
    }
    return 0;
}

/*
 * :condition
 * ...
 * jump-if-not condition :right
 * :left
 * ...
 * jump done (only if not tail)
 * :right
 * ...
 * :done
 */
static JanetSlot janetc_if(JanetFopts opts, int32_t argn, const Janet *argv) {
    JanetCompiler *c = opts.compiler;
    int32_t labelr, labeljr, labeld, labeljd;
    JanetFopts condopts, bodyopts;
    JanetSlot cond, left, right, target;
    Janet truebody, falsebody;
    JanetScope condscope, tempscope;
    const int tail = opts.flags & JANET_FOPTS_TAIL;
    const int drop = opts.flags & JANET_FOPTS_DROP;
    uint8_t ifnjmp = JOP_JUMP_IF_NOT;

    if (argn < 2 || argn > 3) {
        janetc_cerror(c, "expected 2 or 3 arguments to if");
        return janetc_cslot(janet_wrap_nil());
    }

    /* Get the bodies of the if expression */
    truebody = argv[1];
    falsebody = argn > 2 ? argv[2] : janet_wrap_nil();

    /* Get options */
    condopts = janetc_fopts_default(c);
    bodyopts = opts;
    bodyopts.flags &= ~JANET_FOPTS_ACCEPT_SPLICE;

    /* Set target for compilation */
    target = (drop || tail)
             ? janetc_cslot(janet_wrap_nil())
             : janetc_gettarget(opts);

    /* Compile condition */
    janetc_scope(&condscope, c, 0, "if");

    Janet condform = argv[0];
    if (janetc_check_nil_form(condform, &condform, JANET_FUN_EQ)) {
        ifnjmp = JOP_JUMP_IF_NOT_NIL;
    } else if (janetc_check_nil_form(condform, &condform, JANET_FUN_NEQ)) {
        ifnjmp = JOP_JUMP_IF_NIL;
    }

    cond = janetc_value(condopts, condform);

    /* Check constant condition. */
    /* TODO: Use type info for more short circuits */
    if (cond.flags & JANET_SLOT_CONSTANT) {
        int swap_condition = 0;
        if (ifnjmp == JOP_JUMP_IF_NOT && !janet_truthy(cond.constant)) swap_condition = 1;
        if (ifnjmp == JOP_JUMP_IF_NIL && janet_checktype(cond.constant, JANET_NIL)) swap_condition = 1;
        if (ifnjmp == JOP_JUMP_IF_NOT_NIL && !janet_checktype(cond.constant, JANET_NIL)) swap_condition = 1;
        if (swap_condition) {
            /* Swap the true and false bodies */
            Janet temp = falsebody;
            falsebody = truebody;
            truebody = temp;
        }
        janetc_scope(&tempscope, c, 0, "if-true");
        right = janetc_value(bodyopts, truebody);
        if (!drop && !tail) janetc_copy(c, target, right);
        janetc_popscope(c);
        if (!janet_checktype(falsebody, JANET_NIL)) {
            janetc_throwaway(bodyopts, falsebody);
        }
        janetc_popscope(c);
        return target;
    }

    /* Compile jump to right */
    labeljr = janetc_emit_si(c, ifnjmp, cond, 0, 0);

    /* Condition left body */
    janetc_scope(&tempscope, c, 0, "if-true");
    left = janetc_value(bodyopts, truebody);
    if (!drop && !tail) janetc_copy(c, target, left);
    janetc_popscope(c);

    /* Compile jump to done */
    labeljd = janet_v_count(c->buffer);
    if (!tail && !(drop && janet_checktype(falsebody, JANET_NIL))) janetc_emit(c, JOP_JUMP);

    /* Compile right body */
    labelr = janet_v_count(c->buffer);
    janetc_scope(&tempscope, c, 0, "if-false");
    right = janetc_value(bodyopts, falsebody);
    if (!drop && !tail) janetc_copy(c, target, right);
    janetc_popscope(c);

    /* Pop main scope */
    janetc_popscope(c);

    /* Write jumps - only add jump lengths if jump actually emitted */
    labeld = janet_v_count(c->buffer);
    c->buffer[labeljr] |= (labelr - labeljr) << 16;
    if (!tail) c->buffer[labeljd] |= (labeld - labeljd) << 8;

    if (tail) target.flags |= JANET_SLOT_RETURNED;
    return target;
}

/* Compile a do form. Do forms execute their body sequentially and
 * evaluate to the last expression in the body. */
static JanetSlot janetc_do(JanetFopts opts, int32_t argn, const Janet *argv) {
    int32_t i;
    JanetSlot ret = janetc_cslot(janet_wrap_nil());
    JanetCompiler *c = opts.compiler;
    JanetFopts subopts = janetc_fopts_default(c);
    JanetScope tempscope;
    janetc_scope(&tempscope, c, 0, "do");
    for (i = 0; i < argn; i++) {
        if (i != argn - 1) {
            subopts.flags = JANET_FOPTS_DROP;
        } else {
            subopts = opts;
            subopts.flags &= ~JANET_FOPTS_ACCEPT_SPLICE;
        }
        ret = janetc_value(subopts, argv[i]);
        if (i != argn - 1) {
            janetc_freeslot(c, ret);
        }
    }
    janetc_popscope_keepslot(c, ret);
    return ret;
}

/* Compile an upscope form. Upscope forms execute their body sequentially and
 * evaluate to the last expression in the body, but without lexical scope. */
static JanetSlot janetc_upscope(JanetFopts opts, int32_t argn, const Janet *argv) {
    int32_t i;
    JanetSlot ret = janetc_cslot(janet_wrap_nil());
    JanetCompiler *c = opts.compiler;
    JanetFopts subopts = janetc_fopts_default(c);
    for (i = 0; i < argn; i++) {
        if (i != argn - 1) {
            subopts.flags = JANET_FOPTS_DROP;
        } else {
            subopts = opts;
            subopts.flags &= ~JANET_FOPTS_ACCEPT_SPLICE;
        }
        ret = janetc_value(subopts, argv[i]);
        if (i != argn - 1) {
            janetc_freeslot(c, ret);
        }
    }
    return ret;
}

/* Add a funcdef to the top most function scope */
static int32_t janetc_addfuncdef(JanetCompiler *c, JanetFuncDef *def) {
    JanetScope *scope = c->scope;
    while (scope) {
        if (scope->flags & JANET_SCOPE_FUNCTION)
            break;
        scope = scope->parent;
    }
    janet_assert(scope, "could not add funcdef");
    janet_v_push(scope->defs, def);
    return janet_v_count(scope->defs) - 1;
}

/*
 * break
 *
 * jump :end or retn if in function
 */
static JanetSlot janetc_break(JanetFopts opts, int32_t argn, const Janet *argv) {
    JanetCompiler *c = opts.compiler;
    JanetScope *scope = c->scope;
    if (argn > 1) {
        janetc_cerror(c, "expected at most 1 argument");
        return janetc_cslot(janet_wrap_nil());
    }

    /* Find scope to break from */
    while (scope) {
        if (scope->flags & (JANET_SCOPE_FUNCTION | JANET_SCOPE_WHILE))
            break;
        scope = scope->parent;
    }
    if (NULL == scope) {
        janetc_cerror(c, "break must occur in while loop or closure");
        return janetc_cslot(janet_wrap_nil());
    }

    /* Emit code to break from that scope */
    JanetFopts subopts = janetc_fopts_default(c);
    if (scope->flags & JANET_SCOPE_FUNCTION) {
        if (!(scope->flags & JANET_SCOPE_WHILE) && argn) {
            /* Closure body with return argument */
            subopts.flags |= JANET_FOPTS_TAIL;
            janetc_value(subopts, argv[0]);
            return janetc_cslot(janet_wrap_nil());
        } else {
            /* while loop IIFE or no argument */
            if (argn) {
                subopts.flags |= JANET_FOPTS_DROP;
                janetc_value(subopts, argv[0]);
            }
            janetc_emit(c, JOP_RETURN_NIL);
            return janetc_cslot(janet_wrap_nil());
        }
    } else {
        if (argn) {
            subopts.flags |= JANET_FOPTS_DROP;
            janetc_value(subopts, argv[0]);
        }
        /* Tag the instruction so the while special can turn it into a proper jump */
        janetc_emit(c, 0x80 | JOP_JUMP);
        return janetc_cslot(janet_wrap_nil());
    }
}

/*
 * :whiletop
 * ...
 * :condition
 * jump-if-not cond :done
 * ...
 * jump :whiletop
 * :done
 */
static JanetSlot janetc_while(JanetFopts opts, int32_t argn, const Janet *argv) {
    JanetCompiler *c = opts.compiler;
    JanetSlot cond;
    JanetFopts subopts = janetc_fopts_default(c);
    JanetScope tempscope;
    int32_t labelwt, labeld, labeljt, labelc, i;
    int infinite = 0;
    int is_nil_form = 0;
    int is_notnil_form = 0;
    uint8_t ifjmp = JOP_JUMP_IF;
    uint8_t ifnjmp = JOP_JUMP_IF_NOT;

    if (argn < 1) {
        janetc_cerror(c, "expected at least 1 argument to while");
        return janetc_cslot(janet_wrap_nil());
    }

    labelwt = janet_v_count(c->buffer);

    janetc_scope(&tempscope, c, JANET_SCOPE_WHILE, "while");

    /* Check for `(= nil _)` or `(not= nil _)` in condition, and if so, use the
     * jmpnl or jmpnn instructions. This let's us implement `(each ...)`
     * more efficiently. */
    Janet condform = argv[0];
    if (janetc_check_nil_form(condform, &condform, JANET_FUN_EQ)) {
        is_nil_form = 1;
        ifjmp = JOP_JUMP_IF_NIL;
        ifnjmp = JOP_JUMP_IF_NOT_NIL;
    }
    if (janetc_check_nil_form(condform, &condform, JANET_FUN_NEQ)) {
        is_notnil_form = 1;
        ifjmp = JOP_JUMP_IF_NOT_NIL;
        ifnjmp = JOP_JUMP_IF_NIL;
    }

    /* Compile condition */
    cond = janetc_value(subopts, condform);

    /* Check for constant condition */
    if (cond.flags & JANET_SLOT_CONSTANT) {
        /* Loop never executes */
        int never_executes = is_nil_form
                             ? !janet_checktype(cond.constant, JANET_NIL)
                             : is_notnil_form
                             ? janet_checktype(cond.constant, JANET_NIL)
                             : !janet_truthy(cond.constant);
        if (never_executes) {
            janetc_popscope(c);
            return janetc_cslot(janet_wrap_nil());
        }
        /* Infinite loop */
        infinite = 1;
    }

    /* Infinite loop does not need to check condition */
    labelc = infinite
             ? 0
             : janetc_emit_si(c, ifnjmp, cond, 0, 0);

    /* Compile body */
    for (i = 1; i < argn; i++) {
        subopts.flags = JANET_FOPTS_DROP;
        janetc_freeslot(c, janetc_value(subopts, argv[i]));
    }

    /* Check if closure created in while scope. If so,
     * recompile in a function scope. */
    if (tempscope.flags & JANET_SCOPE_CLOSURE) {
        subopts = janetc_fopts_default(c);
        tempscope.flags |= JANET_SCOPE_UNUSED;
        janetc_popscope(c);
        if (c->buffer) janet_v__cnt(c->buffer) = labelwt;
        if (c->mapbuffer) janet_v__cnt(c->mapbuffer) = labelwt;

        janetc_scope(&tempscope, c, JANET_SCOPE_FUNCTION, "while-iife");

        /* Recompile in the function scope */
        cond = janetc_value(subopts, condform);
        if (!(cond.flags & JANET_SLOT_CONSTANT)) {
            /* If not an infinite loop, return nil when condition false */
            janetc_emit_si(c, ifjmp, cond, 2, 0);
            janetc_emit(c, JOP_RETURN_NIL);
        }
        for (i = 1; i < argn; i++) {
            subopts.flags = JANET_FOPTS_DROP;
            janetc_freeslot(c, janetc_value(subopts, argv[i]));
        }
        /* But now add tail recursion */
        int32_t tempself = janetc_regalloc_temp(&tempscope.ra, JANETC_REGTEMP_0);
        janetc_emit(c, JOP_LOAD_SELF | (tempself << 8));
        janetc_emit(c, JOP_TAILCALL | (tempself << 8));
        janetc_regalloc_freetemp(&c->scope->ra, tempself, JANETC_REGTEMP_0);
        /* Compile function */
        JanetFuncDef *def = janetc_pop_funcdef(c);
        def->name = janet_cstring("_while");
        janet_def_addflags(def);
        int32_t defindex = janetc_addfuncdef(c, def);
        /* And then load the closure and call it. */
        int32_t cloreg = janetc_regalloc_temp(&c->scope->ra, JANETC_REGTEMP_0);
        janetc_emit(c, JOP_CLOSURE | (cloreg << 8) | (defindex << 16));
        janetc_emit(c, JOP_CALL | (cloreg << 8) | (cloreg << 16));
        janetc_regalloc_freetemp(&c->scope->ra, cloreg, JANETC_REGTEMP_0);
        c->scope->flags |= JANET_SCOPE_CLOSURE;
        return janetc_cslot(janet_wrap_nil());
    }

    /* Compile jump to :whiletop */
    labeljt = janet_v_count(c->buffer);
    janetc_emit(c, JOP_JUMP);

    /* Calculate jumps */
    labeld = janet_v_count(c->buffer);
    if (!infinite) c->buffer[labelc] |= (uint32_t)(labeld - labelc) << 16;
    c->buffer[labeljt] |= (uint32_t)(labelwt - labeljt) << 8;

    /* Calculate breaks */
    for (int32_t i = labelwt; i < labeld; i++) {
        if (c->buffer[i] == (0x80 | JOP_JUMP)) {
            c->buffer[i] = JOP_JUMP | ((labeld - i) << 8);
        }
    }

    /* Pop scope and return nil slot */
    janetc_popscope(c);

    return janetc_cslot(janet_wrap_nil());
}

static JanetSlot janetc_fn(JanetFopts opts, int32_t argn, const Janet *argv) {
    JanetCompiler *c = opts.compiler;
    JanetFuncDef *def;
    JanetSlot ret;
    Janet head;
    JanetScope fnscope;
    int32_t paramcount, argi, parami, arity, min_arity = 0, max_arity, defindex, i;
    JanetFopts subopts = janetc_fopts_default(c);
    const Janet *params;
    const char *errmsg = NULL;

    /* Function flags */
    int vararg = 0;
    int structarg = 0;
    int allow_extra = 0;
    int selfref = 0;
    int hasname = 0;
    int seenamp = 0;
    int seenopt = 0;
    int namedargs = 0;

    /* Begin function */
    c->scope->flags |= JANET_SCOPE_CLOSURE;
    janetc_scope(&fnscope, c, JANET_SCOPE_FUNCTION, "function");

    if (argn == 0) {
        errmsg = "expected at least 1 argument to function literal";
        goto error;
    }

    /* Read function parameters */
    parami = 0;
    head = argv[0];
    if (janet_checktype(head, JANET_SYMBOL)) {
        selfref = 1;
        hasname = 1;
        parami = 1;
    } else if (janet_checktype(head, JANET_KEYWORD)) {
        hasname = 1;
        parami = 1;
    }
    if (parami >= argn || !janet_checktype(argv[parami], JANET_TUPLE)) {
        errmsg = "expected function parameters";
        goto error;
    }

    /* Keep track of destructured parameters */
    JanetSlot *destructed_params = NULL;
    JanetSlot *named_params = NULL;
    JanetTable *named_table = NULL;
    JanetSlot named_slot;

    /* Compile function parameters */
    params = janet_unwrap_tuple(argv[parami]);
    paramcount = janet_tuple_length(params);
    arity = paramcount;
    for (i = 0; i < paramcount; i++) {
        Janet param = params[i];
        if (namedargs) {
            arity--;
            if (!janet_checktype(param, JANET_SYMBOL)) {
                errmsg = "only named arguments can follow &named";
                goto error;
            }
            Janet key = janet_wrap_keyword(janet_unwrap_symbol(param));
            janet_table_put(named_table, key, param);
            janet_v_push(named_params, janetc_farslot(c));
        } else if (janet_checktype(param, JANET_SYMBOL)) {
            /* Check for varargs and unfixed arity */
            const uint8_t *sym = janet_unwrap_symbol(param);
            if (sym[0] == '&') {
                if (!janet_cstrcmp(sym, "&")) {
                    if (seenamp) {
                        errmsg = "& in unexpected location";
                        goto error;
                    } else if (i == paramcount - 1) {
                        allow_extra = 1;
                        arity--;
                    } else if (i == paramcount - 2) {
                        vararg = 1;
                        arity -= 2;
                    } else {
                        errmsg = "& in unexpected location";
                        goto error;
                    }
                    seenamp = 1;
                } else if (!janet_cstrcmp(sym, "&opt")) {
                    if (seenopt) {
                        errmsg = "only one &opt allowed";
                        goto error;
                    } else if (i == paramcount - 1) {
                        errmsg = "&opt cannot be last item in parameter list";
                        goto error;
                    }
                    min_arity = i;
                    arity--;
                    seenopt = 1;
                } else if (!janet_cstrcmp(sym, "&keys")) {
                    if (seenamp) {
                        errmsg = "&keys in unexpected location";
                        goto error;
                    } else if (i == paramcount - 2) {
                        vararg = 1;
                        structarg = 1;
                        arity -= 2;
                    } else {
                        errmsg = "&keys in unexpected location";
                        goto error;
                    }
                    seenamp = 1;
                } else if (!janet_cstrcmp(sym, "&named")) {
                    if (seenamp) {
                        errmsg = "&named in unexpected location";
                        goto error;
                    }
                    vararg = 1;
                    structarg = 1;
                    arity--;
                    seenamp = 1;
                    namedargs = 1;
                    named_table = janet_table(10);
                    named_slot = janetc_farslot(c);
                } else {
                    janetc_nameslot(c, sym, janetc_farslot(c));
                }
            } else {
                janetc_nameslot(c, sym, janetc_farslot(c));
            }
        } else {
            janet_v_push(destructed_params, janetc_farslot(c));
        }
    }

    /* Compile destructed params */
    int32_t j = 0;
    for (i = 0; i < paramcount; i++) {
        Janet param = params[i];
        if (!janet_checktype(param, JANET_SYMBOL)) {
            janet_assert(janet_v_count(destructed_params) > j, "out of bounds");
            JanetSlot reg = destructed_params[j++];
            destructure(c, param, reg, defleaf, NULL);
            janetc_freeslot(c, reg);
        }
    }
    janet_v_free(destructed_params);

    /* Compile named arguments */
    if (namedargs) {
        Janet param = janet_wrap_table(named_table);
        destructure(c, param, named_slot, defleaf, NULL);
        janetc_freeslot(c, named_slot);
        janet_v_free(named_params);
    }

    max_arity = (vararg || allow_extra) ? INT32_MAX : arity;
    if (!seenopt) min_arity = arity;

    /* Check for self ref (also avoid if arguments shadow own name) */
    if (selfref) {
        /* Check if the parameters shadow the function name. If so, don't
         * emit JOP_LOAD_SELF and add a binding since that most users
         * seem to expect that function parameters take precedence over the
         * function name */
        const uint8_t *sym = janet_unwrap_symbol(head);
        int32_t len = janet_v_count(c->scope->syms);
        int found = 0;
        for (int32_t i = 0; i < len; i++) {
            if (c->scope->syms[i].sym == sym) {
                found = 1;
            }
        }
        if (!found) {
            JanetSlot slot = janetc_farslot(c);
            slot.flags = JANET_SLOT_NAMED | JANET_FUNCTION;
            janetc_emit_s(c, JOP_LOAD_SELF, slot, 1);
            janetc_nameslot(c, sym, slot);
        }
    }

    /* Compile function body */
    if (parami + 1 == argn) {
        janetc_emit(c, JOP_RETURN_NIL);
    } else {
        for (argi = parami + 1; argi < argn; argi++) {
            subopts.flags = (argi == (argn - 1)) ? JANET_FOPTS_TAIL : JANET_FOPTS_DROP;
            janetc_value(subopts, argv[argi]);
            if (c->result.status == JANET_COMPILE_ERROR)
                goto error2;
        }
    }

    /* Build function */
    def = janetc_pop_funcdef(c);
    def->arity = arity;
    def->min_arity = min_arity;
    def->max_arity = max_arity;
    if (vararg) def->flags |= JANET_FUNCDEF_FLAG_VARARG;
    if (structarg) def->flags |= JANET_FUNCDEF_FLAG_STRUCTARG;

    if (hasname) def->name = janet_unwrap_symbol(head); /* Also correctly unwraps keyword */
    janet_def_addflags(def);
    defindex = janetc_addfuncdef(c, def);

    /* Ensure enough slots for vararg function. */
    if (arity + vararg > def->slotcount) def->slotcount = arity + vararg;

    /* Instantiate closure */
    ret = janetc_gettarget(opts);
    janetc_emit_su(c, JOP_CLOSURE, ret, defindex, 1);
    return ret;

error:
    janetc_cerror(c, errmsg);
error2:
    janetc_popscope(c);
    return janetc_cslot(janet_wrap_nil());
}

/* Keep in lexicographic order */
static const JanetSpecial janetc_specials[] = {
    {"break", janetc_break},
    {"def", janetc_def},
    {"do", janetc_do},
    {"fn", janetc_fn},
    {"if", janetc_if},
    {"quasiquote", janetc_quasiquote},
    {"quote", janetc_quote},
    {"set", janetc_varset},
    {"splice", janetc_splice},
    {"unquote", janetc_unquote},
    {"upscope", janetc_upscope},
    {"var", janetc_var},
    {"while", janetc_while}
};

/* Find a special */
const JanetSpecial *janetc_special(const uint8_t *name) {
    return janet_strbinsearch(
               &janetc_specials,
               sizeof(janetc_specials) / sizeof(JanetSpecial),
               sizeof(JanetSpecial),
               name);
}
