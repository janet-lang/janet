/*
* Copyright (c) 2026 Calvin Rose
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

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "gc.h"
#include "util.h"
#include "vector.h"
#endif

/* Bitsets */

/* Embed bitset into array of uint32_ts */
static uint32_t bs_mask(int32_t index) {
    return ((uint32_t)1) << (index & 0x1F);
}
static uint32_t bs_indx(int32_t index) {
    return index >> 5;
}
static void bs_set_bit(uint32_t *bitset, int32_t index) {
    bitset[bs_indx(index)] |= bs_mask(index);
}
static void bs_clear_bit(uint32_t *bitset, int32_t index) {
    bitset[bs_indx(index)] &= ~(bs_mask(index));
}
static int bs_read_bit(uint32_t *bitset, int32_t index) {
    return (bitset[bs_indx(index)] & bs_mask(index)) ? 1 : 0;
}

/* Look up table for instructions */
const enum JanetInstructionType janet_instructions[JOP_INSTRUCTION_COUNT] = {
    JINT_0, /* JOP_NOOP, */
    JINT_S, /* JOP_ERROR, */
    JINT_ST, /* JOP_TYPECHECK, */
    JINT_S, /* JOP_RETURN, */
    JINT_0, /* JOP_RETURN_NIL, */
    JINT_SSI, /* JOP_ADD_IMMEDIATE, */
    JINT_SSS, /* JOP_ADD, */
    JINT_SSI, /* JOP_SUBTRACT_IMMEDIATE, */
    JINT_SSS, /* JOP_SUBTRACT, */
    JINT_SSI, /* JOP_MULTIPLY_IMMEDIATE, */
    JINT_SSS, /* JOP_MULTIPLY, */
    JINT_SSI, /* JOP_DIVIDE_IMMEDIATE, */
    JINT_SSS, /* JOP_DIVIDE, */
    JINT_SSS, /* JOP_DIVIDE_FLOOR */
    JINT_SSS, /* JOP_MODULO, */
    JINT_SSS, /* JOP_REMAINDER, */
    JINT_SSS, /* JOP_BAND, */
    JINT_SSS, /* JOP_BOR, */
    JINT_SSS, /* JOP_BXOR, */
    JINT_SS, /* JOP_BNOT, */
    JINT_SSS, /* JOP_SHIFT_LEFT, */
    JINT_SSI, /* JOP_SHIFT_LEFT_IMMEDIATE, */
    JINT_SSS, /* JOP_SHIFT_RIGHT, */
    JINT_SSI, /* JOP_SHIFT_RIGHT_IMMEDIATE, */
    JINT_SSS, /* JOP_SHIFT_RIGHT_UNSIGNED, */
    JINT_SSU, /* JOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE, */
    JINT_SS, /* JOP_MOVE_FAR, */
    JINT_SS, /* JOP_MOVE_NEAR, */
    JINT_L, /* JOP_JUMP, */
    JINT_SL, /* JOP_JUMP_IF, */
    JINT_SL, /* JOP_JUMP_IF_NOT, */
    JINT_SL, /* JOP_JUMP_IF_NIL, */
    JINT_SL, /* JOP_JUMP_IF_NOT_NIL, */
    JINT_SSS, /* JOP_GREATER_THAN, */
    JINT_SSI, /* JOP_GREATER_THAN_IMMEDIATE, */
    JINT_SSS, /* JOP_LESS_THAN, */
    JINT_SSI, /* JOP_LESS_THAN_IMMEDIATE, */
    JINT_SSS, /* JOP_EQUALS, */
    JINT_SSI, /* JOP_EQUALS_IMMEDIATE, */
    JINT_SSS, /* JOP_COMPARE, */
    JINT_S, /* JOP_LOAD_NIL, */
    JINT_S, /* JOP_LOAD_TRUE, */
    JINT_S, /* JOP_LOAD_FALSE, */
    JINT_SI, /* JOP_LOAD_INTEGER, */
    JINT_SC, /* JOP_LOAD_CONSTANT, */
    JINT_SES, /* JOP_LOAD_UPVALUE, */
    JINT_S, /* JOP_LOAD_SELF, */
    JINT_SES, /* JOP_SET_UPVALUE, */
    JINT_SD, /* JOP_CLOSURE, */
    JINT_S, /* JOP_PUSH, */
    JINT_SS, /* JOP_PUSH_2, */
    JINT_SSS, /* JOP_PUSH_3, */
    JINT_S, /* JOP_PUSH_ARRAY, */
    JINT_SS, /* JOP_CALL, */
    JINT_S, /* JOP_TAILCALL, */
    JINT_SSS, /* JOP_RESUME, */
    JINT_SSU, /* JOP_SIGNAL, */
    JINT_SSS, /* JOP_PROPAGATE */
    JINT_SSS, /* JOP_IN, */
    JINT_SSS, /* JOP_GET, */
    JINT_SSS, /* JOP_PUT, */
    JINT_SSU, /* JOP_GET_INDEX, */
    JINT_SSU, /* JOP_PUT_INDEX, */
    JINT_SS, /* JOP_LENGTH */
    JINT_S, /* JOP_MAKE_ARRAY */
    JINT_S, /* JOP_MAKE_BUFFER */
    JINT_S, /* JOP_MAKE_STRING */
    JINT_S, /* JOP_MAKE_STRUCT */
    JINT_S, /* JOP_MAKE_TABLE */
    JINT_S, /* JOP_MAKE_TUPLE */
    JINT_S, /* JOP_MAKE_BRACKET_TUPLE */
    JINT_SSS, /* JOP_GREATER_THAN_EQUAL */
    JINT_SSS, /* JOP_LESS_THAN_EQUAL */
    JINT_SSS, /* JOP_NEXT */
    JINT_SSS, /* JOP_NOT_EQUALS, */
    JINT_SSI, /* JOP_NOT_EQUALS_IMMEDIATE, */
    JINT_SSS /* JOP_CANCEL, */
};

/* Keep track of how large corelib is. Useful
 * for minimizing code bloat. */
#ifdef JANET_BOOTSTRAP
int64_t total_instruction_count = 0;
int64_t total_optimize_fixpoint_loops = 0;
int64_t total_funcdefs_optimized = 0;
#endif

/* Basic block structure. Only keep forward edges for now. */
typedef struct {
    int32_t start;
    int32_t end;
    int32_t next[2];
} BytecodeBB;

/* Generate a bitmap for all instructions that are leaders
 * of basic blocks. Use this either to construct basic blocks
 * or quickly iterate over them. */
static uint32_t *janet_bytecode_leader_bitmap(JanetFuncDef *def) {
    uint32_t *code = def->bytecode;
    int32_t *codes = (int32_t *)code;
    int32_t len = def->bytecode_length;
    int32_t words = ((len - 1) / 32) + 1;
    uint32_t *leaders = array_allocate(sizeof(uint32_t), words);
    janet_assert(words >= 0, "bad len");
    memset(leaders, 0, (size_t) words * sizeof(uint32_t));
    int did_jump = 1; /* pc=0 is a leader */
    uint32_t target = 0;
    for (int32_t pc = 0; pc < len; pc++) {
        if (did_jump) bs_set_bit(leaders, pc);
        did_jump = 0;
        switch (code[pc] & 0x7F) {
            default:
                continue;
            case JOP_RETURN_NIL:
            case JOP_RETURN:
            case JOP_ERROR:
            case JOP_TAILCALL:
                did_jump = 1;
                continue;
            case JOP_JUMP:
                target = pc + (codes[pc] >> 8);
                bs_set_bit(leaders, target);
                did_jump = 1;
                continue;
            case JOP_JUMP_IF:
            case JOP_JUMP_IF_NIL:
            case JOP_JUMP_IF_NOT:
            case JOP_JUMP_IF_NOT_NIL:
                target = pc + (codes[pc] >> 16);
                bs_set_bit(leaders, target);
                did_jump = 1;
                continue;
        }
    }
    return leaders;
}

