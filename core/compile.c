/*
* Copyright (c) 2017 Calvin Rose
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
#include "compile.h"

/* Lazily sort the optimizers */
/*static int optimizers_sorted = 0;*/

/* Lookups for specials and optimizable c functions. */
/*DstCFunctionOptimizer dst_compiler_optimizers[255];*/
/*DstSpecial dst_compiler_specials[16];*/

/* Throw an error with a dst string */
void dst_compile_error(DstCompiler *c, const DstValue *sourcemap, const uint8_t *m) {
    c->results.error_start = dst_unwrap_integer(sourcemap[0]);
    c->results.error_end = dst_unwrap_integer(sourcemap[1]);
    c->results.error = m;
    longjmp(c->on_error, 1);
}

/* Throw an error with a message in a cstring */
void dst_compile_cerror(DstCompiler *c, const DstValue *sourcemap, const char *m) {
    dst_compile_error(c, sourcemap, dst_cstring(m));
}

/* Use these to get sub options. They will traverse the source map so
 * compiler errors make sense. Then modify the returned options. */
DstFormOptions dst_compile_getopts_index(DstFormOptions opts, int32_t index) {
    const DstValue *sourcemap = dst_parse_submap_index(opts.sourcemap, index);
    DstValue nextval = dst_getindex(opts.x, index);
    opts.x = nextval;
    opts.sourcemap = sourcemap;
    return opts;
}
DstFormOptions dst_compile_getopts_key(DstFormOptions opts, DstValue key) {
    const DstValue *sourcemap = dst_parse_submap_key(opts.sourcemap, key);
    opts.x = key;
    opts.sourcemap = sourcemap;
    return opts;
}
DstFormOptions dst_compile_getopts_value(DstFormOptions opts, DstValue key) {
    const DstValue *sourcemap = dst_parse_submap_value(opts.sourcemap, key);
    DstValue nextval = dst_get(opts.x, key);
    opts.x = nextval;
    opts.sourcemap = sourcemap;
    return opts;
}
/* Eneter a new scope */
void dst_compile_scope(DstCompiler *c, int newfn) {
    int32_t newcount, oldcount;
    int32_t newlevel, oldlevel;
    DstScope *scope;
    oldcount = c->scopecount;
    newcount = oldcount + 1;
    oldlevel = c->scopecount
        ? c->scopes[c->scopecount - 1].level
        : 0;
    newlevel = oldlevel + newfn;
    if (newcount > c->scopecap) {
        int32_t newcap = 2 * newcount;
        c->scopes = realloc(c->scopes, newcap * sizeof(DstScope));
        if (NULL == c->scopes) {
            DST_OUT_OF_MEMORY;
        }
        c->scopecap = newcap;
    }
    scope = c->scopes + oldcount;
    c->scopecount = newcount;
    dst_array_init(&(scope->constants), 0);
    dst_table_init(&scope->symbols, 4);
    dst_table_init(&scope->constantrev, 4);

    scope->envs = NULL;
    scope->envcount = 0;
    scope->envcap = 0;
    
    scope->bytecode_start = c->buffercount;

    dst_compile_slotpool_init(&scope->slots);
    dst_compile_slotpool_init(&scope->unorderedslots);

    scope->level = newlevel;
    scope->flags = newfn ? DST_SCOPE_FUNCTION : 0;
}

/* Leave a scope. */
void dst_compile_popscope(DstCompiler *c) {
    DstScope *scope;
    dst_assert(c->scopecount, "could not pop scope");
    scope = c->scopes + --c->scopecount;
    /* Move free slots to parent scope if not a new function.
     * We need to know the total number of slots used when compiling the function. */
    if (!(scope->flags & DST_SCOPE_FUNCTION) && c->scopecount) {
        DstScope *newscope = dst_compile_topscope(c);
        dst_compile_slotpool_extend(&newscope->slots, scope->slots.count);
    }
    dst_table_deinit(&scope->symbols);
    dst_table_deinit(&scope->constantrev);
    dst_array_deinit(&scope->constants);
    dst_compile_slotpool_deinit(&scope->slots);
    dst_compile_slotpool_deinit(&scope->unorderedslots);
    free(scope->envs);
}

