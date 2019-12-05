/*
* Copyright (c) 2019 Calvin Rose
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
#include <janet.h>
#include "compile.h"
#include "emit.h"
#include "vector.h"
#include "util.h"
#include "state.h"
#endif

JanetFopts janetc_fopts_default(JanetCompiler *c) {
    JanetFopts ret;
    ret.compiler = c;
    ret.flags = 0;
    ret.hint = janetc_cslot(janet_wrap_nil());
    return ret;
}

/* Throw an error with a janet string. */
void janetc_error(JanetCompiler *c, const uint8_t *m) {
    /* Don't override first error */
    if (c->result.status == JANET_COMPILE_ERROR) {
        return;
    }
    c->result.status = JANET_COMPILE_ERROR;
    c->result.error = m;
}

/* Throw an error with a message in a cstring */
void janetc_cerror(JanetCompiler *c, const char *m) {
    janetc_error(c, janet_cstring(m));
}

/* Free a slot */
void janetc_freeslot(JanetCompiler *c, JanetSlot s) {
    if (s.flags & (JANET_SLOT_CONSTANT | JANET_SLOT_REF | JANET_SLOT_NAMED)) return;
    if (s.envindex >= 0) return;
    janetc_regalloc_free(&c->scope->ra, s.index);
}

/* Add a slot to a scope with a symbol associated with it (def or var). */
void janetc_nameslot(JanetCompiler *c, const uint8_t *sym, JanetSlot s) {
    SymPair sp;
    sp.sym = sym;
    sp.slot = s;
    sp.keep = 0;
    sp.slot.flags |= JANET_SLOT_NAMED;
    janet_v_push(c->scope->syms, sp);
}

/* Create a slot with a constant */
JanetSlot janetc_cslot(Janet x) {
    JanetSlot ret;
    ret.flags = (1 << janet_type(x)) | JANET_SLOT_CONSTANT;
    ret.index = -1;
    ret.constant = x;
    ret.envindex = -1;
    return ret;
}

/* Get a local slot */
JanetSlot janetc_farslot(JanetCompiler *c) {
    JanetSlot ret;
    ret.flags = JANET_SLOTTYPE_ANY;
    ret.index = janetc_allocfar(c);
    ret.constant = janet_wrap_nil();
    ret.envindex = -1;
    return ret;
}

/* Enter a new scope */
void janetc_scope(JanetScope *s, JanetCompiler *c, int flags, const char *name) {
    JanetScope scope;
    scope.name = name;
    scope.child = NULL;
    scope.consts = NULL;
    scope.syms = NULL;
    scope.envs = NULL;
    scope.defs = NULL;
    scope.bytecode_start = janet_v_count(c->buffer);
    scope.flags = flags;
    scope.parent = c->scope;
    /* Inherit slots */
    if ((!(flags & JANET_SCOPE_FUNCTION)) && c->scope) {
        janetc_regalloc_clone(&scope.ra, &(c->scope->ra));
    } else {
        janetc_regalloc_init(&scope.ra);
    }
    /* Link parent and child and update pointer */
    if (c->scope)
        c->scope->child = s;
    c->scope = s;
    *s = scope;
}

/* Leave a scope. */
void janetc_popscope(JanetCompiler *c) {
    JanetScope *oldscope = c->scope;
    JanetScope *newscope = oldscope->parent;
    /* Move free slots to parent scope if not a new function.
     * We need to know the total number of slots used when compiling the function. */
    if (!(oldscope->flags & (JANET_SCOPE_FUNCTION | JANET_SCOPE_UNUSED)) && newscope) {
        /* Parent scopes inherit child's closure flag. Needed
         * for while loops. (if a while loop creates a closure, it
         * is compiled to a tail recursive iife) */
        if (oldscope->flags & JANET_SCOPE_CLOSURE) {
            newscope->flags |= JANET_SCOPE_CLOSURE;
        }
        if (newscope->ra.max < oldscope->ra.max)
            newscope->ra.max = oldscope->ra.max;

        /* Keep upvalue slots */
        for (int32_t i = 0; i < janet_v_count(oldscope->syms); i++) {
            SymPair pair = oldscope->syms[i];
            if (pair.keep) {
                /* The variable should not be lexically accessible */
                pair.sym = NULL;
                janet_v_push(newscope->syms, pair);
                janetc_regalloc_touch(&newscope->ra, pair.slot.index);
            }
        }

    }
    /* Free the old scope */
    janet_v_free(oldscope->consts);
    janet_v_free(oldscope->syms);
    janet_v_free(oldscope->envs);
    janet_v_free(oldscope->defs);
    janetc_regalloc_deinit(&oldscope->ra);
    /* Update pointer */
    if (newscope)
        newscope->child = NULL;
    c->scope = newscope;
}