/* Generate basic blocks analysis */
static BytecodeBB *janet_bytecode_basic_blocks(JanetFuncDef *def, int32_t *n_blocks) {
    uint32_t *code = def->bytecode;
    int32_t *codes = (int32_t *)code;
    /* Allocate array of blocks to return */
    uint32_t *bitmap = janet_bytecode_leader_bitmap(def);
    int32_t len = def->bytecode_length;
    int32_t words = ((len - 1) / 32) + 1;
    int32_t block_count = 0;
    for (int32_t i = 0; i < words; i++) {
#ifdef JANET_MSVC
        block_count += __popcnt(bitmap[i]);
#else
        block_count += __builtin_popcount(bitmap[i]);
#endif
    }
    BytecodeBB *blocks = array_allocate(sizeof(BytecodeBB), block_count);
    int32_t next_block_i = 0;
    int32_t pc = 0;
    while (pc < len) {
        BytecodeBB bb;
        bb.start = pc;
        uint32_t done;
        do {
            pc++;
            done = (pc >= len) || (bitmap[pc >> 5] & (((uint32_t)1) << (pc & 0x1F)));
        } while (!done);
        janet_assert(pc <= len, "bad block end");
        bb.end = pc;
        bb.next[0] = -1;
        bb.next[1] = -1;
        blocks[next_block_i++] = bb;
    }
    janet_assert(next_block_i == block_count, "bad block count");
    /* add forward cfg edges */
    for (int32_t i = 0; i < block_count; i++) {
        int32_t target;
        janet_assert(blocks[i].end > blocks[i].start, "degenerate block start >= end");
        int32_t lastpc = blocks[i].end - 1;
        switch (code[lastpc] & 0x7F) {
            default:
                blocks[i].next[0] = i + 1; /* blocks sorted by increasing start */
                continue;
            case JOP_ERROR:
            case JOP_RETURN:
            case JOP_RETURN_NIL:
            case JOP_TAILCALL:
                continue;
            case JOP_JUMP_IF:
            case JOP_JUMP_IF_NIL:
            case JOP_JUMP_IF_NOT:
            case JOP_JUMP_IF_NOT_NIL:
                blocks[i].next[1] = i + 1; /* blocks sorted by increasing start */
                target = lastpc + (codes[lastpc] >> 16);
                break;
            case JOP_JUMP:
                target = lastpc + (codes[lastpc] >> 8);
                break;
        }
        /* Find block whose start = target */
        int32_t j;
        for (j = 0; j < block_count; j++) {
            if (blocks[j].start == target) {
                blocks[i].next[0] = j;
                break;
            }
        }
        janet_assert(j < block_count, "did not find next block");
    }
    janet_free(bitmap);
    *n_blocks = block_count;
    return blocks;
}

/*
 * Local value numbering
 */

/* Pair of value number and optional slot */
typedef struct {
    int32_t value_number;
    uint16_t slot;
    int has_slot;
} VN;

/* Keep seen values for replacement in this structure */
typedef struct {
    JanetTable *value_lookup; /* Map instruction templates -> VN */
    int32_t *value_numbers; /* Map slot -> value numbers */
    JanetFuncDef *def;
} VNContext;

/* Reconstitute instructions (re-encode after changing parameters) */
static uint32_t recon_abc(uint32_t opcode, uint32_t A, uint32_t B, uint32_t C) {
    return (0x7F & opcode) |
           ((A & 0xFF) << 8) |
           ((B & 0xFF) << 16) |
           (C << 24);
}
static uint32_t recon_ae(uint32_t opcode, uint32_t A, uint32_t E) {
    return (0x7F & opcode) |
           ((A & 0xFF) << 8) |
           (E << 16);
}
static uint32_t recon_d(uint32_t opcode, uint32_t D) {
    return (0x7F & opcode) |
           (D << 8);
}

static double vn_encode_1arg(uint32_t opcode, int32_t vn) {
    /* Encode 7 bits for opcode 23 bits for vn */
    uint64_t vnu = (uint64_t)(vn + 0x20000);
    uint64_t integer = opcode | (vnu << 7);
    /* integer should be less than 53 significant bits */
    janet_assert(integer <= JANET_INTMAX_DOUBLE, "encoding failure");
    return (double) integer;
}

static double vn_encode_2arg(uint32_t opcode, int32_t vn1, int32_t vn2) {
    /* Encode 7 bits for opcode, 23 bits each for vn1 and vn2 */
    uint64_t vn1u = (uint64_t)(vn1 + 0x20000);
    uint64_t vn2u = (uint64_t)(vn2 + 0x20000);
    uint64_t integer = opcode | (vn1u << 7) | (vn2u << 30);
    /* integer should be at most 53 significant bits */
    janet_assert(integer <= JANET_INTMAX_DOUBLE, "encoding failure");
    return (double) integer;
}

static double vn_encode_vn(VN vn) {
    uint64_t vn64 = (uint64_t)(vn.value_number + 0x20000);
    uint64_t encoded = 0;
    encoded |= vn.slot & 0xFFFF;
    encoded |= vn.has_slot ? 0x10000 : 0;
    encoded |= vn64 << 17;
    janet_assert(encoded <= JANET_INTMAX_DOUBLE, "encoding failure");
    union {
        double d;
        uint64_t u;
    } un;
    un.u = encoded;
    return un.d;
}

static VN vn_decode_vn(double encoded) {
    union {
        uint64_t u;
        double d;
    } un;
    un.d = encoded;
    VN ret;
    ret.has_slot = (un.u & 0x10000) ? 1 : 0;
    ret.slot = un.u & 0xFFFF;
    uint64_t x = ((un.u >> 17) - 0x20000);
    ret.value_number = (int32_t)x;
    return ret;
}

/* Get a value number for a given instruction key */
static VN vn_check_key(VNContext *ctx, double key) {
    VN zero = {0};
    Janet check = janet_table_get(ctx->value_lookup, janet_wrap_number(key));
    if (!janet_checktype(check, JANET_NUMBER)) return zero;
    VN ret = vn_decode_vn(janet_unwrap_number(check));
    /* Ensure the value number is still valid */
    if (ret.has_slot) {
        if (ctx->value_numbers[ret.slot] != ret.value_number) {
            /* Look for a matching slot */
            for (int32_t i = 0; i < ctx->def->slotcount; i++) {
                if (ctx->value_numbers[i] == ret.value_number) {
                    ret.value_number = ctx->value_numbers[i];
                    return ret;
                }
            }
            return zero;
        }
    }
    return ret;
}

