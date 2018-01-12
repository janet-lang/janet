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
#include "util.h"

/* Throw an error with a dst string. */
void dstc_error(DstCompiler *c, const Dst *sourcemap, const uint8_t *m) {
    /* Don't override first error */
    if (c->result.status == DST_COMPILE_ERROR) {
        return;
    }
    if (NULL != sourcemap) {
        c->result.error_start = dst_unwrap_integer(sourcemap[0]);
        c->result.error_end = dst_unwrap_integer(sourcemap[1]);
    } else {
        c->result.error_start = -1;
        c->result.error_end = -1;
    }
    c->result.status = DST_COMPILE_ERROR;
    c->result.error = m;
}

/* Throw an error with a message in a cstring */
void dstc_cerror(DstCompiler *c, const Dst *sourcemap, const char *m) {
    dstc_error(c, sourcemap, dst_cstring(m));
}

/* Use these to get sub options. They will traverse the source map so
 * compiler errors make sense. Then modify the returned options. */
DstFopts dstc_getindex(DstFopts opts, int32_t index) {
    const Dst *sourcemap = dst_sourcemap_index(opts.sourcemap, index);
    Dst nextval = dst_getindex(opts.x, index);
    opts.x = nextval;
    opts.flags = 0;
    opts.sourcemap = sourcemap;
    return opts;
}

/* Index into the key of a table or struct */
DstFopts dstc_getkey(DstFopts opts, Dst key) {
    const Dst *sourcemap = dst_sourcemap_key(opts.sourcemap, key);
    opts.x = key;
    opts.sourcemap = sourcemap;
    opts.flags = 0;
    return opts;
}

/* Index into the value of a table or struct */
DstFopts dstc_getvalue(DstFopts opts, Dst key) {
    const Dst *sourcemap = dst_sourcemap_value(opts.sourcemap, key);
    Dst nextval = dst_get(opts.x, key);
    opts.x = nextval;
    opts.sourcemap = sourcemap;
    opts.flags = 0;
    return opts;
}

/* Check error */
int dstc_iserr(DstFopts *opts) {
    return (opts->compiler->result.status == DST_COMPILE_ERROR);
}

/* Allocate a slot index */
int32_t dstc_lsloti(DstCompiler *c) {
    DstScope *scope = &dst_v_last(c->scopes);
    /* Get the nth bit in the array */
    int32_t i, biti, len;
    biti = -1;
    len = dst_v_count(scope->slots);
    for (i = 0; i < len; i++) {
        uint32_t block = scope->slots[i];
        if (block == 0xFFFFFFFF) continue;
        biti = i << 5; /* + clz(block) */
        while (block & 1) {
            biti++;
            block >>= 1;
        }
        break;
    }
    if (biti == -1) {
        dst_v_push(scope->slots, len == 7 ? 0xFFFF0000 : 0);
        biti = len << 5;
    }
    /* set the bit at index biti */
    scope->slots[biti >> 5] |= 1 << (biti & 0x1F);
    if (biti > scope->smax)
        scope->smax = biti;
    return biti;
}

/* Free a slot index */
void dstc_sfreei(DstCompiler *c, int32_t index) {
    DstScope *scope = &dst_v_last(c->scopes);
    /* Don't free the pre allocated slots */
    if (index >= 0 && (index < 0xF0 || index > 0xFF) && 
            index < (dst_v_count(scope->slots) << 5))
        scope->slots[index >> 5] &= ~(1 << (index & 0x1F));
}

/* Allocate a local near (n) slot and return its index. Slot
 * has maximum index max. Common value for max would be 0xFF,
 * the highest slot index representable with one byte. */
int32_t dstc_lslotn(DstCompiler *c, int32_t max, int32_t nth) {
    int32_t ret = dstc_lsloti(c);
    if (ret > max) {
        dstc_sfreei(c, ret);
        ret = 0xF0 + nth;
    }
    return ret;
}

/* Free a slot */
void dstc_freeslot(DstCompiler *c, DstSlot s) {
    if (s.flags & (DST_SLOT_CONSTANT | DST_SLOT_NAMED)) return;
    if (s.envindex > 0) return;
    dstc_sfreei(c, s.index);
}