DstSlot *dst_compile_constantslot(DstCompiler *c, DstValue x) {
    DstScope *scope = dst_compile_topscope(c);
    DstSlot *ret = dst_compile_slotpool_alloc(&scope->unorderedslots);
    ret->flags = (1 << dst_type(x)) | DST_SLOT_CONSTANT | DST_SLOT_NOTEMPTY;
    ret->index = -1;
    ret->constant = x;
    ret->envindex = 0;
    return ret;
}

/* Free a single slot */
void dst_compile_freeslot(DstCompiler *c, DstSlot *slot) {
    DstScope *scope = dst_compile_topscope(c);
    if (slot->flags & (DST_SLOT_CONSTANT)) {
        return;
    }
    if (slot->envindex != 0) {
        return;
    }
    dst_compile_slotpool_free(&scope->slots, slot);
}

/*
 * The mechanism for passing environments to closures is a bit complicated,
 * but ensures a few properties.
 * * Environments are on the stack unless they need to be closurized
 * * Environments can be shared between closures
 * * A single closure can access any of multiple parent environments in constant time (no linked lists)
 *
 *  FuncDefs all have a list of a environment indices that are inherited from the
 *  parent function, as well as a flag indicating if the closures own stack variables
 *  are needed in a nested closure. The list of indices says which of the parent environments
 *  go into which environment slot for the new closure. This allows closures to use whatever environments
 *  they need to, as well as pass these environments to sub closures. To access the direct parent's environment,
 *  the FuncDef must copy the 0th parent environment. If a closure does not need to export it's own stack
 *  variables for creating closures, it must keep the 0th entry in the env table to NULL.
 *
 *  TODO - check if this code is bottle neck and search for better data structures.
 */

/* Allow searching for symbols. Return information about the symbol */
DstSlot *dst_compile_resolve(
        DstCompiler *c,
        const DstValue *sourcemap,
        const uint8_t *sym) {

    DstSlot *ret = NULL;
    DstScope *scope = dst_compile_topscope(c);
    int32_t env_index = 0;
    int foundlocal = 1;

    /* Search scopes for symbol, starting from top */
    while (scope >= c->scopes) {
        DstValue check = dst_table_get(&scope->symbols, dst_wrap_symbol(sym));
        if (dst_checktype(check, DST_USERDATA)) {
            ret = dst_unwrap_pointer(check);
            goto found;
        }
        scope--;
        if (scope->flags & DST_SCOPE_FUNCTION)
            foundlocal = 0;
    }

    /* Symbol not found */
    dst_compile_error(c, sourcemap, dst_formatc("unknown symbol %q", sym));

    /* Symbol was found */
    found:

    /* Constants can be returned immediately (they are stateless) */
    if (ret->flags & DST_SLOT_CONSTANT)
        return ret;

    /* non-local scope needs to expose its environment */
    if (!foundlocal) {
        scope->flags |= DST_SCOPE_ENV;
        if (scope->envcount < 1) {
            scope->envcount = 1;
            scope->envs = malloc(sizeof(int32_t) * 10);
            if (NULL == scope->envs) {
                DST_OUT_OF_MEMORY;
            }
            scope->envcap = 10;
            scope->envs[0] = 0;
        }
        scope++;
    }

    /* Propogate env up to current scope */
    while (scope <= dst_compile_topscope(c)) {
        if (scope->flags & DST_SCOPE_FUNCTION) {
            int32_t j;
            int32_t newcount = scope->envcount + 1;
            int scopefound = 0;
            /* Check if scope already has env. If so, break */
            for (j = 1; j < scope->envcount; j++) {
                if (scope->envs[j] == env_index) {
                    scopefound = 1;
                    env_index = j;
                    break;
                }
            }
            if (!scopefound) {
                env_index = scope->envcount;
                /* Ensure capacity for adding scope */
                if (newcount > scope->envcap) {
                    int32_t newcap = 2 * newcount;
                    scope->envs = realloc(scope->envs, sizeof(int32_t) * newcap);
                    if (NULL == scope->envs) {
                        DST_OUT_OF_MEMORY;
                    }
                    scope->envcap = newcap;
                }
                scope->envs[scope->envcount] = env_index;
                scope->envcount = newcount;
            }
        }
        scope++;
    }
    
    /* Store in the unordered slots so we don't modify the original slot. */
    if (!foundlocal) {
        DstSlot *newret = dst_compile_slotpool_alloc(&scope->unorderedslots);
        *newret = *ret;
        newret->envindex = env_index;
        ret = newret;
    }

    return ret;
}

