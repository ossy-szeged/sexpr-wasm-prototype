/*
 * Copyright 2016 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef WASM_COMMON_H_
#define WASM_COMMON_H_

#include <stddef.h>
#include <stdint.h>

#include "wasm-config.h"

#ifdef __cplusplus
#define WASM_EXTERN_C extern "C"
#define WASM_EXTERN_C_BEGIN extern "C" {
#define WASM_EXTERN_C_END }
#else
#define WASM_EXTERN_C
#define WASM_EXTERN_C_BEGIN
#define WASM_EXTERN_C_END
#endif

#define WASM_FATAL(...) fprintf(stderr, __VA_ARGS__), exit(1)
#define WASM_ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define WASM_ZERO_MEMORY(var) memset((void*)&(var), 0, sizeof(var))

#define WASM_PAGE_SIZE 0x10000 /* 64k */

struct WasmAllocator;

typedef enum WasmResult {
  WASM_OK,
  WASM_ERROR,
} WasmResult;

typedef struct WasmStringSlice {
  const char* start;
  size_t length;
} WasmStringSlice;

typedef struct WasmLocation {
  const char* filename;
  int line;
  int first_column;
  int last_column;
} WasmLocation;

/* matches binary format, do not change */
enum {
  WASM_TYPE_VOID,
  WASM_TYPE_I32,
  WASM_TYPE_I64,
  WASM_TYPE_F32,
  WASM_TYPE_F64,
  WASM_NUM_TYPES,
  WASM_TYPE____ = WASM_TYPE_VOID, /* convenient for the opcode table below */
};
typedef unsigned char WasmType;

enum { WASM_USE_NATURAL_ALIGNMENT = 0xFFFFFFFF };

/*
 *   tr: result type
 *   t1: type of the 1st parameter
 *   t2: type of the 2nd parameter
 *    m: memory size of the operation, if any
 * code: opcode
 * NAME: used to generate the opcode enum
 * text: a string of the opcode name in the AST format
 *
 *  tr  t1    t2   m  code  NAME text
 *  ============================ */
