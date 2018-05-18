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
#include <dst/dstparse.h>
#include "compile.h"
#include <headerlibs/strbinsearch.h>
#include <headerlibs/vector.h>

DstSlot dstc_quote(DstFopts opts, DstAst *ast, int32_t argn, const Dst *argv) {
    if (argn != 1) {
        dstc_cerror(opts.compiler, ast, "expected 1 argument");
        return dstc_cslot(dst_wrap_nil());
    }
    return dstc_cslot(dst_ast_unwrap(argv[0]));
}

DstSlot dstc_astquote(DstFopts opts, DstAst *ast, int32_t argn, const Dst *argv) {
    if (argn != 1) {
        dstc_cerror(opts.compiler, ast, "expected 1 argument");
        return dstc_cslot(dst_wrap_nil());
    }
    return dstc_cslot(argv[0]);
}

static void destructure(DstCompiler *c, Dst left, DstSlot right,
        void (*leaf)(DstCompiler *c,
            DstAst *ast,
            const uint8_t *sym,
            DstSlot s,
            DstTable *attr),
        DstTable *attr) {
    DstAst *ast = dst_ast_node(left);
    left = dst_ast_unwrap1(left);
    switch (dst_type(left)) {
        default:
            dstc_cerror(c, ast, "unexpected type in destructuring");
            break;
        case DST_SYMBOL:
            /* Leaf, assign right to left */
            leaf(c, ast, dst_unwrap_symbol(left), right, attr);
            break;
        case DST_TUPLE:
        case DST_ARRAY:
            {
                int32_t i, len, localright, localsub;
                len = dst_length(left);
                for (i = 0; i < len; i++) {
                    DstSlot newright;
                    Dst subval = dst_getindex(left, i);
                    localright = dstc_preread(c, ast, 0xFF, 1, right);
                    localsub = dstc_lslotn(c, 0xFF, 3);
                    if (i < 0x100) {
                        dstc_emit(c, ast,
                                (i << 24) |
                                (localright << 16) |
                                (localsub << 8) |
                                DOP_GET_INDEX);
                    } else {
                        DstSlot islot = dstc_cslot(dst_wrap_integer(i));
                        int32_t locali = dstc_preread(c, ast, 0xFF, 2, islot);
                        dstc_emit(c, ast,
                                (locali << 24) |
                                (localright << 16) |
                                (localsub << 8) |
                                DOP_GET);
                        dstc_postread(c, islot, locali);
                    }
                    newright.index = localsub;
                    newright.envindex = -1;
                    newright.constant = dst_wrap_nil();
                    newright.flags = DST_SLOTTYPE_ANY;
                    /* Traverse into the structure */
                    destructure(c, subval, newright, leaf, attr);
                    dstc_postread(c, right, localright);
                }
            }
            /* Free right */
            dstc_freeslot(c, right);
            break;
        case DST_TABLE:
        case DST_STRUCT:
            {
                int32_t localright, localsub;
                const DstKV *kv = NULL;
                while ((kv = dst_next(left, kv))) {
                    DstSlot newright;
                    DstSlot kslot = dstc_cslot(dst_ast_unwrap(kv->key));
                    Dst subval = kv->value;
                    localright = dstc_preread(c, ast, 0xFF, 1, right);
                    localsub = dstc_lslotn(c, 0xFF, 3);
                    int32_t localk = dstc_preread(c, ast, 0xFF, 2, kslot);
                    dstc_emit(c, ast,
                            (localk << 24) |
                            (localright << 16) |
                            (localsub << 8) |
                            DOP_GET);
                    dstc_postread(c, kslot, localk);
                    newright.index = localsub;
                    newright.envindex = -1;
                    newright.constant = dst_wrap_nil();
                    newright.flags = DST_SLOTTYPE_ANY;
                    /* Traverse into the structure */
                    destructure(c, subval, newright, leaf, attr);
                    dstc_postread(c, right, localright);
                }
            }
            /* Free right */
            dstc_freeslot(c, right);
            break;
    }

}