/* Generate a replacement instruction for the current one. If we can't get a good replacement, keep current */
static uint32_t vn_move_or_load(VNContext *ctx, uint32_t destination_slot, VN vn, uint32_t current_instruction) {
    if (vn.value_number < 0) {
        /* Constant, can be loaded in one instruction */
        ctx->value_numbers[destination_slot] = vn.value_number;
        if (vn.value_number == -1) return JOP_LOAD_NIL | (destination_slot << 8);
        if (vn.value_number == -2) return JOP_LOAD_FALSE | (destination_slot << 8);
        if (vn.value_number == -3) return JOP_LOAD_TRUE | (destination_slot << 8);
        if (vn.value_number <= -4) {
            if (destination_slot > 255) return current_instruction;
            /* Check me? */
            int32_t integer_constant = (int32_t)(int16_t)(-(vn.value_number + 4));
            return JOP_LOAD_INTEGER | ((destination_slot & 0xFF) << 8) | ((uint32_t)integer_constant << 16);
        }
        return current_instruction;
    } else {
        ctx->value_numbers[destination_slot] = vn.value_number;
        /* Emit a move */
        if (!vn.has_slot) return current_instruction;
        if (destination_slot == vn.slot) return JOP_NOOP;
        if ((destination_slot > 255) && vn.slot > 255) return current_instruction;
        /* Check if slot has been rewritten - if so, just use current instruction */
        if (ctx->value_numbers[vn.slot] != vn.value_number) return current_instruction;
        if (destination_slot > 255) {
            return JOP_MOVE_FAR | (destination_slot << 16) | ((uint32_t) vn.slot << 8);
        } else {
            return JOP_MOVE_NEAR | (destination_slot << 8) | ((uint32_t) vn.slot << 16);
        }
    }
}

/* Find the best slot for an input slot. If we can't find a better replacement, return current slot */
static uint32_t vn_find_reg(VNContext *ctx, int32_t value_number, uint32_t current_slot) {
    if (value_number == 0) return current_slot;
    /* TODO - take the earliest match, not the lowest slot number */
    for (int32_t i = 0; i < ctx->def->slotcount; i++) {
        if (ctx->value_numbers[i] == value_number) {
            return (uint32_t) i;
        }
    }
    return current_slot;
}

static void vn_add_lookup(VNContext *ctx, double key, VN vn) {
    janet_table_put(ctx->value_lookup, janet_wrap_number(key), janet_wrap_number(vn_encode_vn(vn)));
    if (vn.has_slot) {
        ctx->value_numbers[vn.slot] = vn.value_number;
    }
}

/* Check for constant propogation. Only implemented for small constants. */
static VN vn_const_prop(VNContext *ctx, uint32_t opcode, int32_t Bv, int32_t Cv) {
    (void) ctx; /* Will be needed for other constants besides 16 bit integers */
    VN zero = {0};
    if (Bv >= -3 || Cv >= -3) return zero;
    /* Both Bv and Cv are constant integers */
    int32_t b = (int16_t)((uint16_t) - (Bv + 4));
    int32_t c = (int16_t)((uint16_t) - (Cv + 4));
    int64_t result = 0; /* Extra precision for multiply */
    int is_bool = 0;
    int bool_result = 0;
    switch (opcode) {
        default:
            return zero;
        case JOP_SUBTRACT:
            result = (int64_t)(b - c);
            break;
        case JOP_ADD:
        case JOP_ADD_IMMEDIATE:
            result = (int64_t)(b + c);
            break;
        case JOP_MULTIPLY:
        case JOP_MULTIPLY_IMMEDIATE:
            result = (int64_t)(b * c);
            break;
        case JOP_DIVIDE_FLOOR:
            if (c <= 0 || b <= 0) return zero;
            result = (int64_t)(b / c);
            break;
        case JOP_REMAINDER:
            if (c <= 0) return zero;
            result = (int64_t)(b % c);
            break;
        case JOP_EQUALS:
        case JOP_EQUALS_IMMEDIATE:
            is_bool = 1;
            bool_result = b == c;
            break;
        case JOP_NOT_EQUALS:
        case JOP_NOT_EQUALS_IMMEDIATE:
            is_bool = 1;
            bool_result = b != c;
            break;
        case JOP_LESS_THAN:
        case JOP_LESS_THAN_IMMEDIATE:
            is_bool = 1;
            bool_result = b < c;
            break;
        case JOP_LESS_THAN_EQUAL:
            is_bool = 1;
            bool_result = b <= c;
            break;
        case JOP_GREATER_THAN:
        case JOP_GREATER_THAN_IMMEDIATE:
            is_bool = 1;
            bool_result = b > c;
            break;
        case JOP_GREATER_THAN_EQUAL:
            is_bool = 1;
            bool_result = b >= c;
            break;
        case JOP_BAND:
            result = (int64_t)(b & c);
            break;
        case JOP_BOR:
            result = (int64_t)(b | c);
            break;
        case JOP_BXOR:
            result = (int64_t)(b ^ c);
            break;
        case JOP_SHIFT_LEFT:
        case JOP_SHIFT_LEFT_IMMEDIATE: {
            if (c < 0 || c > 31) return zero;
            result = (int32_t)b << c;
        }
        break;
        case JOP_SHIFT_RIGHT:
        case JOP_SHIFT_RIGHT_IMMEDIATE: {
            if (c < 0 || c > 31) return zero;
            int32_t iresult = b >> c;
            result = iresult;
        }
        break;
        case JOP_SHIFT_RIGHT_UNSIGNED:
        case JOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE: {
            if (c < 0 || c > 31) return zero;
            int32_t b_sign_extend = (int32_t) b;
            uint32_t b_unsigned = (uint32_t) b_sign_extend;
            result = (int32_t)(b_unsigned >> c);
        }
        break;
    }
    if (result <= INT16_MAX && result >= INT16_MIN) {
        VN result_vn;
        if (is_bool) {
            result_vn.value_number = bool_result ? -3 : -2;
        } else {
            uint16_t uresult = (uint16_t) result;
            result_vn.value_number = (int32_t)(-4 - uresult);
        }
        result_vn.has_slot = 0;
        result_vn.slot = 0;
        return result_vn;
    }
    return zero;
}

