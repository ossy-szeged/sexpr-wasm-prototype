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

#include <alloca.h>
#include <assert.h>
#include <math.h>
#include <memory.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "wasm.h"
#include "wasm-internal.h"

#define DEFAULT_MEMORY_EXPORT 1
#define DUMP_OCTETS_PER_LINE 16

#define SEGMENT_SIZE 13
#define SEGMENT_OFFSET_OFFSET 4

#define IMPORT_SIZE 7
#define IMPORT_NAME_OFFSET 3

#define FUNC_NAME_OFFSET 3

#define ALLOC_FAILURE \
  fprintf(stderr, "%s:%d: allocation failed\n", __FILE__, __LINE__)

#define CHECK_ALLOC_(cond)      \
  do {                          \
    if ((cond)) {               \
      ALLOC_FAILURE;            \
      ctx->result = WASM_ERROR; \
      return;                   \
    }                           \
  } while (0)

#define CHECK_ALLOC(e) CHECK_ALLOC_((e) != WASM_OK)
#define CHECK_ALLOC_NULL(v) CHECK_ALLOC_(!(v))

typedef enum WasmSectionType {
  WASM_SECTION_MEMORY = 0,
  WASM_SECTION_SIGNATURES = 1,
  WASM_SECTION_FUNCTIONS = 2,
  WASM_SECTION_GLOBALS = 3,
  WASM_SECTION_DATA_SEGMENTS = 4,
  WASM_SECTION_FUNCTION_TABLE = 5,
  WASM_SECTION_END = 6,
} WasmSectionType;

enum {
  WASM_FUNCTION_FLAG_NAME = 1,
  WASM_FUNCTION_FLAG_IMPORT = 2,
  WASM_FUNCTION_FLAG_LOCALS = 4,
  WASM_FUNCTION_FLAG_EXPORT = 8,
};
typedef uint8_t WasmFunctionFlags;

typedef enum WasmTypeV8 {
  WASM_TYPE_V8_VOID,
  WASM_TYPE_V8_I32,
  WASM_TYPE_V8_I64,
  WASM_TYPE_V8_F32,
  WASM_TYPE_V8_F64,
  WASM_NUM_V8_TYPES,
} WasmTypeV8;

#define FOREACH_OPCODE(V)      \
  V(NOP, 0x00)                 \
  V(BLOCK, 0x01)               \
  V(LOOP, 0x02)                \
  V(IF, 0x03)                  \
  V(ELSE, 0x04)                \
  V(SELECT, 0x05)              \
  V(BR, 0x06)                  \
  V(BR_IF, 0x07)               \
  V(TABLESWITCH, 0x08)         \
  V(RETURN, 0x14)              \
  V(UNREACHABLE, 0x15)         \
  V(NEXT, 0x16)                \
  V(END, 0x17)                 \
  V(I8_CONST, 0x09)            \
  V(I32_CONST, 0x0a)           \
  V(I64_CONST, 0x0b)           \
  V(F64_CONST, 0x0c)           \
  V(F32_CONST, 0x0d)           \
  V(GET_LOCAL, 0x0e)           \
  V(SET_LOCAL, 0x0f)           \
  V(LOAD_GLOBAL, 0x10)         \
  V(STORE_GLOBAL, 0x11)        \
  V(CALL_FUNCTION, 0x12)       \
  V(CALL_INDIRECT, 0x13)       \
  V(I32_LOAD_MEM8_S, 0x20)     \
  V(I32_LOAD_MEM8_U, 0x21)     \
  V(I32_LOAD_MEM16_S, 0x22)    \
  V(I32_LOAD_MEM16_U, 0x23)    \
  V(I64_LOAD_MEM8_S, 0x24)     \
  V(I64_LOAD_MEM8_U, 0x25)     \
  V(I64_LOAD_MEM16_S, 0x26)    \
  V(I64_LOAD_MEM16_U, 0x27)    \
  V(I64_LOAD_MEM32_S, 0x28)    \
  V(I64_LOAD_MEM32_U, 0x29)    \
  V(I32_LOAD_MEM, 0x2a)        \
  V(I64_LOAD_MEM, 0x2b)        \
  V(F32_LOAD_MEM, 0x2c)        \
  V(F64_LOAD_MEM, 0x2d)        \
  V(I32_STORE_MEM8, 0x2e)      \
  V(I32_STORE_MEM16, 0x2f)     \
  V(I64_STORE_MEM8, 0x30)      \
  V(I64_STORE_MEM16, 0x31)     \
  V(I64_STORE_MEM32, 0x32)     \
  V(I32_STORE_MEM, 0x33)       \
  V(I64_STORE_MEM, 0x34)       \
  V(F32_STORE_MEM, 0x35)       \
  V(F64_STORE_MEM, 0x36)       \
  V(MEMORY_SIZE, 0x3b)         \
  V(RESIZE_MEM_L, 0x39)        \
  V(RESIZE_MEM_H, 0x3a)        \
  V(I32_ADD, 0x40)             \
  V(I32_SUB, 0x41)             \
  V(I32_MUL, 0x42)             \
  V(I32_DIV_S, 0x43)           \
  V(I32_DIV_U, 0x44)           \
  V(I32_REM_S, 0x45)           \
  V(I32_REM_U, 0x46)           \
  V(I32_AND, 0x47)             \
  V(I32_OR, 0x48)              \
  V(I32_XOR, 0x49)             \
  V(I32_SHL, 0x4a)             \
  V(I32_SHR_U, 0x4b)           \
  V(I32_SHR_S, 0x4c)           \
  V(I32_EQ, 0x4d)              \
  V(I32_NE, 0x4e)              \
  V(I32_LT_S, 0x4f)            \
  V(I32_LE_S, 0x50)            \
  V(I32_LT_U, 0x51)            \
  V(I32_LE_U, 0x52)            \
  V(I32_GT_S, 0x53)            \
  V(I32_GE_S, 0x54)            \
  V(I32_GT_U, 0x55)            \
  V(I32_GE_U, 0x56)            \
  V(I32_CLZ, 0x57)             \
  V(I32_CTZ, 0x58)             \
  V(I32_POPCNT, 0x59)          \
  V(BOOL_NOT, 0x5a)            \
  V(I64_ADD, 0x5b)             \
  V(I64_SUB, 0x5c)             \
  V(I64_MUL, 0x5d)             \
  V(I64_DIV_S, 0x5e)           \
  V(I64_DIV_U, 0x5f)           \
  V(I64_REM_S, 0x60)           \
  V(I64_REM_U, 0x61)           \
  V(I64_AND, 0x62)             \
  V(I64_OR, 0x63)              \
  V(I64_XOR, 0x64)             \
  V(I64_SHL, 0x65)             \
  V(I64_SHR_U, 0x66)           \
  V(I64_SHR_S, 0x67)           \
  V(I64_EQ, 0x68)              \
  V(I64_NE, 0x69)              \
  V(I64_LT_S, 0x6a)            \
  V(I64_LE_S, 0x6b)            \
  V(I64_LT_U, 0x6c)            \
  V(I64_LE_U, 0x6d)            \
  V(I64_GT_S, 0x6e)            \
  V(I64_GE_S, 0x6f)            \
  V(I64_GT_U, 0x70)            \
  V(I64_GE_U, 0x71)            \
  V(I64_CLZ, 0x72)             \
  V(I64_CTZ, 0x73)             \
  V(I64_POPCNT, 0x74)          \
  V(F32_ADD, 0x75)             \
  V(F32_SUB, 0x76)             \
  V(F32_MUL, 0x77)             \
  V(F32_DIV, 0x78)             \
  V(F32_MIN, 0x79)             \
  V(F32_MAX, 0x7a)             \
  V(F32_ABS, 0x7b)             \
  V(F32_NEG, 0x7c)             \
  V(F32_COPYSIGN, 0x7d)        \
  V(F32_CEIL, 0x7e)            \
  V(F32_FLOOR, 0x7f)           \
  V(F32_TRUNC, 0x80)           \
  V(F32_NEAREST_INT, 0x81)     \
  V(F32_SQRT, 0x82)            \
  V(F32_EQ, 0x83)              \
  V(F32_NE, 0x84)              \
  V(F32_LT, 0x85)              \
  V(F32_LE, 0x86)              \
  V(F32_GT, 0x87)              \
  V(F32_GE, 0x88)              \
  V(F64_ADD, 0x89)             \
  V(F64_SUB, 0x8a)             \
  V(F64_MUL, 0x8b)             \
  V(F64_DIV, 0x8c)             \
  V(F64_MIN, 0x8d)             \
  V(F64_MAX, 0x8e)             \
  V(F64_ABS, 0x8f)             \
  V(F64_NEG, 0x90)             \
  V(F64_COPYSIGN, 0x91)        \
  V(F64_CEIL, 0x92)            \
  V(F64_FLOOR, 0x93)           \
  V(F64_TRUNC, 0x94)           \
  V(F64_NEAREST_INT, 0x95)     \
  V(F64_SQRT, 0x96)            \
  V(F64_EQ, 0x97)              \
  V(F64_NE, 0x98)              \
  V(F64_LT, 0x99)              \
  V(F64_LE, 0x9a)              \
  V(F64_GT, 0x9b)              \
  V(F64_GE, 0x9c)              \
  V(I32_SCONVERT_F32, 0x9d)    \
  V(I32_SCONVERT_F64, 0x9e)    \
  V(I32_UCONVERT_F32, 0x9f)    \
  V(I32_UCONVERT_F64, 0xa0)    \
  V(I32_CONVERT_I64, 0xa1)     \
  V(I64_SCONVERT_F32, 0xa2)    \
  V(I64_SCONVERT_F64, 0xa3)    \
  V(I64_UCONVERT_F32, 0xa4)    \
  V(I64_UCONVERT_F64, 0xa5)    \
  V(I64_SCONVERT_I32, 0xa6)    \
  V(I64_UCONVERT_I32, 0xa7)    \
  V(F32_SCONVERT_I32, 0xa8)    \
  V(F32_UCONVERT_I32, 0xa9)    \
  V(F32_SCONVERT_I64, 0xaa)    \
  V(F32_UCONVERT_I64, 0xab)    \
  V(F32_CONVERT_F64, 0xac)     \
  V(F32_REINTERPRET_I32, 0xad) \
  V(F64_SCONVERT_I32, 0xae)    \
  V(F64_UCONVERT_I32, 0xaf)    \
  V(F64_SCONVERT_I64, 0xb0)    \
  V(F64_UCONVERT_I64, 0xb1)    \
  V(F64_CONVERT_F32, 0xb2)     \
  V(F64_REINTERPRET_I64, 0xb3) \
  V(I32_REINTERPRET_F32, 0xb4) \
  V(I64_REINTERPRET_F64, 0xb5)