/* Leave a scope but keep a slot allocated. */
void janetc_popscope_keepslot(JanetCompiler *c, JanetSlot retslot) {
    JanetScope *scope;
    janetc_popscope(c);
    scope = c->scope;
    if (scope && retslot.envindex < 0 && retslot.index >= 0) {
        janetc_regalloc_touch(&scope->ra, retslot.index);
    }
}

/* Allow searching for symbols. Return information about the symbol */
JanetSlot janetc_resolve(
    JanetCompiler *c,
    const uint8_t *sym) {

    JanetSlot ret = janetc_cslot(janet_wrap_nil());
    JanetScope *scope = c->scope;
    SymPair *pair;
    int foundlocal = 1;
    int unused = 0;

    /* Search scopes for symbol, starting from top */
    while (scope) {
        int32_t i, len;
        if (scope->flags & JANET_SCOPE_UNUSED)
            unused = 1;
        len = janet_v_count(scope->syms);
        /* Search in reverse order */
        for (i = len - 1; i >= 0; i--) {
            pair = scope->syms + i;
            if (pair->sym == sym) {
                ret = pair->slot;
                goto found;
            }
        }
        if (scope->flags & JANET_SCOPE_FUNCTION)
            foundlocal = 0;
        scope = scope->parent;
    }

    /* Symbol not found - check for global */
    {
        Janet check;
        JanetBindingType btype = janet_resolve(c->env, sym, &check);
        switch (btype) {
            default:
            case JANET_BINDING_NONE:
                janetc_error(c, janet_formatc("unknown symbol %q", sym));
                return janetc_cslot(janet_wrap_nil());
            case JANET_BINDING_DEF:
            case JANET_BINDING_MACRO: /* Macro should function like defs when not in calling pos */
                return janetc_cslot(check);
            case JANET_BINDING_VAR: {
                JanetSlot ret = janetc_cslot(check);
                /* TODO save type info */
                ret.flags |= JANET_SLOT_REF | JANET_SLOT_NAMED | JANET_SLOT_MUTABLE | JANET_SLOTTYPE_ANY;
                ret.flags &= ~JANET_SLOT_CONSTANT;
                return ret;
            }
        }
    }

    /* Symbol was found */
found:

    /* Constants can be returned immediately (they are stateless) */
    if (ret.flags & (JANET_SLOT_CONSTANT | JANET_SLOT_REF))
        return ret;

    /* Unused references and locals shouldn't add captured envs. */
    if (unused || foundlocal) {
        ret.envindex = -1;
        return ret;
    }

    /* non-local scope needs to expose its environment */
    pair->keep = 1;
    while (scope && !(scope->flags & JANET_SCOPE_FUNCTION))
        scope = scope->parent;
    janet_assert(scope, "invalid scopes");
    scope->flags |= JANET_SCOPE_ENV;
    scope = scope->child;

    /* Propagate env up to current scope */
    int32_t envindex = -1;
    while (scope) {
        if (scope->flags & JANET_SCOPE_FUNCTION) {
            int32_t j, len;
            int scopefound = 0;
            /* Check if scope already has env. If so, break */
            len = janet_v_count(scope->envs);
            for (j = 0; j < len; j++) {
                if (scope->envs[j] == envindex) {
                    scopefound = 1;
                    envindex = j;
                    break;
                }
            }
            /* Add the environment if it is not already referenced */
            if (!scopefound) {
                len = janet_v_count(scope->envs);
                janet_v_push(scope->envs, envindex);
                envindex = len;
            }
        }
        scope = scope->child;
    }

    ret.envindex = envindex;
    return ret;
}

/* Generate the return instruction for a slot. */
JanetSlot janetc_return(JanetCompiler *c, JanetSlot s) {
    if (!(s.flags & JANET_SLOT_RETURNED)) {
        if (s.flags & JANET_SLOT_CONSTANT && janet_checktype(s.constant, JANET_NIL))
            janetc_emit(c, JOP_RETURN_NIL);
        else
            janetc_emit_s(c, JOP_RETURN, s, 0);
        s.flags |= JANET_SLOT_RETURNED;
    }
    return s;
}

