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
#include <dst/dststl.h>
#include "compile.h"
#include "gc.h"
#include "sourcemap.h"

/* Lazily sort the optimizers */
/*static int optimizers_sorted = 0;*/

/* Lookups for specials and optimizable c functions. */
/*DstCFunctionOptimizer dst_compiler_optimizers[255];*/

/* Throw an error with a dst string */
void dst_compile_error(DstCompiler *c, const DstValue *sourcemap, const uint8_t *m) {
    if (NULL != sourcemap) {
        c->result.error_start = dst_unwrap_integer(sourcemap[0]);
        c->result.error_end = dst_unwrap_integer(sourcemap[1]);
    } else {
        c->result.error_start = -1;
        c->result.error_end = -1;
    }
    c->result.error = m;
    longjmp(c->on_error, 1);
}

/* Throw an error with a message in a cstring */
void dst_compile_cerror(DstCompiler *c, const DstValue *sourcemap, const char *m) {
    dst_compile_error(c, sourcemap, dst_cstring(m));
}

/* Use these to get sub options. They will traverse the source map so
 * compiler errors make sense. Then modify the returned options. */
DstFormOptions dst_compile_getopts_index(DstFormOptions opts, int32_t index) {
    const DstValue *sourcemap = dst_sourcemap_index(opts.sourcemap, index);
    DstValue nextval = dst_getindex(opts.x, index);
    opts.x = nextval;
    opts.sourcemap = sourcemap;
    return opts;
}

DstFormOptions dst_compile_getopts_key(DstFormOptions opts, DstValue key) {
    const DstValue *sourcemap = dst_sourcemap_key(opts.sourcemap, key);
    opts.x = key;
    opts.sourcemap = sourcemap;
    return opts;
}

DstFormOptions dst_compile_getopts_value(DstFormOptions opts, DstValue key) {
    const DstValue *sourcemap = dst_sourcemap_value(opts.sourcemap, key);
    DstValue nextval = dst_get(opts.x, key);
    opts.x = nextval;
    opts.sourcemap = sourcemap;
    return opts;
}

/* Allocate a slot index */
static int32_t slotalloc_index(DstScope *scope) {
    /* Get the nth bit in the array */
    int32_t i, biti;
    biti = -1;
    for (i = 0; i < scope->scap; i++) {
        uint32_t block = scope->slots[i];
        if (block != 0xFFFFFFFF) {
            biti = i << 5; /* + clz(block) */
            while (block & 1) {
                biti++;
                block >>= 1;
            }
            break;
        }
    }
    if (biti == -1) {
        int32_t j;
        int32_t newcap = scope->scap * 2 + 1;
        scope->slots = realloc(scope->slots, sizeof(int32_t) * newcap);
        if (NULL == scope->slots) {
            DST_OUT_OF_MEMORY;
        }
        for (j = scope->scap; j < newcap; j++) {
            /* Preallocate slots 0xF0 through 0xFF. */
            scope->slots[j] = j == 7 ? 0xFFFF0000 : 0x00000000;
        }
        biti = scope->scap << 5;
        scope->scap = newcap;
    }
    /* set the bit at index biti */
    scope->slots[biti >> 5] |= 1 << (biti & 0x1F);
    if (biti > scope->smax)
        scope->smax = biti;
    return biti;
}

/* Allocate a slot */
static DstSlot slotalloc(DstScope *scope) {
    DstSlot ret;
    ret.index = slotalloc_index(scope);
    ret.envindex = 0;
    ret.constant = dst_wrap_nil();
    ret.flags = 0;
    return ret;
}

/* Free a slot index */
static void slotfree_index(DstScope *scope, int32_t index) {
    /* Don't free the pre allocated slots */
    if (index < 0xF0 || index > 0xFF)
        scope->slots[index >> 5] &= ~(1 << (index & 0x1F));
}

/* Free a slot */
static void slotfree(DstScope *scope, DstSlot s) {
    if (s.flags & DST_SLOT_CONSTANT)
        return;
    if (s.envindex > 0)
        return;
    slotfree_index(scope, s.index);
}

/* Find a slot given a symbol. Return 1 if found, otherwise 0. */
static int slotsymfind(DstScope *scope, const uint8_t *sym, DstSlot *out) {
    int32_t i;
    for (i = 0; i < scope->symcount; i++) {
        if (scope->syms[i].sym == sym) {
            *out = scope->syms[i].slot;
            return 1;
        }
    }
    return 0;
}