typedef enum WasmOpcode {
#define V(name, code) WASM_OPCODE_##name = code,
  FOREACH_OPCODE(V)
#undef V
} WasmOpcode;

#define V(name, code) [code] = "OPCODE_" #name,
static const char* s_opcode_names[] = {FOREACH_OPCODE(V)};
#undef V

static uint8_t s_binary_opcodes[] = {
    WASM_OPCODE_F32_ADD,   WASM_OPCODE_F32_COPYSIGN, WASM_OPCODE_F32_DIV,
    WASM_OPCODE_F32_MAX,   WASM_OPCODE_F32_MIN,      WASM_OPCODE_F32_MUL,
    WASM_OPCODE_F32_SUB,   WASM_OPCODE_F64_ADD,      WASM_OPCODE_F64_COPYSIGN,
    WASM_OPCODE_F64_DIV,   WASM_OPCODE_F64_MAX,      WASM_OPCODE_F64_MIN,
    WASM_OPCODE_F64_MUL,   WASM_OPCODE_F64_SUB,      WASM_OPCODE_I32_ADD,
    WASM_OPCODE_I32_AND,   WASM_OPCODE_I32_DIV_S,    WASM_OPCODE_I32_DIV_U,
    WASM_OPCODE_I32_MUL,   WASM_OPCODE_I32_OR,       WASM_OPCODE_I32_REM_S,
    WASM_OPCODE_I32_REM_U, WASM_OPCODE_I32_SHL,      WASM_OPCODE_I32_SHR_S,
    WASM_OPCODE_I32_SHR_U, WASM_OPCODE_I32_SUB,      WASM_OPCODE_I32_XOR,
    WASM_OPCODE_I64_ADD,   WASM_OPCODE_I64_AND,      WASM_OPCODE_I64_DIV_S,
    WASM_OPCODE_I64_DIV_U, WASM_OPCODE_I64_MUL,      WASM_OPCODE_I64_OR,
    WASM_OPCODE_I64_REM_S, WASM_OPCODE_I64_REM_U,    WASM_OPCODE_I64_SHL,
    WASM_OPCODE_I64_SHR_S, WASM_OPCODE_I64_SHR_U,    WASM_OPCODE_I64_SUB,
    WASM_OPCODE_I64_XOR,
};
STATIC_ASSERT(ARRAY_SIZE(s_binary_opcodes) == WASM_NUM_BINARY_OP_TYPES);

static uint8_t s_compare_opcodes[] = {
    WASM_OPCODE_F32_EQ,   WASM_OPCODE_F32_GE,   WASM_OPCODE_F32_GT,
    WASM_OPCODE_F32_LE,   WASM_OPCODE_F32_LT,   WASM_OPCODE_F32_NE,
    WASM_OPCODE_F64_EQ,   WASM_OPCODE_F64_GE,   WASM_OPCODE_F64_GT,
    WASM_OPCODE_F64_LE,   WASM_OPCODE_F64_LT,   WASM_OPCODE_F64_NE,
    WASM_OPCODE_I32_EQ,   WASM_OPCODE_I32_GE_S, WASM_OPCODE_I32_GE_U,
    WASM_OPCODE_I32_GT_S, WASM_OPCODE_I32_GT_U, WASM_OPCODE_I32_LE_S,
    WASM_OPCODE_I32_LE_U, WASM_OPCODE_I32_LT_S, WASM_OPCODE_I32_LT_U,
    WASM_OPCODE_I32_NE,   WASM_OPCODE_I64_EQ,   WASM_OPCODE_I64_GE_S,
    WASM_OPCODE_I64_GE_U, WASM_OPCODE_I64_GT_S, WASM_OPCODE_I64_GT_U,
    WASM_OPCODE_I64_LE_S, WASM_OPCODE_I64_LE_U, WASM_OPCODE_I64_LT_S,
    WASM_OPCODE_I64_LT_U, WASM_OPCODE_I64_NE,
};
STATIC_ASSERT(ARRAY_SIZE(s_compare_opcodes) == WASM_NUM_COMPARE_OP_TYPES);

static uint8_t s_convert_opcodes[] = {
    WASM_OPCODE_F32_SCONVERT_I32,    WASM_OPCODE_F32_SCONVERT_I64,
    WASM_OPCODE_F32_UCONVERT_I32,    WASM_OPCODE_F32_UCONVERT_I64,
    WASM_OPCODE_F32_CONVERT_F64,     WASM_OPCODE_F64_SCONVERT_I32,
    WASM_OPCODE_F64_SCONVERT_I64,    WASM_OPCODE_F64_UCONVERT_I32,
    WASM_OPCODE_F64_UCONVERT_I64,    WASM_OPCODE_F64_CONVERT_F32,
    WASM_OPCODE_I32_SCONVERT_F32,    WASM_OPCODE_I32_SCONVERT_F64,
    WASM_OPCODE_I32_UCONVERT_F32,    WASM_OPCODE_I32_UCONVERT_F64,
    WASM_OPCODE_I32_CONVERT_I64,     WASM_OPCODE_I64_SCONVERT_I32,
    WASM_OPCODE_I64_UCONVERT_I32,    WASM_OPCODE_I64_SCONVERT_F32,
    WASM_OPCODE_I64_SCONVERT_F64,    WASM_OPCODE_I64_UCONVERT_F32,
    WASM_OPCODE_I64_UCONVERT_F64,    WASM_OPCODE_F32_REINTERPRET_I32,
    WASM_OPCODE_F64_REINTERPRET_I64, WASM_OPCODE_I32_REINTERPRET_F32,
    WASM_OPCODE_I64_REINTERPRET_F64,
};
STATIC_ASSERT(ARRAY_SIZE(s_convert_opcodes) == WASM_NUM_CONVERT_OP_TYPES);

static uint8_t s_mem_opcodes[] = {
    WASM_OPCODE_F32_LOAD_MEM,     WASM_OPCODE_F32_STORE_MEM,
    WASM_OPCODE_F64_LOAD_MEM,     WASM_OPCODE_F64_STORE_MEM,
    WASM_OPCODE_I32_LOAD_MEM,     WASM_OPCODE_I32_LOAD_MEM8_S,
    WASM_OPCODE_I32_LOAD_MEM8_U,  WASM_OPCODE_I32_LOAD_MEM16_S,
    WASM_OPCODE_I32_LOAD_MEM16_U, WASM_OPCODE_I32_STORE_MEM,
    WASM_OPCODE_I32_STORE_MEM8,   WASM_OPCODE_I32_STORE_MEM16,
    WASM_OPCODE_I64_LOAD_MEM,     WASM_OPCODE_I64_LOAD_MEM8_S,
    WASM_OPCODE_I64_LOAD_MEM8_U,  WASM_OPCODE_I64_LOAD_MEM16_S,
    WASM_OPCODE_I64_LOAD_MEM16_U, WASM_OPCODE_I64_LOAD_MEM32_S,
    WASM_OPCODE_I64_LOAD_MEM32_U, WASM_OPCODE_I64_STORE_MEM,
    WASM_OPCODE_I64_STORE_MEM8,   WASM_OPCODE_I64_STORE_MEM16,
    WASM_OPCODE_I64_STORE_MEM32,
};
STATIC_ASSERT(ARRAY_SIZE(s_mem_opcodes) == WASM_NUM_MEM_OP_TYPES);