DstSlot dstc_varset(DstFopts opts, DstAst *ast, int32_t argn, const Dst *argv) {
    DstFopts subopts = dstc_fopts_default(opts.compiler);
    DstSlot ret, dest;
    Dst head;
    if (argn != 2) {
        dstc_cerror(opts.compiler, ast, "expected 2 arguments");
        return dstc_cslot(dst_wrap_nil());
    }
    head = dst_ast_unwrap(argv[0]);
    if (!dst_checktype(head, DST_SYMBOL)) {
        dstc_cerror(opts.compiler, ast, "expected symbol");
        return dstc_cslot(dst_wrap_nil());
    }
    dest = dstc_resolve(opts.compiler, ast, dst_unwrap_symbol(head));
    if (!(dest.flags & DST_SLOT_MUTABLE)) {
        dstc_cerror(opts.compiler, ast, "cannot set constant");
        return dstc_cslot(dst_wrap_nil());
    }
    subopts.flags = DST_FOPTS_HINT;
    subopts.hint = dest;
    ret = dstc_value(subopts, argv[1]);
    dstc_copy(opts.compiler, ast, dest, ret);
    return ret;
}

/* Add attributes to a global def or var table */
static DstTable *handleattr(DstCompiler *c, int32_t argn, const Dst *argv) {
    int32_t i;
    DstTable *tab = dst_table(2);
    for (i = 1; i < argn - 1; i++) {
        Dst attr = dst_ast_unwrap1(argv[i]);
        switch (dst_type(attr)) {
            default:
                dstc_cerror(c, dst_ast_node(argv[i]), "could not add metadata to binding");
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

static DstSlot dohead(DstCompiler *c, DstFopts opts, DstAst *ast, Dst *head, int32_t argn, const Dst *argv) {
    DstFopts subopts = dstc_fopts_default(c);
    DstSlot ret;
    if (argn < 2) {
        dstc_cerror(c, ast, "expected at least 2 arguments");
        return dstc_cslot(dst_wrap_nil());
    }
    *head = dst_ast_unwrap1(argv[0]);
    subopts.flags = opts.flags & ~(DST_FOPTS_TAIL | DST_FOPTS_DROP);
    subopts.hint = opts.hint;
    ret = dstc_value(subopts, argv[argn - 1]);
    return ret;
}

/* Def or var a symbol in a local scope */
static DstSlot namelocal(DstCompiler *c, DstAst *ast, Dst head, int32_t flags, DstSlot ret) {
    /* Non root scope, bring to local slot */
    if (ret.flags & DST_SLOT_NAMED ||
            ret.envindex >= 0 ||
            ret.index < 0 ||
            ret.index > 0xFF) {
        /* Slot is not able to be named */
        DstSlot localslot;
        localslot.index = dstc_lsloti(c);
        /* infer type? */
        localslot.flags = flags;
        localslot.envindex = -1;
        localslot.constant = dst_wrap_nil();
        dstc_copy(c, ast, localslot, ret);
        ret = localslot;
    }
    ret.flags |= flags;
    dstc_nameslot(c, dst_unwrap_symbol(head), ret);
    return ret;
}

static void varleaf(
        DstCompiler *c,
        DstAst *ast,
        const uint8_t *sym,
        DstSlot s,
        DstTable *attr) {
    if (dst_v_last(c->scopes).flags & DST_SCOPE_TOP) {
        DstSlot refslot, refarrayslot;
        /* Global var, generate var */
        DstTable *reftab = dst_table(1);
        reftab->proto = attr;
        DstArray *ref = dst_array(1);
        dst_array_push(ref, dst_wrap_nil());
        dst_table_put(reftab, dst_csymbolv(":ref"), dst_wrap_array(ref));
        dst_table_put(c->env, dst_wrap_symbol(sym), dst_wrap_table(reftab));
        refslot = dstc_cslot(dst_wrap_array(ref));
        refarrayslot = refslot;
        refslot.flags |= DST_SLOT_REF | DST_SLOT_NAMED | DST_SLOT_MUTABLE;
        /* Generate code to set ref */
        int32_t refarrayindex = dstc_preread(c, ast, 0xFF, 1, refarrayslot);
        int32_t retindex = dstc_preread(c, ast, 0xFF, 2, s);
        dstc_emit(c, ast,
                (retindex << 16) |
                (refarrayindex << 8) |
                DOP_PUT_INDEX);
        dstc_postread(c, refarrayslot, refarrayindex);
        dstc_postread(c, s, retindex);
    } else {
        namelocal(c, ast, dst_wrap_symbol(sym), DST_SLOT_NAMED | DST_SLOT_MUTABLE, s) ;
    }
}

DstSlot dstc_var(DstFopts opts, DstAst *ast, int32_t argn, const Dst *argv) {
    DstCompiler *c = opts.compiler;
    Dst head;
    DstSlot ret = dohead(c, opts, ast, &head, argn, argv);
    if (dstc_iserr(&opts)) return dstc_cslot(dst_wrap_nil());
    destructure(c, argv[0], ret, varleaf, handleattr(c, argn, argv));
    return dstc_cslot(dst_wrap_nil());
}

static void defleaf(
        DstCompiler *c,
        DstAst *ast,
        const uint8_t *sym,
        DstSlot s,
        DstTable *attr) {
    if (dst_v_last(c->scopes).flags & DST_SCOPE_TOP) {
        DstTable *tab = dst_table(2);
        tab->proto = attr;
        int32_t tableindex, valsymindex, valueindex;
        DstSlot valsym = dstc_cslot(dst_csymbolv(":value"));
        DstSlot tabslot = dstc_cslot(dst_wrap_table(tab));

        /* Add env entry to env */
        dst_table_put(c->env, dst_wrap_symbol(sym), dst_wrap_table(tab));

        /* Put value in table when evaulated */
        tableindex = dstc_preread(c, ast, 0xFF, 1, tabslot);
        valsymindex = dstc_preread(c, ast, 0xFF, 2, valsym);
        valueindex = dstc_preread(c, ast, 0xFF, 3, s);
        dstc_emit(c, ast,
                (valueindex << 24) |
                (valsymindex << 16) |
                (tableindex << 8) |
                DOP_PUT);
        dstc_postread(c, tabslot, tableindex);
        dstc_postread(c, valsym, valsymindex);
        dstc_postread(c, s, valueindex);
    } else {
        namelocal(c, ast, dst_wrap_symbol(sym), DST_SLOT_NAMED, s);
    }
}

DstSlot dstc_def(DstFopts opts, DstAst *ast, int32_t argn, const Dst *argv) {
    DstCompiler *c = opts.compiler;
    Dst head;
    opts.flags &= ~DST_FOPTS_HINT;
    DstSlot ret = dohead(c, opts, ast, &head, argn, argv);
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
DstSlot dstc_if(DstFopts opts, DstAst *ast, int32_t argn, const Dst *argv) {
    DstCompiler *c = opts.compiler;
    int32_t labelr, labeljr, labeld, labeljd, condlocal;
    DstFopts condopts, bodyopts;
    DstSlot cond, left, right, target;
    Dst truebody, falsebody;
    const int tail = opts.flags & DST_FOPTS_TAIL;
    const int drop = opts.flags & DST_FOPTS_DROP;

    if (argn < 2 || argn > 3) {
        dstc_cerror(c, ast, "expected 2 or 3 arguments to if");
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
        dstc_scope(c, 0);
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
    condlocal = dstc_preread(c, ast, 0xFF, 1, cond);
    labeljr = dst_v_count(c->buffer);
    dstc_emit(c, ast, DOP_JUMP_IF_NOT | (condlocal << 8));
    dstc_postread(c, cond, condlocal);
    dstc_freeslot(c, cond);

    /* Condition left body */
    dstc_scope(c, 0);
    left = dstc_value(bodyopts, truebody);
    if (!drop && !tail) dstc_copy(c, ast, target, left);
    dstc_popscope(c);

    /* Compile jump to done */
    labeljd = dst_v_count(c->buffer);
    if (!tail) dstc_emit(c, ast, DOP_JUMP);

    /* Compile right body */
    labelr = dst_v_count(c->buffer);
    dstc_scope(c, 0);
    right = dstc_value(bodyopts, falsebody);
    if (!drop && !tail) dstc_copy(c, ast, target, right);
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
DstSlot dstc_do(DstFopts opts, DstAst *ast, int32_t argn, const Dst *argv) {
    int32_t i;
    DstSlot ret = dstc_cslot(dst_wrap_nil());
    DstCompiler *c = opts.compiler;
    DstFopts subopts = dstc_fopts_default(c);
    (void) ast;
    dstc_scope(c, 0);
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
DstSlot dstc_while(DstFopts opts, DstAst *ast, int32_t argn, const Dst *argv) {
    DstCompiler *c = opts.compiler;
    DstSlot cond;
    DstFopts subopts = dstc_fopts_default(c);
    int32_t condlocal, labelwt, labeld, labeljt, labelc, i;
    int infinite = 0;

    if (argn < 2) {
        dstc_cerror(c, ast, "expected at least 2 arguments");
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

    dstc_scope(c, 0);

    /* Infinite loop does not need to check condition */
    if (!infinite) {
        condlocal = dstc_preread(c, ast, 0xFF, 1, cond);
        labelc = dst_v_count(c->buffer);
        dstc_emit(c, ast, DOP_JUMP_IF_NOT | (condlocal << 8));
        dstc_postread(c, cond, condlocal);
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
    dstc_emit(c, ast, DOP_JUMP);

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

DstSlot dstc_fn(DstFopts opts, DstAst *ast, int32_t argn, const Dst *argv) {
    DstCompiler *c = opts.compiler;
    DstFuncDef *def;
    DstSlot ret;
    Dst head, paramv;
    int32_t paramcount, argi, parami, arity, localslot, defindex;
    DstFopts subopts = dstc_fopts_default(c);
    const Dst *params;
    int varargs = 0;
    int selfref = 0;

    if (argn < 2) {
        dstc_cerror(c, ast, "expected at least 2 arguments to function literal");
        return dstc_cslot(dst_wrap_nil());
    }

    /* Begin function */
    dstc_scope(c, DST_SCOPE_FUNCTION);

    /* Read function parameters */
    parami = 0;
    arity = 0;
    head = dst_ast_unwrap1(argv[0]);
    if (dst_checktype(head, DST_SYMBOL)) {
        selfref = 1;
        parami = 1;
    }
    if (parami >= argn) {
        dstc_cerror(c, dst_ast_node(argv[0]), "expected function parameters");
        return dstc_cslot(dst_wrap_nil());
    }
    paramv = dst_ast_unwrap(argv[parami]);
    if (dst_seq_view(paramv, &params, &paramcount)) {
        int32_t i;
        for (i = 0; i < paramcount; i++) {
            Dst param = dst_ast_unwrap1(params[i]);
            if (dst_checktype(param, DST_SYMBOL)) {
                DstSlot slot;
                /* Check for varargs */
                if (0 == dst_cstrcmp(dst_unwrap_symbol(param), "&")) {
                    if (i != paramcount - 2) {
                        dstc_cerror(c, dst_ast_node(params[i]), "variable argument symbol in unexpected location");
                        return dstc_cslot(dst_wrap_nil());
                    }
                    varargs = 1;
                    arity--;
                    continue;
                }
                slot.flags = DST_SLOT_NAMED;
                slot.envindex = -1;
                slot.constant = dst_wrap_nil();
                slot.index = dstc_lsloti(c);
                dstc_nameslot(c, dst_unwrap_symbol(param), slot);
            } else {
                DstSlot s;
                s.envindex = -1;
                s.flags = DST_SLOTTYPE_ANY;
                s.constant = dst_wrap_nil();
                s.index = dstc_lsloti(c);
                destructure(c, param, s, defleaf, NULL);
            }
            arity++;
        }
    } else {
        dstc_cerror(c, ast, "expected function parameters");
        return dstc_cslot(dst_wrap_nil());
    }

    /* Check for self ref */
    if (selfref) {
        DstSlot slot;
        slot.envindex = -1;
        slot.flags = DST_SLOT_NAMED | DST_FUNCTION;
        slot.constant = dst_wrap_nil();
        slot.index = dstc_lsloti(c);
        dstc_emit(c, ast, (slot.index << 8) | DOP_LOAD_SELF);
        dstc_nameslot(c, dst_unwrap_symbol(head), slot);
    }

    /* Compile function body */
    if (parami + 1 == argn) {
        dstc_emit(c, ast, DOP_RETURN_NIL);
    } else for (argi = parami + 1; argi < argn; argi++) {
        DstSlot s;
        subopts.flags = argi == (argn - 1) ? DST_FOPTS_TAIL : DST_FOPTS_DROP;
        s = dstc_value(subopts, argv[argi]);
        dstc_freeslot(c, s);
        if (dstc_iserr(&opts)) return dstc_cslot(dst_wrap_nil());
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
    dstc_emit(c, ast,
            (defindex << 16) |
            (localslot << 8) |
            DOP_CLOSURE);

    if (ret.index != localslot) {
        dstc_emit(c, ast,
                (ret.index << 16) |
                (localslot << 8) |
                DOP_MOVE_FAR);
    }

    return ret;
}

/* Keep in lexicographic order */
static const DstSpecial dstc_specials[] = {
    {":=", dstc_varset},
    {"ast-quote", dstc_astquote},
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

