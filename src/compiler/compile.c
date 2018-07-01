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

#include <dst/dst.h>
#include <dst/dstcorelib.h>
#include "compile.h"
#include "emit.h"

#define DST_V_DEF_FLATTENMEM
#include <headerlibs/vector.h>
#undef DST_V_DEF_FLATTENMEM

DstFopts dstc_fopts_default(DstCompiler *c) {
    DstFopts ret;
    ret.compiler = c;
    ret.flags = 0;
    ret.hint = dstc_cslot(dst_wrap_nil());
    return ret;
}

/* Throw an error with a dst string. */
void dstc_error(DstCompiler *c, const uint8_t *m) {
    /* Don't override first error */
    if (c->result.status == DST_COMPILE_ERROR) {
        return;
    }
    c->result.status = DST_COMPILE_ERROR;
    c->result.error = m;
}

/* Throw an error with a message in a cstring */
void dstc_cerror(DstCompiler *c, const char *m) {
    dstc_error(c, dst_cstring(m));
}

/* Check error */
int dstc_iserr(DstFopts *opts) {
    return (opts->compiler->result.status == DST_COMPILE_ERROR);
}

/* Get the next key in an associative data structure. Used for iterating through an
 * associative data structure. */
const DstKV *dstc_next(Dst ds, const DstKV *kv) {
    switch(dst_type(ds)) {
        default:
            return NULL;
        case DST_TABLE:
            return (const DstKV *) dst_table_next(dst_unwrap_table(ds), kv);
        case DST_STRUCT:
            return dst_struct_next(dst_unwrap_struct(ds), kv);
    }
}

/* Free a slot */
void dstc_freeslot(DstCompiler *c, DstSlot s) {
    if (s.flags & (DST_SLOT_CONSTANT | DST_SLOT_REF | DST_SLOT_NAMED)) return;
    if (s.envindex >= 0) return;
    dstc_regalloc_free(&c->scope->ra, s.index);
}

/* Add a slot to a scope with a symbol associated with it (def or var). */
void dstc_nameslot(DstCompiler *c, const uint8_t *sym, DstSlot s) {
    SymPair sp;
    sp.sym = sym;
    sp.slot = s;
    sp.keep = 0;
    sp.slot.flags |= DST_SLOT_NAMED;
    dst_v_push(c->scope->syms, sp);
}

/* Create a slot with a constant */
DstSlot dstc_cslot(Dst x) {
    DstSlot ret;
    ret.flags = (1 << dst_type(x)) | DST_SLOT_CONSTANT;
    ret.index = -1;
    ret.constant = x;
    ret.envindex = -1;
    return ret;
}

/* Get a temp near slot */
DstSlot dstc_nearslot(DstCompiler *c, DstcRegisterTemp tag) {
    DstSlot ret;
    ret.flags = DST_SLOTTYPE_ANY;
    ret.index = dstc_allocnear(c, tag);
    ret.constant = dst_wrap_nil();
    ret.envindex = -1;
    return ret;
}

/* Get a temp near slot */
DstSlot dstc_farslot(DstCompiler *c) {
    DstSlot ret;
    ret.flags = DST_SLOTTYPE_ANY;
    ret.index = dstc_allocfar(c);
    ret.constant = dst_wrap_nil();
    ret.envindex = -1;
    return ret;
}

/* Enter a new scope */
void dstc_scope(DstScope *s, DstCompiler *c, int flags, const char *name) {
    DstScope scope;
    scope.name = name;
    scope.child = NULL;
    scope.consts = NULL;
    scope.syms = NULL;
    scope.envs = NULL;
    scope.defs = NULL;
    scope.selfconst = -1;
    scope.bytecode_start = dst_v_count(c->buffer);
    scope.flags = flags;
    *s = scope;
    /* Inherit slots */
    if ((!(flags & DST_SCOPE_FUNCTION)) && c->scope) {
        dstc_regalloc_clone(&s->ra, &(c->scope->ra));
    } else {
        dstc_regalloc_init(&s->ra);
    }
    /* Link parent and child and update pointer */
    s->parent = c->scope;
    if (c->scope)
        c->scope->child = s;
    c->scope = s;
}