/* Add a slot to a scope with a symbol associated with it (def or var). */
static void slotsym(DstScope *scope, const uint8_t *sym, DstSlot s) {
    int32_t index = scope->symcount;
    int32_t newcount = index + 1;
    if (newcount > scope->symcap) {
        int32_t newcap = 2 * newcount;
        scope->syms = realloc(scope->syms, newcap * sizeof(scope->syms[0]));
        if (NULL == scope->syms) {
            DST_OUT_OF_MEMORY;
        }
        scope->symcap = newcap;
    }
    scope->symcount = newcount;
    scope->syms[index].sym = sym;
    scope->syms[index].slot = s;
}

/* Add a constant to the current scope. Return the index of the constant. */
static int32_t addconst(DstCompiler *c, const DstValue *sourcemap, DstValue x) {
    DstScope *scope = dst_compile_topscope(c);
    int32_t i, index, newcount;
    /* Get the topmost function scope */
    while (scope > c->scopes) {
        if (scope->flags & DST_SCOPE_FUNCTION)
            break;
        scope--;
    }
    for (i = 0; i < scope->ccount; i++) {
        if (dst_equals(x, scope->consts[i]))
            return i;
    }
    if (scope->ccount >= 0xFFFF)
        dst_compile_cerror(c, sourcemap, "too many constants");
    index = scope->ccount;
    newcount = index + 1;
    if (newcount > scope->ccap) {
        int32_t newcap = 2 * newcount;
        scope->consts = realloc(scope->consts, newcap * sizeof(DstValue));
        if (NULL == scope->consts) {
            DST_OUT_OF_MEMORY;
        }
        scope->ccap = newcap;
    }
    scope->consts[index] = x;
    scope->ccount = newcount;
    return index;
}

/* Enter a new scope */
void dst_compile_scope(DstCompiler *c, int flags) {
    int32_t newcount, oldcount;
    DstScope *scope;
    oldcount = c->scopecount;
    newcount = oldcount + 1;
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

    /* Initialize the scope */

    scope->consts = NULL;
    scope->ccap = 0;
    scope->ccount = 0;

    scope->syms = NULL;
    scope->symcount = 0;
    scope->symcap = 0;

    scope->envs = NULL;
    scope->envcount = 0;
    scope->envcap = 0;
    
    scope->bytecode_start = c->buffercount;

    /* Initialize slots */
    scope->slots = NULL;
    scope->scap = 0;
    scope->smax = -1;

    scope->flags = flags;
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
        if (newscope->smax < scope->smax) 
            newscope->smax = scope->smax;
    }
    free(scope->consts);
    free(scope->slots);
    free(scope->syms);
    free(scope->envs);
}

DstSlot dst_compile_constantslot(DstValue x) {
    DstSlot ret;
    ret.flags = (1 << dst_type(x)) | DST_SLOT_CONSTANT;
    ret.index = -1;
    ret.constant = x;
    ret.envindex = 0;
    return ret;
}

/* Free a single slot */
void dst_compile_freeslot(DstCompiler *c, DstSlot slot) {
    slotfree(dst_compile_topscope(c), slot);
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
DstSlot dst_compile_resolve(
        DstCompiler *c,
        const DstValue *sourcemap,
        const uint8_t *sym) {

    DstSlot ret = dst_compile_constantslot(dst_wrap_nil());
    DstScope *scope = dst_compile_topscope(c);
    int32_t envindex = 0;
    int foundlocal = 1;

    /* Search scopes for symbol, starting from top */
    while (scope >= c->scopes) {
        if (slotsymfind(scope, sym, &ret))
            goto found;
        if (scope->flags & DST_SCOPE_FUNCTION)
            foundlocal = 0;
        scope--;
    }

    /* Symbol not found - check for global */
    {
        DstValue check = dst_get(c->env, dst_wrap_symbol(sym));
        if (dst_checktype(check, DST_STRUCT) || dst_checktype(check, DST_TABLE)) {
            DstValue ref = dst_get(check, dst_csymbolv("ref"));
            if (dst_checktype(ref, DST_ARRAY)) {
                DstSlot ret = dst_compile_constantslot(ref);
                ret.flags |= DST_SLOT_REF | DST_SLOT_NAMED | DST_SLOT_MUTABLE;
                return ret;
            } else {
                DstValue value = dst_get(check, dst_csymbolv("value"));
                return dst_compile_constantslot(value);
            }
        } else {
            dst_compile_error(c, sourcemap, dst_formatc("unknown symbol %q", sym));
        }
    }

    /* Symbol was found */
    found:

    /* Constants can be returned immediately (they are stateless) */
    if (ret.flags & DST_SLOT_CONSTANT)
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
                if (scope->envs[j] == envindex) {
                    scopefound = 1;
                    envindex = j;
                    break;
                }
            }
            /* Add the environment if it is not already referenced */
            if (!scopefound) {
                envindex = scope->envcount;
                /* Ensure capacity for adding scope */
                if (newcount > scope->envcap) {
                    int32_t newcap = 2 * newcount;
                    scope->envs = realloc(scope->envs, sizeof(int32_t) * newcap);
                    if (NULL == scope->envs) {
                        DST_OUT_OF_MEMORY;
                    }
                    scope->envcap = newcap;
                }
                scope->envs[scope->envcount] = envindex;
                scope->envcount = newcount;
            }
        }
        scope++;
    }
    
    ret.envindex = envindex;
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

