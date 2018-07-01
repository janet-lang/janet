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
#include <dst/dstcompile.h>
#include "compile.h"
#include <headerlibs/strbinsearch.h>
#include <headerlibs/vector.h>
#include "emit.h"

DstSlot dstc_quote(DstFopts opts, int32_t argn, const Dst *argv) {
    if (argn != 1) {
        dstc_cerror(opts.compiler, "expected 1 argument");
        return dstc_cslot(dst_wrap_nil());
    }
    return dstc_cslot(argv[0]);
}

/* Preform destructuring. Be careful to
 * keep the order registers are freed. */
static void destructure(DstCompiler *c,
        Dst left,
        DstSlot right,
        void (*leaf)(DstCompiler *c,
            const uint8_t *sym,
            DstSlot s,
            DstTable *attr),
        DstTable *attr) {
    switch (dst_type(left)) {
        default:
            dstc_cerror(c, "unexpected type in destructuring");
            break;
        case DST_SYMBOL:
            /* Leaf, assign right to left */
            leaf(c, dst_unwrap_symbol(left), right, attr);
            break;
        case DST_TUPLE:
        case DST_ARRAY:
            {
                int32_t i, len, right_register, subval_register;
                const Dst *values;
                dst_seq_view(left, &values, &len);
                for (i = 0; i < len; i++) {
                    DstSlot nextright;
                    Dst subval = values[i];
                    right_register = dstc_to_tempreg(c, right, DSTC_REGTEMP_0);
                    subval_register = dstc_getreg_temp(c, DSTC_REGTEMP_1);
                    if (i < 0x100) {
                        dstc_emit(c, DOP_GET_INDEX |
                                (subval_register << 8) |
                                (right_register << 16) |
                                (i << 24));
                    } else {
                        DstSlot islot = dstc_cslot(dst_wrap_integer(i));
                        int32_t i_register = dstc_to_tempreg(c, islot, DSTC_REGTEMP_2);
                        dstc_emit(c, DOP_GET_INDEX |
                                (subval_register << 8) |
                                (right_register << 16) |
                                (i_register << 24));
                        dstc_free_reg(c, islot, i_register);
                    }
                    nextright.index = subval_register;
                    nextright.envindex = -1;
                    nextright.constant = dst_wrap_nil();
                    nextright.flags = DST_SLOTTYPE_ANY;
                    destructure(c, subval, nextright, leaf, attr);
                    /* Free right_register AFTER sub destructuring */
                    dstc_free_reg(c, right, right_register);
                }
            }
            /* Free right */
            dstc_freeslot(c, right);
            break;
        case DST_TABLE:
        case DST_STRUCT:
            {
                int32_t right_register, subval_register, k_register;
                const DstKV *kv = NULL;
                while ((kv = dstc_next(left, kv))) {
                    DstSlot nextright;
                    DstSlot kslot = dstc_value(dstc_fopts_default(c), kv->key);

                    right_register = dstc_to_tempreg(c, right, DSTC_REGTEMP_0);
                    subval_register = dstc_getreg_temp(c, DSTC_REGTEMP_1);
                    k_register = dstc_to_tempreg(c, kslot, DSTC_REGTEMP_2);
                    dstc_emit(c, DOP_GET |
                            (subval_register << 8) |
                            (right_register << 16) |
                            (k_register << 24));
                    dstc_free_reg(c, kslot, k_register);
                    nextright.index = subval_register;
                    nextright.envindex = -1;
                    nextright.constant = dst_wrap_nil();
                    nextright.flags = DST_SLOTTYPE_ANY;
                    destructure(c, kv->value, nextright, leaf, attr);
                    /* Free right_register AFTER sub destructuring */
                    dstc_free_reg(c, right, right_register);
                }
            }
            /* Free right */
            dstc_freeslot(c, right);
            break;
    }

}

DstSlot dstc_varset(DstFopts opts, int32_t argn, const Dst *argv) {
    DstFopts subopts = dstc_fopts_default(opts.compiler);
    DstSlot ret, dest;
    Dst head;
    if (argn != 2) {
        dstc_cerror(opts.compiler, "expected 2 arguments");
        return dstc_cslot(dst_wrap_nil());
    }
    head = argv[0];
    if (!dst_checktype(head, DST_SYMBOL)) {
        dstc_cerror(opts.compiler, "expected symbol");
        return dstc_cslot(dst_wrap_nil());
    }
    dest = dstc_resolve(opts.compiler, dst_unwrap_symbol(head));
    if (!(dest.flags & DST_SLOT_MUTABLE)) {
        dstc_cerror(opts.compiler, "cannot set constant");
        return dstc_cslot(dst_wrap_nil());
    }
    subopts.flags = DST_FOPTS_HINT;
    subopts.hint = dest;
    ret = dstc_value(subopts, argv[1]);
    dstc_copy(opts.compiler, dest, ret);
    return ret;
}