/* Leave a scope. */
void dstc_popscope(DstCompiler *c) {
    DstScope *oldscope = c->scope;
    DstScope *newscope = oldscope->parent;
    /* Move free slots to parent scope if not a new function.
     * We need to know the total number of slots used when compiling the function. */
    if (!(oldscope->flags & (DST_SCOPE_FUNCTION | DST_SCOPE_UNUSED)) && newscope) {
        if (newscope->ra.max < oldscope->ra.max)
            newscope->ra.max = oldscope->ra.max;

        /* Keep upvalue slots */
        for (int32_t i = 0; i < dst_v_count(oldscope->syms); i++) {
            SymPair pair = oldscope->syms[i];
            if (pair.keep) {
                /* The variable should not be lexically accessible */
                pair.sym = NULL;
                dst_v_push(newscope->syms, pair);
                dstc_regalloc_touch(&newscope->ra, pair.slot.index);
            }
        }

    }
    /* Free the old scope */
    dst_v_free(oldscope->consts);
    dst_v_free(oldscope->syms);
    dst_v_free(oldscope->envs);
    dst_v_free(oldscope->defs);
    dstc_regalloc_deinit(&oldscope->ra);
    /* Update pointer */
    if (newscope)
        newscope->child = NULL;
    c->scope = newscope;
}

/* Leave a scope but keep a slot allocated. */
void dstc_popscope_keepslot(DstCompiler *c, DstSlot retslot) {
    DstScope *scope;
    dstc_popscope(c);
    scope = c->scope;
    if (scope && retslot.envindex < 0 && retslot.index >= 0) {
        dstc_regalloc_touch(&scope->ra, retslot.index);
    }
}

/* Allow searching for symbols. Return information about the symbol */
DstSlot dstc_resolve(
        DstCompiler *c,
        const uint8_t *sym) {

    DstSlot ret = dstc_cslot(dst_wrap_nil());
    DstScope *scope = c->scope;
    SymPair *pair;
    int foundlocal = 1;
    int unused = 0;

    /* Search scopes for symbol, starting from top */
    while (scope) {
        int32_t i, len;
        if (scope->flags & DST_SCOPE_UNUSED)
            unused = 1;
        len = dst_v_count(scope->syms);
        /* Search in reverse order */
        for (i = len - 1; i >= 0; i--) {
            pair = scope->syms + i;
            if (pair->sym == sym) {
                ret = pair->slot;
                goto found;
            }
        }
        if (scope->flags & DST_SCOPE_FUNCTION)
            foundlocal = 0;
        scope = scope->parent;
    }

    /* Symbol not found - check for global */
    {
        Dst check;
        DstBindingType btype = dst_env_resolve(c->env, sym, &check);
        switch (btype) {
            default:
            case DST_BINDING_NONE:
                dstc_error(c, dst_formatc("unknown symbol %q", sym));
                return dstc_cslot(dst_wrap_nil());
            case DST_BINDING_DEF:
            case DST_BINDING_MACRO: /* Macro should function like defs when not in calling pos */
                return dstc_cslot(check);
            case DST_BINDING_VAR:
            {
                DstSlot ret = dstc_cslot(check);
                /* TODO save type info */
                ret.flags |= DST_SLOT_REF | DST_SLOT_NAMED | DST_SLOT_MUTABLE | DST_SLOTTYPE_ANY;
                ret.flags &= ~DST_SLOT_CONSTANT;
                return ret;
            }
        }
    }

    /* Symbol was found */
    found:

    /* Constants can be returned immediately (they are stateless) */
    if (ret.flags & (DST_SLOT_CONSTANT | DST_SLOT_REF))
        return ret;

    /* Unused references and locals shouldn't add captured envs. */
    if (unused || foundlocal) {
        ret.envindex = -1;
        return ret;
    }

    /* non-local scope needs to expose its environment */
    pair->keep = 1;
    while (scope && !(scope->flags & DST_SCOPE_FUNCTION))
        scope = scope->parent;
    dst_assert(scope, "invalid scopes");
    scope->flags |= DST_SCOPE_ENV;
    scope = scope->child;

    /* Propogate env up to current scope */
    int32_t envindex = -1;
    while (scope) {
        if (scope->flags & DST_SCOPE_FUNCTION) {
            int32_t j, len;
            int scopefound = 0;
            /* Check if scope already has env. If so, break */
            len = dst_v_count(scope->envs);
            for (j = 0; j < len; j++) {
                if (scope->envs[j] == envindex) {
                    scopefound = 1;
                    envindex = j;
                    break;
                }
            }
            /* Add the environment if it is not already referenced */
            if (!scopefound) {
                len = dst_v_count(scope->envs);
                dst_v_push(scope->envs, envindex);
                envindex = len;
            }
        }
        scope = scope->child;
    }

    ret.envindex = envindex;
    return ret;
}