/* Emit a raw instruction with source mapping. */
void dst_compile_emit(DstCompiler *c, const DstValue *sourcemap, uint32_t instr) {
    int32_t index = c->buffercount;
    int32_t newcount = index + 1;
    if (newcount > c->buffercap) {
        int32_t newcap = 2 * newcount;
        c->buffer = realloc(c->buffer, newcap * sizeof(uint32_t));
        c->mapbuffer = realloc(c->mapbuffer, newcap * sizeof(int32_t) * 2);
        if (NULL == c->buffer || NULL == c->mapbuffer) {
            DST_OUT_OF_MEMORY;
        }
        c->buffercap = newcap;
    }
    c->buffercount = newcount;
    if (NULL != sourcemap) {
        c->mapbuffer[index * 2] = dst_unwrap_integer(sourcemap[0]);
        c->mapbuffer[index * 2 + 1] = dst_unwrap_integer(sourcemap[1]);
    }
    c->buffer[index] = instr;
}

/* Represents a local slot - not a constant, and within a specified range. Also
 * contains if it corresponds to a real slot. If temp, then the slot index
 * should be free right after use */
typedef struct DstLocalSlot DstLocalSlot;
struct DstLocalSlot {
    DstSlot *orig;
    int temp;
    int dirty;
    int32_t index;
};

/* Get the index of a constant */
static int32_t dst_compile_constant_index(DstCompiler *c, const DstValue *sourcemap, DstValue x) {
    DstScope *scope = dst_compile_topscope(c);
    DstValue check;
    int32_t count = scope->constants.count;
    check = dst_table_get(&scope->constantrev, x);
    if (dst_checktype(check, DST_INTEGER)) {
        return dst_unwrap_integer(check);
    }
    if (count >= 0xFFFF) {
        dst_compile_cerror(c, sourcemap, "too many constants");
    }
    dst_array_push(&scope->constants, x);
    dst_table_put(&scope->constantrev, x, dst_wrap_integer(count));
    return count;
}

/* Realize any slot to a local slot. Call this to get a slot index
 * that can be used in an instruction. */