/* Local value numbering routine - reduce number of loads, moves, and redundant computations.  */
/* TODO - slot maps for removed symbols can be removed */
void janet_bytecode_local_value_numbering(JanetFuncDef *def, BytecodeBB *blocks, int32_t n_blocks) {
    if (def->closure_bitset) return; /* Upvalues can be changed by side effects */
    if (def->bytecode_length >= 0x10000) return; /* Prevent overflow in value numbering */
    JanetTable value_lookup;
    janet_table_init(&value_lookup, 24);
    VNContext ctx;
    ctx.def = def;
    ctx.value_lookup = &value_lookup;
    ctx.value_numbers = array_allocate(sizeof(int32_t), def->slotcount);
    uint32_t *code = def->bytecode;
    for (int32_t blocki = 0; blocki < n_blocks; blocki++) {
        BytecodeBB block = blocks[blocki];
        int32_t nextv = 1; /* Reserve negative values */
        for (int32_t i = 0; i < def->slotcount; i++) ctx.value_numbers[i] = 0; /* initialize to unknown */
        janet_table_clear(ctx.value_lookup);
        for (int32_t pc = block.start; pc < block.end; pc++) {
            uint32_t A = (code[pc] & 0xFF00U) >> 8;
            uint32_t B = (code[pc] & 0xFF0000U) >> 16;
            uint32_t C = code[pc] >> 24;
            uint32_t D = code[pc] >> 8;
            uint32_t E = code[pc] >> 16;
            uint32_t opcode = code[pc] & 0x7F;
            switch (opcode) {
                default:
                    janet_assert(0, "unhandled opcode");
                    continue;
                /* Moves */
                case JOP_MOVE_NEAR: {
                    int32_t Ev = ctx.value_numbers[E];
                    double key = vn_encode_1arg(opcode, Ev);
                    VN vn = vn_check_key(&ctx, key);
                    if (vn.value_number) {
                        code[pc] = vn_move_or_load(&ctx, A, vn, code[pc]);
                        continue;
                    }
                    E = vn_find_reg(&ctx, Ev, E);
                    /* TODO - this sequence is repetitive and could likely be folded into vn_add_lookup */
                    VN vnnext;
                    vnnext.has_slot = 1;
                    vnnext.slot = A;
                    vnnext.value_number = ctx.value_numbers[E];
                    vn_add_lookup(&ctx, key, vnnext);
                    code[pc] = recon_ae(opcode, A, E);
                    continue;
                }
                case JOP_MOVE_FAR: {
                    int32_t Av = ctx.value_numbers[A];
                    double key = vn_encode_1arg(opcode, Av);
                    VN vn = vn_check_key(&ctx, key);
                    if (vn.value_number) {
                        code[pc] = vn_move_or_load(&ctx, E, vn, code[pc]);
                        continue;
                    }
                    A = vn_find_reg(&ctx, Av, A);
                    VN vnnext;
                    vnnext.has_slot = 1;
                    vnnext.slot = E;
                    vnnext.value_number = ctx.value_numbers[A];
                    vn_add_lookup(&ctx, key, vnnext);
                    code[pc] = recon_ae(opcode, A, E);
                    continue;
                }

                /* Skips */
                case JOP_RETURN_NIL:
                case JOP_NOOP:
                case JOP_JUMP:
                    continue;

                /* Loads */
                case JOP_LOAD_NIL:
                    ctx.value_numbers[D] = -1;
                    continue;
                case JOP_LOAD_FALSE:
                    ctx.value_numbers[D] = -2;
                    continue;
                case JOP_LOAD_TRUE:
                    ctx.value_numbers[D] = -3;
                    continue;
                case JOP_LOAD_INTEGER:
                    ctx.value_numbers[A] = -4 - (int32_t)E;
                    continue;
                case JOP_LOAD_UPVALUE:
                case JOP_LOAD_SELF:
                case JOP_LOAD_CONSTANT:
                    ctx.value_numbers[A] = nextv++;
                    continue;

                /* Read D */
                case JOP_TAILCALL:
                case JOP_PUSH_ARRAY:
                case JOP_PUSH:
                case JOP_RETURN: {
                    int32_t Dv = ctx.value_numbers[D];
                    D = vn_find_reg(&ctx, Dv, D);
                    code[pc] = recon_d(opcode, D);
                    continue;
                }

                /* Read A, E */
                case JOP_PUSH_2: {
                    int32_t Av = ctx.value_numbers[A];
                    int32_t Ev = ctx.value_numbers[E];
                    A = vn_find_reg(&ctx, Av, A);
                    E = vn_find_reg(&ctx, Ev, E);
                    code[pc] = recon_ae(opcode, A, E);
                    continue;
                }

                /* Read A */
                case JOP_ERROR:
                case JOP_TYPECHECK:
                case JOP_SET_UPVALUE:
                case JOP_JUMP_IF:
                case JOP_JUMP_IF_NIL:
                case JOP_JUMP_IF_NOT:
                case JOP_JUMP_IF_NOT_NIL: {
                    int32_t Av = ctx.value_numbers[A];
                    A = vn_find_reg(&ctx, Av, A);
                    code[pc] = recon_ae(opcode, A, E);
                    /* TODO - for jumps, remove condition if we can prove always truthy or falsey using value number */
                    continue;
                }

                /* A = op E */
                case JOP_BNOT: {
                    int32_t Ev = ctx.value_numbers[E];
                    double key = vn_encode_1arg(opcode, Ev);
                    VN vn = vn_check_key(&ctx, key);
                    if (vn.value_number) {
                        code[pc] = vn_move_or_load(&ctx, A, vn, code[pc]);
                        continue;
                    }
                    E = vn_find_reg(&ctx, Ev, E);
                    code[pc] = recon_ae(opcode, A, E);
                    VN vnnext;
                    vnnext.has_slot = 1;
                    vnnext.slot = A;
                    vnnext.value_number = nextv++;
                    vn_add_lookup(&ctx, key, vnnext);
                    continue;
                }

                /* A = op E w/ side effects or not idempotent */
                case JOP_LENGTH:
                case JOP_CALL: {
                    int32_t Ev = ctx.value_numbers[E];
                    E = vn_find_reg(&ctx, Ev, E);
                    code[pc] = recon_ae(opcode, A, E);
                    ctx.value_numbers[A] = nextv++;
                    continue;
                }

                /* A = op B w/ side effects */
                case JOP_SIGNAL: {
                    int32_t Bv = ctx.value_numbers[B];
                    B = vn_find_reg(&ctx, Bv, B);
                    code[pc] = recon_abc(opcode, A, B, C);
                    ctx.value_numbers[A] = nextv++;
                    continue;
                }

                /* A = op */
                case JOP_MAKE_ARRAY:
                case JOP_MAKE_BUFFER:
                case JOP_MAKE_STRING:
                case JOP_MAKE_STRUCT:
                case JOP_MAKE_TABLE:
                case JOP_MAKE_TUPLE:
                case JOP_MAKE_BRACKET_TUPLE:
                case JOP_CLOSURE: {
                    ctx.value_numbers[A] = nextv++;
                    continue;
                }

                /* A = B op C */
                case JOP_ADD:
                case JOP_SUBTRACT:
                case JOP_MULTIPLY:
                case JOP_DIVIDE:
                case JOP_MODULO:
                case JOP_REMAINDER:
                case JOP_DIVIDE_FLOOR:
                case JOP_LESS_THAN:
                case JOP_LESS_THAN_EQUAL:
                case JOP_GREATER_THAN:
                case JOP_GREATER_THAN_EQUAL:
                case JOP_BAND:
                case JOP_BOR:
                case JOP_BXOR:
                case JOP_SHIFT_LEFT:
                case JOP_SHIFT_RIGHT:
                case JOP_SHIFT_RIGHT_UNSIGNED:
                case JOP_COMPARE:
                case JOP_EQUALS:
                case JOP_NOT_EQUALS: {
                    int32_t Bv = ctx.value_numbers[B];
                    int32_t Cv = ctx.value_numbers[C];
                    /* Check for constant prop */
                    VN const_check = vn_const_prop(&ctx, opcode, Bv, Cv);
                    if (const_check.value_number) {
                        code[pc] = vn_move_or_load(&ctx, A, const_check, code[pc]);
                        continue;
                    }
                    double key = vn_encode_2arg(opcode, Bv, Cv);
                    /* TODO - promote to immediates if possible (if Cv is integer constant) */
                    VN vn = vn_check_key(&ctx, key);
                    if (vn.value_number) {
                        code[pc] = vn_move_or_load(&ctx, A, vn, code[pc]);
                        continue;
                    }
                    B = vn_find_reg(&ctx, Bv, B);
                    C = vn_find_reg(&ctx, Cv, C);
                    code[pc] = recon_abc(opcode, A, B, C);
                    VN vnnext;
                    vnnext.has_slot = 1;
                    vnnext.slot = A;
                    vnnext.value_number = nextv++;
                    vn_add_lookup(&ctx, key, vnnext);
                    continue;
                }

                /* A = B op C with side effects */
                case JOP_GET:
                case JOP_IN:
                case JOP_NEXT:
                case JOP_CANCEL:
                case JOP_RESUME:
                case JOP_PROPAGATE: {
                    int32_t Bv = ctx.value_numbers[B];
                    int32_t Cv = ctx.value_numbers[C];
                    B = vn_find_reg(&ctx, Bv, B);
                    C = vn_find_reg(&ctx, Cv, C);
                    code[pc] = recon_abc(opcode, A, B, C);
                    ctx.value_numbers[A] = nextv++;
                    continue;
                }

                /* A = B op IMM */
                case JOP_ADD_IMMEDIATE:
                case JOP_SUBTRACT_IMMEDIATE:
                case JOP_MULTIPLY_IMMEDIATE:
                case JOP_DIVIDE_IMMEDIATE:
                case JOP_NOT_EQUALS_IMMEDIATE:
                case JOP_EQUALS_IMMEDIATE:
                case JOP_LESS_THAN_IMMEDIATE:
                case JOP_GREATER_THAN_IMMEDIATE:
                case JOP_SHIFT_LEFT_IMMEDIATE:
                case JOP_SHIFT_RIGHT_IMMEDIATE:
                case JOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE:
                case JOP_GET_INDEX: {
                    int32_t Bv = ctx.value_numbers[B];
                    /* Check constant prop */
                    {
                        int16_t C_imm = (int8_t) C; /* To 16 bit and sign extend */
                        uint16_t encoded_C = (uint16_t)C_imm;
                        int32_t Cv = -4 - (int32_t)encoded_C;
                        VN check_const_prop = vn_const_prop(&ctx, opcode, Bv, Cv);
                        if (check_const_prop.value_number) {
                            code[pc] = vn_move_or_load(&ctx, A, check_const_prop, code[pc]);
                            continue;
                        }
                    }
                    double key = vn_encode_2arg(opcode, Bv, C);
                    VN vn = vn_check_key(&ctx, key);
                    if (vn.value_number) {
                        code[pc] = vn_move_or_load(&ctx, A, vn, code[pc]);
                        continue;
                    }
                    B = vn_find_reg(&ctx, Bv, B);
                    code[pc] = recon_abc(code[pc], A, B, C);
                    ctx.value_numbers[A] = nextv++;
                    continue;
                }

                /* Read A, B, C */
                case JOP_PUSH_3:
                case JOP_PUT: {
                    int32_t Av = ctx.value_numbers[A];
                    int32_t Bv = ctx.value_numbers[B];
                    int32_t Cv = ctx.value_numbers[C];
                    A = vn_find_reg(&ctx, Av, A);
                    B = vn_find_reg(&ctx, Bv, B);
                    C = vn_find_reg(&ctx, Cv, C);
                    code[pc] = recon_abc(code[pc], A, B, C);
                    continue;
                }
                break;

                /* Read A, B */
                case JOP_PUT_INDEX: {
                    int32_t Av = ctx.value_numbers[A];
                    int32_t Bv = ctx.value_numbers[B];
                    A = vn_find_reg(&ctx, Av, A);
                    B = vn_find_reg(&ctx, Bv, B);
                    code[pc] = recon_abc(code[pc], A, B, C);
                    continue;
                }
            }
        }
    }
    janet_free(ctx.value_numbers);
    janet_table_deinit(&value_lookup);
}