/* Generate the return instruction for a slot. */
DstSlot dstc_return(DstCompiler *c, DstSlot s) {
    if (!(s.flags & DST_SLOT_RETURNED)) {
        if (s.flags & DST_SLOT_CONSTANT && dst_checktype(s.constant, DST_NIL))
            dstc_emit(c, DOP_RETURN_NIL);
        else
            dstc_emit_s(c, DOP_RETURN, s);
        s.flags |= DST_SLOT_RETURNED;
    }
    return s;
}

/* Get a target slot for emitting an instruction. Will always return
 * a local slot. */
DstSlot dstc_gettarget(DstFopts opts) {
    DstSlot slot;
    if ((opts.flags & DST_FOPTS_HINT) &&
        (opts.hint.envindex < 0) &&
        (opts.hint.index >= 0 && opts.hint.index <= 0xFF)) {
        slot = opts.hint;
    } else {
        slot.envindex = -1;
        slot.constant = dst_wrap_nil();
        slot.flags = 0;
        slot.index = dstc_allocnear(opts.compiler, DSTC_REGTEMP_TARGET);
    }
    return slot;
}

/* Get a bunch of slots for function arguments */
DstSlot *dstc_toslots(DstCompiler *c, const Dst *vals, int32_t len) {
    int32_t i;
    DstSlot *ret = NULL;
    DstFopts subopts = dstc_fopts_default(c);
    for (i = 0; i < len; i++) {
        dst_v_push(ret, dstc_value(subopts, vals[i]));
    }
    return ret;
}

/* Get a bunch of slots for function arguments */
DstSlot *dstc_toslotskv(DstCompiler *c, Dst ds) {
    DstSlot *ret = NULL;
    const DstKV *kv = NULL;
    DstFopts subopts = dstc_fopts_default(c);
    while ((kv = dstc_next(ds, kv))) {
        dst_v_push(ret, dstc_value(subopts, kv->key));
        dst_v_push(ret, dstc_value(subopts, kv->value));
    }
    return ret;
}

/* Push slots load via dstc_toslots. */
void dstc_pushslots(DstCompiler *c, DstSlot *slots) {
    int32_t i;
    for (i = 0; i < dst_v_count(slots) - 2; i += 3)
        dstc_emit_sss(c, DOP_PUSH_3, slots[i], slots[i+1], slots[i+2]);
    if (i == dst_v_count(slots) - 2)
        dstc_emit_ss(c, DOP_PUSH_2, slots[i], slots[i+1]);
    else if (i == dst_v_count(slots) - 1)
        dstc_emit_s(c, DOP_PUSH, slots[i]);
}

/* Free slots loaded via dstc_toslots */
void dstc_freeslots(DstCompiler *c, DstSlot *slots) {
    int32_t i;
    for (i = 0; i < dst_v_count(slots); i++) {
        dstc_freeslot(c, slots[i]);
    }
    dst_v_free(slots);
}

/* Compile some code that will be thrown away. Used to ensure
 * that dead code is well formed without including it in the final
 * bytecode. */
void dstc_throwaway(DstFopts opts, Dst x) {
    DstCompiler *c = opts.compiler;
    DstScope unusedScope;
    int32_t bufstart = dst_v_count(c->buffer);
    int32_t mapbufstart = dst_v_count(c->mapbuffer);
    dstc_scope(&unusedScope, c, DST_SCOPE_UNUSED, "unusued");
    dstc_value(opts, x);
    dstc_popscope(c);
    if (c->buffer) {
        dst_v__cnt(c->buffer) = bufstart;
        if (c->mapbuffer)
            dst_v__cnt(c->mapbuffer) = mapbufstart;
    }
}

