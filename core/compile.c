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
    opts.flags = 0;
    opts.sourcemap = sourcemap;
    return opts;
}

/* Index into the key of a table or struct */
DstFormOptions dst_compile_getopts_key(DstFormOptions opts, DstValue key) {
    const DstValue *sourcemap = dst_sourcemap_key(opts.sourcemap, key);
    opts.x = key;
    opts.sourcemap = sourcemap;
    opts.flags = 0;
    return opts;
}

/* Index into the value of a table or struct */
DstFormOptions dst_compile_getopts_value(DstFormOptions opts, DstValue key) {
    const DstValue *sourcemap = dst_sourcemap_value(opts.sourcemap, key);
    DstValue nextval = dst_get(opts.x, key);
    opts.x = nextval;
    opts.sourcemap = sourcemap;
    opts.flags = 0;
    return opts;
}

/* Allocate a slot index */
static int32_t slotalloc_index(DstCompiler *c) {
    DstScope *scope = dst_compile_topscope(c);
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

/* Free a slot index */
static void slotfree_index(DstCompiler *c, int32_t index) {
    DstScope *scope = dst_compile_topscope(c);
    /* Don't free the pre allocated slots */
    if (index >= 0 && (index < 0xF0 || index > 0xFF) && index < (scope->scap << 5))
        scope->slots[index >> 5] &= ~(1 << (index & 0x1F));
}

/* Helper */
static int32_t slotalloc_temp(DstCompiler *c, int32_t max, int32_t nth) {
    int32_t ret = slotalloc_index(c);
    if (ret > max) {
        slotfree_index(c, ret);
        ret = 0xF0 + nth;
    }
    return ret;
}

/* Free a slot */
void dst_compile_freeslot(DstCompiler *c, DstSlot s) {
    if (s.flags & (DST_SLOT_CONSTANT | DST_SLOT_NAMED)) return;
    if (s.envindex > 0) return;
    slotfree_index(c, s.index);
}

/* Find a slot given a symbol. Return 1 if found, otherwise 0. */
static int slotsymfind(DstScope *scope, const uint8_t *sym, DstSlot *out) {
    int32_t i;
    for (i = 0; i < scope->symcount; i++) {
        if (scope->syms[i].sym == sym) {
            *out = scope->syms[i].slot;
            out->flags |= DST_SLOT_NAMED;
            return 1;
        }
    }
    return 0;
}

/* Add a slot to a scope with a symbol associated with it (def or var). */
static void slotsym(DstCompiler *c, const uint8_t *sym, DstSlot s) {
    DstScope *scope = dst_compile_topscope(c);
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

    scope->defs = NULL;
    scope->dcount = 0;
    scope->dcap = 0;
    
    scope->bytecode_start = c->buffercount;

    /* Initialize slots */
    scope->slots = NULL;
    scope->scap = 0;
    scope->smax = -1;

    /* Inherit slots */
    if ((!(flags & DST_SCOPE_FUNCTION)) && oldcount) {
        DstScope *oldscope = c->scopes + oldcount - 1;
        size_t size = sizeof(int32_t) * oldscope->scap;
        scope->smax = oldscope->smax;
        scope->scap = oldscope->scap;
        if (size) {
            scope->slots = malloc(size);
            if (NULL == scope->slots) {
                DST_OUT_OF_MEMORY;
            }
        }
    }

    scope->flags = flags;
}

/* Leave a scope. */
void dst_compile_popscope(DstCompiler *c) {
    DstScope *scope;
    dst_assert(c->scopecount, "could not pop scope");
    scope = c->scopes + --c->scopecount;
    /* Move free slots to parent scope if not a new function.
     * We need to know the total number of slots used when compiling the function. */
    if (!(scope->flags & (DST_SCOPE_FUNCTION | DST_SCOPE_UNUSED)) && c->scopecount) {
        DstScope *newscope = dst_compile_topscope(c);
        if (newscope->smax < scope->smax) 
            newscope->smax = scope->smax;
    }
    free(scope->consts);
    free(scope->slots);
    free(scope->syms);
    free(scope->envs);
    free(scope->defs);
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
static DstSlot checkglobal(DstCompiler *c, const DstValue *sourcemap, const uint8_t *sym) {
    DstValue check = dst_get(c->env, dst_wrap_symbol(sym));
    DstValue ref;
    if (!(dst_checktype(check, DST_STRUCT) || dst_checktype(check, DST_TABLE))) {
        dst_compile_error(c, sourcemap, dst_formatc("unknown symbol %q", sym));
    }
    ref = dst_get(check, dst_csymbolv("ref"));
    if (dst_checktype(ref, DST_ARRAY)) {
        DstSlot ret = dst_compile_constantslot(ref);
        /* TODO save type info */
        ret.flags |= DST_SLOT_REF | DST_SLOT_NAMED | DST_SLOT_MUTABLE | DST_SLOTTYPE_ANY;
        ret.flags &= ~DST_SLOT_CONSTANT;
        return ret;
    } else {
        DstValue value = dst_get(check, dst_csymbolv("value"));
        return dst_compile_constantslot(value);
    }
}

static void envinitscope(DstScope *scope) {
    if (scope->envcount < 1) {
        scope->envcount = 1;
        scope->envs = malloc(sizeof(int32_t) * 10);
        if (NULL == scope->envs) {
            DST_OUT_OF_MEMORY;
        }
        scope->envcap = 10;
        scope->envs[0] = 0;
    }
}

/* Add an env index to a scope */
static int32_t addenvindex(DstScope *scope, int32_t env) {
    int32_t newcount, index;
    envinitscope(scope);
    index = scope->envcount;
    newcount = index + 1;
    /* Ensure capacity for adding scope */
    if (newcount > scope->envcap) {
        int32_t newcap = 2 * newcount;
        scope->envs = realloc(scope->envs, sizeof(int32_t) * newcap);
        if (NULL == scope->envs) {
            DST_OUT_OF_MEMORY;
        }
        scope->envcap = newcap;
    }
    scope->envs[index] = env;
    scope->envcount = newcount;
    return index;
}

/* Allow searching for symbols. Return information about the symbol */
DstSlot dst_compile_resolve(
        DstCompiler *c,
        const DstValue *sourcemap,
        const uint8_t *sym) {

    DstSlot ret = dst_compile_constantslot(dst_wrap_nil());
    DstScope *scope = dst_compile_topscope(c);
    int foundlocal = 1;
    int unused = 0;

    /* Search scopes for symbol, starting from top */
    while (scope >= c->scopes) {
        if (scope->flags & DST_SCOPE_UNUSED)
            unused = 1;
        if (slotsymfind(scope, sym, &ret))
            goto found;
        if (scope->flags & DST_SCOPE_FUNCTION)
            foundlocal = 0;
        scope--;
    }

    /* Symbol not found - check for global */
    return checkglobal(c, sourcemap, sym);

    /* Symbol was found */
    found:

    /* Constants can be returned immediately (they are stateless) */
    if (ret.flags & (DST_SLOT_CONSTANT | DST_SLOT_REF))
        return ret;

    /* Unused references and locals shouldn't add captured envs. */
    if (unused || foundlocal) {
        ret.envindex = 0;
        return ret;
    }

    /* non-local scope needs to expose its environment */
    if (!foundlocal) {
        /* Find function scope */
        while (scope >= c->scopes && !(scope->flags & DST_SCOPE_FUNCTION)) scope--;
        dst_assert(scope >= c->scopes, "invalid scopes");
        scope->flags |= DST_SCOPE_ENV;
        envinitscope(scope);
        scope++;
    }

    /* Propogate env up to current scope */
    int32_t envindex = 0;
    while (scope <= dst_compile_topscope(c)) {
        if (scope->flags & DST_SCOPE_FUNCTION) {
            int32_t j;
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
            if (!scopefound) envindex = addenvindex(scope, envindex);
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

/* Realize any slot to a local slot. Call this to get a slot index
 * that can be used in an instruction. */
static int32_t dst_compile_preread(
        DstCompiler *c,
        const DstValue *sourcemap,
        int32_t max,
        int nth,
        DstSlot s) {

    int32_t ret;

    if (s.flags & DST_SLOT_REF)
        max = 0xFF;

    if (s.flags & (DST_SLOT_CONSTANT | DST_SLOT_REF)) {
        int32_t cindex;
        ret = slotalloc_temp(c, 0xFF, nth);
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
        ret = slotalloc_temp(c, max, nth);
        dst_compile_emit(c, sourcemap, 
                ((uint32_t)(s.index) << 24) |
                ((uint32_t)(s.envindex) << 16) |
                ((uint32_t)(ret) << 8) |
                DOP_LOAD_UPVALUE);
    } else if (s.index > max) {
        ret = slotalloc_temp(c, max, nth);
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
        slotfree_index(c, index);
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

    /* Can't write to constants */
    if (dest.flags & DST_SLOT_CONSTANT) {
        dst_compile_cerror(c, sourcemap, "cannot write to constant");
    }

    /* Short circuit if dest and source are equal */
    if (dest.flags == src.flags &&
        dest.index == src.index &&
        dest.envindex == src.envindex) {
        if (dest.flags & (DST_SLOT_REF)) {
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
        reflocal = slotalloc_temp(c, 0xFF, 2);
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
        slotfree_index(c, reflocal);
    }
    dst_compile_postread(c, src, srclocal);
}

/* Generate the return instruction for a slot. */
static DstSlot dst_compile_return(DstCompiler *c, const DstValue *sourcemap, DstSlot s) {
    if (!(s.flags & DST_SLOT_RETURNED)) {
        if (s.flags & DST_SLOT_CONSTANT && dst_checktype(s.constant, DST_NIL)) {
            dst_compile_emit(c, sourcemap, DOP_RETURN_NIL);
        } else {
            int32_t ls = dst_compile_preread(c, sourcemap, 0xFFFF, 1, s);
            dst_compile_emit(c, sourcemap, DOP_RETURN | (ls << 8));
            dst_compile_postread(c, s, ls);
        }
        s.flags |= DST_SLOT_RETURNED;
    }
    return s;
}

/* Get a target slot for emitting an instruction. Will always return
 * a local slot. */
static DstSlot dst_compile_gettarget(DstFormOptions opts) {
    DstSlot slot;
    if ((opts.flags & DST_FOPTS_HINT) &&
        (opts.hint.envindex == 0) &&
        (opts.hint.index >= 0 && opts.hint.index <= 0xFF)) {
        slot = opts.hint;
    } else {
        slot.envindex = 0;
        slot.constant = dst_wrap_nil();
        slot.flags = 0;
        slot.index = slotalloc_temp(opts.compiler, 0xFF, 4);
    }
    return slot;
}

/* Push a series of values */
static void dst_compile_pushtuple(
        DstCompiler *c,
        const DstValue *sourcemap,
        DstValue x, 
        int32_t start) {
    DstFormOptions opts;
    int32_t i, len;

    /* Set basic opts */
    opts.compiler = c;
    opts.hint = dst_compile_constantslot(dst_wrap_nil());
    opts.flags = 0;
    opts.x = x;
    opts.sourcemap = sourcemap;

    len = dst_length(x);
    for (i = start; i < len - 2; i += 3) {
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

DstSlot dst_compile_quote(DstFormOptions opts, int32_t argn, const DstValue *argv) {
    if (argn != 1)
        dst_compile_cerror(opts.compiler, opts.sourcemap, "expected 1 argument");
    return dst_compile_constantslot(argv[0]);
}

DstSlot dst_compile_var(DstFormOptions opts, int32_t argn, const DstValue *argv) {
    DstCompiler *c = opts.compiler;
    DstFormOptions subopts;
    DstSlot ret;
    if (argn != 2)
        dst_compile_cerror(opts.compiler, opts.sourcemap, "expected 2 arguments");
    if (!dst_checktype(argv[0], DST_SYMBOL))
        dst_compile_cerror(opts.compiler, opts.sourcemap, "expected symbol");
    subopts = dst_compile_getopts_index(opts, 2);
    subopts.flags = opts.flags & ~DST_FOPTS_TAIL;
    ret = dst_compile_value(subopts);
    if (dst_compile_topscope(c)->flags & DST_SCOPE_TOP) {
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
        /*dst_compile_freeslot(c, refarrayslot);*/
        ret = refslot;
    } else {
        /* Non root scope, bring to local slot */
        DstSlot localslot;
        localslot.index = slotalloc_index(c);
        /* infer type? */
        localslot.flags = DST_SLOT_NAMED | DST_SLOT_MUTABLE;
        localslot.envindex = 0;
        localslot.constant = dst_wrap_nil();
        dst_compile_copy(opts.compiler, opts.sourcemap, localslot, ret);
        slotsym(c, dst_unwrap_symbol(argv[0]), localslot); 
        ret = localslot;
    }
    return ret;
}

DstSlot dst_compile_varset(DstFormOptions opts, int32_t argn, const DstValue *argv) {
    DstFormOptions subopts;
    DstSlot ret, dest;
    if (argn != 2)
        dst_compile_cerror(opts.compiler, opts.sourcemap, "expected 2 arguments");
    if (!dst_checktype(argv[0], DST_SYMBOL))
        dst_compile_cerror(opts.compiler, opts.sourcemap, "expected symbol");
    dest = dst_compile_resolve(opts.compiler, opts.sourcemap, dst_unwrap_symbol(argv[0]));
    if (!(dest.flags & DST_SLOT_MUTABLE)) {
        dst_compile_cerror(opts.compiler, opts.sourcemap, "cannot set constant");
    }
    subopts = dst_compile_getopts_index(opts, 2);
    subopts.flags = DST_FOPTS_HINT;
    subopts.hint = dest;
    ret = dst_compile_value(subopts);
    dst_compile_copy(opts.compiler, subopts.sourcemap, dest, ret);
    return ret;
}

DstSlot dst_compile_def(DstFormOptions opts, int32_t argn, const DstValue *argv) {
    DstCompiler *c = opts.compiler;
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
    if (dst_compile_topscope(c)->flags & DST_SCOPE_TOP) {
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
        slotsym(c, dst_unwrap_symbol(argv[0]), ret); 
    }
    return ret;
}

/* Compile some code that will be thrown away. Used to ensure
 * that dead code is well formed without including it in the final
 * bytecode. */
static void dst_compile_throwaway(DstFormOptions opts) {
    DstCompiler *c = opts.compiler;
    int32_t bufstart = c->buffercount;
    dst_compile_scope(c, DST_SCOPE_UNUSED);
    dst_compile_value(opts);
    dst_compile_popscope(c);
    c->buffercount = bufstart;
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
DstSlot dst_compile_if(DstFormOptions opts, int32_t argn, const DstValue *argv) {
    DstCompiler *c = opts.compiler;
    const DstValue *sm = opts.sourcemap;
    int32_t labelr, labeljr, labeld, labeljd, condlocal;
    DstFormOptions leftopts, rightopts, condopts;
    DstSlot cond, left, right, target;
    const int tail = opts.flags & DST_FOPTS_TAIL;
    const int drop = opts.flags & DST_FOPTS_DROP;
    (void) argv;

    if (argn < 2 || argn > 3)
        dst_compile_cerror(c, sm, "expected 2 or 3 arguments to if");

    /* Get options */
    condopts = dst_compile_getopts_index(opts, 1);
    leftopts = dst_compile_getopts_index(opts, 2);
    rightopts = dst_compile_getopts_index(opts, 3);
    if (argn == 2) rightopts.sourcemap = opts.sourcemap;
    if (opts.flags & DST_FOPTS_HINT) {
        leftopts.flags |= DST_FOPTS_HINT;
        rightopts.flags |= DST_FOPTS_HINT;
    }
    if (tail) {
        leftopts.flags |= DST_FOPTS_TAIL;
        rightopts.flags |= DST_FOPTS_TAIL;
    }
    if (drop) {
        leftopts.flags |= DST_FOPTS_DROP;
        rightopts.flags |= DST_FOPTS_DROP;
    }

    /* Compile condition */
    cond = dst_compile_value(condopts);

    /* Check constant condition. */
    /* TODO: Use type info for more short circuits */
    if ((cond.flags & DST_SLOT_CONSTANT) && !(cond.flags & DST_SLOT_REF)) {
        DstFormOptions goodopts, badopts;
        if (dst_truthy(cond.constant)) {
            goodopts = leftopts;
            badopts = rightopts;
        } else {
            goodopts = rightopts;
            badopts = leftopts;
        }
        dst_compile_scope(c, 0);
        target = dst_compile_value(goodopts);
        dst_compile_popscope(c);
        dst_compile_throwaway(badopts);
        return target;
    }

    /* Set target for compilation */
    target = (!drop && !tail) 
        ? dst_compile_gettarget(opts)
        : dst_compile_constantslot(dst_wrap_nil());

    /* Compile jump to right */
    condlocal = dst_compile_preread(c, sm, 0xFF, 1, cond);
    labeljr = c->buffercount;
    dst_compile_emit(c, sm, DOP_JUMP_IF_NOT | (condlocal << 8));
    dst_compile_postread(c, cond, condlocal);
    dst_compile_freeslot(c, cond);

    /* Condition left body */
    dst_compile_scope(c, 0);
    left = dst_compile_value(leftopts);
    if (!drop && !tail) dst_compile_copy(c, sm, target, left); 
    dst_compile_popscope(c);

    /* Compile jump to done */
    labeljd = c->buffercount;
    if (!tail) dst_compile_emit(c, sm, DOP_JUMP);

    /* Compile right body */
    labelr = c->buffercount;
    dst_compile_scope(c, 0);
    right = dst_compile_value(rightopts);
    if (!drop && !tail) dst_compile_copy(c, sm, target, right); 
    dst_compile_popscope(c);

    /* Write jumps - only add jump lengths if jump actually emitted */
    labeld = c->buffercount;
    c->buffer[labeljr] |= (labelr - labeljr) << 16;
    if (!tail) c->buffer[labeljd] |= (labeld - labeljd) << 8;

    if (tail) target.flags |= DST_SLOT_RETURNED;
    return target;
}

DstSlot dst_compile_do(DstFormOptions opts, int32_t argn, const DstValue *argv) {
    int32_t i;
    DstSlot ret;
    dst_compile_scope(opts.compiler, 0);
    (void) argv;
    for (i = 0; i < argn; i++) {
        DstFormOptions subopts = dst_compile_getopts_index(opts, i + 1);
        if (i != argn - 1) {
            subopts.flags = DST_FOPTS_DROP;
        } else if (opts.flags & DST_FOPTS_TAIL) {
            subopts.flags = DST_FOPTS_TAIL;
        }
        ret = dst_compile_value(subopts);
        if (i != argn - 1) {
            dst_compile_freeslot(opts.compiler, ret);
        }
    }
    dst_compile_popscope(opts.compiler);
    return ret;
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
DstSlot dst_compile_while(DstFormOptions opts, int32_t argn, const DstValue *argv) {
    DstCompiler *c = opts.compiler;
    const DstValue *sm = opts.sourcemap;
    DstSlot cond;
    int32_t condlocal, labelwt, labeld, labeljt, labelc, i;
    int infinite = 0;
    (void) argv;

    if (argn < 2) dst_compile_cerror(c, sm, "expected at least 2 arguments");
    dst_compile_scope(opts.compiler, 0);
    labelwt = c->buffercount;

    /* Compile condition */
    cond = dst_compile_value(dst_compile_getopts_index(opts, 1));

    /* Check for constant condition */
    if (cond.flags & DST_SLOT_CONSTANT) {
        /* Loop never executes */
        if (!dst_truthy(cond.constant)) {
            dst_compile_popscope(c);
            return dst_compile_constantslot(dst_wrap_nil());
        }
        /* Infinite loop */
        infinite = 1;
    }

    if (!infinite) {
        condlocal = dst_compile_preread(c, sm, 0xFF, 1, cond);
        labelc = c->buffercount;
        dst_compile_emit(c, sm, DOP_JUMP_IF_NOT | (condlocal << 8));
        dst_compile_postread(c, cond, condlocal);
    }

    /* Compile body */
    for (i = 1; i < argn; i++) {
        DstFormOptions subopts = dst_compile_getopts_index(opts, i + 1);
        subopts.flags = DST_FOPTS_DROP;
        dst_compile_freeslot(c, dst_compile_value(subopts));
    }

    /* Compile jump to whiletop */
    labeljt = c->buffercount;
    dst_compile_emit(c, sm, DOP_JUMP);

    /* Calculate jumps */
    labeld = c->buffercount;
    if (!infinite) c->buffer[labelc] |= (labeld - labelc) << 16;
    c->buffer[labeljt] |= (labelwt - labeljt) << 8;

    dst_compile_popscope(opts.compiler);
    return dst_compile_constantslot(dst_wrap_nil());
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
    def->defs = NULL;
    def->bytecode = NULL;
    def->slotcount = scope->smax + 1;

    /* Copy envs */
    def->environments_length = scope->envcount;
    if (def->environments_length > 1) {
        def->environments = malloc(sizeof(int32_t) * def->environments_length);
        if (def->environments == NULL) {
            DST_OUT_OF_MEMORY;
        }
        memcpy(def->environments, scope->envs, def->environments_length * sizeof(int32_t));
    }

    /* Copy constants */
    def->constants_length = scope->ccount;
    if (def->constants_length) {
        def->constants = malloc(sizeof(DstValue) * def->constants_length);
        if (NULL == def->constants) {
            DST_OUT_OF_MEMORY;
        }
        memcpy(def->constants,
                scope->consts,
                def->constants_length * sizeof(DstValue));
    }

    /* Copy funcdefs */
    def->defs_length = scope->dcount;
    if (def->defs_length) {
        def->defs = malloc(sizeof(DstFuncDef *) * def->defs_length);
        if (NULL == def->defs) {
            DST_OUT_OF_MEMORY;
        }
        memcpy(def->defs,
                scope->defs,
                def->defs_length * sizeof(DstFuncDef *));
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
    def->arity = 0;

    /* Set some flags */
    def->flags = 0;
    if (scope->flags & DST_SCOPE_ENV) {
        def->flags |= DST_FUNCDEF_FLAG_NEEDSENV;
    }

    /* Pop the scope */
    dst_compile_popscope(c);

    return def;
}

/* Add a funcdef to the top most function scope */
static int32_t dst_compile_addfuncdef(DstCompiler *c, DstFuncDef *def) {
    DstScope *scope = dst_compile_topscope(c);
    while (scope >= c->scopes) {
        if (scope->flags & DST_SCOPE_FUNCTION)
            break;
        scope--;
    }
    dst_assert(scope >= c->scopes, "could not add funcdef");
    int32_t defindex = scope->dcount;
    int32_t newcount = defindex + 1;
    if (newcount >= scope->dcap) {
        int32_t newcap = 2 * newcount;
        DstFuncDef **defs = realloc(scope->defs, sizeof(DstFuncDef **) * newcap);
        if (NULL == defs) {
            DST_OUT_OF_MEMORY;
        }
        scope->defs = defs;
        scope->dcap = newcap;
    }
    scope->dcount = newcount;
    scope->defs[defindex] = def;
    return defindex;
}

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

DstSlot dst_compile_fn(DstFormOptions opts, int32_t argn, const DstValue *argv) {
    DstCompiler *c = opts.compiler;
    const DstValue *sm = opts.sourcemap;
    DstFuncDef *def;
    DstSlot ret;
    int32_t paramcount, argi, parami, arity, localslot, defindex;
    const DstValue *params;
    const DstValue *psm;
    int varargs = 0;

    if (argn < 2) dst_compile_cerror(c, sm, "expected at least 2 arguments to function literal");

    /* Begin function */
    dst_compile_scope(c, DST_SCOPE_FUNCTION);

    /* Read function parameters */
    parami = 0;
    arity = 0;
    if (dst_checktype(argv[0], DST_SYMBOL)) parami = 1;
    if (parami >= argn) dst_compile_cerror(c, sm, "expected function parameters");
    if (dst_seq_view(argv[parami], &params, &paramcount)) {
        psm = dst_sourcemap_index(sm, parami + 1);
        int32_t i;
        for (i = 0; i < paramcount; i++) {
            const DstValue *psmi = dst_sourcemap_index(psm, i);
            if (dst_checktype(params[i], DST_SYMBOL)) {
                DstSlot slot;
                /* Check for varargs */
                if (0 == dst_strcompare(dst_unwrap_symbol(params[i]), "&")) {
                    if (i != paramcount - 2) {
                        dst_compile_cerror(c, psmi, "variable argument symbol in unexpected location");
                    }
                    varargs = 1;
                    arity--;
                    continue;
                }
                slot.flags = DST_SLOT_NAMED;
                slot.envindex = 0;
                slot.constant = dst_wrap_nil();
                slot.index = slotalloc_index(c);
                slotsym(c, dst_unwrap_symbol(params[i]), slot);
                arity++;
            } else {
                dst_compile_cerror(c, psmi, "expected symbol as function parameter");
            }
        }
    } else {
        dst_compile_cerror(c, sm, "expected function parameters");
    }

    /* Compile function body */
    for (argi = parami + 1; argi < argn; argi++) {
        DstSlot s;
        DstFormOptions subopts = dst_compile_getopts_index(opts, argi + 1);
        subopts.flags = argi == (argn - 1) ? DST_FOPTS_TAIL : DST_FOPTS_DROP;
        s = dst_compile_value(subopts);
        dst_compile_freeslot(c, s);
    }
    
    /* Build function */
    def = dst_compile_pop_funcdef(c);
    def->arity = arity;
    if (varargs) def->flags |= DST_FUNCDEF_FLAG_VARARG;
    defindex = dst_compile_addfuncdef(c, def);

    /* Instantiate closure */
    ret.flags = 0;
    ret.envindex = 0;
    ret.constant = dst_wrap_nil();
    ret.index = slotalloc_index(c);

    localslot = ret.index > 0xF0 ? 0xF1 : ret.index;
    dst_compile_emit(c, sm,
            (defindex << 16) |
            (localslot << 8) |
            DOP_CLOSURE);
    
    if (ret.index != localslot) {
        dst_compile_emit(c, sm, 
                (ret.index << 16) |
                (localslot << 8) |
                DOP_MOVE_FAR);
    }

    return ret;
}

/* Keep in lexographic order */
static const DstSpecial dst_compiler_specials[] = {
    {"def", dst_compile_def},
    {"do", dst_compile_do},
    {"fn", dst_compile_fn},
    {"if", dst_compile_if},
    {"quote", dst_compile_quote},
    {"var", dst_compile_var},
    {"varset!", dst_compile_varset},
    {"while", dst_compile_while}
};

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

/* Compile a tuple */
DstSlot dst_compile_tuple(DstFormOptions opts) {
    DstSlot head;
    DstFormOptions subopts;
    DstCompiler *c = opts.compiler;
    const DstValue *tup = dst_unwrap_tuple(opts.x);
    int headcompiled = 0;
    subopts = dst_compile_getopts_index(opts, 0);
    subopts.flags = DST_FUNCTION | DST_CFUNCTION;
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
        dst_compile_pushtuple(opts.compiler, opts.sourcemap, opts.x, 1);
        if (opts.flags & DST_FOPTS_TAIL) {
            dst_compile_emit(c, subopts.sourcemap, (headindex << 8) | DOP_TAILCALL);
            retslot = dst_compile_constantslot(dst_wrap_nil());
            retslot.flags = DST_SLOT_RETURNED;
        } else {
            retslot = dst_compile_gettarget(opts);
            dst_compile_emit(c, subopts.sourcemap, (headindex << 16) | (retslot.index << 8) | DOP_CALL);
        }
        dst_compile_postread(c, head, headindex);
        return retslot;
    }
}

static DstSlot dst_compile_array(DstFormOptions opts) {
    DstCompiler *c = opts.compiler;
    const DstValue *sm = opts.sourcemap;
    DstSlot ctor, retslot;
    int32_t localindex;
    dst_compile_pushtuple(c, sm, opts.x, 0);
    ctor = dst_compile_constantslot(dst_wrap_cfunction(dst_stl_array));
    localindex = dst_compile_preread(c, sm, 0xFF, 1, ctor);
    if (opts.flags & DST_FOPTS_TAIL) {
        dst_compile_emit(c, sm, (localindex << 8) | DOP_TAILCALL);
        retslot = dst_compile_constantslot(dst_wrap_nil());
        retslot.flags = DST_SLOT_RETURNED;
    } else {
        retslot = dst_compile_gettarget(opts);
        dst_compile_emit(c, sm, (localindex << 16) | (retslot.index << 8) | DOP_CALL);
    }
    dst_compile_postread(c, ctor, localindex);
    return retslot;
}

static DstSlot dst_compile_tablector(DstFormOptions opts, DstCFunction cfun) {
    DstCompiler *c = opts.compiler;
    const DstValue *sm = opts.sourcemap;
    const DstValue *hmap;
    DstSlot ctor, retslot;
    int32_t localindex, i, count, cap;
    dst_assert(dst_hashtable_view(opts.x, &hmap, &count, &cap), "expected table or struct");
    for (i = 0; i < cap; i += 2) {
        if (!dst_checktype(hmap[i], DST_NIL)) {
            DstFormOptions o1 = dst_compile_getopts_key(opts, hmap[i]);
            DstFormOptions o2 = dst_compile_getopts_value(opts, hmap[i]);
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
        }
    }
    ctor = dst_compile_constantslot(dst_wrap_cfunction(cfun));
    localindex = dst_compile_preread(c, sm, 0xFF, 1, ctor);
    if (opts.flags & DST_FOPTS_TAIL) {
        dst_compile_emit(c, sm, (localindex << 8) | DOP_TAILCALL);
        retslot = dst_compile_constantslot(dst_wrap_nil());
        retslot.flags = DST_SLOT_RETURNED;
    } else {
        retslot = dst_compile_gettarget(opts);
        dst_compile_emit(c, sm, (localindex << 16) | (retslot.index << 8) | DOP_CALL);
    }
    dst_compile_postread(c, ctor, localindex);
    return retslot;
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
        case DST_ARRAY:
            ret = dst_compile_array(opts); 
            break;
        case DST_STRUCT:
            ret = dst_compile_tablector(opts, dst_stl_struct); 
            break;
        case DST_TABLE:
            ret = dst_compile_tablector(opts, dst_stl_table);
            break;
    }
    if (opts.flags & DST_FOPTS_TAIL) {
        ret = dst_compile_return(opts.compiler, opts.sourcemap, ret);
    }
    opts.compiler->recursion_guard++;
    return ret;
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
    while (c->scopecount) dst_compile_popscope(c);

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