/* Get a target slot for emitting an instruction. */
JanetSlot janetc_gettarget(JanetFopts opts) {
    JanetSlot slot;
    if ((opts.flags & JANET_FOPTS_HINT) &&
            (opts.hint.envindex < 0) &&
            (opts.hint.index >= 0 && opts.hint.index <= 0xFF)) {
        slot = opts.hint;
    } else {
        slot.envindex = -1;
        slot.constant = janet_wrap_nil();
        slot.flags = 0;
        slot.index = janetc_allocfar(opts.compiler);
    }
    return slot;
}

/* Get a bunch of slots for function arguments */
JanetSlot *janetc_toslots(JanetCompiler *c, const Janet *vals, int32_t len) {
    int32_t i;
    JanetSlot *ret = NULL;
    JanetFopts subopts = janetc_fopts_default(c);
    for (i = 0; i < len; i++) {
        janet_v_push(ret, janetc_value(subopts, vals[i]));
    }
    return ret;
}

/* Get a bunch of slots for function arguments */
JanetSlot *janetc_toslotskv(JanetCompiler *c, Janet ds) {
    JanetSlot *ret = NULL;
    JanetFopts subopts = janetc_fopts_default(c);
    const JanetKV *kvs = NULL;
    int32_t cap = 0, len = 0;
    janet_dictionary_view(ds, &kvs, &len, &cap);
    for (int32_t i = 0; i < cap; i++) {
        if (janet_checktype(kvs[i].key, JANET_NIL)) continue;
        janet_v_push(ret, janetc_value(subopts, kvs[i].key));
        janet_v_push(ret, janetc_value(subopts, kvs[i].value));
    }
    return ret;
}

/* Push slots loaded via janetc_toslots. Return the minimum number of slots pushed,
 * or -1 - min_arity if there is a splice. (if there is no splice, min_arity is also
 * the maximum possible arity). */
int32_t janetc_pushslots(JanetCompiler *c, JanetSlot *slots) {
    int32_t i;
    int32_t count = janet_v_count(slots);
    int32_t min_arity = 0;
    int has_splice = 0;
    for (i = 0; i < count;) {
        if (slots[i].flags & JANET_SLOT_SPLICED) {
            janetc_emit_s(c, JOP_PUSH_ARRAY, slots[i], 0);
            i++;
            has_splice = 1;
        } else if (i + 1 == count) {
            janetc_emit_s(c, JOP_PUSH, slots[i], 0);
            i++;
            min_arity++;
        } else if (slots[i + 1].flags & JANET_SLOT_SPLICED) {
            janetc_emit_s(c, JOP_PUSH, slots[i], 0);
            janetc_emit_s(c, JOP_PUSH_ARRAY, slots[i + 1], 0);
            i += 2;
            min_arity++;
            has_splice = 1;
        } else if (i + 2 == count) {
            janetc_emit_ss(c, JOP_PUSH_2, slots[i], slots[i + 1], 0);
            i += 2;
            min_arity += 2;
        } else if (slots[i + 2].flags & JANET_SLOT_SPLICED) {
            janetc_emit_ss(c, JOP_PUSH_2, slots[i], slots[i + 1], 0);
            janetc_emit_s(c, JOP_PUSH_ARRAY, slots[i + 2], 0);
            i += 3;
            min_arity += 2;
            has_splice = 1;
        } else {
            janetc_emit_sss(c, JOP_PUSH_3, slots[i], slots[i + 1], slots[i + 2], 0);
            i += 3;
            min_arity += 3;
        }
    }
    return has_splice ? (-1 - min_arity) : min_arity;
}

/* Check if a list of slots has any spliced slots */
static int has_spliced(JanetSlot *slots) {
    int32_t i;
    for (i = 0; i < janet_v_count(slots); i++) {
        if (slots[i].flags & JANET_SLOT_SPLICED)
            return 1;
    }
    return 0;
}

/* Free slots loaded via janetc_toslots */
void janetc_freeslots(JanetCompiler *c, JanetSlot *slots) {
    int32_t i;
    for (i = 0; i < janet_v_count(slots); i++) {
        janetc_freeslot(c, slots[i]);
    }
    janet_v_free(slots);
}

/* Compile some code that will be thrown away. Used to ensure
 * that dead code is well formed without including it in the final
 * bytecode. */