/* Add attributes to a global def or var table */
static DstTable *handleattr(DstCompiler *c, int32_t argn, const Dst *argv) {
    int32_t i;
    DstTable *tab = dst_table(2);
    for (i = 1; i < argn - 1; i++) {
        Dst attr = argv[i];
        switch (dst_type(attr)) {
            default:
                dstc_cerror(c, "could not add metadata to binding");
                break;
            case DST_SYMBOL:
                dst_table_put(tab, attr, dst_wrap_true());
                break;
            case DST_STRING:
                dst_table_put(tab, dst_csymbolv("doc"), attr);
                break;
        }
    }
    return tab;
}

static DstSlot dohead(DstCompiler *c, DstFopts opts, Dst *head, int32_t argn, const Dst *argv) {
    DstFopts subopts = dstc_fopts_default(c);
    DstSlot ret;
    if (argn < 2) {
        dstc_cerror(c, "expected at least 2 arguments");
        return dstc_cslot(dst_wrap_nil());
    }
    *head = argv[0];
    subopts.flags = opts.flags & ~(DST_FOPTS_TAIL | DST_FOPTS_DROP);
    subopts.hint = opts.hint;
    ret = dstc_value(subopts, argv[argn - 1]);
    return ret;
}

/* Def or var a symbol in a local scope */
static DstSlot namelocal(DstCompiler *c, Dst head, int32_t flags, DstSlot ret) {
    /* Non root scope, bring to local slot */
    if (ret.flags & DST_SLOT_NAMED ||
            ret.envindex >= 0 ||
            ret.index < 0 ||
            ret.index > 0xFF) {
        /* Slot is not able to be named */
        DstSlot localslot;
        localslot.index = dstc_getreg(c);
        /* infer type? */
        localslot.flags = flags;
        localslot.envindex = -1;
        localslot.constant = dst_wrap_nil();
        dstc_copy(c, localslot, ret);
        ret = localslot;
    }
    ret.flags |= flags;
    dstc_nameslot(c, dst_unwrap_symbol(head), ret);
    return ret;
}

static void varleaf(
        DstCompiler *c,
        const uint8_t *sym,
        DstSlot s,
        DstTable *attr) {
    if (c->scope->flags & DST_SCOPE_TOP) {
        /* Global var, generate var */
        DstSlot refslot;
        DstTable *reftab = dst_table(1);
        reftab->proto = attr;
        DstArray *ref = dst_array(1);
        dst_array_push(ref, dst_wrap_nil());
        dst_table_put(reftab, dst_csymbolv(":ref"), dst_wrap_array(ref));
        dst_table_put(c->env, dst_wrap_symbol(sym), dst_wrap_table(reftab));
        refslot = dstc_cslot(dst_wrap_array(ref));
        dstc_emit_ssu(c, DOP_PUT_INDEX, refslot, s, 0);
    } else {
        namelocal(c, dst_wrap_symbol(sym), DST_SLOT_NAMED | DST_SLOT_MUTABLE, s) ;
    }
}

DstSlot dstc_var(DstFopts opts, int32_t argn, const Dst *argv) {
    DstCompiler *c = opts.compiler;
    Dst head;
    DstSlot ret = dohead(c, opts, &head, argn, argv);
    if (dstc_iserr(&opts)) return dstc_cslot(dst_wrap_nil());
    destructure(c, argv[0], ret, varleaf, handleattr(c, argn, argv));
    return dstc_cslot(dst_wrap_nil());
}

static void defleaf(
        DstCompiler *c,
        const uint8_t *sym,
        DstSlot s,
        DstTable *attr) {
    if (c->scope->flags & DST_SCOPE_TOP) {
        DstTable *tab = dst_table(2);
        tab->proto = attr;
        DstSlot valsym = dstc_cslot(dst_csymbolv(":value"));
        DstSlot tabslot = dstc_cslot(dst_wrap_table(tab));

        /* Add env entry to env */
        dst_table_put(c->env, dst_wrap_symbol(sym), dst_wrap_table(tab));

        /* Put value in table when evaulated */
        dstc_emit_sss(c, DOP_PUT, tabslot, valsym, s);
    } else {
        namelocal(c, dst_wrap_symbol(sym), DST_SLOT_NAMED, s);
    }
}