static DstLocalSlot dst_compile_slot_pre(
        DstCompiler *c,
        const DstValue *sourcemap,
        int32_t max,
        int32_t hint,
        int isdest,
        int nth,
        DstSlot *s) {

    DstScope *scope = dst_compile_topscope(c);
    DstLocalSlot ret;
    ret.orig = s;
    ret.dirty = isdest;
    ret.temp = 0;

    if (s->flags & DST_SLOT_CONSTANT) {
        int32_t cindex;
        int32_t nextfree = dst_compile_slotpool_alloc(&scope->slots)->index;
        if (hint >= 0 && hint <= 0xFF) {
            ret.index = hint;
        } else if (nextfree >= 0xF0) {
            ret.index = 0xF0 + nth;
            dst_compile_slotpool_freeindex(&scope->slots, nextfree);
        } else {
            ret.temp = 1;
            ret.index = nextfree;
        }
        /* Use instructions for loading certain constants */
        switch (dst_type(s->constant)) {
            case DST_NIL:
                dst_compile_emit(c, sourcemap, ((uint32_t)(ret.index) << 8) | DOP_LOAD_NIL);
                break;
            case DST_TRUE:
                dst_compile_emit(c, sourcemap, ((uint32_t)(ret.index) << 8) | DOP_LOAD_TRUE);
                break;
            case DST_FALSE:
                dst_compile_emit(c, sourcemap, ((uint32_t)(ret.index) << 8) | DOP_LOAD_FALSE);
                break;
            case DST_INTEGER:
                {
                    int32_t i = dst_unwrap_integer(s->constant);
                    if (i <= INT16_MAX && i >= INT16_MIN) {
                        dst_compile_emit(c, sourcemap, 
                                ((uint32_t)i << 16) |
                                ((uint32_t)(ret.index) << 8) |
                                DOP_LOAD_INTEGER);
                        break;
                    }
                    /* fallthrough */
                }
            default:
                cindex = dst_compile_constant_index(c, sourcemap, s->constant);
                if (isdest)
                    dst_compile_cerror(c, sourcemap, "cannot write to a constant");
                dst_compile_emit(c, sourcemap, 
                        ((uint32_t)cindex << 16) |
                        ((uint32_t)(ret.index) << 8) |
                        DOP_LOAD_CONSTANT);
                break;
        }
    } else if (s->envindex > 0 || s->index > max) {
        /* Get a local slot to shadow the environment or far slot */
        int32_t nextfree = dst_compile_slotpool_alloc(&scope->slots)->index;
        if (hint >= 0 && hint <= 0xFF) {
            ret.index = hint;
        } else if (nextfree >= 0xF0) {
            ret.index = 0xF0 + nth;
            dst_compile_slotpool_freeindex(&scope->slots, nextfree);
        } else {
            ret.temp = 1;
            ret.index = nextfree;
        }
        if (!isdest) {
            /* Move the remote slot into the local space */
            if (s->envindex > 0) {
                /* Load the higher slot */
                dst_compile_emit(c, sourcemap, 
                        ((uint32_t)(s->index) << 24) |
                        ((uint32_t)(s->envindex) << 16) |
                        ((uint32_t)(ret.index) << 8) |
                        DOP_LOAD_UPVALUE);
            } else {
                /* Slot is a far slot: greater than 0xFF. Get
                 * the far data and bring it to the near slot. */
                dst_compile_emit(c, sourcemap, 
                        ((uint32_t)(s->index) << 16) |
                        ((uint32_t)(ret.index) << 8) |
                        DOP_MOVE_NEAR);
            }
        }
    } else if (hint >= 0 && hint <= 0xFF && isdest) {
        ret.index = hint;
    } else {
        /* We have a normal slot that fits in the required bit width */            
        ret.index = s->index;
    }
    return ret;
}

/* Call this on a DstLocalSlot to free the slot or sync any changes
 * made after the instruction has been emitted. */
static void dst_compile_slot_post(
        DstCompiler *c,
        const DstValue *sourcemap,
        DstLocalSlot ls) {
    DstSlot *s = ls.orig;
    DstScope *scope = dst_compile_topscope(c);
    if (ls.temp)
        dst_compile_slotpool_freeindex(&scope->slots, ls.index);
    if (ls.dirty) {
        /* We need to save the data in the local slot to the original slot */
        if (s->envindex > 0) {
            /* Load the higher slot */
            dst_compile_emit(c, sourcemap, 
                    ((uint32_t)(s->index) << 24) |
                    ((uint32_t)(s->envindex) << 16) |
                    ((uint32_t)(ls.index) << 8) |
                    DOP_SET_UPVALUE);
        } else if (s->index != ls.index) {
            /* There was a local remapping */
            dst_compile_emit(c, sourcemap, 
                    ((uint32_t)(s->index) << 16) |
                    ((uint32_t)(ls.index) << 8) |
                    DOP_MOVE_FAR);
        }
    }
}