#define WASM_FOREACH_OPCODE(V)                                     \
  V(___, ___, ___, 0, 0x00, NOP, "nop")                                 \
  V(___, ___, ___, 0, 0x01, BLOCK, "block")                             \
  V(___, ___, ___, 0, 0x02, LOOP, "loop")                               \
  V(___, ___, ___, 0, 0x03, IF, "if")                                   \
  V(___, ___, ___, 0, 0x04, IF_ELSE, "if_else")                         \
  V(___, ___, ___, 0, 0x05, SELECT, "select")                           \
  V(___, ___, ___, 0, 0x06, BR, "br")                                   \
  V(___, ___, ___, 0, 0x07, BR_IF, "br_if")                             \
  V(___, ___, ___, 0, 0x08, BR_TABLE, "br_table")                       \
  V(___, ___, ___, 0, 0x14, RETURN, "return")                           \
  V(___, ___, ___, 0, 0x15, UNREACHABLE, "unreachable")                 \
  V(___, ___, ___, 0, 0x09, I8_CONST, "i8.const")                       \
  V(I32, ___, ___, 0, 0x0a, I32_CONST, "i32.const")                     \
  V(I64, ___, ___, 0, 0x0b, I64_CONST, "i64.const")                     \
  V(F64, ___, ___, 0, 0x0c, F64_CONST, "f64.const")                     \
  V(F32, ___, ___, 0, 0x0d, F32_CONST, "f32.const")                     \
  V(___, ___, ___, 0, 0x0e, GET_LOCAL, "get_local")                     \
  V(___, ___, ___, 0, 0x0f, SET_LOCAL, "set_local")                     \
  V(___, ___, ___, 0, 0x12, CALL_FUNCTION, "call")                      \
  V(___, ___, ___, 0, 0x13, CALL_INDIRECT, "call_indirect")             \
  V(___, ___, ___, 0, 0x1f, CALL_IMPORT, "call_import")                 \
  V(I32, I32, ___, 1, 0x20, I32_LOAD8_S, "i32.load8_s")                 \
  V(I32, I32, ___, 1, 0x21, I32_LOAD8_U, "i32.load8_u")                 \
  V(I32, I32, ___, 2, 0x22, I32_LOAD16_S, "i32.load16_s")               \
  V(I32, I32, ___, 2, 0x23, I32_LOAD16_U, "i32.load16_u")               \
  V(I64, I32, ___, 1, 0x24, I64_LOAD8_S, "i64.load8_s")                 \
  V(I64, I32, ___, 1, 0x25, I64_LOAD8_U, "i64.load8_u")                 \
  V(I64, I32, ___, 2, 0x26, I64_LOAD16_S, "i64.load16_s")               \
  V(I64, I32, ___, 2, 0x27, I64_LOAD16_U, "i64.load16_u")               \
  V(I64, I32, ___, 4, 0x28, I64_LOAD32_S, "i64.load32_s")               \
  V(I64, I32, ___, 4, 0x29, I64_LOAD32_U, "i64.load32_u")               \
  V(I32, I32, ___, 4, 0x2a, I32_LOAD, "i32.load")                       \
  V(I64, I32, ___, 8, 0x2b, I64_LOAD, "i64.load")                       \
  V(F32, I32, ___, 4, 0x2c, F32_LOAD, "f32.load")                       \
  V(F64, I32, ___, 8, 0x2d, F64_LOAD, "f64.load")                       \
  V(I32, I32, I32, 1, 0x2e, I32_STORE8, "i32.store8")                   \
  V(I32, I32, I32, 2, 0x2f, I32_STORE16, "i32.store16")                 \
  V(I64, I32, I64, 1, 0x30, I64_STORE8, "i64.store8")                   \
  V(I64, I32, I64, 2, 0x31, I64_STORE16, "i64.store16")                 \
  V(I64, I32, I64, 4, 0x32, I64_STORE32, "i64.store32")                 \
  V(I32, I32, I32, 4, 0x33, I32_STORE, "i32.store")                     \
  V(I64, I32, I64, 8, 0x34, I64_STORE, "i64.store")                     \
  V(F32, I32, F32, 4, 0x35, F32_STORE, "f32.store")                     \
  V(F64, I32, F64, 8, 0x36, F64_STORE, "f64.store")                     \
  V(I32, ___, ___, 0, 0x3b, MEMORY_SIZE, "memory_size")                 \
  V(I32, I32, ___, 0, 0x39, GROW_MEMORY, "grow_memory")                 \
  V(I32, I32, I32, 0, 0x40, I32_ADD, "i32.add")                         \
  V(I32, I32, I32, 0, 0x41, I32_SUB, "i32.sub")                         \
  V(I32, I32, I32, 0, 0x42, I32_MUL, "i32.mul")                         \
  V(I32, I32, I32, 0, 0x43, I32_DIV_S, "i32.div_s")                     \
  V(I32, I32, I32, 0, 0x44, I32_DIV_U, "i32.div_u")                     \
  V(I32, I32, I32, 0, 0x45, I32_REM_S, "i32.rem_s")                     \
  V(I32, I32, I32, 0, 0x46, I32_REM_U, "i32.rem_u")                     \
  V(I32, I32, I32, 0, 0x47, I32_AND, "i32.and")                         \
  V(I32, I32, I32, 0, 0x48, I32_OR, "i32.or")                           \
  V(I32, I32, I32, 0, 0x49, I32_XOR, "i32.xor")                         \
  V(I32, I32, I32, 0, 0x4a, I32_SHL, "i32.shl")                         \
  V(I32, I32, I32, 0, 0x4b, I32_SHR_U, "i32.shr_u")                     \
  V(I32, I32, I32, 0, 0x4c, I32_SHR_S, "i32.shr_s")                     \
  V(I32, I32, I32, 0, 0x4d, I32_EQ, "i32.eq")                           \
  V(I32, I32, I32, 0, 0x4e, I32_NE, "i32.ne")                           \
  V(I32, I32, I32, 0, 0x4f, I32_LT_S, "i32.lt_s")                       \
  V(I32, I32, I32, 0, 0x50, I32_LE_S, "i32.le_s")                       \
  V(I32, I32, I32, 0, 0x51, I32_LT_U, "i32.lt_u")                       \
  V(I32, I32, I32, 0, 0x52, I32_LE_U, "i32.le_u")                       \
  V(I32, I32, I32, 0, 0x53, I32_GT_S, "i32.gt_s")                       \
  V(I32, I32, I32, 0, 0x54, I32_GE_S, "i32.ge_s")                       \
  V(I32, I32, I32, 0, 0x55, I32_GT_U, "i32.gt_u")                       \
  V(I32, I32, I32, 0, 0x56, I32_GE_U, "i32.ge_u")                       \
  V(I32, I32, ___, 0, 0x57, I32_CLZ, "i32.clz")                         \
  V(I32, I32, ___, 0, 0x58, I32_CTZ, "i32.ctz")                         \
  V(I32, I32, ___, 0, 0x59, I32_POPCNT, "i32.popcnt")                   \
  V(I32, I32, ___, 0, 0x5a, I32_EQZ, "i32.eqz")                         \
  V(I64, I64, I64, 0, 0x5b, I64_ADD, "i64.add")                         \
  V(I64, I64, I64, 0, 0x5c, I64_SUB, "i64.sub")                         \
  V(I64, I64, I64, 0, 0x5d, I64_MUL, "i64.mul")                         \
  V(I64, I64, I64, 0, 0x5e, I64_DIV_S, "i64.div_s")                     \
  V(I64, I64, I64, 0, 0x5f, I64_DIV_U, "i64.div_u")                     \
  V(I64, I64, I64, 0, 0x60, I64_REM_S, "i64.rem_s")                     \
  V(I64, I64, I64, 0, 0x61, I64_REM_U, "i64.rem_u")                     \
  V(I64, I64, I64, 0, 0x62, I64_AND, "i64.and")                         \
  V(I64, I64, I64, 0, 0x63, I64_OR, "i64.or")                           \
  V(I64, I64, I64, 0, 0x64, I64_XOR, "i64.xor")                         \
  V(I64, I64, I64, 0, 0x65, I64_SHL, "i64.shl")                         \
  V(I64, I64, I64, 0, 0x66, I64_SHR_U, "i64.shr_u")                     \
  V(I64, I64, I64, 0, 0x67, I64_SHR_S, "i64.shr_s")                     \
  V(I32, I64, I64, 0, 0x68, I64_EQ, "i64.eq")                           \
  V(I32, I64, I64, 0, 0x69, I64_NE, "i64.ne")                           \
  V(I32, I64, I64, 0, 0x6a, I64_LT_S, "i64.lt_s")                       \
  V(I32, I64, I64, 0, 0x6b, I64_LE_S, "i64.le_s")                       \
  V(I32, I64, I64, 0, 0x6c, I64_LT_U, "i64.lt_u")                       \
  V(I32, I64, I64, 0, 0x6d, I64_LE_U, "i64.le_u")                       \
  V(I32, I64, I64, 0, 0x6e, I64_GT_S, "i64.gt_s")                       \
  V(I32, I64, I64, 0, 0x6f, I64_GE_S, "i64.ge_s")                       \
  V(I32, I64, I64, 0, 0x70, I64_GT_U, "i64.gt_u")                       \
  V(I32, I64, I64, 0, 0x71, I64_GE_U, "i64.ge_u")                       \
  V(I64, I64, I64, 0, 0x72, I64_CLZ, "i64.clz")                         \
  V(I64, I64, I64, 0, 0x73, I64_CTZ, "i64.ctz")                         \
  V(I64, I64, I64, 0, 0x74, I64_POPCNT, "i64.popcnt")                   \
  V(F32, F32, F32, 0, 0x75, F32_ADD, "f32.add")                         \
  V(F32, F32, F32, 0, 0x76, F32_SUB, "f32.sub")                         \
  V(F32, F32, F32, 0, 0x77, F32_MUL, "f32.mul")                         \
  V(F32, F32, F32, 0, 0x78, F32_DIV, "f32.div")                         \
  V(F32, F32, F32, 0, 0x79, F32_MIN, "f32.min")                         \
  V(F32, F32, F32, 0, 0x7a, F32_MAX, "f32.max")                         \
  V(F32, F32, F32, 0, 0x7b, F32_ABS, "f32.abs")                         \
  V(F32, F32, F32, 0, 0x7c, F32_NEG, "f32.neg")                         \
  V(F32, F32, F32, 0, 0x7d, F32_COPYSIGN, "f32.copysign")               \
  V(F32, F32, F32, 0, 0x7e, F32_CEIL, "f32.ceil")                       \
  V(F32, F32, F32, 0, 0x7f, F32_FLOOR, "f32.floor")                     \
  V(F32, F32, F32, 0, 0x80, F32_TRUNC, "f32.trunc")                     \
  V(F32, F32, F32, 0, 0x81, F32_NEAREST, "f32.nearest")                 \
  V(F32, F32, F32, 0, 0x82, F32_SQRT, "f32.sqrt")                       \
  V(I32, F32, F32, 0, 0x83, F32_EQ, "f32.eq")                           \
  V(I32, F32, F32, 0, 0x84, F32_NE, "f32.ne")                           \
  V(I32, F32, F32, 0, 0x85, F32_LT, "f32.lt")                           \
  V(I32, F32, F32, 0, 0x86, F32_LE, "f32.le")                           \
  V(I32, F32, F32, 0, 0x87, F32_GT, "f32.gt")                           \
  V(I32, F32, F32, 0, 0x88, F32_GE, "f32.ge")                           \
  V(F64, F64, F64, 0, 0x89, F64_ADD, "f64.add")                         \
  V(F64, F64, F64, 0, 0x8a, F64_SUB, "f64.sub")                         \
  V(F64, F64, F64, 0, 0x8b, F64_MUL, "f64.mul")                         \
  V(F64, F64, F64, 0, 0x8c, F64_DIV, "f64.div")                         \
  V(F64, F64, F64, 0, 0x8d, F64_MIN, "f64.min")                         \
  V(F64, F64, F64, 0, 0x8e, F64_MAX, "f64.max")                         \
  V(F64, F64, F64, 0, 0x8f, F64_ABS, "f64.abs")                         \
  V(F64, F64, F64, 0, 0x90, F64_NEG, "f64.neg")                         \
  V(F64, F64, F64, 0, 0x91, F64_COPYSIGN, "f64.copysign")               \
  V(F64, F64, F64, 0, 0x92, F64_CEIL, "f64.ceil")                       \
  V(F64, F64, F64, 0, 0x93, F64_FLOOR, "f64.floor")                     \
  V(F64, F64, F64, 0, 0x94, F64_TRUNC, "f64.trunc")                     \
  V(F64, F64, F64, 0, 0x95, F64_NEAREST, "f64.nearest")                 \
  V(F64, F64, F64, 0, 0x96, F64_SQRT, "f64.sqrt")                       \
  V(I32, F64, F64, 0, 0x97, F64_EQ, "f64.eq")                           \
  V(I32, F64, F64, 0, 0x98, F64_NE, "f64.ne")                           \
  V(I32, F64, F64, 0, 0x99, F64_LT, "f64.lt")                           \
  V(I32, F64, F64, 0, 0x9a, F64_LE, "f64.le")                           \
  V(I32, F64, F64, 0, 0x9b, F64_GT, "f64.gt")                           \
  V(I32, F64, F64, 0, 0x9c, F64_GE, "f64.ge")                           \
  V(I32, F32, ___, 0, 0x9d, I32_TRUNC_S_F32, "i32.trunc_s/f32")         \
  V(I32, F64, ___, 0, 0x9e, I32_TRUNC_S_F64, "i32.trunc_s/f64")         \
  V(I32, F32, ___, 0, 0x9f, I32_TRUNC_U_F32, "i32.trunc_u/f32")         \
  V(I32, F64, ___, 0, 0xa0, I32_TRUNC_U_F64, "i32.trunc_u/f64")         \
  V(I32, I64, ___, 0, 0xa1, I32_WRAP_I64, "i32.wrap/i64")               \
  V(I64, F32, ___, 0, 0xa2, I64_TRUNC_S_F32, "i64.trunc_s/f32")         \
  V(I64, F64, ___, 0, 0xa3, I64_TRUNC_S_F64, "i64.trunc_s/f64")         \
  V(I64, F32, ___, 0, 0xa4, I64_TRUNC_U_F32, "i64.trunc_u/f32")         \
  V(I64, F64, ___, 0, 0xa5, I64_TRUNC_U_F64, "i64.trunc_u/f64")         \
  V(I64, I32, ___, 0, 0xa6, I64_EXTEND_S_I32, "i64.extend_s/i32")       \
  V(I64, I32, ___, 0, 0xa7, I64_EXTEND_U_I32, "i64.extend_u/i32")       \
  V(F32, I32, ___, 0, 0xa8, F32_CONVERT_S_I32, "f32.convert_s/i32")     \
  V(F32, I32, ___, 0, 0xa9, F32_CONVERT_U_I32, "f32.convert_u/i32")     \
  V(F32, I64, ___, 0, 0xaa, F32_CONVERT_S_I64, "f32.convert_s/i64")     \
  V(F32, I64, ___, 0, 0xab, F32_CONVERT_U_I64, "f32.convert_u/i64")     \
  V(F32, F64, ___, 0, 0xac, F32_DEMOTE_F64, "f32.demote/f64")           \
  V(F32, I32, ___, 0, 0xad, F32_REINTERPRET_I32, "f32.reinterpret/i32") \
  V(F64, I32, ___, 0, 0xae, F64_CONVERT_S_I32, "f64.convert_s/i32")     \
  V(F64, I32, ___, 0, 0xaf, F64_CONVERT_U_I32, "f64.convert_u/i32")     \
  V(F64, I64, ___, 0, 0xb0, F64_CONVERT_S_I64, "f64.convert_s/i64")     \
  V(F64, I64, ___, 0, 0xb1, F64_CONVERT_U_I64, "f64.convert_u/i64")     \
  V(F64, F32, ___, 0, 0xb2, F64_PROMOTE_F32, "f64.promote/f32")         \
  V(F64, I64, ___, 0, 0xb3, F64_REINTERPRET_I64, "f64.reinterpret/i64") \
  V(I32, F32, ___, 0, 0xb4, I32_REINTERPRET_F32, "i32.reinterpret/f32") \
  V(I64, F64, ___, 0, 0xb5, I64_REINTERPRET_F64, "i64.reinterpret/f64") \
  V(I32, I32, I32, 0, 0xb6, I32_ROTR, "i32.rotr")                       \
  V(I32, I32, I32, 0, 0xb7, I32_ROTL, "i32.rotl")                       \
  V(I64, I64, I64, 0, 0xb8, I64_ROTR, "i64.rotr")                       \
  V(I64, I64, I64, 0, 0xb9, I64_ROTL, "i64.rotl")                       \
  V(I32, I64, ___, 0, 0xba, I64_EQZ, "i64.eqz")