/* Helper */
static int32_t slotalloc_temp(DstScope *scope, int32_t max, int32_t nth) {
    int32_t ret = slotalloc_index(scope);
    if (ret > max) {
        slotfree_index(scope, ret);
        ret = 0xF0 + nth;
    }
    return ret;
}

/* Realize any slot to a local slot. Call this to get a slot index
 * that can be used in an instruction. */
static int32_t dst_compile_preread(
        DstCompiler *c,
        const DstValue *sourcemap,
        int32_t max,
        int nth,
        DstSlot s) {

    DstScope *scope = dst_compile_topscope(c);
    int32_t ret;

    if (s.flags & DST_SLOT_REF)
        max = 0xFF;

    if (s.flags & DST_SLOT_CONSTANT) {
        int32_t cindex;
        ret = slotalloc_temp(scope, max, nth);
        /* Use instructions for loading certain constants */
        switch (dst_type(s.constant)) {
            case DST_NIL:
                dst_compile_emit(c, sourcemap, (ret << 8) | DOP_LOAD_NIL);
                break;
            case DST_TRUE:
                dst_compile_emit(c, sourcemap, (ret << 8) | DOP_LOAD_TRUE);
                break;
            case DST_FALSE:
                dst_compile_emit(c, sourcemap, (ret << 8) | DOP_LOAD_FALSE);
                break;
            case DST_INTEGER:
                {
                    int32_t i = dst_unwrap_integer(s.constant);
                    if (i <= INT16_MAX && i >= INT16_MIN) {
                        dst_compile_emit(c, sourcemap, 
                                (i << 16) |
                                (ret << 8) |
                                DOP_LOAD_INTEGER);
                        break;
                    }
                    /* fallthrough */
                }
            default:
                cindex = addconst(c, sourcemap, s.constant);
                dst_compile_emit(c, sourcemap, 
                        (cindex << 16) |
                        (ret << 8) |
                        DOP_LOAD_CONSTANT);
                break;
        }
        /* If we also are a reference, deref the one element array */
        if (s.flags & DST_SLOT_REF) {
            dst_compile_emit(c, sourcemap, 
                    (ret << 16) |
                    (ret << 8) |
                    DOP_GET_INDEX);
        }
    } else if (s.envindex > 0 || s.index > max) {
        ret = slotalloc_temp(scope, max, nth);
        dst_compile_emit(c, sourcemap, 
                ((uint32_t)(s.index) << 24) |
                ((uint32_t)(s.envindex) << 16) |
                ((uint32_t)(ret) << 8) |
                DOP_LOAD_UPVALUE);
    } else if (s.index > max) {
        ret = slotalloc_temp(scope, max, nth);
        dst_compile_emit(c, sourcemap, 
                ((uint32_t)(s.index) << 16) |
                ((uint32_t)(ret) << 8) |
                    DOP_MOVE_NEAR);
    } else {
        /* We have a normal slot that fits in the required bit width */            
        ret = s.index;
    }
    return ret;
}

/* Call this to release a read handle after emitting the instruction. */
static void dst_compile_postread(DstCompiler *c, DstSlot s, int32_t index) {
    if (index != s.index || s.envindex > 0 || s.flags & DST_SLOT_CONSTANT) {
        /* We need to free the temporary slot */
        DstScope *scope = dst_compile_topscope(c);
        slotfree_index(scope, index);
    }
}

