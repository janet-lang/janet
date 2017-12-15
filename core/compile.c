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

static void dst_compile_cleanup(DstCompiler *c) {

}

DstSlot dst_compile_error(DstCompiler *c, const DstValue *sourcemap, const uint8_t *m) {
    DstSlot ret;
    c->error_start = dst_unwrap_integer(sourcemap[0]);
    c->error_end = dst_unwrap_integer(sourcemap[1]);
    c->error = m;
    ret.flags = DST_SLOT_ERROR;
    ret.index = 0;
    ret.constant = dst_wrap_nil();
    return ret;
}

DstSlot dst_compile_cerror(DstCompiler *c, const DstValue *sourcemap, const char *m) {
    return dst_compile_error(c, sourcemap, dst_cstring(m));
}

/* Use these to get sub options. They will traverse the source map so
 * compiler errors make sense. Then modify the returned options. */
DstFormOptions dst_compile_getopts_index(DstFormOptions opts, int32_t index) {
    DstCompiler *c = opts.compiler;
    const DstValue *sourcemap = dst_parse_submap_index(opts.sourcemap, index);
    DstValue nextval = dst_getindex(opts.x, index);
    opts.x = nextval;
    opts.sourcemap = sourcemap;
    return opts;
}

DstFormOptions dst_compile_getopts_key(DstFormOptions opts, DstValue key) {
    DstCompiler *c = opts.compiler;
    const DstValue *sourcemap = dst_parse_submap_key(opts.sourcemap, key);
    opts.x = key;
    opts.sourcemap = sourcemap;
    return opts;
}

DstFormOptions dst_compile_getopts_value(DstFormOptions opts, DstValue key) {
    DstCompiler *c = opts.compiler;
    const DstValue *sourcemap = dst_parse_submap_value(opts.sourcemap, key);
    DstValue nextval = dst_get(opts.x, key);
    opts.x = nextval;
    opts.sourcemap = sourcemap;
    return opts;
}

/* Eneter a new scope */
void dst_compile_scope(DstCompiler *c, int newfn) {
    DstScope *scope;
    if (c->scopecap < c->scopecount) {
        c->scopes = realloc(c->scopes, 2 * sizeof(DstScope) * c->scopecount + 2);
        if (NULL == c->scope) {
            DST_OUT_OF_MEMORY;
        }
    }
    scope = c->scopes + c->scopecount++;
    dst_array_init(&scope->constants, 0);
    dst_table_init(&scope->symbols, 4);

    scope->envs = NULL;
    scope->envcount = 0;
    scope->envcap = 0;

    scope->slots = NULL;
    scope->slotcount = 0;
    scope->slotcap = 0;

    scope->freeslots = NULL;
    scope->freeslotcount = 0;
    scope->freeslotcap = 0;

    scope->buffer_offset = c->buffer.count;
    scope->nextslot = 0;
    scope->lastslot = -1;
    scope->flags = newfn ? DST_SCOPE_FUNCTION : 0;
}

DstSlot dst_slot_nil() {
    DstSlot ret;
    ret.index = 0;
    ret.flags = (1 << DST_TYPE_NIL) | DST_SLOT_CONSTANT;
    ret.constant = dst_wrap_nil();
    return ret;
}

/* Leave a scope.  Does not build closure*/
void dst_compile_popscope(DstCompiler *c) {
    DstScope *scope;
    DstSlot ret;
    dst_assert(c->scopecount, "could not pop scope");
    scope = c->scopes + --c->scopecount;
    /* Move free slots to parent scope if not a new function */
    if (!(scope->flags & DST_SCOPE_FUNCTION) && c->scopecount) {
        int32_t i;
        int32_t newcount;
        DstScope *topscope = c->scopes + c->scopecount - 1;
        topscope->nextslot = scope->nextslot; 
        newcount = topscope->freeslotcount + scope->freeslotcount;
        if (topscope->freeslotcap < newcount) {
            topscope->freeslots = realloc(topscope->freeslot, sizeof(int32_t) * newcount);
            if (NULL == topscope->freeslots) {
                DST_OUT_OF_MEMORY;
            }
            topscope->freeslotcap = newcount;
        }
        memcpy(
            topscope->freeslots + topescope->freeslotcount,
            scope->freeslots,
            sizeof(int32_t) * scope->freeslotcount);
        topscope->freeslotcount = newcount;
    }
    dst_table_deinit(&scope->symbols);
    dst_array_deinit(&scope->constants);
    free(scope->slots);
    free(scope->freeslots);
    free(scope->envs);
    return ret;
}

#define dst_compile_topscope(c) ((c)->scopes + (c)->scopecount - 1)

/* Allocate a slot space */
static int32_t dst_compile_slotalloc(DstCompiler *c) {
    DstScope *scope = dst_compile_topscope(c);
    if (scope->freeslotcount == 0) {
        return scope->nextslot++;
    } else {
        return scope->freeslots[--scope->freeslotcount]; 
    }
}