/* Traverse bytecode and remove unreachable code my marking it as a noop. */
void janet_bytecode_dead_code(JanetFuncDef *def) {
    int32_t *pcstack = NULL;
    uint32_t *code = def->bytecode;
    int32_t *codes = (int32_t *)code;
    int32_t len = def->bytecode_length;
    int32_t words = ((len - 1) / 32) + 1;
    uint32_t *visited_bitmap = array_allocate(sizeof(uint32_t), words);
    memset(visited_bitmap, 0, words * sizeof(uint32_t));
    janet_v_push(pcstack, 0);
    while (janet_v_count(pcstack)) {
        int32_t pc = janet_v_last(pcstack);
        janet_v_pop(pcstack);
        while (pc < len) {
            int32_t index = pc >> 5;
            int32_t mask = ((uint32_t)1) << (pc & 0x1F);
            if (visited_bitmap[index] & mask) break;
            visited_bitmap[index] |= mask;
            switch (code[pc] & 0x7F) {
                default:
                    pc++;
                    continue;
                case JOP_RETURN_NIL:
                case JOP_RETURN:
                case JOP_ERROR:
                case JOP_TAILCALL:
                    pc = len;
                    continue;
                case JOP_JUMP:
                    janet_v_push(pcstack, pc + (codes[pc] >> 8));
                    pc = len;
                    continue;
                case JOP_JUMP_IF:
                case JOP_JUMP_IF_NIL:
                case JOP_JUMP_IF_NOT:
                case JOP_JUMP_IF_NOT_NIL:
                    janet_v_push(pcstack, pc + (codes[pc] >> 16));
                    pc++;
                    continue;
            }
        }
    }
    for (int32_t pc = 0; pc < len; pc++) {
        if (!(visited_bitmap[pc >> 5] & (((uint32_t)1) << (pc & 0x1F)))) {
            code[pc] = JOP_NOOP;
        }
    }
    janet_free(visited_bitmap);
    janet_v_free(pcstack);
}

/* Handle remapping symbols when bytecode changes */
static void rewrite_symbolmap(JanetFuncDef *def, uint32_t *pc_map) {
    int32_t smout = 0;
    for (int32_t i = 0; i < def->symbolmap_length; i++) {
        JanetSymbolMap *sm = def->symbolmap + i;
        int keep = 1;
        /* Don't rewrite upvalue mappings */
        if (sm->birth_pc < UINT32_MAX) {
            sm->birth_pc = pc_map[sm->birth_pc];
            sm->death_pc = pc_map[sm->death_pc];
            /* entirely dead symbols can be removed from the symbol map. This can happen if a symbol is in dead code. */
            if (sm->death_pc > (uint32_t) def->bytecode_length) sm->death_pc = (uint32_t) def->bytecode_length;
            if (sm->birth_pc >= sm->death_pc) keep = 0;
        }
        /* Now shift if needed */
        if (keep) def->symbolmap[smout++] = *sm;
    }
    def->symbolmap_length = smout;
}

/* Remove all noops while preserving jumps and debugging information.
 * Useful as part of a filtering compiler pass. */