/* Move values from one slot to another. The destination must be mutable. */
static void dst_compile_copy(
        DstCompiler *c,
        const DstValue *sourcemap,
        DstSlot dest,
        DstSlot src) {
    int writeback = 0;
    int32_t destlocal = -1;
    int32_t srclocal = -1;
    int32_t reflocal = -1;
    DstScope *scope = dst_compile_topscope(c);

    /* Only write to mutable slots */
    if (!(dest.flags & DST_SLOT_MUTABLE)) {
        dst_compile_cerror(c, sourcemap, "cannot write to constant");
    }

    /* Short circuit if dest and source are equal */
    if (dest.flags == src.flags &&
        dest.index == src.index &&
        dest.envindex == src.envindex) {
        if (dest.flags & DST_SLOT_REF) {
            if (dst_equals(dest.constant, src.constant))
                return;
        } else {
            return;
        }
    }

    /* Process: src -> srclocal -> destlocal -> dest */
    
    /* src -> srclocal */
    srclocal = dst_compile_preread(c, sourcemap, 0xFF, 1, src);

    /* Pull down dest (find destlocal) */
    if (dest.flags & DST_SLOT_REF) {
        writeback = 1;
        destlocal = srclocal;
        reflocal = slotalloc_temp(scope, 0xFF, 2);
        dst_compile_emit(c, sourcemap,
                (addconst(c, sourcemap, dest.constant) << 16) |
                (reflocal << 8) |
                DOP_LOAD_CONSTANT);
    } else if (dest.envindex > 0) {
        writeback = 2;
        destlocal = srclocal;
    } else if (dest.index > 0xFF) {
        writeback = 3;
        destlocal = srclocal;
    } else {
        destlocal = dest.index;
    }

    /* srclocal -> destlocal */
    if (srclocal != destlocal) {
        dst_compile_emit(c, sourcemap,
                ((uint32_t)(srclocal) << 16) |
                ((uint32_t)(destlocal) << 8) |
                DOP_MOVE_NEAR);
    }

    /* destlocal -> dest */ 
    if (writeback == 1) {
        dst_compile_emit(c, sourcemap,
                (destlocal << 16) |
                (reflocal << 8) |
                DOP_PUT_INDEX);
    } else if (writeback == 2) {
        dst_compile_emit(c, sourcemap, 
                ((uint32_t)(dest.index) << 24) |
                ((uint32_t)(dest.envindex) << 16) |
                ((uint32_t)(destlocal) << 8) |
                DOP_SET_UPVALUE);
    } else if (writeback == 3) {
        dst_compile_emit(c, sourcemap,
                ((uint32_t)(dest.index) << 16) |
                ((uint32_t)(destlocal) << 8) |
                DOP_MOVE_FAR);
    }

    /* Cleanup */
    if (reflocal >= 0) {
        slotfree_index(scope, reflocal);
    }
    dst_compile_postread(c, src, srclocal);
}

/* Generate the return instruction for a slot. */
static void dst_compile_return(DstCompiler *c, const DstValue *sourcemap, DstSlot s) {
    if (s.flags & DST_SLOT_CONSTANT && dst_checktype(s.constant, DST_NIL)) {
        dst_compile_emit(c, sourcemap, DOP_RETURN_NIL);
    } else {
        int32_t ls = dst_compile_preread(c, sourcemap, 0xFFFF, 1, s);
        dst_compile_emit(c, sourcemap, DOP_RETURN | (ls << 8));
        dst_compile_postread(c, s, ls);
    }
}

/* Check if the last instructions emitted returned. Relies on the fact that
 * a form should emit no more instructions after returning. */
static int dst_compile_did_return(DstCompiler *c) {
    uint32_t lastop;
    if (!c->buffercount)
        return 0;
    lastop = (c->buffer[c->buffercount - 1]) & 0xFF;
    return lastop == DOP_RETURN ||
        lastop == DOP_RETURN_NIL ||
        lastop == DOP_TAILCALL;
}

/* Get a target slot for emitting an instruction. */
static DstSlot dst_compile_gettarget(DstFormOptions opts) {
    DstScope *scope;
    DstSlot ret;
    if (opts.flags & DST_FOPTS_HINT) {
        return opts.hint;
    }
    scope = dst_compile_topscope(opts.compiler);
    ret = slotalloc(scope);
    /* Inherit type of opts */
    ret.flags |= opts.flags & DST_SLOTTYPE_ANY;
    return ret;
}