void janetc_throwaway(JanetFopts opts, Janet x) {
    JanetCompiler *c = opts.compiler;
    JanetScope unusedScope;
    int32_t bufstart = janet_v_count(c->buffer);
    int32_t mapbufstart = janet_v_count(c->mapbuffer);
    janetc_scope(&unusedScope, c, JANET_SCOPE_UNUSED, "unusued");
    janetc_value(opts, x);
    janetc_popscope(c);
    if (c->buffer) {
        janet_v__cnt(c->buffer) = bufstart;
        if (c->mapbuffer)
            janet_v__cnt(c->mapbuffer) = mapbufstart;
    }
}

/* Compile a call or tailcall instruction */
static JanetSlot janetc_call(JanetFopts opts, JanetSlot *slots, JanetSlot fun) {
    JanetSlot retslot;
    JanetCompiler *c = opts.compiler;
    int specialized = 0;
    if (fun.flags & JANET_SLOT_CONSTANT && !has_spliced(slots)) {
        if (janet_checktype(fun.constant, JANET_FUNCTION)) {
            JanetFunction *f = janet_unwrap_function(fun.constant);
            const JanetFunOptimizer *o = janetc_funopt(f->def->flags);
            if (o && (!o->can_optimize || o->can_optimize(opts, slots))) {
                specialized = 1;
                retslot = o->optimize(opts, slots);
            }
        }
        /* TODO janet function inlining (no c functions)*/
    }
    if (!specialized) {
        int32_t min_arity = janetc_pushslots(c, slots);
        /* Check for provably incorrect function calls */
        if (fun.flags & JANET_SLOT_CONSTANT) {

            /* Check for bad arity type if fun is a constant */
            switch (janet_type(fun.constant)) {
                case JANET_FUNCTION: {
                    JanetFunction *f = janet_unwrap_function(fun.constant);
                    int32_t min = f->def->min_arity;
                    int32_t max = f->def->max_arity;
                    if (min_arity < 0) {
                        /* Call has splices */
                        min_arity = -1 - min_arity;
                        if (min_arity > max && max >= 0) {
                            const uint8_t *es = janet_formatc(
                                                    "%v expects at most %d argument, got at least %d",
                                                    fun.constant, max, min_arity);
                            janetc_error(c, es);
                        }
                    } else {
                        /* Call has no splices */
                        if (min_arity > max && max >= 0) {
                            const uint8_t *es = janet_formatc(
                                                    "%v expects at most %d argument, got %d",
                                                    fun.constant, max, min_arity);
                            janetc_error(c, es);
                        }
                        if (min_arity < min) {
                            const uint8_t *es = janet_formatc(
                                                    "%v expects at least %d argument, got %d",
                                                    fun.constant, min, min_arity);
                            janetc_error(c, es);
                        }
                    }
                }
                break;
                case JANET_CFUNCTION:
                case JANET_ABSTRACT:
                    break;
                case JANET_KEYWORD:
                    if (min_arity == 0) {
                        const uint8_t *es = janet_formatc("%v expects at least 1 argument, got 0",
                                                          fun.constant);
                        janetc_error(c, es);
                    }
                    break;
                default:
                    if (min_arity > 1 || min_arity == 0) {
                        const uint8_t *es = janet_formatc("%v expects 1 argument, got %d",
                                                          fun.constant, min_arity);
                        janetc_error(c, es);
                    }
                    if (min_arity < -2) {
                        const uint8_t *es = janet_formatc("%v expects 1 argument, got at least %d",
                                                          fun.constant, -1 - min_arity);
                        janetc_error(c, es);
                    }
                    break;
            }
        }

        if ((opts.flags & JANET_FOPTS_TAIL) &&
                /* Prevent top level tail calls for better errors */
                !(c->scope->flags & JANET_SCOPE_TOP)) {
            janetc_emit_s(c, JOP_TAILCALL, fun, 0);
            retslot = janetc_cslot(janet_wrap_nil());
            retslot.flags = JANET_SLOT_RETURNED;
        } else {
            retslot = janetc_gettarget(opts);
            janetc_emit_ss(c, JOP_CALL, retslot, fun, 1);
        }
    }
    janetc_freeslots(c, slots);
    return retslot;
}