static uint8_t s_unary_opcodes[] = {
    WASM_OPCODE_F32_ABS,         WASM_OPCODE_F32_CEIL,
    WASM_OPCODE_F32_FLOOR,       WASM_OPCODE_F32_NEAREST_INT,
    WASM_OPCODE_F32_NEG,         WASM_OPCODE_F32_SQRT,
    WASM_OPCODE_F32_TRUNC,       WASM_OPCODE_F64_ABS,
    WASM_OPCODE_F64_CEIL,        WASM_OPCODE_F64_FLOOR,
    WASM_OPCODE_F64_NEAREST_INT, WASM_OPCODE_F64_NEG,
    WASM_OPCODE_F64_SQRT,        WASM_OPCODE_F64_TRUNC,
    WASM_OPCODE_I32_CLZ,         WASM_OPCODE_I32_CTZ,
    WASM_OPCODE_BOOL_NOT,        WASM_OPCODE_I32_POPCNT,
    WASM_OPCODE_I64_CLZ,         WASM_OPCODE_I64_CTZ,
    WASM_OPCODE_I64_POPCNT,
};
STATIC_ASSERT(ARRAY_SIZE(s_unary_opcodes) == WASM_NUM_UNARY_OP_TYPES);

typedef struct WasmLabelNode {
  WasmLabel* label;
  int depth;
  struct WasmLabelNode* next;
} WasmLabelNode;

typedef struct WasmWriterState {
  WasmWriter* writer;
  size_t offset;
  WasmResult* result;
  int log_writes;
} WasmWriterState;

typedef struct WasmWriteContext {
  WasmWriterState writer_state;
  WasmWriterState spec_writer_state;
  WasmWriteBinaryOptions* options;
  WasmMemoryWriter* mem_writer;
  WasmLabelNode* top_label;
  int max_depth;
  WasmResult result;

  int* import_sig_indexes;
  int* func_sig_indexes;
  int* remapped_locals;
  size_t* func_offsets;
} WasmWriteContext;

DECLARE_VECTOR(func_signature, WasmFuncSignature);
DEFINE_VECTOR(func_signature, WasmFuncSignature);

static void wasm_destroy_func_signature_vector_and_elements(
    WasmFuncSignatureVector* sigs) {
  DESTROY_VECTOR_AND_ELEMENTS(*sigs, func_signature);
}

static int is_power_of_two(uint32_t x) {
  return x && ((x & (x - 1)) == 0);
}

static uint32_t log_two_u32(uint32_t x) {
  if (x <= 1)
    return 0;
  return sizeof(unsigned int) * 8 - __builtin_clz(x - 1);
}

static WasmTypeV8 wasm_type_to_v8_type(WasmType type) {
  switch (type) {
    case WASM_TYPE_VOID:
      return WASM_TYPE_V8_VOID;
    case WASM_TYPE_I32:
      return WASM_TYPE_V8_I32;
    case WASM_TYPE_I64:
      return WASM_TYPE_V8_I64;
    case WASM_TYPE_F32:
      return WASM_TYPE_V8_F32;
    case WASM_TYPE_F64:
      return WASM_TYPE_V8_F64;
    default:
      FATAL("v8-native does not support type %d\n", type);
  }
}

static void print_header(WasmWriteContext* ctx, const char* name, int index) {
  if (ctx->options->log_writes)
    printf("; %s %d\n", name, index);
}

static void out_data_at(WasmWriterState* writer_state,
                        size_t offset,
                        const void* src,
                        size_t size,
                        const char* desc) {
  if (*writer_state->result != WASM_OK)
    return;
  if (writer_state->log_writes)
    wasm_print_memory(src, size, offset, 0, desc);
  if (writer_state->writer->write_data)
    *writer_state->result = writer_state->writer->write_data(
        offset, src, size, writer_state->writer->user_data);
}

static void out_u8(WasmWriterState* writer_state,
                   uint32_t value,
                   const char* desc) {
  assert(value <= UINT8_MAX);
  uint8_t value8 = value;
  out_data_at(writer_state, writer_state->offset, &value8, sizeof(value8),
              desc);
  writer_state->offset += sizeof(value8);
}

/* TODO(binji): endianness */
static void out_u16(WasmWriterState* writer_state,
                   uint32_t value,
                   const char* desc) {
  assert(value <= UINT16_MAX);
  uint16_t value16 = value;
  out_data_at(writer_state, writer_state->offset, &value16, sizeof(value16),
              desc);
  writer_state->offset += sizeof(value16);
}

static void out_u32(WasmWriterState* writer_state,
                   uint32_t value,
                   const char* desc) {
  out_data_at(writer_state, writer_state->offset, &value, sizeof(value), desc);
  writer_state->offset += sizeof(value);
}

static void out_u64(WasmWriterState* writer_state,
                   uint64_t value,
                   const char* desc) {
  out_data_at(writer_state, writer_state->offset, &value, sizeof(value), desc);
  writer_state->offset += sizeof(value);
}

static void out_f32(WasmWriterState* writer_state,
                      float value,
                      const char* desc) {
  out_data_at(writer_state, writer_state->offset, &value, sizeof(value), desc);
  writer_state->offset += sizeof(value);
}

static void out_f64(WasmWriterState* writer_state,
                    double value,
                    const char* desc) {
  out_data_at(writer_state, writer_state->offset, &value, sizeof(value), desc);
  writer_state->offset += sizeof(value);
}

static void out_u8_at(WasmWriterState* writer_state,
                      uint32_t offset,
                      uint32_t value,
                      const char* desc) {
  assert(value <= UINT8_MAX);
  uint8_t value8 = value;
  out_data_at(writer_state, offset, &value8, sizeof(value8), desc);
}

static void out_u16_at(WasmWriterState* writer_state,
                       uint32_t offset,
                       uint32_t value,
                       const char* desc) {
  assert(value <= UINT16_MAX);
  uint16_t value16 = value;
  out_data_at(writer_state, offset, &value16, sizeof(value16), desc);
}

static void out_u32_at(WasmWriterState* writer_state,
                       uint32_t offset,
                       uint32_t value,
                       const char* desc) {
  out_data_at(writer_state, offset, &value, sizeof(value), desc);
}

#undef OUT_AT_TYPE

/* returns the number of bytes written */
static int out_leb128_at(WasmWriterState* writer_state,
                         size_t offset,
                         uint32_t value,
                         const char* desc) {
  uint8_t data[5]; /* max 5 bytes */
  int i = 0;
  do {
    assert(i < 5);
    data[i] = value & 0x7f;
    value >>= 7;
    if (value)
      data[i] |= 0x80;
    ++i;
  } while (value);
  out_data_at(writer_state, offset, data, i, desc);
  return i;
}

static void out_leb128(WasmWriterState* writer_state,
                       uint32_t value,
                       const char* desc) {
  writer_state->offset +=
      out_leb128_at(writer_state, writer_state->offset, value, desc);
}

static void out_str(WasmWriterState* writer_state,
                    const char* s,
                    size_t length,
                    const char* desc) {
  out_data_at(writer_state, writer_state->offset, s, length, desc);
  writer_state->offset += length;
  out_u8(writer_state, 0, "\\0");
}

static void out_opcode(WasmWriterState* writer_state, uint8_t opcode) {
  out_u8(writer_state, opcode, s_opcode_names[opcode]);
}

static void out_printf(WasmWriterState* writer_state, const char* format, ...) {
  va_list args;
  va_list args_copy;
  va_start(args, format);
  va_copy(args_copy, args);
  /* + 1 to account for the \0 that will be added automatically by vsnprintf */
  int len = vsnprintf(NULL, 0, format, args) + 1;
  va_end(args);
  char* buffer = alloca(len);
  vsnprintf(buffer, len, format, args_copy);
  va_end(args_copy);
  /* - 1 to remove the trailing \0 that was added by vsnprintf */
  out_data_at(writer_state, writer_state->offset, buffer, len - 1, "");
  writer_state->offset += len - 1;
}

static WasmLabelNode* find_label_by_name(WasmLabelNode* top_label,
                                         WasmStringSlice* name) {
  WasmLabelNode* node = top_label;
  while (node) {
    if (node->label && wasm_string_slices_are_equal(node->label, name))
      return node;
    node = node->next;
  }
}

static WasmLabelNode* find_label_by_var(WasmLabelNode* top_label,
                                        WasmVar* var) {
  if (var->type == WASM_VAR_TYPE_NAME)
    return find_label_by_name(top_label, &var->name);

  WasmLabelNode* node = top_label;
  int i = 0;
  while (node && i != var->index) {
    node = node->next;
    i++;
  }
  return node;
}

static void push_label(WasmWriteContext* ctx,
                       WasmLabelNode* node,
                       WasmLabel* label) {
  assert(label);
  node->label = label;
  node->next = ctx->top_label;
  node->depth = ctx->max_depth;
  ctx->top_label = node;
  ctx->max_depth++;
}

static void pop_label(WasmWriteContext* ctx, WasmLabel* label) {
  ctx->max_depth--;
  if (ctx->top_label && ctx->top_label->label == label)
    ctx->top_label = ctx->top_label->next;
}

static void push_implicit_block(WasmWriteContext* ctx) {
  ctx->max_depth++;
}

static void pop_implicit_block(WasmWriteContext* ctx) {
  ctx->max_depth--;
}

static int find_func_signature(WasmFuncSignatureVector* sigs,
                               WasmType result_type,
                               WasmTypeVector* param_types) {
  int i;
  for (i = 0; i < sigs->size; ++i) {
    WasmFuncSignature* sig2 = &sigs->data[i];
    if (sig2->result_type != result_type)
      continue;
    if (sig2->param_types.size != param_types->size)
      continue;
    int j;
    for (j = 0; j < param_types->size; ++j) {
      if (sig2->param_types.data[j] != param_types->data[j])
        break;
    }
    if (j == param_types->size)
      return i;
  }
  return -1;
}

