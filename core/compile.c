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
static int dstc_iserr(DstFopts *opts) {
    return (opts->compiler->result.status == DST_COMPILE_ERROR);
}

/* Allocate a slot index */
static int32_t dstc_lsloti(DstCompiler *c) {
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
static void dstc_sfreei(DstCompiler *c, int32_t index) {
    DstScope *scope = &dst_v_last(c->scopes);
    /* Don't free the pre allocated slots */
    if (index >= 0 && (index < 0xF0 || index > 0xFF) && 
            index < (dst_v_count(scope->slots) << 5))
        scope->slots[index >> 5] &= ~(1 << (index & 0x1F));
}

/* Allocate a local near (n) slot and return its index. Slot
 * has maximum index max. Common value for max would be 0xFF,
 * the highest slot index representable with one byte. */
static int32_t dstc_lslotn(DstCompiler *c, int32_t max, int32_t nth) {
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
static void dstc_nameslot(DstCompiler *c, const uint8_t *sym, DstSlot s) {
    DstScope *scope = &dst_v_last(c->scopes);
    SymPair sp;
    sp.sym = sym;
    sp.slot = s;
    sp.slot.flags |= DST_SLOT_NAMED;
    dst_v_push(scope->syms, sp);
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
static int32_t dstc_preread(
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
static void dstc_postread(DstCompiler *c, DstSlot s, int32_t index) {
    if (index != s.index || s.envindex > 0 || s.flags & DST_SLOT_CONSTANT) {
        /* We need to free the temporary slot */
        dstc_sfreei(c, index);
    }
}

/* Move values from one slot to another. The destination must
 * be writeable (not a literal). */
static void dstc_copy(
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
static DstSlot dstc_return(DstCompiler *c, const Dst *sourcemap, DstSlot s) {
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
static DstSlot dstc_gettarget(DstFopts opts) {
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

/* Slot and map pairing */
typedef struct SlotMap {
    DstSlot slot;
    const Dst *map;
} SlotMap;

/* Get a bunch of slots for function arguments */
SlotMap *toslots(DstFopts opts, int32_t start) {
    int32_t i, len;
    SlotMap *ret = NULL;
    len = dst_length(opts.x);
    for (i = start; i < len; i++) {
        SlotMap sm;
        DstFopts subopts = dstc_getindex(opts, i);
        sm.slot = dstc_value(subopts);
        sm.map = subopts.sourcemap;
        dst_v_push(ret, sm);
    }
    return ret;
}

/* Get a bunch of slots for function arguments */
static SlotMap *toslotskv(DstFopts opts) {
    SlotMap *ret = NULL;
    const DstKV *kv = NULL;
    while (NULL != (kv = dst_next(opts.x, kv))) {
        SlotMap km, vm;
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

/* Push slots load via toslots. */
static void pushslots(DstFopts opts, SlotMap *sms) {
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

/* Free slots loaded via toslots */
static void freeslots(DstFopts opts, SlotMap *sms) {
    int32_t i;
    for (i = 0; i < dst_v_count(sms); i++) {
        dstc_freeslot(opts.compiler, sms[i].slot);
    }
    dst_v_free(sms);
}

DstSlot dstc_quote(DstFopts opts, int32_t argn, const Dst *argv) {
    if (argn != 1) {
        dstc_cerror(opts.compiler, opts.sourcemap, "expected 1 argument");
        return dstc_cslot(dst_wrap_nil());
    }
    return dstc_cslot(argv[0]);
}

DstSlot dstc_var(DstFopts opts, int32_t argn, const Dst *argv) {
    DstCompiler *c = opts.compiler;
    DstFopts subopts;
    DstSlot ret;
    if (argn != 2) {
        dstc_cerror(opts.compiler, opts.sourcemap, "expected 2 arguments");
        return dstc_cslot(dst_wrap_nil());
    }
    if (!dst_checktype(argv[0], DST_SYMBOL)) {
        dstc_cerror(opts.compiler, opts.sourcemap, "expected symbol");
        return dstc_cslot(dst_wrap_nil());
    }
    subopts = dstc_getindex(opts, 2);
    subopts.flags = opts.flags & ~DST_FOPTS_TAIL;
    ret = dstc_value(subopts);
    if (dst_v_last(c->scopes).flags & DST_SCOPE_TOP) {
        DstCompiler *c = opts.compiler;
        const Dst *sm = opts.sourcemap;
        DstSlot refslot, refarrayslot;
        /* Global var, generate var */
        DstTable *reftab = dst_table(1);
        DstArray *ref = dst_array(1);
        dst_array_push(ref, dst_wrap_nil());
        dst_table_put(reftab, dst_csymbolv("ref"), dst_wrap_array(ref));
        dst_put(opts.compiler->env, argv[0], dst_wrap_table(reftab));
        refslot = dstc_cslot(dst_wrap_array(ref));
        refarrayslot = refslot;
        refslot.flags |= DST_SLOT_REF | DST_SLOT_NAMED | DST_SLOT_MUTABLE;
        /* Generate code to set ref */
        int32_t refarrayindex = dstc_preread(c, sm, 0xFF, 1, refarrayslot);
        int32_t retindex = dstc_preread(c, sm, 0xFF, 2, ret);
        dstc_emit(c, sm,
                (retindex << 16) |
                (refarrayindex << 8) |
                DOP_PUT_INDEX);
        dstc_postread(c, refarrayslot, refarrayindex);
        dstc_postread(c, ret, retindex);
        /*dstc_freeslot(c, refarrayslot);*/
        ret = refslot;
    } else {
        /* Non root scope, bring to local slot */
        if (ret.flags & DST_SLOT_NAMED ||
            ret.envindex != 0 ||
            ret.index < 0 ||
            ret.index > 0xFF) {
            /* Slot is not able to be named */
            DstSlot localslot;
            localslot.index = dstc_lsloti(c);
            /* infer type? */
            localslot.flags = DST_SLOT_NAMED | DST_SLOT_MUTABLE;
            localslot.envindex = 0;
            localslot.constant = dst_wrap_nil();
            dstc_copy(opts.compiler, opts.sourcemap, localslot, ret);
            ret = localslot;
        }
        dstc_nameslot(c, dst_unwrap_symbol(argv[0]), ret); 
    }
    return ret;
}

DstSlot dstc_varset(DstFopts opts, int32_t argn, const Dst *argv) {
    DstFopts subopts;
    DstSlot ret, dest;
    if (argn != 2) {
        dstc_cerror(opts.compiler, opts.sourcemap, "expected 2 arguments");
        return dstc_cslot(dst_wrap_nil());
    }
    if (!dst_checktype(argv[0], DST_SYMBOL)) {
        dstc_cerror(opts.compiler, opts.sourcemap, "expected symbol");
        return dstc_cslot(dst_wrap_nil());
    }
    dest = dstc_resolve(opts.compiler, opts.sourcemap, dst_unwrap_symbol(argv[0]));
    if (!(dest.flags & DST_SLOT_MUTABLE)) {
        dstc_cerror(opts.compiler, opts.sourcemap, "cannot set constant");
        return dstc_cslot(dst_wrap_nil());
    }
    subopts = dstc_getindex(opts, 2);
    subopts.flags = DST_FOPTS_HINT;
    subopts.hint = dest;
    ret = dstc_value(subopts);
    dstc_copy(opts.compiler, subopts.sourcemap, dest, ret);
    return ret;
}

DstSlot dstc_def(DstFopts opts, int32_t argn, const Dst *argv) {
    DstCompiler *c = opts.compiler;
    DstFopts subopts;
    DstSlot ret;
    if (argn != 2) {
        dstc_cerror(opts.compiler, opts.sourcemap, "expected 2 arguments");
        return dstc_cslot(dst_wrap_nil());
    }
    if (!dst_checktype(argv[0], DST_SYMBOL)) {
        dstc_cerror(opts.compiler, opts.sourcemap, "expected symbol");
        return dstc_cslot(dst_wrap_nil());
    }
    subopts = dstc_getindex(opts, 2);
    subopts.flags &= ~DST_FOPTS_TAIL;
    ret = dstc_value(subopts);
    ret.flags |= DST_SLOT_NAMED;
    if (dst_v_last(c->scopes).flags & DST_SCOPE_TOP) {
        /* Global def, generate code to store in env when executed */
        DstCompiler *c = opts.compiler;
        const Dst *sm = opts.sourcemap;
        /* Root scope, add to def table */
        DstSlot envslot = dstc_cslot(c->env);
        DstSlot nameslot = dstc_cslot(argv[0]);
        DstSlot valsymslot = dstc_cslot(dst_csymbolv("value"));
        DstSlot tableslot = dstc_cslot(dst_wrap_cfunction(dst_stl_table));
        /* Create env entry */
        int32_t valsymindex = dstc_preread(c, sm, 0xFF, 1, valsymslot);
        int32_t retindex = dstc_preread(c, sm, 0xFFFF, 2, ret);
        dstc_emit(c, sm,
                (retindex << 16) |
                (valsymindex << 8) |
                DOP_PUSH_2);
        dstc_postread(c, ret, retindex);
        dstc_postread(c, valsymslot, valsymindex);
        dstc_freeslot(c, valsymslot);
        int32_t tableindex = dstc_preread(opts.compiler, opts.sourcemap, 0xFF, 1, tableslot);
        dstc_emit(c, sm,
                (tableindex << 16) |
                (tableindex << 8) |
                DOP_CALL);
        /* Add env entry to env */
        int32_t nameindex = dstc_preread(opts.compiler, opts.sourcemap, 0xFF, 2, nameslot);
        int32_t envindex = dstc_preread(opts.compiler, opts.sourcemap, 0xFF, 3, envslot);
        dstc_emit(opts.compiler, opts.sourcemap, 
                (tableindex << 24) |
                (nameindex << 16) |
                (envindex << 8) |
                DOP_PUT);
        dstc_postread(opts.compiler, envslot, envindex);
        dstc_postread(opts.compiler, nameslot, nameindex);
        dstc_postread(c, tableslot, tableindex);
        dstc_freeslot(c, tableslot);
        dstc_freeslot(c, envslot);
        dstc_freeslot(c, tableslot);
    } else {
        /* Non root scope, simple slot alias */
        dstc_nameslot(c, dst_unwrap_symbol(argv[0]), ret); 
    }
    return ret;
}

/* Compile some code that will be thrown away. Used to ensure
 * that dead code is well formed without including it in the final
 * bytecode. */
static void dstc_throwaway(DstFopts opts) {
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
DstSlot dstc_if(DstFopts opts, int32_t argn, const Dst *argv) {
    DstCompiler *c = opts.compiler;
    const Dst *sm = opts.sourcemap;
    int32_t labelr, labeljr, labeld, labeljd, condlocal;
    DstFopts leftopts, rightopts, condopts;
    DstSlot cond, left, right, target;
    const int tail = opts.flags & DST_FOPTS_TAIL;
    const int drop = opts.flags & DST_FOPTS_DROP;
    (void) argv;

    if (argn < 2 || argn > 3) {
        dstc_cerror(c, sm, "expected 2 or 3 arguments to if");
        return dstc_cslot(dst_wrap_nil());
    }

    /* Get options */
    condopts = dstc_getindex(opts, 1);
    leftopts = dstc_getindex(opts, 2);
    rightopts = dstc_getindex(opts, 3);
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
    cond = dstc_value(condopts);

    /* Check constant condition. */
    /* TODO: Use type info for more short circuits */
    if ((cond.flags & DST_SLOT_CONSTANT) && !(cond.flags & DST_SLOT_REF)) {
        DstFopts goodopts, badopts;
        if (dst_truthy(cond.constant)) {
            goodopts = leftopts;
            badopts = rightopts;
        } else {
            goodopts = rightopts;
            badopts = leftopts;
        }
        dstc_scope(c, 0);
        target = dstc_value(goodopts);
        dstc_popscope(c);
        dstc_throwaway(badopts);
        return target;
    }

    /* Set target for compilation */
    target = (!drop && !tail) 
        ? dstc_gettarget(opts)
        : dstc_cslot(dst_wrap_nil());

    /* Compile jump to right */
    condlocal = dstc_preread(c, sm, 0xFF, 1, cond);
    labeljr = dst_v_count(c->buffer);
    dstc_emit(c, sm, DOP_JUMP_IF_NOT | (condlocal << 8));
    dstc_postread(c, cond, condlocal);
    dstc_freeslot(c, cond);

    /* Condition left body */
    dstc_scope(c, 0);
    left = dstc_value(leftopts);
    if (!drop && !tail) dstc_copy(c, sm, target, left); 
    dstc_popscope(c);

    /* Compile jump to done */
    labeljd = dst_v_count(c->buffer);
    if (!tail) dstc_emit(c, sm, DOP_JUMP);

    /* Compile right body */
    labelr = dst_v_count(c->buffer);
    dstc_scope(c, 0);
    right = dstc_value(rightopts);
    if (!drop && !tail) dstc_copy(c, sm, target, right); 
    dstc_popscope(c);

    /* Write jumps - only add jump lengths if jump actually emitted */
    labeld = dst_v_count(c->buffer);
    c->buffer[labeljr] |= (labelr - labeljr) << 16;
    if (!tail) c->buffer[labeljd] |= (labeld - labeljd) << 8;

    if (tail) target.flags |= DST_SLOT_RETURNED;
    return target;
}

DstSlot dstc_do(DstFopts opts, int32_t argn, const Dst *argv) {
    int32_t i;
    DstSlot ret;
    dstc_scope(opts.compiler, 0);
    (void) argv;
    for (i = 0; i < argn; i++) {
        DstFopts subopts = dstc_getindex(opts, i + 1);
        if (i != argn - 1) {
            subopts.flags = DST_FOPTS_DROP;
        } else if (opts.flags & DST_FOPTS_TAIL) {
            subopts.flags = DST_FOPTS_TAIL;
        }
        ret = dstc_value(subopts);
        if (i != argn - 1) {
            dstc_freeslot(opts.compiler, ret);
        }
    }
    dstc_popscope(opts.compiler);
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
DstSlot dstc_while(DstFopts opts, int32_t argn, const Dst *argv) {
    DstCompiler *c = opts.compiler;
    const Dst *sm = opts.sourcemap;
    DstSlot cond;
    int32_t condlocal, labelwt, labeld, labeljt, labelc, i;
    int infinite = 0;
    (void) argv;

    if (argn < 2) {
        dstc_cerror(c, sm, "expected at least 2 arguments");
        return dstc_cslot(dst_wrap_nil());
    }

    labelwt = dst_v_count(c->buffer);

    /* Compile condition */
    cond = dstc_value(dstc_getindex(opts, 1));

    /* Check for constant condition */
    if (cond.flags & DST_SLOT_CONSTANT) {
        /* Loop never executes */
        if (!dst_truthy(cond.constant)) {
            return dstc_cslot(dst_wrap_nil());
        }
        /* Infinite loop */
        infinite = 1;
    }

    dstc_scope(c, 0);

    /* Infinite loop does not need to check condition */
    if (!infinite) {
        condlocal = dstc_preread(c, sm, 0xFF, 1, cond);
        labelc = dst_v_count(c->buffer);
        dstc_emit(c, sm, DOP_JUMP_IF_NOT | (condlocal << 8));
        dstc_postread(c, cond, condlocal);
    }

    /* Compile body */
    for (i = 1; i < argn; i++) {
        DstFopts subopts = dstc_getindex(opts, i + 1);
        subopts.flags = DST_FOPTS_DROP;
        dstc_freeslot(c, dstc_value(subopts));
    }

    /* Compile jump to whiletop */
    labeljt = dst_v_count(c->buffer);
    dstc_emit(c, sm, DOP_JUMP);

    /* Calculate jumps */
    labeld = dst_v_count(c->buffer);
    if (!infinite) c->buffer[labelc] |= (labeld - labelc) << 16;
    c->buffer[labeljt] |= (labelwt - labeljt) << 8;

    /* Pop scope and return nil slot */
    dstc_popscope(opts.compiler);

    return dstc_cslot(dst_wrap_nil());
}

/* Compile a funcdef */
static DstFuncDef *dstc_pop_funcdef(DstCompiler *c) {
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

/* Add a funcdef to the top most function scope */
static int32_t dstc_addfuncdef(DstCompiler *c, DstFuncDef *def) {
    DstScope *scope = &dst_v_last(c->scopes);
    while (scope >= c->scopes) {
        if (scope->flags & DST_SCOPE_FUNCTION)
            break;
        scope--;
    }
    dst_assert(scope >= c->scopes, "could not add funcdef");
    dst_v_push(scope->defs, def);
    return dst_v_count(scope->defs) - 1;
}

DstSlot dstc_fn(DstFopts opts, int32_t argn, const Dst *argv) {
    DstCompiler *c = opts.compiler;
    const Dst *sm = opts.sourcemap;
    DstFuncDef *def;
    DstSlot ret;
    int32_t paramcount, argi, parami, arity, localslot, defindex;
    const Dst *params;
    const Dst *psm;
    int varargs = 0;

    if (argn < 2) {
        dstc_cerror(c, sm, "expected at least 2 arguments to function literal");
        return dstc_cslot(dst_wrap_nil());
    }

    /* Begin function */
    dstc_scope(c, DST_SCOPE_FUNCTION);

    /* Read function parameters */
    parami = 0;
    arity = 0;
    if (dst_checktype(argv[0], DST_SYMBOL)) parami = 1;
    if (parami >= argn) {
        dstc_cerror(c, sm, "expected function parameters");
        return dstc_cslot(dst_wrap_nil());
    }
    if (dst_seq_view(argv[parami], &params, &paramcount)) {
        psm = dst_sourcemap_index(sm, parami + 1);
        int32_t i;
        for (i = 0; i < paramcount; i++) {
            const Dst *psmi = dst_sourcemap_index(psm, i);
            if (dst_checktype(params[i], DST_SYMBOL)) {
                DstSlot slot;
                /* Check for varargs */
                if (0 == dst_cstrcmp(dst_unwrap_symbol(params[i]), "&")) {
                    if (i != paramcount - 2) {
                        dstc_cerror(c, psmi, "variable argument symbol in unexpected location");
                        return dstc_cslot(dst_wrap_nil());
                    }
                    varargs = 1;
                    arity--;
                    continue;
                }
                slot.flags = DST_SLOT_NAMED;
                slot.envindex = 0;
                slot.constant = dst_wrap_nil();
                slot.index = dstc_lsloti(c);
                dstc_nameslot(c, dst_unwrap_symbol(params[i]), slot);
                arity++;
            } else {
                dstc_cerror(c, psmi, "expected symbol as function parameter");
                return dstc_cslot(dst_wrap_nil());
            }
        }
    } else {
        dstc_cerror(c, sm, "expected function parameters");
        return dstc_cslot(dst_wrap_nil());
    }

    /* Compile function body */
    for (argi = parami + 1; argi < argn; argi++) {
        DstSlot s;
        DstFopts subopts = dstc_getindex(opts, argi + 1);
        subopts.flags = argi == (argn - 1) ? DST_FOPTS_TAIL : DST_FOPTS_DROP;
        s = dstc_value(subopts);
        dstc_freeslot(c, s);
    }
    
    /* Build function */
    def = dstc_pop_funcdef(c);
    def->arity = arity;
    if (varargs) def->flags |= DST_FUNCDEF_FLAG_VARARG;
    defindex = dstc_addfuncdef(c, def);

    /* Instantiate closure */
    ret.flags = 0;
    ret.envindex = 0;
    ret.constant = dst_wrap_nil();
    ret.index = dstc_lsloti(c);

    localslot = ret.index > 0xF0 ? 0xF1 : ret.index;
    dstc_emit(c, sm,
            (defindex << 16) |
            (localslot << 8) |
            DOP_CLOSURE);
    
    if (ret.index != localslot) {
        dstc_emit(c, sm, 
                (ret.index << 16) |
                (localslot << 8) |
                DOP_MOVE_FAR);
    }

    return ret;
}

/* Keep in lexographic order */
static const DstSpecial dstc_specials[] = {
    {"def", dstc_def},
    {"do", dstc_do},
    {"fn", dstc_fn},
    {"if", dstc_if},
    {"quote", dstc_quote},
    {"var", dstc_var},
    {"varset!", dstc_varset},
    {"while", dstc_while}
};

/* Compile a tuple */
DstSlot dstc_tuple(DstFopts opts) {
    DstSlot head;
    DstFopts subopts;
    DstCompiler *c = opts.compiler;
    const Dst *tup = dst_unwrap_tuple(opts.x);
    int headcompiled = 0;
    subopts = dstc_getindex(opts, 0);
    subopts.flags = DST_FUNCTION | DST_CFUNCTION;
    if (dst_tuple_length(tup) == 0) {
        return dstc_cslot(opts.x);
    }
    if (dst_checktype(tup[0], DST_SYMBOL)) {
        const DstSpecial *s = dst_strbinsearch(
                &dstc_specials,
                sizeof(dstc_specials)/sizeof(DstSpecial),
                sizeof(DstSpecial),
                dst_unwrap_symbol(tup[0]));
        if (NULL != s) {
            return s->compile(opts, dst_tuple_length(tup) - 1, tup + 1);
        }
    }
    if (!headcompiled) {
        head = dstc_value(subopts);
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
        SlotMap *sms;
        if (!headcompiled) {
            head = dstc_value(subopts);
            headcompiled = 1;
        }
        headindex = dstc_preread(c, subopts.sourcemap, 0xFFFF, 1, head);
        sms = toslots(opts, 1);
        pushslots(opts, sms);
        freeslots(opts, sms);
        if (opts.flags & DST_FOPTS_TAIL) {
            dstc_emit(c, subopts.sourcemap, (headindex << 8) | DOP_TAILCALL);
            retslot = dstc_cslot(dst_wrap_nil());
            retslot.flags = DST_SLOT_RETURNED;
        } else {
            retslot = dstc_gettarget(opts);
            dstc_emit(c, subopts.sourcemap, (headindex << 16) | (retslot.index << 8) | DOP_CALL);
        }
        dstc_postread(c, head, headindex);
        return retslot;
    }
}

static DstSlot dstc_array(DstFopts opts) {
    DstCompiler *c = opts.compiler;
    const Dst *sm = opts.sourcemap;
    DstSlot ctor, retslot;
    SlotMap *sms;
    int32_t localindex;
    sms = toslots(opts, 0);
    pushslots(opts, sms);
    freeslots(opts, sms);
    ctor = dstc_cslot(dst_wrap_cfunction(dst_stl_array));
    localindex = dstc_preread(c, sm, 0xFF, 1, ctor);
    if (opts.flags & DST_FOPTS_TAIL) {
        dstc_emit(c, sm, (localindex << 8) | DOP_TAILCALL);
        retslot = dstc_cslot(dst_wrap_nil());
        retslot.flags = DST_SLOT_RETURNED;
    } else {
        retslot = dstc_gettarget(opts);
        dstc_emit(c, sm, (localindex << 16) | (retslot.index << 8) | DOP_CALL);
    }
    dstc_postread(c, ctor, localindex);
    return retslot;
}

static DstSlot dstc_tablector(DstFopts opts, DstCFunction cfun) {
    DstCompiler *c = opts.compiler;
    const Dst *sm = opts.sourcemap;
    DstSlot ctor, retslot;
    SlotMap *sms;
    int32_t localindex;
    sms = toslotskv(opts);
    pushslots(opts, sms);
    freeslots(opts, sms);
    ctor = dstc_cslot(dst_wrap_cfunction(cfun));
    localindex = dstc_preread(c, sm, 0xFF, 1, ctor);
    if (opts.flags & DST_FOPTS_TAIL) {
        dstc_emit(c, sm, (localindex << 8) | DOP_TAILCALL);
        retslot = dstc_cslot(dst_wrap_nil());
        retslot.flags = DST_SLOT_RETURNED;
    } else {
        retslot = dstc_gettarget(opts);
        dstc_emit(c, sm, (localindex << 16) | (retslot.index << 8) | DOP_CALL);
    }
    dstc_postread(c, ctor, localindex);
    return retslot;
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
    DstSlot s;

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
    s = dstc_value(fopts);

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