static JanetSlot janetc_maker(JanetFopts opts, JanetSlot *slots, int op) {
    JanetCompiler *c = opts.compiler;
    JanetSlot retslot;
    janetc_pushslots(c, slots);
    janetc_freeslots(c, slots);
    retslot = janetc_gettarget(opts);
    janetc_emit_s(c, op, retslot, 1);
    return retslot;
}

static JanetSlot janetc_array(JanetFopts opts, Janet x) {
    JanetCompiler *c = opts.compiler;
    JanetArray *a = janet_unwrap_array(x);
    return janetc_maker(opts,
                        janetc_toslots(c, a->data, a->count),
                        JOP_MAKE_ARRAY);
}

static JanetSlot janetc_tuple(JanetFopts opts, Janet x) {
    JanetCompiler *c = opts.compiler;
    const Janet *t = janet_unwrap_tuple(x);
    return janetc_maker(opts,
                        janetc_toslots(c, t, janet_tuple_length(t)),
                        JOP_MAKE_TUPLE);
}

static JanetSlot janetc_tablector(JanetFopts opts, Janet x, int op) {
    JanetCompiler *c = opts.compiler;
    return janetc_maker(opts,
                        janetc_toslotskv(c, x),
                        op);
}

static JanetSlot janetc_bufferctor(JanetFopts opts, Janet x) {
    JanetCompiler *c = opts.compiler;
    JanetBuffer *b = janet_unwrap_buffer(x);
    Janet onearg = janet_stringv(b->data, b->count);
    return janetc_maker(opts,
                        janetc_toslots(c, &onearg, 1),
                        JOP_MAKE_BUFFER);
}

/* Expand a macro one time. Also get the special form compiler if we
 * find that instead. */
static int macroexpand1(
    JanetCompiler *c,
    Janet x,
    Janet *out,
    const JanetSpecial **spec) {
    if (!janet_checktype(x, JANET_TUPLE))
        return 0;
    const Janet *form = janet_unwrap_tuple(x);
    if (janet_tuple_length(form) == 0)
        return 0;
    /* Source map - only set when we get a tuple */
    if (janet_tuple_sm_line(form) >= 0) {
        c->current_mapping.line = janet_tuple_sm_line(form);
        c->current_mapping.column = janet_tuple_sm_column(form);
    }
    /* Bracketed tuples are not specials or macros! */
    if (janet_tuple_flag(form) & JANET_TUPLE_FLAG_BRACKETCTOR)
        return 0;
    if (!janet_checktype(form[0], JANET_SYMBOL))
        return 0;
    const uint8_t *name = janet_unwrap_symbol(form[0]);
    const JanetSpecial *s = janetc_special(name);
    if (s) {
        *spec = s;
        return 0;
    }
    Janet macroval;
    JanetBindingType btype = janet_resolve(c->env, name, &macroval);
    if (btype != JANET_BINDING_MACRO ||
            !janet_checktype(macroval, JANET_FUNCTION))
        return 0;

    /* Evaluate macro */
    JanetFunction *macro = janet_unwrap_function(macroval);
    int32_t arity = janet_tuple_length(form) - 1;
    JanetFiber *fiberp = janet_fiber(macro, 64, arity, form + 1);
    if (NULL == fiberp) {
        int32_t minar = macro->def->min_arity;
        int32_t maxar = macro->def->max_arity;
        const uint8_t *es = NULL;
        if (minar >= 0 && arity < minar)
            es = janet_formatc("macro arity mismatch, expected at least %d, got %d", minar, arity);
        if (maxar >= 0 && arity > maxar)
            es = janet_formatc("macro arity mismatch, expected at most %d, got %d", maxar, arity);
        c->result.macrofiber = NULL;
        janetc_error(c, es);
    }
    /* Set env */
    fiberp->env = c->env;
    int lock = janet_gclock();
    JanetSignal status = janet_continue(fiberp, janet_wrap_nil(), &x);
    janet_gcunlock(lock);
    if (status != JANET_SIGNAL_OK) {
        const uint8_t *es = janet_formatc("(macro) %V", x);
        c->result.macrofiber = fiberp;
        janetc_error(c, es);
    } else {
        *out = x;
    }

    return 1;
}