/* Add a slot to a scope with a symbol associated with it (def or var). */
void dstc_nameslot(DstCompiler *c, const uint8_t *sym, DstSlot s) {
    DstScope *scope = &dst_v_last(c->scopes);
    SymPair sp;
    sp.sym = sym;
    sp.slot = s;
    sp.slot.flags |= DST_SLOT_NAMED;
    dst_v_push(scope->syms, sp);
}

/* Enter a new scope */
void dstc_scope(DstCompiler *c, int flags) {
    DstScope scope;
    scope.consts = NULL;
    scope.syms = NULL;
    scope.envs = NULL;
    scope.defs = NULL;
    scope.slots = NULL;
    scope.smax = -1;
    scope.bytecode_start = dst_v_count(c->buffer);
    scope.flags = flags;

    /* Inherit slots */
    if ((!(flags & DST_SCOPE_FUNCTION)) && dst_v_count(c->scopes)) {
        DstScope *oldscope = &dst_v_last(c->scopes);
        scope.smax = oldscope->smax;
        scope.slots = dst_v_copy(oldscope->slots);
    }

    dst_v_push(c->scopes, scope);
}

/* Leave a scope. */
void dstc_popscope(DstCompiler *c) {
    DstScope scope;
    int32_t oldcount = dst_v_count(c->scopes);
    dst_assert(oldcount, "could not pop scope");
    scope = dst_v_last(c->scopes);
    /* Free the scope */
    dst_v_free(scope.consts);
    dst_v_free(scope.syms);
    dst_v_free(scope.envs);
    dst_v_free(scope.defs);
    dst_v_free(scope.slots);
    dst_v_pop(c->scopes);
    /* Move free slots to parent scope if not a new function.
     * We need to know the total number of slots used when compiling the function. */
    if (!(scope.flags & (DST_SCOPE_FUNCTION | DST_SCOPE_UNUSED)) && oldcount > 1) {
        DstScope *newscope = &dst_v_last(c->scopes);
        if (newscope->smax < scope.smax) 
            newscope->smax = scope.smax;
    }
}

/* Create a slot with a constant */
DstSlot dstc_cslot(Dst x) {
    DstSlot ret;
    ret.flags = (1 << dst_type(x)) | DST_SLOT_CONSTANT;
    ret.index = -1;
    ret.constant = x;
    ret.envindex = 0;
    return ret;
}

/* Allow searching for symbols. Return information about the symbol */
DstSlot dstc_resolve(
        DstCompiler *c,
        const Dst *sourcemap,
        const uint8_t *sym) {

    DstSlot ret = dstc_cslot(dst_wrap_nil());
    DstScope *top = &dst_v_last(c->scopes);
    DstScope *scope = top;
    int foundlocal = 1;
    int unused = 0;

    /* Search scopes for symbol, starting from top */
    while (scope >= c->scopes) {
        int32_t i, len;
        if (scope->flags & DST_SCOPE_UNUSED)
            unused = 1;
        len = dst_v_count(scope->syms);
        for (i = 0; i < len; i++) {
            if (scope->syms[i].sym == sym) {
                ret = scope->syms[i].slot;
                ret.flags |= DST_SLOT_NAMED;
                goto found;
            }
        }
        if (scope->flags & DST_SCOPE_FUNCTION)
            foundlocal = 0;
        scope--;
    }

    /* Symbol not found - check for global */
    {
        Dst check = dst_get(c->env, dst_wrap_symbol(sym));
        Dst ref;
        if (!(dst_checktype(check, DST_STRUCT) || dst_checktype(check, DST_TABLE))) {
            dstc_error(c, sourcemap, dst_formatc("unknown symbol %q", sym));
            return dstc_cslot(dst_wrap_nil());
        }
        ref = dst_get(check, dst_csymbolv("ref"));
        if (dst_checktype(ref, DST_ARRAY)) {
            DstSlot ret = dstc_cslot(ref);
            /* TODO save type info */
            ret.flags |= DST_SLOT_REF | DST_SLOT_NAMED | DST_SLOT_MUTABLE | DST_SLOTTYPE_ANY;
            ret.flags &= ~DST_SLOT_CONSTANT;
            return ret;
        } else {
            Dst value = dst_get(check, dst_csymbolv("value"));
            return dstc_cslot(value);
        }
    }

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
        if (!dst_v_count(scope->envs)) dst_v_push(scope->envs, 0);
        scope++;
    }

    /* Propogate env up to current scope */
    int32_t envindex = 0;
    while (scope <= top) {
        if (scope->flags & DST_SCOPE_FUNCTION) {
            int32_t j, len;
            int scopefound = 0;
            /* Check if scope already has env. If so, break */
            len = dst_v_count(scope->envs);
            for (j = 1; j < len; j++) {
                if (scope->envs[j] == envindex) {
                    scopefound = 1;
                    envindex = j;
                    break;
                }
            }
            /* Add the environment if it is not already referenced */
            if (!scopefound) {
                if (!dst_v_count(scope->envs)) dst_v_push(scope->envs, 0);
                dst_v_push(scope->envs, envindex);
                envindex = len;
            }
        }
        scope++;
    }
    
    ret.envindex = envindex;
    return ret;
}