void janet_bytecode_remove_noops(JanetFuncDef *def) {

    /* Get an instruction rewrite map so we can rewrite jumps */
    uint32_t *pc_map = janet_smalloc(sizeof(uint32_t) * (1 + def->bytecode_length));
    uint32_t new_bytecode_length = 0;
    for (int32_t i = 0; i < def->bytecode_length; i++) {
        uint32_t instr = def->bytecode[i];
        uint32_t opcode = instr & 0x7F;
        pc_map[i] = new_bytecode_length;
        if (opcode != JOP_NOOP) {
            new_bytecode_length++;
        }
    }
    pc_map[def->bytecode_length] = new_bytecode_length;

    /* Linear scan rewrite bytecode and sourcemap. Also fix jumps. */
    int32_t j = 0;
    for (int32_t i = 0; i < def->bytecode_length; i++) {
        uint32_t instr = def->bytecode[i];
        uint32_t opcode = instr & 0x7F;
        int32_t old_jump_target = 0;
        int32_t new_jump_target = 0;
        switch (opcode) {
            case JOP_NOOP:
                continue;
            case JOP_JUMP:
                /* relative pc is in DS field of instruction */
                old_jump_target = i + (((int32_t)instr) >> 8);
                janet_assert(old_jump_target >= 0, "bounds");
                janet_assert(old_jump_target < def->bytecode_length, "bounds");
                new_jump_target = pc_map[old_jump_target];
                instr += (uint32_t)(new_jump_target - old_jump_target + (i - j)) << 8;
                break;
            case JOP_JUMP_IF:
            case JOP_JUMP_IF_NIL:
            case JOP_JUMP_IF_NOT:
            case JOP_JUMP_IF_NOT_NIL:
                /* relative pc is in ES field of instruction */
                old_jump_target = i + (((int32_t)instr) >> 16);
                janet_assert(old_jump_target >= 0, "bounds");
                janet_assert(old_jump_target < def->bytecode_length, "bounds");
                new_jump_target = pc_map[old_jump_target];
                instr += (uint32_t)(new_jump_target - old_jump_target + (i - j)) << 16;
                break;
            default:
                break;
        }
        def->bytecode[j] = instr;
        if (def->sourcemap != NULL) {
            def->sourcemap[j] = def->sourcemap[i];
        }
        j++;
    }

    /* Rewrite symbolmap */
    rewrite_symbolmap(def, pc_map);

    def->bytecode_length = new_bytecode_length;
    def->bytecode = janet_realloc(def->bytecode, def->bytecode_length * sizeof(uint32_t));
    janet_sfree(pc_map);
}

/* Simple Jump threading. Traverse basic blocks instead?
 * jump instructions are always last instruction in basic block. */
void janet_bytecode_jump_threading(JanetFuncDef *def) {
    int32_t blen = def->bytecode_length;
    uint32_t *code = def->bytecode;
    int32_t *codes = (int32_t *)code;
    int recur = 1;
    while (recur) {
        recur = 0;
        for (int32_t i = 0; i < blen; i++) {
            int32_t target;
            int is_branch = 0;
            switch (code[i] & 0x7F) {
                default:
                    continue;
                case JOP_JUMP:
                    target = i + (codes[i] >> 8);
                    break;
                case JOP_JUMP_IF:
                case JOP_JUMP_IF_NOT:
                case JOP_JUMP_IF_NIL:
                case JOP_JUMP_IF_NOT_NIL:
                    is_branch = 1;
                    target = i + (codes[i] >> 16);
                    break;
            }
            if (target == i) continue; /* infinite loop */
            if (target == i + 1) {
                code[i] = JOP_NOOP;
                recur = 1;
                continue;
            }
            while ((code[target] & 0x7F) == JOP_NOOP) { /* Skip noops */
                target++;
            }
            if ((code[target] & 0x7F) == JOP_RETURN_NIL && !is_branch) {
                code[i] = JOP_RETURN_NIL;
                recur = 1;
                continue;
            }
            /* update target */
            if ((code[target] & 0x7F) == JOP_JUMP) {
                target += (codes[target] >> 8);
            }
            uint32_t newcode;
            if (is_branch) {
                newcode = (code[i] & 0xFFFF) | ((uint32_t)(target - i) << 16);
            } else {
                newcode = (code[i] & 0xFF) | ((uint32_t)(target - i) << 8);
            }
            if (newcode != code[i]) recur = 1;
            code[i] = newcode;
        }
    }
}

/* Traverse backwards and remove writes that are never read.
 *
 * Algorithm:
 * 1. Keep a bitset for all slots
 * 2. For every instruction, extract touched slots.
 *    if slot is written to, check bit. If bit is already 1, remove instruction. Otherwise, mark bit to 1
 *    if slot is read from, mark bit to 0
 * 3. If we encounter a function call, set all bits to 0 for now. We can probably use the closure_bitset
 *    for more fine-grained tracking.
 *
 * As is, this can't do much since most primitives have side effects. We should combine with the lattice_types
 * analysis to allow removal of instructions that don't have side effects.
 *
 * mode == 0: Remove unused writes - bitset should be initialized to inverse of all dominated input
 *            slots (root node is all 0s, exit node is all 1s).
 * mode == 1: Fill bitset with input slots (uses) - slots that are read before writes
 * mode == 2: Fill bitset with output slots (defs) - all written slots (currently disabled)
 */