/* Compile a single value */
JanetSlot janetc_value(JanetFopts opts, Janet x) {
    JanetSlot ret;
    JanetCompiler *c = opts.compiler;
    JanetSourceMapping last_mapping = c->current_mapping;
    c->recursion_guard--;

    /* Guard against previous errors and unbounded recursion */
    if (c->result.status == JANET_COMPILE_ERROR) return janetc_cslot(janet_wrap_nil());
    if (c->recursion_guard <= 0) {
        janetc_cerror(c, "recursed too deeply");
        return janetc_cslot(janet_wrap_nil());
    }

    /* Macro expand. Also gets possible special form and
     * refines source mapping cursor if possible. */
    const JanetSpecial *spec = NULL;
    int macroi = JANET_MAX_MACRO_EXPAND;
    while (macroi &&
            c->result.status != JANET_COMPILE_ERROR &&
            macroexpand1(c, x, &x, &spec))
        macroi--;
    if (macroi == 0) {
        janetc_cerror(c, "recursed too deeply in macro expansion");
        return janetc_cslot(janet_wrap_nil());
    }

    /* Special forms */
    if (spec) {
        const Janet *tup = janet_unwrap_tuple(x);
        ret = spec->compile(opts, janet_tuple_length(tup) - 1, tup + 1);
    } else {
        switch (janet_type(x)) {
            case JANET_TUPLE: {
                JanetFopts subopts = janetc_fopts_default(c);
                const Janet *tup = janet_unwrap_tuple(x);
                /* Empty tuple is tuple literal */
                if (janet_tuple_length(tup) == 0) {
                    ret = janetc_cslot(janet_wrap_tuple(janet_tuple_n(NULL, 0)));
                } else if (janet_tuple_flag(tup) & JANET_TUPLE_FLAG_BRACKETCTOR) { /* [] tuples are not function call */
                    ret = janetc_tuple(opts, x);
                } else {
                    JanetSlot head = janetc_value(subopts, tup[0]);
                    subopts.flags = JANET_FUNCTION | JANET_CFUNCTION;
                    ret = janetc_call(opts, janetc_toslots(c, tup + 1, janet_tuple_length(tup) - 1), head);
                    janetc_freeslot(c, head);
                }
                ret.flags &= ~JANET_SLOT_SPLICED;
            }
            break;
            case JANET_SYMBOL:
                ret = janetc_resolve(c, janet_unwrap_symbol(x));
                break;
            case JANET_ARRAY:
                ret = janetc_array(opts, x);
                break;
            case JANET_STRUCT:
                ret = janetc_tablector(opts, x, JOP_MAKE_STRUCT);
                break;
            case JANET_TABLE:
                ret = janetc_tablector(opts, x, JOP_MAKE_TABLE);
                break;
            case JANET_BUFFER:
                ret = janetc_bufferctor(opts, x);
                break;
            default:
                ret = janetc_cslot(x);
                break;
        }
    }

    if (c->result.status == JANET_COMPILE_ERROR)
        return janetc_cslot(janet_wrap_nil());
    if (opts.flags & JANET_FOPTS_TAIL)
        ret = janetc_return(c, ret);
    if (opts.flags & JANET_FOPTS_HINT) {
        janetc_copy(c, opts.hint, ret);
        ret = opts.hint;
    }
    c->current_mapping = last_mapping;
    c->recursion_guard++;
    return ret;
}

/* Compile a funcdef */
JanetFuncDef *janetc_pop_funcdef(JanetCompiler *c) {
    JanetScope *scope = c->scope;
    JanetFuncDef *def = janet_funcdef_alloc();
    def->slotcount = scope->ra.max + 1;

    janet_assert(scope->flags & JANET_SCOPE_FUNCTION, "expected function scope");

    /* Copy envs */
    def->environments_length = janet_v_count(scope->envs);
    def->environments = janet_v_flatten(scope->envs);

    def->constants_length = janet_v_count(scope->consts);
    def->constants = janet_v_flatten(scope->consts);

    def->defs_length = janet_v_count(scope->defs);
    def->defs = janet_v_flatten(scope->defs);

    /* Copy bytecode (only last chunk) */
    def->bytecode_length = janet_v_count(c->buffer) - scope->bytecode_start;
    if (def->bytecode_length) {
        size_t s = sizeof(int32_t) * def->bytecode_length;
        def->bytecode = malloc(s);
        if (NULL == def->bytecode) {
            JANET_OUT_OF_MEMORY;
        }
        memcpy(def->bytecode, c->buffer + scope->bytecode_start, s);
        janet_v__cnt(c->buffer) = scope->bytecode_start;
        if (NULL != c->mapbuffer && c->source) {
            size_t s = sizeof(JanetSourceMapping) * def->bytecode_length;
            def->sourcemap = malloc(s);
            if (NULL == def->sourcemap) {
                JANET_OUT_OF_MEMORY;
            }
            memcpy(def->sourcemap, c->mapbuffer + scope->bytecode_start, s);
            janet_v__cnt(c->mapbuffer) = scope->bytecode_start;
        }
    }

    /* Get source from parser */
    def->source = c->source;

    def->arity = 0;
    def->min_arity = 0;
    def->flags = 0;
    if (scope->flags & JANET_SCOPE_ENV) {
        def->flags |= JANET_FUNCDEF_FLAG_NEEDSENV;
    }

    /* Pop the scope */
    janetc_popscope(c);

    return def;
}