/* Compile a call or tailcall instruction */
static DstSlot dstc_call(DstFopts opts, DstSlot *slots, DstSlot fun) {
    DstSlot retslot;
    DstCompiler *c = opts.compiler;
    int specialized = 0;
    if (fun.flags & DST_SLOT_CONSTANT) {
        if (dst_checktype(fun.constant, DST_CFUNCTION)) {
            const DstCFunOptimizer *o = dstc_cfunopt(dst_unwrap_cfunction(fun.constant));
            if (o && (!o->can_optimize || o->can_optimize(opts, slots))) {
                specialized = 1;
                retslot = o->optimize(opts, slots);
            }
        } else if (dst_checktype(fun.constant, DST_FUNCTION)) {
            DstFunction *f = dst_unwrap_function(fun.constant);
            const DstFunOptimizer *o = dstc_funopt(f->def->flags);
            if (o && (!o->can_optimize || o->can_optimize(opts, slots))) {
                specialized = 1;
                retslot = o->optimize(opts, slots);
            }
        }
        /* TODO dst function inlining (no c functions)*/
    }
    if (!specialized) {
        dstc_pushslots(c, slots);
        if (opts.flags & DST_FOPTS_TAIL) {
            dstc_emit_s(c, DOP_TAILCALL, fun);
            retslot = dstc_cslot(dst_wrap_nil());
            retslot.flags = DST_SLOT_RETURNED;
        } else {
            retslot = dstc_gettarget(opts);
            int32_t fun_register = dstc_regnear(c, fun, DSTC_REGTEMP_0);
            dstc_emit(c, DOP_CALL |
                    (retslot.index << 8) |
                    (fun_register << 16));
            /* Don't free ret register */
            dstc_free_reg(c, fun, fun_register);
        }
    }
    dstc_freeslots(c, slots);
    return retslot;
}

static DstSlot dstc_array(DstFopts opts, Dst x) {
    DstCompiler *c = opts.compiler;
    DstArray *a = dst_unwrap_array(x);
    return dstc_call(opts,
            dstc_toslots(c, a->data, a->count),
            dstc_cslot(dst_wrap_cfunction(dst_core_array)));
}

static DstSlot dstc_tablector(DstFopts opts, Dst x, DstCFunction cfun) {
    DstCompiler *c = opts.compiler;
    return dstc_call(opts, dstc_toslotskv(c, x), dstc_cslot(dst_wrap_cfunction(cfun)));
}

static DstSlot dstc_bufferctor(DstFopts opts, Dst x) {
    DstCompiler *c = opts.compiler;
    DstBuffer *b = dst_unwrap_buffer(x);
    Dst onearg = dst_stringv(b->data, b->count);
    return dstc_call(opts,
            dstc_toslots(c, &onearg, 1),
            dstc_cslot(dst_wrap_cfunction(dst_core_buffer)));
}

/* Compile a symbol */
DstSlot dstc_symbol(DstFopts opts, const uint8_t *sym) {
    if (dst_string_length(sym) && sym[0] != ':') {
        /* Non keyword */
        return dstc_resolve(opts.compiler, sym);
    } else {
        /* Keyword */
        return dstc_cslot(dst_wrap_symbol(sym));
    }
}