static void get_func_signatures(WasmWriteContext* ctx,
                                WasmModule* module,
                                WasmFuncSignatureVector* sigs) {
  /* function types are not deduped; we don't want the signature index to match
   if they were specified separately in the source */
  int i;
  for (i = 0; i < module->func_types.size; ++i) {
    WasmFuncType* func_type = module->func_types.data[i];
    WasmFuncSignature* sig = wasm_append_func_signature(sigs);
    CHECK_ALLOC_NULL(sig);
    sig->result_type = func_type->sig.result_type;
    memset(&sig->param_types, 0, sizeof(sig->param_types));
    CHECK_ALLOC(
        wasm_extend_types(&sig->param_types, &func_type->sig.param_types));
  }

  ctx->import_sig_indexes =
      realloc(ctx->import_sig_indexes, module->imports.size * sizeof(int));
  if (module->imports.size)
    CHECK_ALLOC_NULL(ctx->import_sig_indexes);
  for (i = 0; i < module->imports.size; ++i) {
    WasmImport* import = module->imports.data[i];
    int index;
    if (import->import_type == WASM_IMPORT_HAS_FUNC_SIGNATURE) {
      index = find_func_signature(sigs, import->func_sig.result_type,
                                  &import->func_sig.param_types);
      if (index == -1) {
        index = sigs->size;
        WasmFuncSignature* sig = wasm_append_func_signature(sigs);
        CHECK_ALLOC_NULL(sig);
        sig->result_type = import->func_sig.result_type;
        memset(&sig->param_types, 0, sizeof(sig->param_types));
        CHECK_ALLOC(wasm_extend_types(&sig->param_types,
                                      &import->func_sig.param_types));
      }
    } else {
      assert(import->import_type == WASM_IMPORT_HAS_TYPE);
      WasmFuncType* func_type =
          wasm_get_func_type_by_var(module, &import->type_var);
      assert(func_type);
      index = find_func_signature(sigs, func_type->sig.result_type,
                                  &func_type->sig.param_types);
      assert(index != -1);
    }

    ctx->import_sig_indexes[i] = index;
  }

  ctx->func_sig_indexes =
      realloc(ctx->func_sig_indexes, module->funcs.size * sizeof(int));
  if (module->funcs.size)
    CHECK_ALLOC_NULL(ctx->func_sig_indexes);
  for (i = 0; i < module->funcs.size; ++i) {
    WasmFunc* func = module->funcs.data[i];
    int index;
    if (func->flags & WASM_FUNC_FLAG_HAS_FUNC_TYPE) {
      index = wasm_get_func_type_index_by_var(module, &func->type_var);
      assert(index != -1);
    } else {
      assert(func->flags & WASM_FUNC_FLAG_HAS_SIGNATURE);
      index = find_func_signature(sigs, func->result_type, &func->params.types);
      if (index == -1) {
        index = sigs->size;
        WasmFuncSignature* sig = wasm_append_func_signature(sigs);
        CHECK_ALLOC_NULL(sig);
        sig->result_type = func->result_type;
        memset(&sig->param_types, 0, sizeof(sig->param_types));
        CHECK_ALLOC(wasm_extend_types(&sig->param_types, &func->params.types));
      }
    }

    ctx->func_sig_indexes[i] = index;
  }
}

static void remap_locals(WasmWriteContext* ctx, WasmFunc* func) {
  int num_params = func->params.types.size;
  int num_locals = func->locals.types.size;
  int num_params_and_locals = num_params + num_locals;
  ctx->remapped_locals =
      realloc(ctx->remapped_locals, num_params_and_locals * sizeof(int));
  if (num_params_and_locals)
    CHECK_ALLOC_NULL(ctx->remapped_locals);

  int max[WASM_NUM_V8_TYPES] = {};
  int i;
  for (i = 0; i < num_locals; ++i) {
    WasmType type = func->locals.types.data[i];
    max[wasm_type_to_v8_type(type)]++;
  }

  /* params don't need remapping */
  for (i = 0; i < num_params; ++i)
    ctx->remapped_locals[i] = i;

  int start[WASM_NUM_V8_TYPES];
  start[WASM_TYPE_V8_I32] = num_params;
  start[WASM_TYPE_V8_I64] = start[WASM_TYPE_V8_I32] + max[WASM_TYPE_V8_I32];
  start[WASM_TYPE_V8_F32] = start[WASM_TYPE_V8_I64] + max[WASM_TYPE_V8_I64];
  start[WASM_TYPE_V8_F64] = start[WASM_TYPE_V8_F32] + max[WASM_TYPE_V8_F32];

  int seen[WASM_NUM_V8_TYPES] = {};
  for (i = 0; i < num_locals; ++i) {
    WasmType type = func->locals.types.data[i];
    WasmTypeV8 v8_type = wasm_type_to_v8_type(type);
    ctx->remapped_locals[num_params + i] = start[v8_type] + seen[v8_type]++;
  }
}

static void write_tableswitch_target(WasmWriteContext* ctx,
                                     WasmBindingHash* case_bindings,
                                     WasmCaseVector* cases,
                                     WasmTarget* target) {
  switch (target->type) {
    case WASM_TARGET_TYPE_CASE: {
      int index = wasm_get_index_from_var(case_bindings, &target->var);
      assert(index >= 0 && index < cases->size);
      out_u16(&ctx->writer_state, index, "case index");
      break;
    }

    case WASM_TARGET_TYPE_BR: {
      WasmLabelNode* node = find_label_by_var(ctx->top_label, &target->var);
      assert(node);
      out_u16(&ctx->writer_state, 0x8000 | (ctx->max_depth - node->depth - 1),
              "br depth");
      break;
    }
  }
}

static void write_expr_list(WasmWriteContext* ctx,
                            WasmModule* module,
                            WasmFunc* func,
                            WasmExprPtrVector* exprs);