/* Push a series of values */
static void dst_compile_pushtuple(
        DstCompiler *c,
        const DstValue *sourcemap,
        DstValue x) {
    DstFormOptions opts;
    int32_t i, len;

    opts.compiler = c;
    opts.hint = dst_compile_constantslot(dst_wrap_nil());
    opts.flags = 0;
    opts.x = x;
    opts.sourcemap = sourcemap;

    len = dst_length(x);
    for (i = 1; i < len - 2; i += 3) {
        DstFormOptions o1 = dst_compile_getopts_index(opts, i);
        DstFormOptions o2 = dst_compile_getopts_index(opts, i + 1);
        DstFormOptions o3 = dst_compile_getopts_index(opts, i + 2);
        DstSlot s1 = dst_compile_value(o1);
        DstSlot s2 = dst_compile_value(o2);
        DstSlot s3 = dst_compile_value(o3);
        int32_t ls1 = dst_compile_preread(c, o1.sourcemap, 0xFF, 1, s1);
        int32_t ls2 = dst_compile_preread(c, o2.sourcemap, 0xFF, 2, s2);
        int32_t ls3 = dst_compile_preread(c, o3.sourcemap, 0xFF, 3, s3);
        dst_compile_emit(c, o1.sourcemap, 
                (ls3 << 24) |
                (ls2 << 16) |
                (ls1 << 8) |
                DOP_PUSH_3);
        dst_compile_postread(c, s1, ls1);
        dst_compile_postread(c, s2, ls2);
        dst_compile_postread(c, s3, ls3);
        dst_compile_freeslot(c, s1);
        dst_compile_freeslot(c, s2);
        dst_compile_freeslot(c, s3);
    }
    if (i == len - 2) {
        DstFormOptions o1 = dst_compile_getopts_index(opts, i);
        DstFormOptions o2 = dst_compile_getopts_index(opts, i + 1);
        DstSlot s1 = dst_compile_value(o1);
        DstSlot s2 = dst_compile_value(o2);
        int32_t ls1 = dst_compile_preread(c, o1.sourcemap, 0xFF, 1, s1);
        int32_t ls2 = dst_compile_preread(c, o2.sourcemap, 0xFFFF, 2, s2);
        dst_compile_emit(c, o1.sourcemap, 
                (ls2 << 16) |
                (ls1 << 8) |
                DOP_PUSH_2);
        dst_compile_postread(c, s1, ls1);
        dst_compile_postread(c, s2, ls2);
        dst_compile_freeslot(c, s1);
        dst_compile_freeslot(c, s2);
    } else if (i == len - 1) {
        DstFormOptions o1 = dst_compile_getopts_index(opts, i);
        DstSlot s1 = dst_compile_value(o1);
        int32_t ls1 = dst_compile_preread(c, o1.sourcemap, 0xFFFFFF, 1, s1);
        dst_compile_emit(c, o1.sourcemap, 
                (ls1 << 8) |
                DOP_PUSH);
        dst_compile_postread(c, s1, ls1);
        dst_compile_freeslot(c, s1);
    }
}

/* Quote */
DstSlot dst_compile_quote(DstFormOptions opts, int32_t argn, const DstValue *argv) {
    if (argn != 1)
        dst_compile_cerror(opts.compiler, opts.sourcemap, "expected 1 argument");
    return dst_compile_constantslot(argv[0]);
}

/* Var */
DstSlot dst_compile_var(DstFormOptions opts, int32_t argn, const DstValue *argv) {
    DstScope *scope = dst_compile_topscope(opts.compiler);
    DstFormOptions subopts;
    DstSlot ret;
    if (argn != 2)
        dst_compile_cerror(opts.compiler, opts.sourcemap, "expected 2 arguments");
    if (!dst_checktype(argv[0], DST_SYMBOL))
        dst_compile_cerror(opts.compiler, opts.sourcemap, "expected symbol");
    subopts = dst_compile_getopts_index(opts, 2);
    subopts.flags &= ~DST_FOPTS_TAIL;
    ret = dst_compile_value(subopts);
    if (scope->flags & DST_SCOPE_TOP) {
        DstCompiler *c = opts.compiler;
        const DstValue *sm = opts.sourcemap;
        DstSlot refslot, refarrayslot;
        /* Global var, generate var */
        DstTable *reftab = dst_table(1);
        DstArray *ref = dst_array(1);
        dst_array_push(ref, dst_wrap_nil());
        dst_table_put(reftab, dst_csymbolv("ref"), dst_wrap_array(ref));
        dst_put(opts.compiler->env, argv[0], dst_wrap_table(reftab));
        refslot = dst_compile_constantslot(dst_wrap_array(ref));
        refarrayslot = refslot;
        refslot.flags |= DST_SLOT_REF | DST_SLOT_NAMED | DST_SLOT_MUTABLE;
        /* Generate code to set ref */
        int32_t refarrayindex = dst_compile_preread(c, sm, 0xFF, 1, refarrayslot);
        int32_t retindex = dst_compile_preread(c, sm, 0xFF, 2, ret);
        dst_compile_emit(c, sm,
                (retindex << 16) |
                (refarrayindex << 8) |
                DOP_PUT_INDEX);
        dst_compile_postread(c, refarrayslot, refarrayindex);
        dst_compile_postread(c, ret, retindex);
        dst_compile_freeslot(c, refarrayslot);
        ret = refslot;
    } else {
        /* Non root scope, bring to local slot */
        DstSlot localslot = dst_compile_gettarget(opts);
        localslot.flags |= DST_SLOT_NAMED | DST_SLOT_MUTABLE;
        dst_compile_copy(opts.compiler, opts.sourcemap, localslot, ret);
        slotsym(scope, dst_unwrap_symbol(argv[0]), localslot); 
        ret = localslot;
    }
    return ret;
}