/* Emit a raw instruction with source mapping. */
void dstc_emit(DstCompiler *c, const Dst *sourcemap, uint32_t instr) {
    dst_v_push(c->buffer, instr);
    if (NULL != sourcemap) {
        dst_v_push(c->mapbuffer, dst_unwrap_integer(sourcemap[0]));
        dst_v_push(c->mapbuffer, dst_unwrap_integer(sourcemap[1]));
    } else {
        dst_v_push(c->mapbuffer, -1);
        dst_v_push(c->mapbuffer, -1);
    }
}

/* Add a constant to the current scope. Return the index of the constant. */
static int32_t dstc_const(DstCompiler *c, const Dst *sourcemap, Dst x) {
    DstScope *scope = &dst_v_last(c->scopes);
    int32_t i, len;
    /* Get the topmost function scope */
    while (scope > c->scopes) {
        if (scope->flags & DST_SCOPE_FUNCTION)
            break;
        scope--;
    }
    /* Check if already added */
    len = dst_v_count(scope->consts);
    for (i = 0; i < len; i++) {
        if (dst_equals(x, scope->consts[i]))
            return i;
    }
    /* Ensure not too many constsants. */
    if (len >= 0xFFFF) {
        dstc_cerror(c, sourcemap, "too many constants");
        return 0;
    }
    dst_v_push(scope->consts, x);
    return len;
}

/* Load a constant into a local slot */
static void dstc_loadconst(DstCompiler *c, const Dst *sourcemap, Dst k, int32_t dest) {
    switch (dst_type(k)) {
        case DST_NIL:
            dstc_emit(c, sourcemap, (dest << 8) | DOP_LOAD_NIL);
            break;
        case DST_TRUE:
            dstc_emit(c, sourcemap, (dest << 8) | DOP_LOAD_TRUE);
            break;
        case DST_FALSE:
            dstc_emit(c, sourcemap, (dest << 8) | DOP_LOAD_FALSE);
            break;
        case DST_INTEGER:
            {
                int32_t i = dst_unwrap_integer(k);
                if (i <= INT16_MAX && i >= INT16_MIN) {
                    dstc_emit(c, sourcemap, 
                            (i << 16) |
                            (dest << 8) |
                            DOP_LOAD_INTEGER);
                    break;
                }
                /* fallthrough */
            }
        default:
            {
                int32_t cindex = dstc_const(c, sourcemap, k);
                dstc_emit(c, sourcemap, 
                        (cindex << 16) |
                        (dest << 8) |
                        DOP_LOAD_CONSTANT);
                break;
            }
    }
}

/* Realize any slot to a local slot. Call this to get a slot index
 * that can be used in an instruction. */