static void write_expr(WasmWriteContext* ctx,
                       WasmModule* module,
                       WasmFunc* func,
                       WasmExpr* expr) {
  WasmWriterState* ws = &ctx->writer_state;
  switch (expr->type) {
    case WASM_EXPR_TYPE_BINARY:
      write_expr(ctx, module, func, expr->binary.left);
      write_expr(ctx, module, func, expr->binary.right);
      out_opcode(ws, s_binary_opcodes[expr->binary.op.op_type]);
      break;
    case WASM_EXPR_TYPE_BLOCK: {
      WasmLabelNode node;
      push_label(ctx, &node, &expr->block.label);
      out_opcode(ws, WASM_OPCODE_BLOCK);
      write_expr_list(ctx, module, func, &expr->block.exprs);
      out_opcode(ws, WASM_OPCODE_END);
      pop_label(ctx, &expr->block.label);
      break;
    }
    case WASM_EXPR_TYPE_BR: {
      WasmLabelNode* node = find_label_by_var(ctx->top_label, &expr->br.var);
      assert(node);
      if (expr->br.expr)
        write_expr(ctx, module, func, expr->br.expr);
      else
        out_opcode(ws, WASM_OPCODE_NOP);
      out_opcode(ws, WASM_OPCODE_BR);
      out_u8(ws, ctx->max_depth - node->depth - 1, "break depth");
      break;
    }
    case WASM_EXPR_TYPE_BR_IF: {
      WasmLabelNode* node = find_label_by_var(ctx->top_label, &expr->br_if.var);
      assert(node);
      if (expr->br_if.expr)
        write_expr(ctx, module, func, expr->br_if.expr);
      else
        out_opcode(ws, WASM_OPCODE_NOP);
      write_expr(ctx, module, func, expr->br_if.cond);
      out_opcode(ws, WASM_OPCODE_BR_IF);
      out_u8(ws, ctx->max_depth - node->depth - 1, "break depth");
      break;
    }
    case WASM_EXPR_TYPE_CALL: {
      int index = wasm_get_func_index_by_var(module, &expr->call.var);
      assert(index >= 0 && index < module->funcs.size);
      write_expr_list(ctx, module, func, &expr->call.args);
      out_opcode(ws, WASM_OPCODE_CALL_FUNCTION);
      /* defined functions are always after all imports */
      out_leb128(ws, module->imports.size + index, "func index");
      break;
    }
    case WASM_EXPR_TYPE_CALL_IMPORT: {
      int index = wasm_get_import_index_by_var(module, &expr->call.var);
      assert(index >= 0 && index < module->imports.size);
      write_expr_list(ctx, module, func, &expr->call.args);
      out_opcode(ws, WASM_OPCODE_CALL_FUNCTION);
      /* imports are always first */
      out_leb128(ws, index, "import index");
      break;
    }
    case WASM_EXPR_TYPE_CALL_INDIRECT: {
      int index =
          wasm_get_func_type_index_by_var(module, &expr->call_indirect.var);
      assert(index >= 0 && index < module->func_types.size);
      write_expr(ctx, module, func, expr->call_indirect.expr);
      write_expr_list(ctx, module, func, &expr->call_indirect.args);
      out_opcode(ws, WASM_OPCODE_CALL_INDIRECT);
      out_leb128(ws, index, "signature index");
      break;
    }
    case WASM_EXPR_TYPE_COMPARE:
      write_expr(ctx, module, func, expr->compare.left);
      write_expr(ctx, module, func, expr->compare.right);
      out_opcode(ws, s_compare_opcodes[expr->compare.op.op_type]);
      break;
    case WASM_EXPR_TYPE_CONST:
      switch (expr->const_.type) {
        case WASM_TYPE_I32: {
          int32_t i32;
          memcpy(&i32, &expr->const_.u32, sizeof(i32));
          if (i32 >= -128 && i32 <= 127) {
            out_opcode(ws, WASM_OPCODE_I8_CONST);
            out_u8(ws, (uint8_t)expr->const_.u32, "u8 literal");
          } else {
            out_opcode(ws, WASM_OPCODE_I32_CONST);
            out_u32(ws, expr->const_.u32, "u32 literal");
          }
          break;
        }
        case WASM_TYPE_I64:
          out_opcode(ws, WASM_OPCODE_I64_CONST);
          out_u64(ws, expr->const_.u64, "u64 literal");
          break;
        case WASM_TYPE_F32:
          out_opcode(ws, WASM_OPCODE_F32_CONST);
          out_f32(ws, expr->const_.f32, "f32 literal");
          break;
        case WASM_TYPE_F64:
          out_opcode(ws, WASM_OPCODE_F64_CONST);
          out_f64(ws, expr->const_.f64, "f64 literal");
          break;
        default:
          assert(0);
      }
      break;
    case WASM_EXPR_TYPE_CONVERT:
      write_expr(ctx, module, func, expr->convert.expr);
      out_opcode(ws, s_convert_opcodes[expr->convert.op.op_type]);
      break;
    case WASM_EXPR_TYPE_GET_LOCAL: {
      int index = wasm_get_local_index_by_var(func, &expr->get_local.var);
      assert(index >= 0 && index < func->params_and_locals.types.size);
      out_opcode(ws, WASM_OPCODE_GET_LOCAL);
      out_leb128(ws, ctx->remapped_locals[index], "remapped local index");
      break;
    }
    case WASM_EXPR_TYPE_GROW_MEMORY:
      write_expr(ctx, module, func, expr->grow_memory.expr);
      out_opcode(ws, WASM_OPCODE_RESIZE_MEM_L);
      break;
    case WASM_EXPR_TYPE_HAS_FEATURE:
      /* TODO(binji): add when supported by v8-native */
      out_u8(ws, WASM_OPCODE_I8_CONST, "has_feature not supported");
      out_u8(ws, 0, "");
      break;
    case WASM_EXPR_TYPE_IF:
      write_expr(ctx, module, func, expr->if_.cond);
      out_opcode(ws, WASM_OPCODE_IF);
      write_expr(ctx, module, func, expr->if_.true_);
      out_opcode(ws, WASM_OPCODE_END);
      break;
    case WASM_EXPR_TYPE_IF_ELSE:
      write_expr(ctx, module, func, expr->if_else.cond);
      out_opcode(ws, WASM_OPCODE_IF);
      write_expr(ctx, module, func, expr->if_else.true_);
      out_opcode(ws, WASM_OPCODE_ELSE);
      write_expr(ctx, module, func, expr->if_else.false_);
      out_opcode(ws, WASM_OPCODE_END);
      break;
    case WASM_EXPR_TYPE_LOAD: {
      /* Access byte: 0bAaao0000
       A = Alignment. If set, access is unaligned
       a = Atomicity. 0 = None, 1 = SeqCst, 2 = Acq, 3 = Rel
       o = Offset. If set, offset field follows access byte */
      write_expr(ctx, module, func, expr->load.addr);
      out_opcode(ws, s_mem_opcodes[expr->load.op.op_type]);
      uint32_t natural_align = expr->load.op.size >> 3;
      uint32_t align = expr->load.align;
      uint8_t access = 0;
      if (align == WASM_USE_NATURAL_ALIGNMENT)
        align = natural_align;
      if (align < natural_align)
        access |= 0x80;
      if (expr->load.offset)
        access |= 0x10;
      out_u8(ws, access, "load access byte");
      if (expr->load.offset)
        out_leb128(ws, (uint32_t)expr->load.offset, "load offset");
      break;
    }
    case WASM_EXPR_TYPE_LOAD_GLOBAL: {
      out_opcode(ws, WASM_OPCODE_LOAD_GLOBAL);
      int index = wasm_get_global_index_by_var(module, &expr->load_global.var);
      out_leb128(ws, index, "global index");
      break;
    }
    case WASM_EXPR_TYPE_LOOP: {
      WasmLabelNode outer;
      WasmLabelNode inner;
      push_label(ctx, &outer, &expr->loop.outer);
      push_label(ctx, &inner, &expr->loop.inner);
      out_opcode(ws, WASM_OPCODE_LOOP);
      write_expr_list(ctx, module, func, &expr->loop.exprs);
      out_opcode(ws, WASM_OPCODE_END);
      pop_label(ctx, &expr->loop.inner);
      pop_label(ctx, &expr->loop.outer);
      break;
    }
    case WASM_EXPR_TYPE_MEMORY_SIZE:
      out_opcode(ws, WASM_OPCODE_MEMORY_SIZE);
      break;
    case WASM_EXPR_TYPE_NOP:
      out_opcode(ws, WASM_OPCODE_NOP);
      break;
    case WASM_EXPR_TYPE_RETURN:
      if (expr->return_.expr)
        write_expr(ctx, module, func, expr->return_.expr);
      out_opcode(ws, WASM_OPCODE_RETURN);
      break;
    case WASM_EXPR_TYPE_SELECT:
      write_expr(ctx, module, func, expr->select.true_);
      write_expr(ctx, module, func, expr->select.false_);
      write_expr(ctx, module, func, expr->select.cond);
      out_opcode(ws, WASM_OPCODE_SELECT);
      break;
    case WASM_EXPR_TYPE_SET_LOCAL: {
      int index = wasm_get_local_index_by_var(func, &expr->get_local.var);
      write_expr(ctx, module, func, expr->set_local.expr);
      out_opcode(ws, WASM_OPCODE_SET_LOCAL);
      out_leb128(ws, ctx->remapped_locals[index], "remapped local index");
      break;
    }
    case WASM_EXPR_TYPE_STORE: {
      /* See LOAD for format of memory access byte */
      write_expr(ctx, module, func, expr->store.addr);
      write_expr(ctx, module, func, expr->store.value);
      out_opcode(ws, s_mem_opcodes[expr->store.op.op_type]);
      uint32_t natural_align = expr->load.op.size >> 3;
      uint32_t align = expr->load.align;
      uint8_t access = 0;
      if (align == WASM_USE_NATURAL_ALIGNMENT)
        align = natural_align;
      if (align < natural_align)
        access |= 0x80;
      if (expr->store.offset)
        access |= 0x10;
      out_u8(ws, access, "store access byte");
      if (expr->store.offset)
        out_leb128(ws, (uint32_t)expr->store.offset, "store offset");
      break;
    }
    case WASM_EXPR_TYPE_STORE_GLOBAL: {
      write_expr(ctx, module, func, expr->store_global.expr);
      out_opcode(ws, WASM_OPCODE_STORE_GLOBAL);
      int index = wasm_get_global_index_by_var(module, &expr->load_global.var);
      out_leb128(ws, index, "global index");
      break;
    }
    case WASM_EXPR_TYPE_TABLESWITCH: {
      WasmLabelNode node;
      push_label(ctx, &node, &expr->tableswitch.label);
      write_expr(ctx, module, func, expr->tableswitch.expr);
      out_opcode(ws, WASM_OPCODE_TABLESWITCH);
      out_u16(ws, expr->tableswitch.cases.size, "num cases");
      out_u16(ws, expr->tableswitch.targets.size + 1, "num targets");
      int i;
      for (i = 0; i < expr->tableswitch.targets.size; ++i) {
        WasmTarget* target = &expr->tableswitch.targets.data[i];
        write_tableswitch_target(ctx, &expr->tableswitch.case_bindings,
                                 &expr->tableswitch.cases, target);
      }
      write_tableswitch_target(ctx, &expr->tableswitch.case_bindings,
                               &expr->tableswitch.cases,
                               &expr->tableswitch.default_target);
      for (i = 0; i < expr->tableswitch.cases.size; ++i) {
        WasmCase* case_ = &expr->tableswitch.cases.data[i];
        write_expr_list(ctx, module, func, &case_->exprs);
        if (i < expr->tableswitch.cases.size - 1) {
          out_opcode(ws, WASM_OPCODE_NEXT);
        } else {
          out_opcode(ws, WASM_OPCODE_END);
        }
      }
      pop_label(ctx, &expr->tableswitch.label);
      break;
    }
    case WASM_EXPR_TYPE_UNARY:
      write_expr(ctx, module, func, expr->unary.expr);
      out_opcode(ws, s_unary_opcodes[expr->unary.op.op_type]);
      break;
    case WASM_EXPR_TYPE_UNREACHABLE:
      out_opcode(ws, WASM_OPCODE_UNREACHABLE);
      break;
  }
}

static void write_expr_list(WasmWriteContext* ctx,
                            WasmModule* module,
                            WasmFunc* func,
                            WasmExprPtrVector* exprs) {
  int i;
  for (i = 0; i < exprs->size; ++i)
    write_expr(ctx, module, func, exprs->data[i]);
}

static void write_func(WasmWriteContext* ctx,
                       WasmModule* module,
                       WasmFunc* func) {
  write_expr_list(ctx, module, func, &func->exprs);
}