/* Compile a single value */
DstSlot dstc_value(DstFopts opts, Dst x) {
    DstSlot ret;
    DstCompiler *c = opts.compiler;
    int macrorecur = 0;
    DstSourceMapping last_mapping = c->current_mapping;
    opts.compiler->recursion_guard--;
recur:
    if (dstc_iserr(&opts)) {
        return dstc_cslot(dst_wrap_nil());
    }
    if (opts.compiler->recursion_guard <= 0) {
        dstc_cerror(opts.compiler, "recursed too deeply");
        return dstc_cslot(dst_wrap_nil());
    }
    switch (dst_type(x)) {
        default:
            ret = dstc_cslot(x);
            break;
        case DST_SYMBOL:
            {
                const uint8_t *sym = dst_unwrap_symbol(x);
                ret = dstc_symbol(opts, sym);
                break;
            }
        case DST_TUPLE:
            {
                int compiled = 0;
                Dst headval;
                DstSlot head;
                DstFopts subopts = dstc_fopts_default(c);
                const Dst *tup = dst_unwrap_tuple(x);
                /* Get ast mapping */
                if (dst_tuple_sm_line(tup) > 0) {
                    c->current_mapping.line = dst_tuple_sm_line(tup);
                    c->current_mapping.column = dst_tuple_sm_col(tup);
                }
                /* Empty tuple is tuple literal */
                if (dst_tuple_length(tup) == 0) {
                    compiled = 1;
                    ret = dstc_cslot(x);
                } else {
                    /* Symbols could be specials */
                    headval = tup[0];
                    if (dst_checktype(headval, DST_SYMBOL)) {
                        const uint8_t *headsym = dst_unwrap_symbol(headval);
                        const DstSpecial *s = dstc_special(headsym);
                        if (NULL != s) {
                            ret = s->compile(opts, dst_tuple_length(tup) - 1, tup + 1);
                            compiled = 1;
                        } else {
                            /* Check macro */
                            Dst macVal;
                            DstBindingType btype = dst_env_resolve(c->env, headsym, &macVal);
                            if (btype == DST_BINDING_MACRO &&
                                    dst_checktype(macVal, DST_FUNCTION)) {
                                if (macrorecur++ > DST_RECURSION_GUARD) {
                                    dstc_cerror(c, "macro expansion recursed too deeply");
                                    return dstc_cslot(dst_wrap_nil());
                                } else {
                                    DstFunction *f = dst_unwrap_function(macVal);
                                    int lock = dst_gclock();
                                    DstSignal status = dst_call(f, dst_tuple_length(tup) - 1, tup + 1, &x);
                                    dst_gcunlock(lock);
                                    if (status != DST_SIGNAL_OK) {
                                        const uint8_t *es = dst_formatc("error in macro expansion: %V", x);
                                        dstc_error(c, es);
                                    }
                                    /* Tail recur on the value */
                                    goto recur;
                                }
                            }
                        }
                    }
                    if (!compiled) {
                        /* Compile the head of the tuple */
                        subopts.flags = DST_FUNCTION | DST_CFUNCTION;
                        head = dstc_value(subopts, tup[0]);
                        /* Add compile function call */
                        ret = dstc_call(opts, dstc_toslots(c, tup + 1, dst_tuple_length(tup) - 1), head);
                    }
                }
                /* Pop source mapping */
                if (c->result.status != DST_COMPILE_ERROR)
                    c->current_mapping = last_mapping;
            }
            break;
        case DST_ARRAY:
            ret = dstc_array(opts, x);
            break;
        case DST_STRUCT:
            ret = dstc_tablector(opts, x, dst_core_struct);
            break;
        case DST_TABLE:
            ret = dstc_tablector(opts, x, dst_core_table);
            break;
        case DST_BUFFER:
            ret = dstc_bufferctor(opts, x);
            break;
    }
    if (dstc_iserr(&opts)) {
        return dstc_cslot(dst_wrap_nil());
    }
    if (opts.flags & DST_FOPTS_TAIL) {
        ret = dstc_return(opts.compiler, ret);
    }
    if (opts.flags & DST_FOPTS_HINT) {
        dstc_copy(opts.compiler, opts.hint, ret);
        ret = opts.hint;
    }
    opts.compiler->recursion_guard++;
    return ret;
}