int dst_compile_slotmatch(DstFormOptions opts, DstSlot slot) {
    return opts.type & slot.type;
}

DstSlot dst_compile_normalslot(DstCompiler *c, uint32_t flags) {
    DstSlot ret;
    int32_t index = dst_compile_slotalloc(c);
    ret.flags = flags;
    ret.constant = dst_wrap_nil();
    ret.index = index;
    ret.envindex = 0;
    return ret;
}

DstSlot dst_compile_constantslot(DstCompiler *c, DstValue x) {
    DstSlot ret;
    ret.flags = (1 << dst_type(x)) | DST_SLOT_CONSTANT;
    ret.index = -1;
    ret.constant = x;
    ret.envindex = 0;
    return ret;
}

/* Free a single slot */
void dst_compile_freeslot(DstCompiler *c, DstSlot slot) {
    DstScope *scope = dst_compile_topscope(c);
    int32_t newcount = scope->freeslotcount + 1;
    if (slot.flags & (DST_SLOT_CONSTANT | DST_SLOT_ERROR))
        return;
    if (scope->freeslotcap < newcount) {
        int32_t newcap = 2 * newcount;
        scope->freeslots = realloc(scope->freeslots, sizeof(int32_t) * newcap);
        if (NULL == scope->freeslots) {
            DST_OUT_OF_MEMORY;
        }
        scope->freeslotcap = newcap;
    }
    scope->freeslots[scope->freeslotcount] = slot.index;
    scope->freeslotcount = newcount;
}

/* Free an array of slots */
void dst_compile_freeslotarray(DstCompiler *c, DstArray *slots) {
    int32_t i;
    for (i = 0; i < slots->count; i++) {
        dst_compile_freeslot(c, slots->data[i]);
    }
}

/*
 * The mechanism for passing environments to to closures is a bit complicated,
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

    DstSlot ret;
    DstScope *scope = dst_compile_topscope(c);
    int32_t env_index = 0;
    int foundlocal;

    /* Search scopes for symbol, starting from top */
    while (scope >= c->scopes) {
        DstValue check = dst_table_get(scope->symbols, dst_wrap_symbol(sym));
        if (dst_checktype(check, DST_INTEGER)) {
            ret = scope->slots[dst_unwrap_integer(check)];
            goto found;
        }
        scope--;
    }

    /* Symbol not found */
    return dst_compile_error(c, sourcemap, dst_formatc("unknown symbol %q", sym));

    /* Symbol was found */
    found:

    /* Constants and errors can be returned immediately (they are stateless) */
    if (ret.flags & (DST_SLOT_CONSTANT | DST_SLOT_ERROR))
        return ret;

    /* non-local scope needs to expose its environment */
    foundlocal = scope == dst_compile_topscope(c);
    if (!foundlocal) {
        scope->flags |= DST_SCOPE_ENV;
        if (scope->envcount < 1) {
            scope->envcount = 1;
            scope->envs = malloc(sizeof(int32_t) * 10);
            if (NULL == scope->envs) {
                DST_OUT_OF_MEMORY;
            }
            scope->envcap = 10;
            scope->envs[0] = -1;
        }
        scope++;
    }

    /* Propogate env up to current scope */
    while (scope <= dst_compile_topscope(c)) {
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
        scope++;
    }

    /* Take the slot from the upper scope, and set its envindex before returning. */
    if (!foundlocal) {
        ret.envindex = env_index;
    }

    return ret;
}

/* Compile an array */

/* Compile a single value */
DstSlot dst_compile_value(DstFormOptions opts) {
    DstSlot ret;
    if (opts.compiler->recursion_guard <= 0) {
        return dst_compiler_cerror(opts.compiler, opts.sourcemap, "recursed too deeply");
    }
    opts.compiler->recursion_guard--;
    switch (dst_type(opts.x)) {
        default:
            ret = dst_compile_constantslot(opts.x);
            break;
        case DST_SYMBOL:
            {
                const uint8_t *sym = dst_unwrap_symbol(opts.x);
                if (dst_string_length(sym) > 0 && sym[0] != ':')
                    ret = dst_compile_resolve(opts.compiler, opts.sourcemap, sym);
                else
                    ret = dst_compile_constantslot(opts.x);
                break;
            }
        case DST_TUPLE:
            ret = dst_compile_tuple(opts); 
            break;
        case DST_ARRAY:
            ret = dst_compile_array(opts); 
            break;
        case DST_STRUCT:
            ret = dst_compile_struct(opts); 
            break;
        case DST_TABLE:
            ret = dst_compile_table(opts);
            break;
    }
    opts.compiler->recursion_guard++;
    return ret;
}

DstSlot dst_compile_targetslot(DstFormOptions opts, DstSlot s);

/* Coerce any slot into the target slot. If no target is specified, return
 * the slot unaltered. Otherwise, move and load upvalues as necesarry to set the slot. */
DstSlot dst_compile_coercetargetslot(DstFormOptions opts, DstSlot s);