/* Generate the return instruction for a slot. */
static void dst_compile_return(DstCompiler *c, const DstValue *sourcemap, DstSlot *s) {
    if (s->flags & DST_SLOT_CONSTANT && dst_checktype(s->constant, DST_NIL)) {
            dst_compile_emit(c, sourcemap, DOP_RETURN_NIL);
    } else {
        DstLocalSlot ls = dst_compile_slot_pre(
                c, sourcemap, 0xFFFF, -1,
                1, 1, s);
        dst_compile_emit(c, sourcemap, DOP_RETURN | (ls.index << 8));
        dst_compile_slot_post(c, sourcemap, ls);
    }
}

DstSlot *dst_compile_def(DstFormOptions opts, int32_t argn, const DstValue *argv) {
    DstScope *scope;
    DstSlot *rvalue;
    DstFormOptions subopts;
    DstValue check;
    if (argn != 2)
        dst_compile_cerror(opts.compiler, opts.sourcemap, "expected 2 arguments");
    if (!dst_checktype(argv[0], DST_SYMBOL))
        dst_compile_cerror(opts.compiler, opts.sourcemap, "expected symbol");
    scope = dst_compile_topscope(opts.compiler);
    check = dst_table_get(&scope->symbols, argv[0]);
    if (dst_checktype(check, DST_INTEGER)) {
        dst_compile_cerror(opts.compiler, opts.sourcemap, "cannot redefine symbol");
    }
    subopts = dst_compile_getopts_index(opts, 1);
    rvalue = dst_compile_value(subopts);
    dst_table_put(&scope->symbols, argv[0], dst_wrap_userdata(rvalue));
    return rvalue;
}

/* Compile an array */

/* Compile a single value */
DstSlot *dst_compile_value(DstFormOptions opts) {
    DstSlot *ret;
    int doreturn = opts.flags & DST_FOPTS_TAIL;
    if (opts.compiler->recursion_guard <= 0) {
        dst_compile_cerror(opts.compiler, opts.sourcemap, "recursed too deeply");
    }
    opts.compiler->recursion_guard--;
    switch (dst_type(opts.x)) {
        default:
            ret = dst_compile_constantslot(opts.compiler, opts.x);
            break;
        case DST_SYMBOL:
            {
                const uint8_t *sym = dst_unwrap_symbol(opts.x);
                if (dst_string_length(sym) > 0 && sym[0] != ':') {
                    ret = dst_compile_resolve(opts.compiler, opts.sourcemap, sym);
                } else {
                    ret = dst_compile_constantslot(opts.compiler, opts.x);
                }
                break;
            }
        /*case DST_TUPLE:*/
            /*ret = dst_compile_tuple(opts); */
            /*break;*/
        /*case DST_ARRAY:*/
            /*ret = dst_compile_array(opts); */
            /*break;*/
        /*case DST_STRUCT:*/
            /*ret = dst_compile_struct(opts); */
            /*break;*/
        /*case DST_TABLE:*/
            /*ret = dst_compile_table(opts);*/
            /*break;*/
    }
    if (doreturn) {
        dst_compile_return(opts.compiler, opts.sourcemap, ret);
    }
    opts.compiler->recursion_guard++;
    return ret;
}