/* Initialize a compiler */
static void janetc_init(JanetCompiler *c, JanetTable *env, const uint8_t *where) {
    c->scope = NULL;
    c->buffer = NULL;
    c->mapbuffer = NULL;
    c->recursion_guard = JANET_RECURSION_GUARD;
    c->env = env;
    c->source = where;
    c->current_mapping.line = -1;
    c->current_mapping.column = -1;
    /* Init result */
    c->result.error = NULL;
    c->result.status = JANET_COMPILE_OK;
    c->result.funcdef = NULL;
    c->result.macrofiber = NULL;
    c->result.error_mapping.line = -1;
    c->result.error_mapping.column = -1;
}

/* Deinitialize a compiler struct */
static void janetc_deinit(JanetCompiler *c) {
    janet_v_free(c->buffer);
    janet_v_free(c->mapbuffer);
    c->env = NULL;
}

/* Compile a form. */
JanetCompileResult janet_compile(Janet source, JanetTable *env, const uint8_t *where) {
    JanetCompiler c;
    JanetScope rootscope;
    JanetFopts fopts;

    janetc_init(&c, env, where);

    /* Push a function scope */
    janetc_scope(&rootscope, &c, JANET_SCOPE_FUNCTION | JANET_SCOPE_TOP, "root");

    /* Set initial form options */
    fopts.compiler = &c;
    fopts.flags = JANET_FOPTS_TAIL | JANET_SLOTTYPE_ANY;
    fopts.hint = janetc_cslot(janet_wrap_nil());

    /* Compile the value */
    janetc_value(fopts, source);

    if (c.result.status == JANET_COMPILE_OK) {
        JanetFuncDef *def = janetc_pop_funcdef(&c);
        def->name = janet_cstring("_thunk");
        c.result.funcdef = def;
    } else {
        c.result.error_mapping = c.current_mapping;
        janetc_popscope(&c);
    }

    janetc_deinit(&c);

    return c.result;
}

/* C Function for compiling */
static Janet cfun(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 3);
    JanetTable *env = argc > 1 ? janet_gettable(argv, 1) : janet_vm_fiber->env;
    if (NULL == env) {
        env = janet_table(0);
        janet_vm_fiber->env = env;
    }
    const uint8_t *source = NULL;
    if (argc == 3) {
        source = janet_getstring(argv, 2);
    }
    JanetCompileResult res = janet_compile(argv[0], env, source);
    if (res.status == JANET_COMPILE_OK) {
        return janet_wrap_function(janet_thunk(res.funcdef));
    } else {
        JanetTable *t = janet_table(4);
        janet_table_put(t, janet_ckeywordv("error"), janet_wrap_string(res.error));
        janet_table_put(t, janet_ckeywordv("line"), janet_wrap_integer(res.error_mapping.line));
        janet_table_put(t, janet_ckeywordv("column"), janet_wrap_integer(res.error_mapping.column));
        if (res.macrofiber) {
            janet_table_put(t, janet_ckeywordv("fiber"), janet_wrap_fiber(res.macrofiber));
        }
        return janet_wrap_table(t);
    }
}

static const JanetReg compile_cfuns[] = {
    {
        "compile", cfun,
        JDOC("(compile ast &opt env source)\n\n"
             "Compiles an Abstract Syntax Tree (ast) into a janet function. "
             "Pair the compile function with parsing functionality to implement "
             "eval. Returns a janet function and does not modify ast. Throws an "
             "error if the ast cannot be compiled.")
    },
    {NULL, NULL, NULL}
};

void janet_lib_compile(JanetTable *env) {
    janet_core_cfuns(env, NULL, compile_cfuns);
}