/* Varset */
DstSlot dst_compile_varset(DstFormOptions opts, int32_t argn, const DstValue *argv) {
    DstFormOptions subopts;
    DstSlot ret, dest;
    if (argn != 2)
        dst_compile_cerror(opts.compiler, opts.sourcemap, "expected 2 arguments");
    if (!dst_checktype(argv[0], DST_SYMBOL))
        dst_compile_cerror(opts.compiler, opts.sourcemap, "expected symbol");
    subopts = dst_compile_getopts_index(opts, 2);
    subopts.flags &= ~DST_FOPTS_TAIL;
    dest = dst_compile_resolve(opts.compiler, opts.sourcemap, dst_unwrap_symbol(argv[0]));
    if (!(dest.flags & DST_SLOT_MUTABLE)) {
        dst_compile_cerror(opts.compiler, opts.sourcemap, "cannot set constant");
    }
    subopts.flags |= DST_FOPTS_HINT;
    subopts.hint = dest;
    ret = dst_compile_value(subopts);
    return ret;
}

/* Def */
DstSlot dst_compile_def(DstFormOptions opts, int32_t argn, const DstValue *argv) {
    DstScope *scope = dst_compile_topscope(opts.compiler);
    DstFormOptions subopts;
    DstSlot ret;
    if (argn != 2)
        dst_compile_cerror(opts.compiler, opts.sourcemap, "expected 2 arguments");
    if (!dst_checktype(argv[0], DST_SYMBOL))
        dst_compile_cerror(opts.compiler, opts.sourcemap, "expected symbol");
    subopts = dst_compile_getopts_index(opts, 2);
    subopts.flags &= ~DST_FOPTS_TAIL;
    ret = dst_compile_value(subopts);
    ret.flags |= DST_SLOT_NAMED;
    if (scope->flags & DST_SCOPE_TOP) {
        /* Global def, generate code to store in env when executed */
        DstCompiler *c = opts.compiler;
        const DstValue *sm = opts.sourcemap;
        /* Root scope, add to def table */
        DstSlot envslot = dst_compile_constantslot(c->env);
        DstSlot nameslot = dst_compile_constantslot(argv[0]);
        DstSlot valsymslot = dst_compile_constantslot(dst_csymbolv("value"));
        DstSlot tableslot = dst_compile_constantslot(dst_wrap_cfunction(dst_stl_table));
        /* Create env entry */
        int32_t valsymindex = dst_compile_preread(c, sm, 0xFF, 1, valsymslot);
        int32_t retindex = dst_compile_preread(c, sm, 0xFFFF, 2, ret);
        dst_compile_emit(c, sm,
                (retindex << 16) |
                (valsymindex << 8) |
                DOP_PUSH_2);
        dst_compile_postread(c, ret, retindex);
        dst_compile_postread(c, valsymslot, valsymindex);
        dst_compile_freeslot(c, valsymslot);
        int32_t tableindex = dst_compile_preread(opts.compiler, opts.sourcemap, 0xFF, 1, tableslot);
        dst_compile_emit(c, sm,
                (tableindex << 16) |
                (tableindex << 8) |
                DOP_CALL);
        /* Add env entry to env */
        int32_t nameindex = dst_compile_preread(opts.compiler, opts.sourcemap, 0xFF, 2, nameslot);
        int32_t envindex = dst_compile_preread(opts.compiler, opts.sourcemap, 0xFF, 3, envslot);
        dst_compile_emit(opts.compiler, opts.sourcemap, 
                (tableindex << 24) |
                (nameindex << 16) |
                (envindex << 8) |
                DOP_PUT);
        dst_compile_postread(opts.compiler, envslot, envindex);
        dst_compile_postread(opts.compiler, nameslot, nameindex);
        dst_compile_postread(c, tableslot, tableindex);
        dst_compile_freeslot(c, tableslot);
        dst_compile_freeslot(c, envslot);
        dst_compile_freeslot(c, tableslot);
    } else {
        /* Non root scope, simple slot alias */
        slotsym(scope, dst_unwrap_symbol(argv[0]), ret); 
    }
    return ret;
}

/* Do */
DstSlot dst_compile_do(DstFormOptions opts, int32_t argn, const DstValue *argv) {
    int32_t i;
    DstSlot ret;
    dst_compile_scope(opts.compiler, 0);
    for (i = 0; i < argn; i++) {
        DstFormOptions subopts = dst_compile_getopts_index(opts, i + 1);
        subopts.x = argv[i];
        if (i == argn - 1) {
            subopts.flags |= DST_FOPTS_TAIL;
        } else {
            subopts.flags &= ~DST_FOPTS_TAIL;
        }
        ret = dst_compile_value(subopts);
        if (i != argn - 1) {
            dst_compile_freeslot(opts.compiler, ret);
        }
    }
    dst_compile_popscope(opts.compiler);
    return ret;
}