typedef enum WasmOpcode {
#define V(rtype, type1, type2, mem_size, code, NAME, text) \
  WASM_OPCODE_##NAME = code,
  WASM_FOREACH_OPCODE(V)
#undef V
} WasmOpcode;

typedef enum WasmLiteralType {
  WASM_LITERAL_TYPE_INT,
  WASM_LITERAL_TYPE_FLOAT,
  WASM_LITERAL_TYPE_HEXFLOAT,
  WASM_LITERAL_TYPE_INFINITY,
  WASM_LITERAL_TYPE_NAN,
} WasmLiteralType;

typedef struct WasmLiteral {
  WasmLiteralType type;
  WasmStringSlice text;
} WasmLiteral;

WASM_EXTERN_C_BEGIN
/* return 1 if |alignment| matches the alignment of |opcode|, or if |alignment|
 * is WASM_USE_NATURAL_ALIGNMENT */
int wasm_is_naturally_aligned(WasmOpcode opcode, uint32_t alignment);

/* if |alignment| is WASM_USE_NATURAL_ALIGNMENT, return the alignment of
 * |opcode|, else return |alignment| */
uint32_t wasm_get_opcode_alignment(WasmOpcode opcode, uint32_t alignment);

int wasm_string_slices_are_equal(const WasmStringSlice*,
                                 const WasmStringSlice*);
void wasm_destroy_string_slice(struct WasmAllocator*, WasmStringSlice*);
/* dump memory to stdout similar to the xxd format */
void wasm_print_memory(const void* start,
                       size_t size,
                       size_t offset,
                       int print_chars,
                       const char* desc);
WASM_EXTERN_C_END

#endif /* WASM_COMMON_H_ */