/* Compile a funcdef */
static DstFuncDef *dst_compile_pop_funcdef(DstCompiler *c) {
    DstScope *scope = dst_compile_topscope(c);
    DstFuncDef *def;

    /* Initialize funcdef */
    def = dst_alloc(DST_MEMORY_FUNCDEF, sizeof(DstFuncDef));
    def->environments_length = scope->envcount;
    def->environments = malloc(sizeof(int32_t) * def->environments_length);
    def->constants_length = 0;
    def->constants = malloc(sizeof(DstValue) * scope->constants.count);
    def->bytecode_length = c->buffercount - scope->bytecode_start;
    def->bytecode = malloc(sizeof(uint32_t) * def->bytecode_length);
    def->slotcount = scope->slots.count;

    if (NULL == def->environments ||
        NULL == def->constants ||
        NULL == def->bytecode) {
        DST_OUT_OF_MEMORY;
    }

    memcpy(def->environments, scope->envs, def->environments_length * sizeof(int32_t));
    memcpy(def->constants, scope->constants.data, def->constants_length * sizeof(DstValue));
    memcpy(def->bytecode, c->buffer + c->buffercount, def->bytecode_length * sizeof(uint32_t));

    if (c->mapbuffer) {
        def->sourcemap = malloc(sizeof(uint32_t) * 2 * def->bytecode_length);
        if (NULL == def->sourcemap) {
            DST_OUT_OF_MEMORY;
        }
        memcpy(def->sourcemap, c->mapbuffer + 2 * c->buffercount, def->bytecode_length * 2 * sizeof(uint32_t));
    }

    /* Reset bytecode gen */
    c->buffercount = scope->bytecode_start;

    /* Manually set arity and flags later */
    def->flags = 0;
    def->arity = 0;

    /* Set some flags */
    if (scope->flags & DST_SCOPE_ENV) {
        def->flags |= DST_FUNCDEF_FLAG_NEEDSENV;
    }

    /* Pop the scope */
    dst_compile_popscope(c);

    return def;
}

/* Print a slot for debugging */
/*static void print_slot(DstSlot *s) {*/
    /*if (!(s->flags & DST_SLOT_NOTEMPTY)) {*/
        /*printf("X");*/
    /*} else if (s->flags & DST_SLOT_CONSTANT) {*/
        /*dst_puts(dst_short_description(s->constant));*/
    /*} else if (s->envindex > 0) {*/
        /*printf("UP%d[%d]", s->envindex, s->index);*/
    /*} else {*/
        /*printf("%d", s->index);*/
    /*}*/
/*}*/

/* Deinitialize a compiler struct */
static void dst_compile_cleanup(DstCompiler *c) {
    while (c->scopecount)
        dst_compile_popscope(c);
    free(c->scopes);
    free(c->buffer);
    free(c->mapbuffer);
    c->buffer = NULL;
    c->mapbuffer = NULL;
    c->scopes = NULL;
}

DstCompileResults dst_compile(DstCompileOptions opts) {
    DstCompiler c;
    DstFormOptions fopts;
    DstSlot *s;

    if (setjmp(c.on_error)) {
        c.results.status = DST_COMPILE_ERROR;
        dst_compile_cleanup(&c);
        c.results.funcdef = NULL;
        return c.results;
    }

    /* Initialize the compiler struct */
    c.scopecount = 0;
    c.scopecap = 0;
    c.scopes = NULL;
    c.buffercap = 0;
    c.buffercount = 0;
    c.buffer = NULL;
    c.mapbuffer = NULL;
    c.recursion_guard = 1024;

    /* Push a function scope */
    dst_compile_scope(&c, 1);

    fopts.compiler = &c;
    fopts.sourcemap = opts.sourcemap;
    fopts.flags = DST_FOPTS_TAIL | DST_SLOTTYPE_ANY;
    fopts.hint = 0;
    fopts.x = opts.source;

    /* Compile the value */
    s = dst_compile_value(fopts);

    c.results.funcdef = dst_compile_pop_funcdef(&c);
    c.results.status = DST_COMPILE_OK;

    dst_compile_cleanup(&c);

    return c.results;
}

DstFunction *dst_compile_func(DstCompileResults res) {
    if (res.status != DST_COMPILE_OK) {
        return NULL;
    }
    DstFunction *func = dst_alloc(DST_MEMORY_FUNCTION, sizeof(DstFunction));
    func->def = res.funcdef;
    func->envs = NULL;
    return func;
}