/* Keep in lexographic order */
static const DstSpecial dst_compiler_specials[] = {
    {"def", dst_compile_def},
    {"do", dst_compile_do},
    {"quote", dst_compile_quote},
    {"var", dst_compile_var},
    {"varset", dst_compile_varset}
};

static int dst_strcompare(const uint8_t *str, const char *other) {
    int32_t len = dst_string_length(str);
    int32_t index;
    for (index = 0; index < len; index++) {
        uint8_t c = str[index];
        uint8_t k = ((const uint8_t *)other)[index];
        if (c < k) return -1;
        if (c > k) return 1;
        if (k == '\0') break;
    }
    return (other[index] == '\0') ? 0 : -1;
}

/* Find an instruction definition given its name */
static const DstSpecial *dst_finds(const uint8_t *key) {
    const DstSpecial *low = dst_compiler_specials;
    const DstSpecial *hi = dst_compiler_specials +
        (sizeof(dst_compiler_specials) / sizeof(DstSpecial));
    while (low < hi) {
        const DstSpecial *mid = low + ((hi - low) / 2);
        int comp = dst_strcompare(key, mid->name);
        if (comp < 0) {
            hi = mid;
        } else if (comp > 0) {
            low = mid + 1;
        } else {
            return mid;
        }
    }
    return NULL;
}

/* Compile a tuplle */
DstSlot dst_compile_tuple(DstFormOptions opts) {
    DstSlot head;
    DstFormOptions subopts;
    DstCompiler *c = opts.compiler;
    const DstValue *tup = dst_unwrap_tuple(opts.x);
    int headcompiled = 0;
    subopts = dst_compile_getopts_index(opts, 0);
    subopts.flags &= DST_FUNCTION | DST_CFUNCTION;
    if (dst_tuple_length(tup) == 0) {
        return dst_compile_constantslot(opts.x);
    }
    if (dst_checktype(tup[0], DST_SYMBOL)) {
        const DstSpecial *s = dst_finds(dst_unwrap_symbol(tup[0]));
        if (NULL != s) {
            return s->compile(opts, dst_tuple_length(tup) - 1, tup + 1);
        }
    }
    if (!headcompiled) {
        head = dst_compile_value(subopts);
        headcompiled = 1;
        /*
         if ((head.flags & DST_SLOT_CONSTANT)) {
            if (dst_checktype(head.constant, DST_CFUNCTION)) {
                printf("add cfunction optimization here...\n");
            }
        }
        */
    }
    /* Compile a normal function call */
    {
        int32_t headindex;
        DstSlot retslot;
        if (!headcompiled) {
            head = dst_compile_value(subopts);
            headcompiled = 1;
        }
        headindex = dst_compile_preread(c, subopts.sourcemap, 0xFFFF, 1, head);
        dst_compile_pushtuple(opts.compiler, opts.sourcemap, opts.x);
        if (opts.flags & DST_FOPTS_TAIL) {
            dst_compile_emit(c, subopts.sourcemap, (headindex << 8) | DOP_TAILCALL);
            retslot = dst_compile_constantslot(dst_wrap_nil());
        } else {
            int32_t retindex;
            retslot = dst_compile_gettarget(opts);
            retindex = dst_compile_preread(c, subopts.sourcemap, 0xFF, 2, retslot);
            dst_compile_emit(c, subopts.sourcemap, (headindex << 16) | (retindex << 8) | DOP_CALL);
            dst_compile_postread(c, retslot, retindex);
        }
        dst_compile_postread(c, head, headindex);
        return retslot;
    }
}