static void write_module(WasmWriteContext* ctx, WasmModule* module) {
  int i;
  WasmWriterState* ws = &ctx->writer_state;
  ctx->writer_state.offset = 0;
  size_t segments_offset = 0;
  if (module->memory) {
    out_u8(ws, WASM_SECTION_MEMORY, "WASM_SECTION_MEMORY");
    out_u8(ws, log_two_u32(module->memory->initial_size), "min mem size log 2");
    out_u8(ws, log_two_u32(module->memory->max_size), "max mem size log 2");
    out_u8(ws, DEFAULT_MEMORY_EXPORT, "export mem");

    if (module->memory->segments.size) {
      out_u8(ws, WASM_SECTION_DATA_SEGMENTS, "WASM_SECTION_DATA_SEGMENTS");
      out_leb128(ws, module->memory->segments.size, "num data segments");
      segments_offset = ctx->writer_state.offset;
      for (i = 0; i < module->memory->segments.size; ++i) {
        WasmSegment* segment = &module->memory->segments.data[i];
        print_header(ctx, "segment header", i);
        out_u32(ws, segment->addr, "segment address");
        out_u32(ws, 0, "segment data offset");
        out_u32(ws, segment->size, "segment size");
        out_u8(ws, 1, "segment init");
      }
    }
  }

  if (module->globals.types.size) {
    out_u8(ws, WASM_SECTION_GLOBALS, "WASM_SECTION_GLOBALS");
    out_leb128(ws, module->globals.types.size, "num globals");
    for (i = 0; i < module->globals.types.size; ++i) {
      WasmType global_type = module->globals.types.data[i];
      print_header(ctx, "global header", i);
      const uint8_t global_type_codes[WASM_NUM_V8_TYPES] = {-1, 4, 6, 8, 9};
      out_u32(ws, 0, "global name offset");
      out_u8(ws, global_type_codes[wasm_type_to_v8_type(global_type)],
             "global mem type");
      out_u8(ws, 0, "export global");
    }
  }

  WasmFuncSignatureVector sigs = {};
  get_func_signatures(ctx, module, &sigs);
  if (sigs.size) {
    out_u8(ws, WASM_SECTION_SIGNATURES, "WASM_SECTION_SIGNATURES");
    out_leb128(ws, sigs.size, "num signatures");
    for (i = 0; i < sigs.size; ++i) {
      WasmFuncSignature* sig = &sigs.data[i];
      print_header(ctx, "signature", i);
      out_u8(ws, sig->param_types.size, "num params");
      out_u8(ws, wasm_type_to_v8_type(sig->result_type), "result_type");
      int j;
      for (j = 0; j < sig->param_types.size; ++j)
        out_u8(ws, wasm_type_to_v8_type(sig->param_types.data[j]),
               "param type");
    }
  }

  size_t imports_offset = 0;
  int num_funcs = module->imports.size + module->funcs.size;
  if (num_funcs) {
    out_u8(ws, WASM_SECTION_FUNCTIONS, "WASM_SECTION_FUNCTIONS");
    out_leb128(ws, num_funcs, "num functions");

    imports_offset = ctx->writer_state.offset;
    for (i = 0; i < module->imports.size; ++i) {
      print_header(ctx, "import header", i);
      WasmFunctionFlags flags =
          WASM_FUNCTION_FLAG_NAME | WASM_FUNCTION_FLAG_IMPORT;
      out_u8(ws, flags, "import flags");
      out_u16(ws, ctx->import_sig_indexes[i], "import signature index");
      out_u32(ws, 0, "import name offset");
    }

    ctx->func_offsets =
        realloc(ctx->func_offsets, module->funcs.size * sizeof(size_t));
    if (module->funcs.size)
      CHECK_ALLOC_NULL(ctx->func_offsets);
    for (i = 0; i < module->funcs.size; ++i) {
      WasmFunc* func = module->funcs.data[i];
      print_header(ctx, "function", i);
      ctx->func_offsets[i] = ctx->writer_state.offset;
      remap_locals(ctx, func);

      int is_exported = wasm_func_is_exported(module, func);
      int has_name = is_exported;
      int has_locals = func->locals.types.size > 0;
      uint8_t flags = 0;
      /* TODO(binji): add flag for start function (when added to the binary
       format) */
      if (has_name)
        flags |= WASM_FUNCTION_FLAG_NAME;
      if (is_exported)
        flags |= WASM_FUNCTION_FLAG_NAME | WASM_FUNCTION_FLAG_EXPORT;
      if (has_locals)
        flags |= WASM_FUNCTION_FLAG_LOCALS;
      out_u8(ws, flags, "func flags");
      out_u16(ws, ctx->func_sig_indexes[i], "func signature index");
      if (has_name)
        out_u32(ws, 0, "func name offset");
      if (has_locals) {
        int num_locals[WASM_NUM_V8_TYPES] = {};
        int j;
        for (j = 0; j < func->locals.types.size; ++j)
          num_locals[wasm_type_to_v8_type(func->locals.types.data[j])]++;

        out_u16(ws, num_locals[WASM_TYPE_V8_I32], "num local i32");
        out_u16(ws, num_locals[WASM_TYPE_V8_I64], "num local i64");
        out_u16(ws, num_locals[WASM_TYPE_V8_F32], "num local f32");
        out_u16(ws, num_locals[WASM_TYPE_V8_F64], "num local f64");
      }

      size_t func_body_offset = ctx->writer_state.offset;
      out_u16(ws, 0, "func body size");
      write_func(ctx, module, func);
      int func_size =
          ctx->writer_state.offset - func_body_offset - sizeof(uint16_t);
      out_u16_at(ws, func_body_offset, func_size, "FIXUP func body size");
    }
  }

  if (module->table && module->table->size) {
    out_u8(ws, WASM_SECTION_FUNCTION_TABLE, "WASM_SECTION_FUNCTION_TABLE");
    out_leb128(ws, module->table->size, "num function table entries");
    for (i = 0; i < module->table->size; ++i) {
      int index = wasm_get_func_index_by_var(module, &module->table->data[i]);
      assert(index >= 0 && index < module->funcs.size);
      out_u16(ws, index + module->imports.size, "function table entry");
    }
  }

  out_u8(ws, WASM_SECTION_END, "WASM_SECTION_END");

  /* output segment data */
  size_t offset;
  if (module->memory) {
    offset = segments_offset;
    for (i = 0; i < module->memory->segments.size; ++i) {
      print_header(ctx, "segment data", i);
      WasmSegment* segment = &module->memory->segments.data[i];
      out_u32_at(ws, offset + SEGMENT_OFFSET_OFFSET, ctx->writer_state.offset,
                 "FIXUP segment data offset");
      out_data_at(ws, ctx->writer_state.offset, segment->data, segment->size,
                  "segment data");
      ctx->writer_state.offset += segment->size;
      offset += SEGMENT_SIZE;
    }
  }

  /* output import names */
  offset = imports_offset;
  for (i = 0; i < module->imports.size; ++i) {
    print_header(ctx, "import", i);
    WasmImport* import = module->imports.data[i];
    out_u32_at(ws, offset + IMPORT_NAME_OFFSET, ctx->writer_state.offset,
               "FIXUP import name offset");
    out_str(ws, import->func_name.start, import->func_name.length,
            "import name");
    offset += IMPORT_SIZE;
  }

  /* output exported func names */
  for (i = 0; i < module->exports.size; ++i) {
    print_header(ctx, "export", i);
    WasmExport* export = module->exports.data[i];
    int func_index = wasm_get_func_index_by_var(module, &export->var);
    assert(func_index >= 0 && func_index < module->funcs.size);
    offset = ctx->func_offsets[func_index];
    out_u32_at(ws, offset + FUNC_NAME_OFFSET, ctx->writer_state.offset,
               "FIXUP func name offset");
    out_str(ws, export->name.start, export->name.length, "export name");
  }

  wasm_destroy_func_signature_vector_and_elements(&sigs);
}

static WasmExpr* new_expr(WasmExprType type) {
  WasmExpr* expr = calloc(1, sizeof(WasmExpr));
  if (!expr)
    return NULL;
  expr->type = type;
  return expr;
}

static WasmExpr* create_const_expr(WasmConst* const_) {
  WasmExpr* expr = new_expr(WASM_EXPR_TYPE_CONST);
  if (!expr)
    return NULL;
  expr->const_ = *const_;
  return expr;
}

static WasmExpr* create_invoke_expr(WasmCommandInvoke* invoke, int func_index) {
  WasmExpr* expr = new_expr(WASM_EXPR_TYPE_CALL);
  if (!expr)
    return NULL;
  expr->call.var.type = WASM_VAR_TYPE_INDEX;
  expr->call.var.index = func_index;
  int i;
  for (i = 0; i < invoke->args.size; ++i) {
    WasmConst* arg = &invoke->args.data[i];
    WasmExprPtr* expr_ptr = wasm_append_expr_ptr(&expr->call.args);
    if (!expr_ptr)
      goto fail;
    *expr_ptr = create_const_expr(arg);
    if (!*expr_ptr)
      goto fail;
  }
  return expr;
fail:
  for (i = 0; i < invoke->args.size; ++i)
    wasm_destroy_expr_ptr(&expr->call.args.data[i]);
  free(expr);
  return NULL;
}

