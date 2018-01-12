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

/* Compile a do form. Do forms execute their body sequentially and
 * evaluate to the last expression in the body. */
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

DstSlot dstc_transfer(DstFopts opts, int32_t argn, const Dst *argv) {
    DstCompiler *c = opts.compiler;
    const Dst *sm = opts.sourcemap;
    DstSlot dest, fib, val;
    int32_t destindex, fibindex, valindex;
    (void) argv;
    if (argn > 2) {
        dstc_cerror(c, sm, "expected no more than 2 arguments");
        return dstc_cslot(dst_wrap_nil());
    }
    dest = dstc_gettarget(opts);
    fib = (argn > 0) ? dstc_value(dstc_getindex(opts, 1)) : dstc_cslot(dst_wrap_nil());
    val = (argn > 1) ? dstc_value(dstc_getindex(opts, 2)) : dstc_cslot(dst_wrap_nil());
    destindex = dstc_preread(c, sm, 0xFF, 1, dest);
    fibindex = dstc_preread(c, sm, 0xFF, 2, fib);
    valindex = dstc_preread(c, sm, 0xFF, 3, val);
    dstc_emit(c, sm, 
            (valindex << 24) |
            (fibindex << 16) |
            (destindex << 8) |
            DOP_TRANSFER);
    dstc_postread(c, dest, destindex);
    dstc_postread(c, fib, fibindex);
    dstc_postread(c, val, valindex);
    dstc_freeslot(c, fib);
    dstc_freeslot(c, val);
    return dest;
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

    /* Ensure enough slots for vararg function. */
    if (arity + varargs > def->slotcount) def->slotcount = arity + varargs;

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
    {"transfer", dstc_transfer},
    {"var", dstc_var},
    {"varset!", dstc_varset},
    {"while", dstc_while}
};

/* Find a special */
const DstSpecial *dstc_special(const uint8_t *name) {
    return dst_strbinsearch(
            &dstc_specials,
            sizeof(dstc_specials)/sizeof(DstSpecial),
            sizeof(DstSpecial),
            name);
}