DstSlot dstc_def(DstFopts opts, int32_t argn, const Dst *argv) {
    DstCompiler *c = opts.compiler;
    Dst head;
    opts.flags &= ~DST_FOPTS_HINT;
    DstSlot ret = dohead(c, opts, &head, argn, argv);
    if (dstc_iserr(&opts)) return dstc_cslot(dst_wrap_nil());
    destructure(c, argv[0], ret, defleaf, handleattr(c, argn, argv));
    return dstc_cslot(dst_wrap_nil());
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
    int32_t labelr, labeljr, labeld, labeljd;
    DstFopts condopts, bodyopts;
    DstSlot cond, left, right, target;
    Dst truebody, falsebody;
    DstScope tempscope;
    const int tail = opts.flags & DST_FOPTS_TAIL;
    const int drop = opts.flags & DST_FOPTS_DROP;

    if (argn < 2 || argn > 3) {
        dstc_cerror(c, "expected 2 or 3 arguments to if");
        return dstc_cslot(dst_wrap_nil());
    }

    /* Get the bodies of the if expression */
    truebody = argv[1];
    falsebody = argn > 2 ? argv[2] : dst_wrap_nil();

    /* Get options */
    condopts = dstc_fopts_default(c);
    bodyopts = opts;

    /* Compile condition */
    cond = dstc_value(condopts, argv[0]);

    /* Check constant condition. */
    /* TODO: Use type info for more short circuits */
    if (cond.flags & DST_SLOT_CONSTANT) {
        if (!dst_truthy(cond.constant)) {
            /* Swap the true and false bodies */
            Dst temp = falsebody;
            falsebody = truebody;
            truebody = temp;
        }
        dstc_scope(&tempscope, c, 0, "if-body");
        target = dstc_value(bodyopts, truebody);
        dstc_popscope(c);
        dstc_throwaway(bodyopts, falsebody);
        return target;
    }

    /* Set target for compilation */
    target = (drop || tail)
        ? dstc_cslot(dst_wrap_nil())
        : dstc_gettarget(opts);

    /* Compile jump to right */
    labeljr = dstc_emit_si(c, DOP_JUMP_IF_NOT, cond, 0);

    /* Condition left body */
    dstc_scope(&tempscope, c, 0, "if-true");
    left = dstc_value(bodyopts, truebody);
    if (!drop && !tail) dstc_copy(c, target, left);
    dstc_popscope(c);

    /* Compile jump to done */
    labeljd = dst_v_count(c->buffer);
    if (!tail) dstc_emit(c, DOP_JUMP);

    /* Compile right body */
    labelr = dst_v_count(c->buffer);
    dstc_scope(&tempscope, c, 0, "if-false");
    right = dstc_value(bodyopts, falsebody);
    if (!drop && !tail) dstc_copy(c, target, right);
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
    DstSlot ret = dstc_cslot(dst_wrap_nil());
    DstCompiler *c = opts.compiler;
    DstFopts subopts = dstc_fopts_default(c);
    DstScope tempscope;
    dstc_scope(&tempscope, c, 0, "do");
    for (i = 0; i < argn; i++) {
        if (i != argn - 1) {
            subopts.flags = DST_FOPTS_DROP;
        } else {
            subopts = opts;
        }
        ret = dstc_value(subopts, argv[i]);
        if (i != argn - 1) {
            dstc_freeslot(c, ret);
        }
    }
    dstc_popscope_keepslot(c, ret);
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
    DstSlot cond;
    DstFopts subopts = dstc_fopts_default(c);
    DstScope tempscope;
    int32_t labelwt, labeld, labeljt, labelc, i;
    int infinite = 0;

    if (argn < 2) {
        dstc_cerror(c, "expected at least 2 arguments");
        return dstc_cslot(dst_wrap_nil());
    }

    labelwt = dst_v_count(c->buffer);

    /* Compile condition */
    cond = dstc_value(subopts, argv[0]);

    /* Check for constant condition */
    if (cond.flags & DST_SLOT_CONSTANT) {
        /* Loop never executes */
        if (!dst_truthy(cond.constant)) {
            return dstc_cslot(dst_wrap_nil());
        }
        /* Infinite loop */
        infinite = 1;
    }

    dstc_scope(&tempscope, c, 0, "while");

    /* Infinite loop does not need to check condition */
    if (!infinite) {
        labelc = dstc_emit_si(c, DOP_JUMP_IF_NOT, cond, 0);
    } else {
        labelc = 0;
    }

    /* Compile body */
    for (i = 1; i < argn; i++) {
        subopts.flags = DST_FOPTS_DROP;
        dstc_freeslot(c, dstc_value(subopts, argv[i]));
    }

    /* Compile jump to whiletop */
    labeljt = dst_v_count(c->buffer);
    dstc_emit(c, DOP_JUMP);

    /* Calculate jumps */
    labeld = dst_v_count(c->buffer);
    if (!infinite) c->buffer[labelc] |= (labeld - labelc) << 16;
    c->buffer[labeljt] |= (labelwt - labeljt) << 8;

    /* Pop scope and return nil slot */
    dstc_popscope(c);

    return dstc_cslot(dst_wrap_nil());
}