static WasmExpr* create_eq_expr(WasmType type,
                                WasmExpr* left,
                                WasmExpr* right) {
  if (!left || !right) {
    wasm_destroy_expr_ptr(&left);
    wasm_destroy_expr_ptr(&right);
    return NULL;
  }

  WasmExpr* expr = new_expr(WASM_EXPR_TYPE_COMPARE);
  if (!expr)
    return NULL;
  expr->compare.op.type = type;
  switch (type) {
    case WASM_TYPE_I32:
      expr->compare.op.op_type = WASM_COMPARE_OP_TYPE_I32_EQ;
      break;
    case WASM_TYPE_I64:
      expr->compare.op.op_type = WASM_COMPARE_OP_TYPE_I64_EQ;
      break;
    case WASM_TYPE_F32:
      expr->compare.op.op_type = WASM_COMPARE_OP_TYPE_F32_EQ;
      break;
    case WASM_TYPE_F64:
      expr->compare.op.op_type = WASM_COMPARE_OP_TYPE_F64_EQ;
      break;
    default:
      assert(0);
  }
  expr->compare.left = left;
  expr->compare.right = right;
  return expr;
}

static WasmExpr* create_ne_expr(WasmType type,
                                WasmExpr* left,
                                WasmExpr* right) {
  if (!left || !right) {
    wasm_destroy_expr_ptr(&left);
    wasm_destroy_expr_ptr(&right);
    return NULL;
  }

  WasmExpr* expr = new_expr(WASM_EXPR_TYPE_COMPARE);
  if (!expr)
    return NULL;
  expr->compare.op.type = type;
  switch (type) {
    case WASM_TYPE_I32:
      expr->compare.op.op_type = WASM_COMPARE_OP_TYPE_I32_NE;
      break;
    case WASM_TYPE_I64:
      expr->compare.op.op_type = WASM_COMPARE_OP_TYPE_I64_NE;
      break;
    case WASM_TYPE_F32:
      expr->compare.op.op_type = WASM_COMPARE_OP_TYPE_F32_NE;
      break;
    case WASM_TYPE_F64:
      expr->compare.op.op_type = WASM_COMPARE_OP_TYPE_F64_NE;
      break;
    default:
      assert(0);
  }
  expr->compare.left = left;
  expr->compare.right = right;
  return expr;
}

static WasmExpr* create_set_local_expr(int index, WasmExpr* value) {
  if (!value) {
    wasm_destroy_expr_ptr(&value);
    return NULL;
  }

  WasmExpr* expr = new_expr(WASM_EXPR_TYPE_SET_LOCAL);
  if (!expr)
    return NULL;
  expr->set_local.var.type = WASM_VAR_TYPE_INDEX;
  expr->set_local.var.index = index;
  expr->set_local.expr = value;
  return expr;
}

static WasmExpr* create_get_local_expr(int index) {
  WasmExpr* expr = new_expr(WASM_EXPR_TYPE_GET_LOCAL);
  if (!expr)
    return NULL;
  expr->get_local.var.type = WASM_VAR_TYPE_INDEX;
  expr->get_local.var.index = index;
  return expr;
}

static WasmExpr* create_reinterpret_expr(WasmType type, WasmExpr* expr) {
  if (!expr) {
    wasm_destroy_expr_ptr(&expr);
    return NULL;
  }

  WasmExpr* result = new_expr(WASM_EXPR_TYPE_CONVERT);
  if (!result)
    return NULL;
  switch (type) {
    case WASM_TYPE_F32:
      result->convert.op.op_type = WASM_CONVERT_OP_TYPE_I32_REINTERPRET_F32;
      break;
    case WASM_TYPE_F64:
      result->convert.op.op_type = WASM_CONVERT_OP_TYPE_I64_REINTERPRET_F64;
      break;
    default:
      assert(0);
      break;
  }
  result->convert.expr = expr;
  return result;
}

static WasmModuleField* append_module_field_and_fixup(
    WasmModule* module,
    WasmModuleFieldType module_field_type) {
  WasmModuleField* first_field = &module->fields.data[0];
  WasmModuleField* result = wasm_append_module_field(&module->fields);
  if (!result)
    return NULL;
  memset(result, 0, sizeof(*result));
  result->type = module_field_type;

  /* make space for the new entry, it will be assigned twice, once here, once
   below. It is easier to do this than check below whether the new value was
   already assigned. */
  switch (module_field_type) {
    case WASM_MODULE_FIELD_TYPE_FUNC: {
      WasmFuncPtr* func_ptr = wasm_append_func_ptr(&module->funcs);
      if (!func_ptr)
        goto fail;
      *func_ptr = &result->func;
      break;
    }
    case WASM_MODULE_FIELD_TYPE_IMPORT: {
      WasmImportPtr* import_ptr = wasm_append_import_ptr(&module->imports);
      if (!import_ptr)
        goto fail;
      *import_ptr = &result->import;
      break;
    }
    case WASM_MODULE_FIELD_TYPE_EXPORT: {
      WasmExportPtr* export_ptr = wasm_append_export_ptr(&module->exports);
      if (!export_ptr)
        goto fail;
      *export_ptr = &result->export_;
      break;
    }
    case WASM_MODULE_FIELD_TYPE_FUNC_TYPE: {
      WasmFuncTypePtr* func_type_ptr =
          wasm_append_func_type_ptr(&module->func_types);
      if (!func_type_ptr)
        goto fail;
      *func_type_ptr = &result->func_type;
      break;
    }

    case WASM_MODULE_FIELD_TYPE_TABLE:
    case WASM_MODULE_FIELD_TYPE_MEMORY:
    case WASM_MODULE_FIELD_TYPE_GLOBAL:
    case WASM_MODULE_FIELD_TYPE_START:
      /* not supported */
      assert(0);
      break;
  }

  if (first_field == &module->fields.data[0])
    return result;

  /* the first element moved, so we need to fixup all the pointers */
  int i;
  int num_funcs = 0;
  int num_imports = 0;
  int num_exports = 0;
  int num_func_types = 0;
  for (i = 0; i < module->fields.size; ++i) {
    WasmModuleField* field = &module->fields.data[i];
    switch (field->type) {
      case WASM_MODULE_FIELD_TYPE_FUNC:
        assert(num_funcs < module->funcs.size);
        module->funcs.data[num_funcs++] = &field->func;
        break;
      case WASM_MODULE_FIELD_TYPE_IMPORT:
        assert(num_imports < module->imports.size);
        module->imports.data[num_imports++] = &field->import;
        break;
      case WASM_MODULE_FIELD_TYPE_EXPORT:
        assert(num_exports < module->exports.size);
        module->exports.data[num_exports++] = &field->export_;
        break;
      case WASM_MODULE_FIELD_TYPE_TABLE:
        module->table = &field->table;
        break;
      case WASM_MODULE_FIELD_TYPE_FUNC_TYPE:
        assert(num_func_types < module->func_types.size);
        module->func_types.data[num_func_types++] = &field->func_type;
        break;
      case WASM_MODULE_FIELD_TYPE_MEMORY:
        module->memory = &field->memory;
        break;
      case WASM_MODULE_FIELD_TYPE_GLOBAL:
      case WASM_MODULE_FIELD_TYPE_START:
        /* not pointers, so they don't need to be fixed */
        break;
    }
  }

  return result;
fail:
  module->fields.size--;
  return NULL;
}

static WasmStringSlice create_assert_func_name(const char* format,
                                               int format_index) {
  WasmStringSlice name;
  char buffer[256];
  int buffer_len = snprintf(buffer, 256, format, format_index);
  name.start = strdup(buffer);
  name.length = buffer_len;
  return name;
}

static WasmFunc* append_nullary_func(WasmModule* module,
                                     WasmType result_type,
                                     WasmStringSlice export_name) {
  WasmModuleField* func_field =
      append_module_field_and_fixup(module, WASM_MODULE_FIELD_TYPE_FUNC);
  if (!func_field)
    return NULL;
  WasmFunc* func = &func_field->func;
  func->flags = WASM_FUNC_FLAG_HAS_SIGNATURE;
  func->result_type = result_type;
  int func_index = module->funcs.size - 1;
  /* invalidated by append_module_field_and_fixup below */
  func_field = NULL;
  func = NULL;

  WasmModuleField* export_field =
      append_module_field_and_fixup(module, WASM_MODULE_FIELD_TYPE_EXPORT);
  if (!export_field) {
    /* leave the func field, it will be cleaned up later */
    return NULL;
  }
  WasmExport* export = &export_field->export_;
  export->var.type = WASM_VAR_TYPE_INDEX;
  export->var.index = func_index;
  export->name = export_name;
  return module->funcs.data[func_index];
}

static void write_header_spec(WasmWriteContext* ctx) {
  out_printf(&ctx->spec_writer_state, "var quiet = %s;\n",
             ctx->options->spec_verbose ? "false" : "true");
}

static void write_footer_spec(WasmWriteContext* ctx) {
  out_printf(&ctx->spec_writer_state, "end();\n");
}

static void write_module_header_spec(WasmWriteContext* ctx) {
  out_printf(&ctx->spec_writer_state, "var tests = function(m) {\n");
}