static void janet_bytecode_movopt_basic_block(JanetFuncDef *def, BytecodeBB bb, uint32_t *bitset, int mode) {
    if (bb.start >= bb.end) return; /* Degenerate block */
    if (mode == 0) {
        /* Flip bits */
        for (int32_t word = 0; word < (((def->slotcount - 1) >> 5) + 1); word++) {
            bitset[word] = ~bitset[word];
        }
    } else {
        memset(bitset, 0x00, (def->slotcount + 7) / 8);
    }
    for (int32_t i = bb.end - 1; i >= bb.start; i--) {
        int32_t ins[3];
        int32_t out = -1;
        int nins = 0;
        int pin = 0; /* side effects, don't remove */
        uint32_t I = def->bytecode[i];
        uint32_t Iop = I & 0x7F;
        uint32_t Ia = (I >> 8) & 0xFF;
        uint32_t Ib = (I >> 16) & 0xFF;
        uint32_t Ic = (I >> 24);
        uint32_t Id = (I >> 8);
        uint32_t Ie = (I >> 16);
        /* Whenever we execute an instruction that can yield or await, clear the bitset.
         * Closures could read some slots that seem to be unused, and then control could
         * return to our function. */
        switch (Iop) {
            default:
                janet_assert(0, "unhandled instruction");
                continue;
            case JOP_JUMP:
            case JOP_NOOP:
            case JOP_RETURN_NIL:
                continue;

            /* Side effects, don't remove */

            /* Write A, Read E */
            case JOP_CALL:
                /* Similar logic applies to tail calls, but they are always at the end of blocks anyway */
                out = Ia;
                pin = 1;
                nins = 1;
                ins[0] = Ie;
                break;
            /* Write A, Read B */
            case JOP_SIGNAL:
                out = Ia;
                nins = 1;
                ins[0] = Ib;
                pin = 1;
                break;
            /* Read A */
            case JOP_ERROR:
            case JOP_TYPECHECK:
            case JOP_JUMP_IF:
            case JOP_JUMP_IF_NOT:
            case JOP_JUMP_IF_NIL:
            case JOP_JUMP_IF_NOT_NIL:
            case JOP_SET_UPVALUE:
                nins = 1;
                pin = 1;
                ins[0] = Ia;
                break;
            /* Read D */
            case JOP_RETURN:
            case JOP_PUSH:
            case JOP_PUSH_ARRAY:
            case JOP_TAILCALL:
                pin = 1;
                nins = 1;
                ins[0] = Id;
                break;
            case JOP_PUT:
            case JOP_PUSH_3:
                nins = 3;
                pin = 1;
                ins[0] = Ia;
                ins[1] = Ib;
                ins[2] = Ic;
                break;
            /* Write A */
            case JOP_MAKE_ARRAY:
            case JOP_MAKE_BUFFER:
            case JOP_MAKE_STRING:
            case JOP_MAKE_STRUCT:
            case JOP_MAKE_TABLE:
            case JOP_MAKE_TUPLE:
            case JOP_MAKE_BRACKET_TUPLE:
                out = Ia;
                pin = 1; /* TODO - we need an instruction (or psuedo instruction) that can clear the stack but do nothing */
                break;
            case JOP_LOAD_INTEGER:
            case JOP_LOAD_CONSTANT:
            case JOP_LOAD_UPVALUE:
            case JOP_CLOSURE:
                out = Ia;
                break;
            /* Read A, B */
            case JOP_PUT_INDEX:
                nins = 2;
                ins[0] = Ia;
                ins[1] = Ib;
                pin = 1;
                break;
            /* Read A, E */
            case JOP_PUSH_2:
                nins = 2;
                ins[0] = Ia;
                ins[1] = Ie;
                pin = 1;
                break;
            /* A = B op C */
            case JOP_PROPAGATE:
            case JOP_RESUME:
            case JOP_CANCEL:
            case JOP_NEXT:
                pin = 1;
                out = Ia;
                nins = 2;
                ins[0] = Ib;
                ins[1] = Ic;
                break;

            /* No side effects below, can remove.
             * Some of these instructions may have side effects if
             * the inputs are abstracts. */

            /* Loads that write D */
            case JOP_LOAD_NIL:
            case JOP_LOAD_TRUE:
            case JOP_LOAD_FALSE:
            case JOP_LOAD_SELF:
                out = Id;
                break;

            /* Moves and similar */
            case JOP_MOVE_FAR:
                out = Ie;
                nins = 1;
                ins[0] = Ia;
                break;
            case JOP_MOVE_NEAR:
            case JOP_LENGTH:
            case JOP_BNOT:
                out = Ia;
                nins = 1;
                ins[0] = Ie;
                break;

            /* A = B op C */
            case JOP_BAND:
            case JOP_BOR:
            case JOP_BXOR:
            case JOP_ADD:
            case JOP_SUBTRACT:
            case JOP_MULTIPLY:
            case JOP_DIVIDE:
            case JOP_DIVIDE_FLOOR:
            case JOP_MODULO:
            case JOP_REMAINDER:
            case JOP_SHIFT_LEFT:
            case JOP_SHIFT_RIGHT:
            case JOP_SHIFT_RIGHT_UNSIGNED:
            case JOP_GREATER_THAN:
            case JOP_LESS_THAN:
            case JOP_EQUALS:
            case JOP_COMPARE:
            case JOP_IN:
            case JOP_GET:
            case JOP_GREATER_THAN_EQUAL:
            case JOP_LESS_THAN_EQUAL:
            case JOP_NOT_EQUALS:
                out = Ia;
                nins = 2;
                ins[0] = Ib;
                ins[1] = Ic;
                break;

            /* A = op B imm */
            case JOP_ADD_IMMEDIATE:
            case JOP_SUBTRACT_IMMEDIATE:
            case JOP_MULTIPLY_IMMEDIATE:
            case JOP_DIVIDE_IMMEDIATE:
            case JOP_SHIFT_LEFT_IMMEDIATE:
            case JOP_SHIFT_RIGHT_IMMEDIATE:
            case JOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE:
            case JOP_GREATER_THAN_IMMEDIATE:
            case JOP_LESS_THAN_IMMEDIATE:
            case JOP_EQUALS_IMMEDIATE:
            case JOP_NOT_EQUALS_IMMEDIATE:
            case JOP_GET_INDEX:
                out = Ia;
                nins = 1;
                ins[0] = Ib;
                break;
        }
        /* Test and set output bit in bitmap. If already set, change to noop. */
        /* Add check to avoid messing with upvalues */
        if (mode == 0) {
            if (out != -1) {
                if (!pin && bs_read_bit(bitset, out) &&
                        (!def->closure_bitset || !bs_read_bit(def->closure_bitset, out))) {
                    def->bytecode[i] = JOP_NOOP;
                }
                bs_set_bit(bitset, out);
            }
            /* Clear input slots from bitmap */
            for (int j = 0; j < nins; j++) {
                bs_clear_bit(bitset, ins[j]);
            }
        } else if (mode == 1) {
            /* Clear writes, set reads (in that order!) */
            if (out != -1) bs_clear_bit(bitset, out);
            for (int j = 0; j < nins; j++) {
                janet_assert(ins[j] < def->slotcount, "slot too big");
                bs_set_bit(bitset, ins[j]);
            }
        }
    }
}

/* Generate use-def information for all basic blocks, then remove extra writes. */
void janet_bytecode_movopt_full(JanetFuncDef *def, BytecodeBB *blocks, int32_t nblocks) {

    /* Allocate nblocks bitmaps */
    int32_t wordlen = ((def->slotcount - 1) >> 5) + 1;
    janet_assert(wordlen > 0, "bad len");
    uint32_t **bitsets = array_allocate(sizeof(uint32_t *), nblocks);
    for (int32_t block_i = 0; block_i < nblocks; block_i++) {
        bitsets[block_i] = array_allocate(sizeof(uint32_t), wordlen);
        memset(bitsets[block_i], 0, wordlen * sizeof(uint32_t));
    }

    /* Find all local block inputs */
    for (int32_t block_i = 0; block_i < nblocks; block_i++) {
        janet_bytecode_movopt_basic_block(def, blocks[block_i], bitsets[block_i], 1);
    }

    /* Propagate inputs to predecessors */
    int recur = 1;
    while (recur) {
        recur = 0;
        for (int32_t block_i = 0; block_i < nblocks; block_i++) {
            for (int j = 0; j < 2; j++) {
                int32_t block_next = blocks[block_i].next[j];
                if (block_next != -1) {
                    for (int32_t i = 0; i < wordlen; i++) {
                        uint32_t before = bitsets[block_i][i];
                        bitsets[block_i][i] |= bitsets[block_next][i];
                        if (before != bitsets[block_i][i]) recur = 1;
                    }
                }
            }
        }
    }

    /* Remove extra writes */
    for (int32_t block_i = 0; block_i < nblocks; block_i++) {
        janet_bytecode_movopt_basic_block(def, blocks[block_i], bitsets[block_i], 0);
    }

    /* Cleanup */
    for (int32_t block_i = 0; block_i < nblocks; block_i++) {
        janet_free(bitsets[block_i]);
    }
    janet_free(bitsets);
}