int32_t dstc_preread(
        DstCompiler *c,
        const Dst *sourcemap,
        int32_t max,
        int nth,
        DstSlot s) {

    int32_t ret;

    if (s.flags & DST_SLOT_REF)
        max = 0xFF;

    if (s.flags & (DST_SLOT_CONSTANT | DST_SLOT_REF)) {
        ret = dstc_lslotn(c, 0xFF, nth);
        dstc_loadconst(c, sourcemap, s.constant, ret);
        /* If we also are a reference, deref the one element array */
        if (s.flags & DST_SLOT_REF) {
            dstc_emit(c, sourcemap, 
                    (ret << 16) |
                    (ret << 8) |
                    DOP_GET_INDEX);
        }
    } else if (s.envindex > 0 || s.index > max) {
        ret = dstc_lslotn(c, max, nth);
        dstc_emit(c, sourcemap, 
                ((uint32_t)(s.index) << 24) |
                ((uint32_t)(s.envindex) << 16) |
                ((uint32_t)(ret) << 8) |
                DOP_LOAD_UPVALUE);
    } else if (s.index > max) {
        ret = dstc_lslotn(c, max, nth);
        dstc_emit(c, sourcemap, 
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
void dstc_postread(DstCompiler *c, DstSlot s, int32_t index) {
    if (index != s.index || s.envindex > 0 || s.flags & DST_SLOT_CONSTANT) {
        /* We need to free the temporary slot */
        dstc_sfreei(c, index);
    }
}

/* Move values from one slot to another. The destination must
 * be writeable (not a literal). */
void dstc_copy(
        DstCompiler *c,
        const Dst *sourcemap,
        DstSlot dest,
        DstSlot src) {
    int writeback = 0;
    int32_t destlocal = -1;
    int32_t srclocal = -1;
    int32_t reflocal = -1;

    /* Can't write to constants */
    if (dest.flags & DST_SLOT_CONSTANT) {
        dstc_cerror(c, sourcemap, "cannot write to constant");
        return;
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

    /* Types of slots - src */
    /* constants */
    /* upvalues */
    /* refs */
    /* near index */
    /* far index */

    /* Types of slots - dest */
    /* upvalues */
    /* refs */
    /* near index */
    /* far index */

    /* If dest is a near index, do some optimization */
    if (dest.envindex == 0 && dest.index >= 0 && dest.index <= 0xFF) {
        if (src.flags & DST_SLOT_CONSTANT) {
            dstc_loadconst(c, sourcemap, src.constant, dest.index);
        } else if (src.flags & DST_SLOT_REF) {
            dstc_loadconst(c, sourcemap, src.constant, dest.index);
            dstc_emit(c, sourcemap,
                    (dest.index << 16) |
                    (dest.index << 8) |
                    DOP_GET_INDEX);
        } else if (src.envindex > 0) {
            dstc_emit(c, sourcemap,
                    (src.index << 24) |
                    (src.envindex << 16) |
                    (dest.index << 8) |
                    DOP_LOAD_UPVALUE);
        } else {
            dstc_emit(c, sourcemap,
                    (src.index << 16) |
                    (dest.index << 8) |
                    DOP_MOVE_NEAR);
        }
        return;
    }
    
    /* Process: src -> srclocal -> destlocal -> dest */

    /* src -> srclocal */
    srclocal = dstc_preread(c, sourcemap, 0xFF, 1, src);

    /* Pull down dest (find destlocal) */
    if (dest.flags & DST_SLOT_REF) {
        writeback = 1;
        destlocal = srclocal;
        reflocal = dstc_lslotn(c, 0xFF, 2);
        dstc_emit(c, sourcemap,
                (dstc_const(c, sourcemap, dest.constant) << 16) |
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
        dstc_emit(c, sourcemap,
                ((uint32_t)(srclocal) << 16) |
                ((uint32_t)(destlocal) << 8) |
                DOP_MOVE_NEAR);
    }

    /* destlocal -> dest */ 
    if (writeback == 1) {
        dstc_emit(c, sourcemap,
                (destlocal << 16) |
                (reflocal << 8) |
                DOP_PUT_INDEX);
    } else if (writeback == 2) {
        dstc_emit(c, sourcemap, 
                ((uint32_t)(dest.index) << 24) |
                ((uint32_t)(dest.envindex) << 16) |
                ((uint32_t)(destlocal) << 8) |
                DOP_SET_UPVALUE);
    } else if (writeback == 3) {
        dstc_emit(c, sourcemap,
                ((uint32_t)(dest.index) << 16) |
                ((uint32_t)(destlocal) << 8) |
                DOP_MOVE_FAR);
    }

    /* Cleanup */
    if (reflocal >= 0) {
        dstc_sfreei(c, reflocal);
    }
    dstc_postread(c, src, srclocal);
}

/* Generate the return instruction for a slot. */
DstSlot dstc_return(DstCompiler *c, const Dst *sourcemap, DstSlot s) {
    if (!(s.flags & DST_SLOT_RETURNED)) {
        if (s.flags & DST_SLOT_CONSTANT && dst_checktype(s.constant, DST_NIL)) {
            dstc_emit(c, sourcemap, DOP_RETURN_NIL);
        } else {
            int32_t ls = dstc_preread(c, sourcemap, 0xFFFF, 1, s);
            dstc_emit(c, sourcemap, DOP_RETURN | (ls << 8));
            dstc_postread(c, s, ls);
        }
        s.flags |= DST_SLOT_RETURNED;
    }
    return s;
}

/* Get a target slot for emitting an instruction. Will always return
 * a local slot. */
DstSlot dstc_gettarget(DstFopts opts) {
    DstSlot slot;
    if ((opts.flags & DST_FOPTS_HINT) &&
        (opts.hint.envindex == 0) &&
        (opts.hint.index >= 0 && opts.hint.index <= 0xFF)) {
        slot = opts.hint;
    } else {
        slot.envindex = 0;
        slot.constant = dst_wrap_nil();
        slot.flags = 0;
        slot.index = dstc_lslotn(opts.compiler, 0xFF, 4);
    }
    return slot;
}

/* Get a bunch of slots for function arguments */
DstSM *dstc_toslots(DstFopts opts, int32_t start) {
    int32_t i, len;
    DstSM *ret = NULL;
    len = dst_length(opts.x);
    for (i = start; i < len; i++) {
        DstSM sm;
        DstFopts subopts = dstc_getindex(opts, i);
        sm.slot = dstc_value(subopts);
        sm.map = subopts.sourcemap;
        dst_v_push(ret, sm);
    }
    return ret;
}

/* Get a bunch of slots for function arguments */
DstSM *dstc_toslotskv(DstFopts opts) {
    DstSM *ret = NULL;
    const DstKV *kv = NULL;
    while (NULL != (kv = dst_next(opts.x, kv))) {
        DstSM km, vm;
        DstFopts kopts = dstc_getkey(opts, kv->key);
        DstFopts vopts = dstc_getvalue(opts, kv->key);
        km.slot = dstc_value(kopts);
        km.map = kopts.sourcemap;
        vm.slot = dstc_value(vopts);
        vm.map = vopts.sourcemap;
        dst_v_push(ret, km);
        dst_v_push(ret, vm);
    }
    return ret;
}

/* Push slots load via dstc_toslots. */
void dstc_pushslots(DstFopts opts, DstSM *sms) {
    DstCompiler *c = opts.compiler;
    const Dst *sm = opts.sourcemap;
    int32_t i;
    for (i = 0; i < dst_v_count(sms) - 2; i += 3) {
        int32_t ls1 = dstc_preread(c, sms[i].map, 0xFF, 1, sms[i].slot);
        int32_t ls2 = dstc_preread(c, sms[i + 1].map, 0xFF, 2, sms[i + 1].slot);
        int32_t ls3 = dstc_preread(c, sms[i + 2].map, 0xFF, 3, sms[i + 2].slot);
        dstc_emit(c, sm, 
                (ls3 << 24) |
                (ls2 << 16) |
                (ls1 << 8) |
                DOP_PUSH_3);
        dstc_postread(c, sms[i].slot, ls1);
        dstc_postread(c, sms[i + 1].slot, ls2);
        dstc_postread(c, sms[i + 2].slot, ls3);
    }
    if (i == dst_v_count(sms) - 2) {
        int32_t ls1 = dstc_preread(c, sms[i].map, 0xFF, 1, sms[i].slot);
        int32_t ls2 = dstc_preread(c, sms[i + 1].map, 0xFFFF, 2, sms[i + 1].slot);
        dstc_emit(c, sm, 
                (ls2 << 16) |
                (ls1 << 8) |
                DOP_PUSH_2);
        dstc_postread(c, sms[i].slot, ls1);
        dstc_postread(c, sms[i + 1].slot, ls2);
    } else if (i == dst_v_count(sms) - 1) {
        int32_t ls1 = dstc_preread(c, sms[i].map, 0xFFFFFF, 1, sms[i].slot);
        dstc_emit(c, sm, 
                (ls1 << 8) |
                DOP_PUSH);
        dstc_postread(c, sms[i].slot, ls1);
    }
}

/* Free slots loaded via dstc_toslots */
void dstc_freeslots(DstFopts opts, DstSM *sms) {
    int32_t i;
    for (i = 0; i < dst_v_count(sms); i++) {
        dstc_freeslot(opts.compiler, sms[i].slot);
    }
    dst_v_free(sms);
}

/* Compile some code that will be thrown away. Used to ensure
 * that dead code is well formed without including it in the final
 * bytecode. */
void dstc_throwaway(DstFopts opts) {
    DstCompiler *c = opts.compiler;
    int32_t bufstart = dst_v_count(c->buffer);
    dstc_scope(c, DST_SCOPE_UNUSED);
    dstc_value(opts);
    dstc_popscope(c);
    if (NULL != c->buffer) {
        dst_v__cnt(c->buffer) = bufstart;
        if (NULL != c->mapbuffer) 
            dst_v__cnt(c->mapbuffer) = bufstart;
    }
}

/* Compile a call or tailcall instruction */
static DstSlot dstc_call(DstFopts opts, DstSM *sms, DstSlot fun) {
    DstSlot retslot;
    int32_t localindex;
    DstCompiler *c = opts.compiler;
    const Dst *sm = opts.sourcemap;
    dstc_pushslots(opts, sms);
    dstc_freeslots(opts, sms);
    localindex = dstc_preread(c, sm, 0xFF, 1, fun);
    if (opts.flags & DST_FOPTS_TAIL) {
        dstc_emit(c, sm, (localindex << 8) | DOP_TAILCALL);
        retslot = dstc_cslot(dst_wrap_nil());
        retslot.flags = DST_SLOT_RETURNED;
    } else {
        retslot = dstc_gettarget(opts);
        dstc_emit(c, sm, (localindex << 16) | (retslot.index << 8) | DOP_CALL);
    }
    dstc_postread(c, fun, localindex);
    return retslot;
}

/* Compile a tuple */
DstSlot dstc_tuple(DstFopts opts) {
    DstSlot head;
    DstFopts subopts;
    const Dst *tup = dst_unwrap_tuple(opts.x);
    /* Empty tuple is tuple literal */
    if (dst_tuple_length(tup) == 0) return dstc_cslot(opts.x);
    /* Symbols could be specials */
    if (dst_checktype(tup[0], DST_SYMBOL)) {
        const DstSpecial *s = dstc_special(dst_unwrap_symbol(tup[0]));
        if (NULL != s) {
            return s->compile(opts, dst_tuple_length(tup) - 1, tup + 1);
        }
    }
    /* Compile the head of the tuple */
    subopts = dstc_getindex(opts, 0);
    subopts.flags = DST_FUNCTION | DST_CFUNCTION;
    head = dstc_value(subopts);
    return dstc_call(opts, dstc_toslots(opts, 1), head);
}

static DstSlot dstc_array(DstFopts opts) {
    return dstc_call(opts, dstc_toslots(opts, 0), dstc_cslot(dst_wrap_cfunction(dst_stl_array)));
}

static DstSlot dstc_tablector(DstFopts opts, DstCFunction cfun) {
    return dstc_call(opts, dstc_toslotskv(opts), dstc_cslot(dst_wrap_cfunction(cfun)));
}

/* Compile a single value */
DstSlot dstc_value(DstFopts opts) {
    DstSlot ret;
    if (dstc_iserr(&opts)) {
        return dstc_cslot(dst_wrap_nil());
    }
    if (opts.compiler->recursion_guard <= 0) {
        dstc_cerror(opts.compiler, opts.sourcemap, "recursed too deeply");
        return dstc_cslot(dst_wrap_nil());
    }
    opts.compiler->recursion_guard--;
    switch (dst_type(opts.x)) {
        default:
            ret = dstc_cslot(opts.x);
            break;
        case DST_SYMBOL:
            {
                const uint8_t *sym = dst_unwrap_symbol(opts.x);
                ret = dstc_resolve(opts.compiler, opts.sourcemap, sym);
                break;
            }
        case DST_TUPLE:
            ret = dstc_tuple(opts);
            break;
        case DST_ARRAY:
            ret = dstc_array(opts); 
            break;
        case DST_STRUCT:
            ret = dstc_tablector(opts, dst_stl_struct); 
            break;
        case DST_TABLE:
            ret = dstc_tablector(opts, dst_stl_table);
            break;
    }
    if (opts.flags & DST_FOPTS_TAIL) {
        ret = dstc_return(opts.compiler, opts.sourcemap, ret);
    }
    opts.compiler->recursion_guard++;
    return ret;
}

/* Compile a funcdef */
DstFuncDef *dstc_pop_funcdef(DstCompiler *c) {
    DstScope scope = dst_v_last(c->scopes);
    DstFuncDef *def = dst_gcalloc(DST_MEMORY_FUNCDEF, sizeof(DstFuncDef));
    def->source = NULL;
    def->sourcepath = NULL;
    def->sourcemap = NULL;
    def->slotcount = scope.smax + 1;

    /* Copy envs */
    def->environments_length = dst_v_count(scope.envs);
    def->environments = NULL;
    if (def->environments_length > 1) def->environments = dst_v_flatten(scope.envs);

    def->constants_length = dst_v_count(scope.consts);
    def->constants = dst_v_flatten(scope.consts);

    def->defs_length = dst_v_count(scope.defs);
    def->defs = dst_v_flatten(scope.defs);

    /* Copy bytecode */
    def->bytecode_length = dst_v_count(c->buffer) - scope.bytecode_start;
    if (def->bytecode_length) {
        size_t s = sizeof(int32_t) * def->bytecode_length;
        def->bytecode = malloc(s);
        if (NULL == def->bytecode) {
            DST_OUT_OF_MEMORY;
        }
        memcpy(def->bytecode, c->buffer + scope.bytecode_start, s);
        dst_v__cnt(c->buffer) = scope.bytecode_start;
        if (NULL != c->mapbuffer) {
            def->sourcemap = malloc(2 * s);
            if (NULL == def->sourcemap) {
                DST_OUT_OF_MEMORY;
            }
            memcpy(def->sourcemap, c->mapbuffer + scope.bytecode_start, 2 * s);
            dst_v__cnt(c->mapbuffer) = scope.bytecode_start;
        }
    }

    def->arity = 0;
    def->flags = 0;
    if (scope.flags & DST_SCOPE_ENV) {
        def->flags |= DST_FUNCDEF_FLAG_NEEDSENV;
    }

    /* Pop the scope */
    dstc_popscope(c);

    return def;
}


/* Initialize a compiler */
static void dstc_init(DstCompiler *c, Dst env) {
    c->scopes = NULL;
    c->buffer = NULL;
    c->mapbuffer = NULL;
    c->recursion_guard = DST_RECURSION_GUARD;
    c->env = env;
    /* Init result */
    c->result.error = NULL;
    c->result.status = DST_COMPILE_OK;
    c->result.error_start = -1;
    c->result.error_end = -1;
    c->result.funcdef = NULL;
}

/* Deinitialize a compiler struct */
static void dstc_deinit(DstCompiler *c) {
    while (dst_v_count(c->scopes)) dstc_popscope(c);
    dst_v_free(c->scopes);
    dst_v_free(c->buffer);
    dst_v_free(c->mapbuffer);
    c->env = dst_wrap_nil();
}

/* Compile a form. */
DstCompileResult dst_compile(DstCompileOptions opts) {
    DstCompiler c;
    DstFopts fopts;

    dstc_init(&c, opts.env);

    /* Push a function scope */
    dstc_scope(&c, DST_SCOPE_FUNCTION | DST_SCOPE_TOP);

    /* Set initial form options */
    fopts.compiler = &c;
    fopts.sourcemap = opts.sourcemap;
    fopts.flags = DST_FOPTS_TAIL | DST_SLOTTYPE_ANY;
    fopts.hint = dstc_cslot(dst_wrap_nil());
    fopts.x = opts.source;

    /* Compile the value */
    dstc_value(fopts);

    if (c.result.status == DST_COMPILE_OK) {
        c.result.funcdef = dstc_pop_funcdef(&c);
    }

    dstc_deinit(&c);

    return c.result;
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