static void write_module_spec(WasmWriteContext* ctx, WasmModule* module) {
  /* reset the memory writer in case it's already been used */
  ctx->mem_writer->buf.size = 0;
  ctx->writer_state.offset = 0;

  write_module(ctx, module);
  /* copy the written module from the output buffer */
  out_printf(&ctx->spec_writer_state, "};\nvar m = createModule([\n");
  WasmOutputBuffer* module_buf = &ctx->mem_writer->buf;
  const uint8_t* p = module_buf->start;
  const uint8_t* end = module_buf->start + module_buf->size;
  while (p < end) {
    out_printf(&ctx->spec_writer_state, " ");
    const uint8_t* line_end = p + DUMP_OCTETS_PER_LINE;
    for (; p < line_end; ++p)
      out_printf(&ctx->spec_writer_state, "%4d,", *p);
    out_printf(&ctx->spec_writer_state, "\n");
  }
  out_printf(&ctx->spec_writer_state, "]);\ntests(m);\n");
}

static void write_commands_spec(WasmWriteContext* ctx, WasmScript* script) {
  int i;
  WasmModule* last_module = NULL;
  int num_assert_funcs = 0;
  for (i = 0; i < script->commands.size; ++i) {
    WasmCommand* command = &script->commands.data[i];
    if (command->type == WASM_COMMAND_TYPE_MODULE) {
      if (last_module)
        write_module_spec(ctx, last_module);
      write_module_header_spec(ctx);
      last_module = &command->module;
      num_assert_funcs = 0;
    } else {
      const char* js_call = NULL;
      const char* format = NULL;
      WasmCommandInvoke* invoke = NULL;
      switch (command->type) {
        case WASM_COMMAND_TYPE_INVOKE:
          js_call = "invoke";
          format = "$invoke_%d";
          invoke = &command->invoke;
          break;
        case WASM_COMMAND_TYPE_ASSERT_RETURN:
          js_call = "assertReturn";
          format = "$assert_return_%d";
          invoke = &command->assert_return.invoke;
          break;
        case WASM_COMMAND_TYPE_ASSERT_RETURN_NAN:
          js_call = "assertReturn";
          format = "$assert_return_nan_%d";
          invoke = &command->assert_return_nan.invoke;
          break;
        case WASM_COMMAND_TYPE_ASSERT_TRAP:
          js_call = "assertTrap";
          format = "$assert_trap_%d";
          invoke = &command->assert_trap.invoke;
          break;
        default:
          continue;
      }

      WasmExport* export = wasm_get_export_by_name(last_module, &invoke->name);
      assert(export);
      int func_index = wasm_get_func_index_by_var(last_module, &export->var);
      assert(func_index >= 0 && func_index < last_module->funcs.size);
      WasmFunc* callee = last_module->funcs.data[func_index];
      WasmType result_type = callee->result_type;
      /* these pointers will be invalidated later, so we can't use them */
      export = NULL;
      callee = NULL;

      WasmStringSlice name =
          create_assert_func_name(format, num_assert_funcs++);
      out_printf(&ctx->spec_writer_state, "  %s(m, \"%.*s\", \"%s\", %d);\n",
                 js_call, name.length, name.start, invoke->loc.filename,
                 invoke->loc.first_line);

      WasmExprPtr* expr_ptr;
      switch (command->type) {
        case WASM_COMMAND_TYPE_INVOKE: {
          WasmFunc* caller =
              append_nullary_func(last_module, result_type, name);
          CHECK_ALLOC_NULL(caller);
          expr_ptr = wasm_append_expr_ptr(&caller->exprs);
          CHECK_ALLOC_NULL(expr_ptr);
          *expr_ptr = create_invoke_expr(&command->invoke, func_index);
          CHECK_ALLOC_NULL(*expr_ptr);
          break;
        }

        case WASM_COMMAND_TYPE_ASSERT_RETURN: {
          WasmFunc* caller =
              append_nullary_func(last_module, WASM_TYPE_I32, name);
          CHECK_ALLOC_NULL(caller);

          expr_ptr = wasm_append_expr_ptr(&caller->exprs);
          CHECK_ALLOC_NULL(expr_ptr);

          WasmExpr* invoke_expr =
              create_invoke_expr(&command->assert_return.invoke, func_index);
          CHECK_ALLOC_NULL(invoke_expr);

          if (result_type == WASM_TYPE_VOID) {
            /* The return type of the assert_return function is i32, but this
             invoked function has a return type of void, so we have nothing to
             compare to. Just return 1 to the caller, signifying everything is
             OK. */
            *expr_ptr = invoke_expr;
            WasmConst const_;
            const_.type = WASM_TYPE_I32;
            const_.u32 = 1;
            expr_ptr = wasm_append_expr_ptr(&caller->exprs);
            CHECK_ALLOC_NULL(expr_ptr);
            *expr_ptr = create_const_expr(&const_);
            CHECK_ALLOC_NULL(*expr_ptr);
          } else {
            WasmConst* expected = &command->assert_return.expected;
            WasmExpr* const_expr = create_const_expr(expected);
            CHECK_ALLOC_NULL(const_expr);

            if (expected->type == WASM_TYPE_F32 && isnan(expected->f32)) {
              *expr_ptr = create_eq_expr(
                  WASM_TYPE_I32,
                  create_reinterpret_expr(WASM_TYPE_F32, invoke_expr),
                  create_reinterpret_expr(WASM_TYPE_F32, const_expr));
              CHECK_ALLOC_NULL(*expr_ptr);
            } else if (expected->type == WASM_TYPE_F64 &&
                       isnan(expected->f64)) {
              *expr_ptr = create_eq_expr(
                  WASM_TYPE_I64,
                  create_reinterpret_expr(WASM_TYPE_F64, invoke_expr),
                  create_reinterpret_expr(WASM_TYPE_F64, const_expr));
              CHECK_ALLOC_NULL(*expr_ptr);
            } else {
              *expr_ptr = create_eq_expr(result_type, invoke_expr, const_expr);
              CHECK_ALLOC_NULL(*expr_ptr);
            }
          }
          break;
        }

        case WASM_COMMAND_TYPE_ASSERT_RETURN_NAN: {
          WasmFunc* caller =
              append_nullary_func(last_module, WASM_TYPE_I32, name);
          CHECK_ALLOC_NULL(caller);
          CHECK_ALLOC(
              wasm_append_type_value(&caller->locals.types, &result_type));
          CHECK_ALLOC(wasm_append_type_value(&caller->params_and_locals.types,
                                             &result_type));
          expr_ptr = wasm_append_expr_ptr(&caller->exprs);
          CHECK_ALLOC_NULL(expr_ptr);
          *expr_ptr = create_set_local_expr(
              0, create_invoke_expr(&command->assert_return_nan.invoke,
                                    func_index));
          CHECK_ALLOC_NULL(*expr_ptr);
          /* x != x is true iff x is NaN */
          expr_ptr = wasm_append_expr_ptr(&caller->exprs);
          CHECK_ALLOC_NULL(expr_ptr);
          *expr_ptr = create_ne_expr(result_type, create_get_local_expr(0),
                                     create_get_local_expr(0));
          CHECK_ALLOC_NULL(*expr_ptr);
          break;
        }

        case WASM_COMMAND_TYPE_ASSERT_TRAP: {
          WasmFunc* caller =
              append_nullary_func(last_module, result_type, name);
          CHECK_ALLOC_NULL(caller);
          expr_ptr = wasm_append_expr_ptr(&caller->exprs);
          CHECK_ALLOC_NULL(expr_ptr);
          *expr_ptr = create_invoke_expr(&command->invoke, func_index);
          CHECK_ALLOC_NULL(*expr_ptr);
          break;
        }

        default:
          assert(0);
      }
    }
  }
  if (last_module)
    write_module_spec(ctx, last_module);
}

static void write_commands_no_spec(WasmWriteContext* ctx, WasmScript* script) {
  int i;
  for (i = 0; i < script->commands.size; ++i) {
    WasmCommand* command = &script->commands.data[i];
    if (command->type != WASM_COMMAND_TYPE_MODULE)
      continue;

    write_module(ctx, &command->module);
  }
}

static void cleanup_context(WasmWriteContext* ctx) {
  free(ctx->import_sig_indexes);
  free(ctx->func_sig_indexes);
  free(ctx->remapped_locals);
  free(ctx->func_offsets);
}

WasmResult wasm_write_binary(WasmWriter* writer,
                             WasmScript* script,
                             WasmWriteBinaryOptions* options) {
  WasmWriteContext ctx = {};
  ctx.options = options;
  ctx.result = WASM_OK;

  if (options->spec) {
    WasmMemoryWriter mem_writer = {};
    WasmResult result = wasm_init_mem_writer(&mem_writer);
    if (result != WASM_OK)
      return result;

    ctx.spec_writer_state.writer = writer;
    ctx.spec_writer_state.result = &ctx.result;
    ctx.writer_state.writer = &mem_writer.base;
    ctx.writer_state.result = &ctx.result;
    ctx.writer_state.log_writes = options->log_writes;
    ctx.mem_writer = &mem_writer;
    write_header_spec(&ctx);
    write_commands_spec(&ctx, script);
    write_footer_spec(&ctx);
    wasm_close_mem_writer(&mem_writer);
  } else {
    ctx.writer_state.writer = writer;
    ctx.writer_state.result = &ctx.result;
    ctx.writer_state.log_writes = options->log_writes;
    write_commands_no_spec(&ctx, script);
  }

  cleanup_context(&ctx);
  return ctx.result;
}