/* Entry point for optimization */
void janet_bytecode_optimize(JanetFuncDef *def, int32_t level) {
    int32_t delta;
#ifdef JANET_DEBUG
    int result = janet_verify(def);
    janet_assert(result == 0, "input bytecode bad");
#endif
    if (level >= 0) {
        do { /* Fixpoint for instruction removal */
            int32_t before = def->bytecode_length;
            janet_bytecode_jump_threading(def);
            {
                int32_t nblocks = 0;
                BytecodeBB *basic_blocks = janet_bytecode_basic_blocks(def, &nblocks);
                janet_assert(nblocks > 0, "no blocks");
                if (level >= 1) {
                    janet_bytecode_local_value_numbering(def, basic_blocks, nblocks);
                }
                janet_bytecode_movopt_full(def, basic_blocks, nblocks);
                janet_free(basic_blocks);
            }
            janet_bytecode_dead_code(def);
            janet_bytecode_remove_noops(def);
            delta = def->bytecode_length - before;
#ifdef JANET_BOOTSTRAP
            total_optimize_fixpoint_loops++;
#endif
        } while (delta < 0);
    }
#ifdef JANET_BOOTSTRAP
    total_instruction_count += def->bytecode_length;
    total_funcdefs_optimized++;
#endif
}

/* Verify some bytecode */
int janet_verify(JanetFuncDef *def) {
    int vargs = !!(def->flags & JANET_FUNCDEF_FLAG_VARARG);
    int32_t i;
    int32_t maxslot = def->arity + vargs;
    int32_t sc = def->slotcount;

    if (def->environments_length > 256) return 15;
    if (def->bytecode_length == 0) return 1;

    if (maxslot > sc) return 2;

    /* Verify each instruction */
    for (i = 0; i < def->bytecode_length; i++) {
        uint32_t instr = def->bytecode[i];
        /* Check for invalid instructions */
        if ((instr & 0x7F) >= JOP_INSTRUCTION_COUNT) {
            return 3;
        }
        enum JanetInstructionType type = janet_instructions[instr & 0x7F];
        switch (type) {
            case JINT_0:
                continue;
            case JINT_S: {
                if ((int32_t)(instr >> 8) >= sc) return 4;
                continue;
            }
            case JINT_SI:
            case JINT_SU:
            case JINT_ST: {
                if ((int32_t)((instr >> 8) & 0xFF) >= sc) return 4;
                continue;
            }
            case JINT_L: {
                int32_t jumpdest = i + (((int32_t)instr) >> 8);
                if (jumpdest < 0 || jumpdest >= def->bytecode_length) return 5;
                continue;
            }
            case JINT_SS: {
                if ((int32_t)((instr >> 8) & 0xFF) >= sc ||
                        (int32_t)(instr >> 16) >= sc) return 4;
                continue;
            }
            case JINT_SSI:
            case JINT_SSU: {
                if ((int32_t)((instr >> 8) & 0xFF) >= sc ||
                        (int32_t)((instr >> 16) & 0xFF) >= sc) return 4;
                continue;
            }
            case JINT_SL: {
                int32_t jumpdest = i + (((int32_t)instr) >> 16);
                if ((int32_t)((instr >> 8) & 0xFF) >= sc) return 4;
                if (jumpdest < 0 || jumpdest >= def->bytecode_length) return 5;
                continue;
            }
            case JINT_SSS: {
                if (((int32_t)(instr >> 8) & 0xFF) >= sc ||
                        ((int32_t)(instr >> 16) & 0xFF) >= sc ||
                        ((int32_t)(instr >> 24) & 0xFF) >= sc) return 4;
                continue;
            }
            case JINT_SD: {
                if ((int32_t)((instr >> 8) & 0xFF) >= sc) return 4;
                if ((int32_t)(instr >> 16) >= def->defs_length) return 6;
                continue;
            }
            case JINT_SC: {
                if ((int32_t)((instr >> 8) & 0xFF) >= sc) return 4;
                if ((int32_t)(instr >> 16) >= def->constants_length) return 7;
                continue;
            }
            case JINT_SES: {
                /* How can we check the last slot index? We need info parent funcdefs. Resort
                 * to runtime checks for now. Maybe invalid upvalue references could be defaulted
                 * to nil? (don't commit to this in the long term, though) */
                if ((int32_t)((instr >> 8) & 0xFF) >= sc) return 4;
                if ((int32_t)((instr >> 16) & 0xFF) >= def->environments_length) return 8;
                continue;
            }
        }
    }

    /* Verify last instruction is either a jump, return, return-nil, or tailcall. Eventually,
     * some real flow analysis would be ideal, but this should be very effective. Will completely
     * prevent running over the end of bytecode. However, valid functions with dead code will
     * be rejected. */
    {
        uint32_t lastop = def->bytecode[def->bytecode_length - 1] & 0xFF;
        switch (lastop) {
            default:
                return 9;
            case JOP_RETURN:
            case JOP_RETURN_NIL:
            case JOP_JUMP:
            case JOP_ERROR:
            case JOP_TAILCALL:
                break;
        }
    }

    /* Verify debug info - slotmapping, etc. */
    for (int32_t i = def->symbolmap_length - 1; i >= 0; i--) {
        JanetSymbolMap jsm = def->symbolmap[i];
        if (jsm.birth_pc == UINT32_MAX) {
            if (jsm.death_pc >= (uint32_t) def->environments_length) {
                return 10;
                /* We should also check jsm.slot_index */
            }
        } else {
            if (jsm.slot_index >= (uint32_t) def->slotcount) {
                return 11;
            }
            if (jsm.birth_pc != UINT32_MAX && jsm.birth_pc >= (uint32_t) def->bytecode_length) {
                return 12;
            }
            if (jsm.death_pc != UINT32_MAX && jsm.death_pc > (uint32_t) def->bytecode_length) {
                return 13;
            }
        }
        if (jsm.symbol == NULL) {
            return 14;
        }
    }

    return 0;
}

/* Allocate an empty funcdef. This function may have added functionality
 * as commonalities between asm and compile arise. */
JanetFuncDef *janet_funcdef_alloc(void) {
    JanetFuncDef *def = janet_gcalloc(JANET_MEMORY_FUNCDEF, sizeof(JanetFuncDef));
    def->environments = NULL;
    def->constants = NULL;
    def->bytecode = NULL;
    def->closure_bitset = NULL;
    def->flags = 0;
    def->slotcount = 0;
    def->symbolmap = NULL;
    def->arity = 0;
    def->min_arity = 0;
    def->max_arity = INT32_MAX;
    def->source = NULL;
    def->sourcemap = NULL;
    def->name = NULL;
    def->defs = NULL;
    def->defs_length = 0;
    def->constants_length = 0;
    def->bytecode_length = 0;
    def->environments_length = 0;
    def->symbolmap_length = 0;
    def->named_args_count = 0;
    return def;
}

/* Create a simple closure from a funcdef */
JanetFunction *janet_thunk(JanetFuncDef *def) {
    JanetFunction *func = janet_gcalloc(JANET_MEMORY_FUNCTION, sizeof(JanetFunction));
    func->def = def;
    janet_assert(def->environments_length == 0, "tried to create thunk that needs upvalues");
    return func;
}