/* Compile a single value */
DstSlot dst_compile_value(DstFormOptions opts) {
    DstSlot ret;
    if (opts.compiler->recursion_guard <= 0) {
        dst_compile_cerror(opts.compiler, opts.sourcemap, "recursed too deeply");
    }
    opts.compiler->recursion_guard--;
    switch (dst_type(opts.x)) {
        default:
            ret = dst_compile_constantslot(opts.x);
            break;
        case DST_SYMBOL:
            {
                const uint8_t *sym = dst_unwrap_symbol(opts.x);
                ret = dst_compile_resolve(opts.compiler, opts.sourcemap, sym);
                break;
            }
        case DST_TUPLE:
            ret = dst_compile_tuple(opts);
            break;
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
    if ((opts.flags & DST_FOPTS_TAIL) && !dst_compile_did_return(opts.compiler)) {
        dst_compile_return(opts.compiler, opts.sourcemap, ret);
    } else if (opts.flags & DST_FOPTS_HINT) {
        dst_compile_copy(opts.compiler, opts.sourcemap, opts.hint, ret);
        ret = opts.hint;
    }
    opts.compiler->recursion_guard++;
    return ret;
}

/* Compile a funcdef */
static DstFuncDef *dst_compile_pop_funcdef(DstCompiler *c) {
    DstScope *scope = dst_compile_topscope(c);
    DstFuncDef *def;

    /* Initialize funcdef */
    def = dst_gcalloc(DST_MEMORY_FUNCDEF, sizeof(DstFuncDef));
    def->environments = NULL;
    def->constants = NULL;
    def->source = NULL;
    def->sourcepath = NULL;
    def->slotcount = scope->smax + 1;

    /* Copy envs */
    def->environments_length = scope->envcount;
    if (def->environments_length) {
        def->environments = malloc(sizeof(int32_t) * def->environments_length);
        if (def->environments == NULL) {
            DST_OUT_OF_MEMORY;
        }
        memcpy(def->environments, scope->envs, def->environments_length * sizeof(int32_t));
    }

    /* Copy constants */
    def->constants_length = scope->ccount;
    if (def->constants_length) {
        def->constants = malloc(sizeof(DstValue) * scope->ccount);
        if (NULL == def->constants) {
            DST_OUT_OF_MEMORY;
        }
        memcpy(def->constants,
                scope->consts,
                def->constants_length * sizeof(DstValue));
    }

    /* Copy bytecode */
    def->bytecode_length = c->buffercount - scope->bytecode_start;
    if (def->bytecode_length) {
        def->bytecode = malloc(sizeof(uint32_t) * def->bytecode_length);
        if (NULL == def->bytecode) {
            DST_OUT_OF_MEMORY;
        }
        memcpy(def->bytecode,
                c->buffer + scope->bytecode_start,
                def->bytecode_length * sizeof(uint32_t));
    }

    /* Copy source map over */
    if (c->mapbuffer) {
        def->sourcemap = malloc(sizeof(int32_t) * 2 * def->bytecode_length);
        if (NULL == def->sourcemap) {
            DST_OUT_OF_MEMORY;
        }
        memcpy(def->sourcemap, 
                c->mapbuffer + 2 * scope->bytecode_start, 
                def->bytecode_length * 2 * sizeof(int32_t));
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

/* Initialize a compiler */
static void dst_compile_init(DstCompiler *c, DstValue env) {
    c->scopecount = 0;
    c->scopecap = 0;
    c->scopes = NULL;
    c->buffercap = 0;
    c->buffercount = 0;
    c->buffer = NULL;
    c->mapbuffer = NULL;
    c->recursion_guard = DST_RECURSION_GUARD;
    c->env = env;

    /* Push an empty scope. This will be the global scope. */
    dst_compile_scope(c, 0);
    
    dst_compile_topscope(c)->flags |= DST_SCOPE_TOP;
}

/* Deinitialize a compiler struct */
static void dst_compile_deinit(DstCompiler *c) {
    while (c->scopecount)
        dst_compile_popscope(c);
    free(c->scopes);
    free(c->buffer);
    free(c->mapbuffer);
    c->buffer = NULL;
    c->mapbuffer = NULL;
    c->scopes = NULL;
    c->env = dst_wrap_nil();
}

/* Compile a single form */
DstCompileResult dst_compile_one(DstCompiler *c, DstCompileOptions opts) {
    DstFormOptions fopts;
    DstSlot s;

    /* Ensure only one scope */
    while (c->scopecount > 1)
        dst_compile_popscope(c);

    if (setjmp(c->on_error)) {
        c->result.status = DST_COMPILE_ERROR;
        c->result.funcdef = NULL;
        return c->result;
    }

    /* Push a function scope */
    dst_compile_scope(c, DST_SCOPE_FUNCTION | DST_SCOPE_TOP);

    /* Set the global environment */
    c->env = opts.env;

    fopts.compiler = c;
    fopts.sourcemap = opts.sourcemap;
    fopts.flags = DST_FOPTS_TAIL | DST_SLOTTYPE_ANY;
    fopts.hint = dst_compile_constantslot(dst_wrap_nil());
    fopts.x = opts.source;

    /* Compile the value */
    s = dst_compile_value(fopts);

    c->result.funcdef = dst_compile_pop_funcdef(c);
    c->result.status = DST_COMPILE_OK;

    return c->result;
}

/* Compile a form. */
DstCompileResult dst_compile(DstCompileOptions opts) {
    DstCompiler c;
    DstCompileResult res;

    dst_compile_init(&c, opts.env);

    res = dst_compile_one(&c, opts);

    dst_compile_deinit(&c);

    return res;
}

DstFunction *dst_compile_func(DstCompileResult res) {
    if (res.status != DST_COMPILE_OK) {
        return NULL;
    }
    DstFunction *func = dst_gcalloc(DST_MEMORY_FUNCTION, sizeof(DstFunction));
    func->def = res.funcdef;
    func->envs = NULL;
    return func;
}