/* Compile a funcdef */
DstFuncDef *dstc_pop_funcdef(DstCompiler *c) {
    DstScope *scope = c->scope;
    DstFuncDef *def = dst_funcdef_alloc();
    def->slotcount = scope->ra.max + 1;

    dst_assert(scope->flags & DST_SCOPE_FUNCTION, "expected function scope");

    /* Copy envs */
    def->environments_length = dst_v_count(scope->envs);
    def->environments = dst_v_flatten(scope->envs);

    def->constants_length = dst_v_count(scope->consts);
    def->constants = dst_v_flatten(scope->consts);

    def->defs_length = dst_v_count(scope->defs);
    def->defs = dst_v_flatten(scope->defs);

    /* Copy bytecode (only last chunk) */
    def->bytecode_length = dst_v_count(c->buffer) - scope->bytecode_start;
    if (def->bytecode_length) {
        size_t s = sizeof(int32_t) * def->bytecode_length;
        def->bytecode = malloc(s);
        if (NULL == def->bytecode) {
            DST_OUT_OF_MEMORY;
        }
        memcpy(def->bytecode, c->buffer + scope->bytecode_start, s);
        dst_v__cnt(c->buffer) = scope->bytecode_start;
        if (NULL != c->mapbuffer) {
            size_t s = sizeof(DstSourceMapping) * def->bytecode_length;
            def->sourcemap = malloc(s);
            if (NULL == def->sourcemap) {
                DST_OUT_OF_MEMORY;
            }
            memcpy(def->sourcemap, c->mapbuffer + scope->bytecode_start, s);
            dst_v__cnt(c->mapbuffer) = scope->bytecode_start;
        }
    }

    /* Get source from parser */
    def->source = c->source;

    def->arity = 0;
    def->flags = 0;
    if (scope->flags & DST_SCOPE_ENV) {
        def->flags |= DST_FUNCDEF_FLAG_NEEDSENV;
    }

    /* Pop the scope */
    dstc_popscope(c);

    return def;
}

/* Initialize a compiler */
static void dstc_init(DstCompiler *c, DstTable *env, const uint8_t *where) {
    c->scope = NULL;
    c->buffer = NULL;
    c->mapbuffer = NULL;
    c->recursion_guard = DST_RECURSION_GUARD;
    c->env = env;
    c->source = where;
    c->current_mapping.line = 0;
    c->current_mapping.column = 0;
    /* Init result */
    c->result.error = NULL;
    c->result.status = DST_COMPILE_OK;
    c->result.funcdef = NULL;
    c->result.error_mapping.line = 0;
    c->result.error_mapping.column = 0;
}

/* Deinitialize a compiler struct */
static void dstc_deinit(DstCompiler *c) {
    dst_v_free(c->buffer);
    dst_v_free(c->mapbuffer);
    c->env = NULL;
}

/* Compile a form. */
DstCompileResult dst_compile(Dst source, DstTable *env, const uint8_t *where) {
    DstCompiler c;
    DstScope rootscope;
    DstFopts fopts;

    dstc_init(&c, env, where);

    /* Push a function scope */
    dstc_scope(&rootscope, &c, DST_SCOPE_FUNCTION | DST_SCOPE_TOP, "root");

    /* Set initial form options */
    fopts.compiler = &c;
    fopts.flags = DST_FOPTS_TAIL | DST_SLOTTYPE_ANY;
    fopts.hint = dstc_cslot(dst_wrap_nil());

    /* Compile the value */
    dstc_value(fopts, source);

    if (c.result.status == DST_COMPILE_OK) {
        DstFuncDef *def = dstc_pop_funcdef(&c);
        def->name = dst_cstring("_thunk");
        c.result.funcdef = def;
    } else {
        c.result.error_mapping = c.current_mapping;
        dstc_popscope(&c);
    }

    dstc_deinit(&c);

    return c.result;
}

/* C Function for compiling */
static int cfun(DstArgs args) {
    DstCompileResult res;
    DstTable *t;
    DstTable *env;
    DST_MINARITY(args, 2);
    DST_MAXARITY(args, 3);
    DST_ARG_TABLE(env, args, 1);
    const uint8_t *source = NULL;
    if (args.n == 3) {
        DST_ARG_STRING(source, args, 2);
    }
    res = dst_compile(args.v[0], env, source);
    if (res.status == DST_COMPILE_OK) {
        DST_RETURN_FUNCTION(args, dst_thunk(res.funcdef));
    } else {
        t = dst_table(2);
        dst_table_put(t, dst_csymbolv(":error"), dst_wrap_string(res.error));
        dst_table_put(t, dst_csymbolv(":error-line"), dst_wrap_integer(res.error_mapping.line));
        dst_table_put(t, dst_csymbolv(":error-column"), dst_wrap_integer(res.error_mapping.column));
        DST_RETURN_TABLE(args, t);
    }
}

static const DstReg cfuns[] = {
    {"compile", cfun},
    {NULL, NULL}
};

int dst_lib_compile(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_cfuns(env, cfuns); 
    return 0;
}