/* Add a funcdef to the top most function scope */
static int32_t dstc_addfuncdef(DstCompiler *c, DstFuncDef *def) {
    DstScope *scope = c->scope;
    while (scope) {
        if (scope->flags & DST_SCOPE_FUNCTION)
            break;
        scope = scope->parent;
    }
    dst_assert(scope, "could not add funcdef");
    dst_v_push(scope->defs, def);
    return dst_v_count(scope->defs) - 1;
}

DstSlot dstc_fn(DstFopts opts, int32_t argn, const Dst *argv) {
    DstCompiler *c = opts.compiler;
    DstFuncDef *def;
    DstSlot ret;
    Dst head, paramv;
    DstScope fnscope;
    int32_t paramcount, argi, parami, arity, localslot, defindex;
    DstFopts subopts = dstc_fopts_default(c);
    const Dst *params;
    const char *errmsg = NULL;
    int varargs = 0;
    int selfref = 0;

    /* Begin function */
    dstc_scope(&fnscope, c, DST_SCOPE_FUNCTION, "function");

    if (argn < 2) {
        errmsg = "expected at least 2 arguments to function literal";
        goto error;
    }

    /* Read function parameters */
    parami = 0;
    arity = 0;
    head = argv[0];
    if (dst_checktype(head, DST_SYMBOL)) {
        selfref = 1;
        parami = 1;
    }
    if (parami >= argn) {
        errmsg = "expected function parameters";
        goto error;
    }
    paramv = argv[parami];
    if (dst_seq_view(paramv, &params, &paramcount)) {
        int32_t i;
        for (i = 0; i < paramcount; i++) {
            Dst param = params[i];
            if (dst_checktype(param, DST_SYMBOL)) {
                /* Check for varargs */
                if (0 == dst_cstrcmp(dst_unwrap_symbol(param), "&")) {
                    if (i != paramcount - 2) {
                        errmsg = "variable argument symbol in unexpected location";
                        goto error;
                    }
                    varargs = 1;
                    arity--;
                    continue;
                }
                dstc_nameslot(c, dst_unwrap_symbol(param), dstc_farslot(c));
            } else {
                destructure(c, param, dstc_farslot(c), defleaf, NULL);
            }
            arity++;
        }
    } else {
        errmsg = "expected function parameters";
        goto error;
    }

    /* Check for self ref */
    if (selfref) {
        DstSlot slot = dstc_farslot(c);
        slot.flags = DST_SLOT_NAMED | DST_FUNCTION;
        dstc_emit_s(c, DOP_LOAD_SELF, slot);
        dstc_nameslot(c, dst_unwrap_symbol(head), slot);
    }

    /* Compile function body */
    if (parami + 1 == argn) {
        dstc_emit(c, DOP_RETURN_NIL);
    } else for (argi = parami + 1; argi < argn; argi++) {
        DstSlot s;
        subopts.flags = (argi == (argn - 1)) ? DST_FOPTS_TAIL : DST_FOPTS_DROP;
        s = dstc_value(subopts, argv[argi]);
        dstc_freeslot(c, s);
        if (dstc_iserr(&opts))
            goto error2;
    }

    /* Build function */
    def = dstc_pop_funcdef(c);
    def->arity = arity;
    if (varargs) def->flags |= DST_FUNCDEF_FLAG_VARARG;
    if (selfref) def->name = dst_unwrap_symbol(head);
    defindex = dstc_addfuncdef(c, def);

    /* Ensure enough slots for vararg function. */
    if (arity + varargs > def->slotcount) def->slotcount = arity + varargs;

    /* Instantiate closure */
    ret = dstc_gettarget(opts);

    localslot = ret.index > 0xF0 ? 0xF1 : ret.index;
    dstc_emit(c,
            (defindex << 16) |
            (localslot << 8) |
            DOP_CLOSURE);

    if (ret.index != localslot) {
        dstc_emit(c,
                (ret.index << 16) |
                (localslot << 8) |
                DOP_MOVE_FAR);
    }

    return ret;

error:
    dstc_cerror(c, errmsg);
error2:
    dstc_popscope(c);
    return dstc_cslot(dst_wrap_nil());
}

/* Keep in lexicographic order */
static const DstSpecial dstc_specials[] = {
    {":=", dstc_varset},
    {"def", dstc_def},
    {"do", dstc_do},
    {"fn", dstc_fn},
    {"if", dstc_if},
    {"quote", dstc_quote},
    {"var", dstc_var},
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

