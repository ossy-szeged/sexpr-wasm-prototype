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

#include "wasm-ast-writer.h"

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

#include "wasm-ast.h"
#include "wasm-common.h"
#include "wasm-literal.h"
#include "wasm-writer.h"

#define INDENT_SIZE 2
#define NO_FORCE_NEWLINE 0
#define FORCE_NEWLINE 1

#define V(type1, type2, mem_size, code, NAME, text) [code] = text,
static const char* s_opcode_name[] = {WASM_FOREACH_OPCODE(V)};
#undef V

#define ALLOC_FAILURE \
  fprintf(stderr, "%s:%d: allocation failed\n", __FILE__, __LINE__)

#define CHECK_ALLOC_(ctx, cond) \
  do {                          \
    if (!(cond)) {              \
      ALLOC_FAILURE;            \
      ctx->result = WASM_ERROR; \
      return;                   \
    }                           \
  } while (0)

#define CHECK_ALLOC(ctx, e) CHECK_ALLOC_(ctx, (e) == WASM_OK)

WASM_DEFINE_VECTOR(string_slice, WasmStringSlice);

static const uint8_t s_is_char_escaped[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

typedef enum WasmNextChar {
  WASM_NEXT_CHAR_NONE,
  WASM_NEXT_CHAR_SPACE,
  WASM_NEXT_CHAR_NEWLINE,
  WASM_NEXT_CHAR_FORCE_NEWLINE,
} WasmNextChar;

typedef struct WasmWriteContext {
  WasmAllocator* allocator;
  WasmWriter* writer;
  size_t offset;
  WasmResult result;
  int indent;
  WasmNextChar next_char;
  int depth;
  WasmStringSliceVector index_to_name;
} WasmWriteContext;

static void indent(WasmWriteContext* ctx) {
  ctx->indent += INDENT_SIZE;
}

static void dedent(WasmWriteContext* ctx) {
  ctx->indent -= INDENT_SIZE;
  assert(ctx->indent >= 0);
}

static void out_data_at(WasmWriteContext* ctx,
                        size_t offset,
                        const void* src,
                        size_t size) {
  if (ctx->result != WASM_OK)
    return;
  if (ctx->writer->write_data) {
    ctx->result =
        ctx->writer->write_data(offset, src, size, ctx->writer->user_data);
  }
}

static void out_data(WasmWriteContext* ctx, const void* src, size_t size) {
  out_data_at(ctx, ctx->offset, src, size);
  ctx->offset += size;
}

static void out_indent(WasmWriteContext* ctx) {
  static char s_indent[] =
      "                                                                       "
      "                                                                       ";
  static size_t s_indent_len = sizeof(s_indent) - 1;
  int indent = ctx->indent;
  while (indent > s_indent_len) {
    out_data(ctx, s_indent, s_indent_len);
    indent -= s_indent_len;
  }
  if (indent > 0) {
    out_data(ctx, s_indent, indent);
  }
}

static void out_next_char(WasmWriteContext* ctx) {
  switch (ctx->next_char) {
    case WASM_NEXT_CHAR_SPACE:
      out_data(ctx, " ", 1);
      break;
    case WASM_NEXT_CHAR_NEWLINE:
    case WASM_NEXT_CHAR_FORCE_NEWLINE:
      out_data(ctx, "\n", 1);
      out_indent(ctx);
      break;

    default:
    case WASM_NEXT_CHAR_NONE:
      break;
  }
  ctx->next_char = WASM_NEXT_CHAR_NONE;
}

static void out_data_with_next_char(WasmWriteContext* ctx,
                                    const void* src,
                                    size_t size) {
  out_next_char(ctx);
  out_data(ctx, src, size);
}

static void out_printf(WasmWriteContext* ctx, const char* format, ...) {
  va_list args;
  va_list args_copy;
  va_start(args, format);
  va_copy(args_copy, args);

  char buffer[128];
  int len = wasm_vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (len + 1 > sizeof(buffer)) {
    char* buffer2 = alloca(len + 1);
    len = wasm_vsnprintf(buffer2, len + 1, format, args_copy);
    va_end(args_copy);
  }

  out_data_with_next_char(ctx, buffer, len);
  /* default to following space */
  ctx->next_char = WASM_NEXT_CHAR_SPACE;
}

static void out_putc(WasmWriteContext* ctx, char c) {
  out_data(ctx, &c, 1);
}

static void out_puts(WasmWriteContext* ctx,
                     const char* s,
                     WasmNextChar next_char) {
  size_t len = strlen(s);
  out_data_with_next_char(ctx, s, len);
  ctx->next_char = next_char;
}

static void out_puts_space(WasmWriteContext* ctx, const char* s) {
  out_puts(ctx, s, WASM_NEXT_CHAR_SPACE);
}

static void out_newline(WasmWriteContext* ctx, int force) {
  if (ctx->next_char == WASM_NEXT_CHAR_FORCE_NEWLINE)
    out_next_char(ctx);
  ctx->next_char =
      force ? WASM_NEXT_CHAR_FORCE_NEWLINE : WASM_NEXT_CHAR_NEWLINE;
}

static void out_open(WasmWriteContext* ctx,
                     const char* name,
                     WasmNextChar next_char) {
  out_puts(ctx, "(", WASM_NEXT_CHAR_NONE);
  out_puts(ctx, name, next_char);
  indent(ctx);
}

static void out_open_newline(WasmWriteContext* ctx, const char* name) {
  out_open(ctx, name, WASM_NEXT_CHAR_NEWLINE);
}

static void out_open_space(WasmWriteContext* ctx, const char* name) {
  out_open(ctx, name, WASM_NEXT_CHAR_SPACE);
}

static void out_close(WasmWriteContext* ctx, WasmNextChar next_char) {
  if (ctx->next_char != WASM_NEXT_CHAR_FORCE_NEWLINE)
    ctx->next_char = WASM_NEXT_CHAR_NONE;
  dedent(ctx);
  out_puts(ctx, ")", next_char);
}

static void out_close_newline(WasmWriteContext* ctx) {
  out_close(ctx, WASM_NEXT_CHAR_NEWLINE);
}

static void out_close_space(WasmWriteContext* ctx) {
  out_close(ctx, WASM_NEXT_CHAR_SPACE);
}

static void out_string_slice(WasmWriteContext* ctx,
                             WasmStringSlice* str,
                             WasmNextChar next_char) {
  out_printf(ctx, "%.*s", (int)str->length, str->start);
  ctx->next_char = next_char;
}

static void out_string_slice_opt(WasmWriteContext* ctx,
                                 WasmStringSlice* str,
                                 WasmNextChar next_char) {
  if (str->start)
    out_string_slice(ctx, str, next_char);
}

static void out_quoted_data(WasmWriteContext* ctx,
                            const void* data,
                            size_t length) {
  const uint8_t* u8_data = data;
  static const char s_hexdigits[] = "0123456789abcdef";
  out_next_char(ctx);
  out_putc(ctx, '\"');
  size_t i;
  for (i = 0; i < length; ++i) {
    uint8_t c = u8_data[i];
    if (s_is_char_escaped[c]) {
      out_putc(ctx, '\\');
      out_putc(ctx, s_hexdigits[c >> 4]);
      out_putc(ctx, s_hexdigits[c & 0xf]);
    } else {
      out_putc(ctx, c);
    }
  }
  out_putc(ctx, '\"');
  ctx->next_char = WASM_NEXT_CHAR_SPACE;
}

static void out_quoted_string_slice(WasmWriteContext* ctx,
                                    WasmStringSlice* str,
                                    WasmNextChar next_char) {
  out_quoted_data(ctx, str->start, str->length);
  ctx->next_char = next_char;
}

static void out_var(WasmWriteContext* ctx,
                    WasmVar* var,
                    WasmNextChar next_char) {
  if (var->type == WASM_VAR_TYPE_INDEX) {
    out_printf(ctx, "%d", var->index);
    ctx->next_char = next_char;
  } else {
    out_string_slice(ctx, &var->name, next_char);
  }
}

static void out_br_var(WasmWriteContext* ctx,
                       WasmVar* var,
                       WasmNextChar next_char) {
  if (var->type == WASM_VAR_TYPE_INDEX) {
    out_printf(ctx, "%d (; @%d ;)", var->index, ctx->depth - var->index - 1);
    ctx->next_char = next_char;
  } else {
    out_string_slice(ctx, &var->name, next_char);
  }
}

static void out_type(WasmWriteContext* ctx,
                     WasmType type,
                     WasmNextChar next_char) {
  static const char* s_types[] = {NULL, "i32", "i64", "f32", "f64"};
  out_puts(ctx, s_types[type], next_char);
}

static void out_func_sig_space(WasmWriteContext* ctx,
                               WasmFuncSignature* func_sig) {
  if (func_sig->param_types.size) {
    size_t i;
    out_open_space(ctx, "param");
    for (i = 0; i < func_sig->param_types.size; ++i) {
      out_type(ctx, func_sig->param_types.data[i], WASM_NEXT_CHAR_SPACE);
    }
    out_close_space(ctx);
  }

  if (func_sig->result_type != WASM_TYPE_VOID) {
    out_open_space(ctx, "result");
    out_type(ctx, func_sig->result_type, WASM_NEXT_CHAR_NONE);
    out_close_space(ctx);
  }
}

static void write_exprs(WasmWriteContext* ctx, WasmExprPtrVector* exprs);

static void write_expr(WasmWriteContext* ctx, WasmExpr* expr);

static void write_block(WasmWriteContext* ctx,
                        WasmBlock* block,
                        const char* text) {
  out_open_space(ctx, text);
  out_string_slice_opt(ctx, &block->label, WASM_NEXT_CHAR_SPACE);
  out_printf(ctx, " ;; exit = @%d", ctx->depth);
  out_newline(ctx, FORCE_NEWLINE);
  ctx->depth++;
  write_exprs(ctx, &block->exprs);
  ctx->depth--;
  out_close_newline(ctx);
}

/* TODO(binji): remove all this if-block stuff when we switch to postorder */
typedef enum WriteIfBranchType {
  WASM_IF_BRANCH_TYPE_ONE_EXPRESSION,
  WASM_IF_BRANCH_TYPE_BLOCK_EXPRESSION,
} WriteIfBranchType;

static WriteIfBranchType get_if_branch_type(WasmExpr* expr) {
  return expr->type == WASM_EXPR_TYPE_BLOCK
             ? WASM_IF_BRANCH_TYPE_BLOCK_EXPRESSION
             : WASM_IF_BRANCH_TYPE_ONE_EXPRESSION;
}

static void write_if_branch(WasmWriteContext* ctx,
                            WriteIfBranchType type,
                            WasmExpr* expr,
                            const char* text) {
  switch (type) {
    case WASM_IF_BRANCH_TYPE_ONE_EXPRESSION:
      out_printf(ctx, ";; exit = @%d", ctx->depth);
      out_newline(ctx, FORCE_NEWLINE);
      ctx->depth++;
      write_expr(ctx, expr);
      ctx->depth--;
      break;
    case WASM_IF_BRANCH_TYPE_BLOCK_EXPRESSION:
      if (expr->type == WASM_EXPR_TYPE_BLOCK) {
        write_block(ctx, &expr->block, text);
      } else {
        out_open_space(ctx, text);
        out_printf(ctx, " ;; exit = @%d", ctx->depth);
        out_newline(ctx, FORCE_NEWLINE);
        ctx->depth++;
        write_expr(ctx, expr);
        ctx->depth--;
        out_close_newline(ctx);
      }
      break;
    default:
      assert(0);
      break;
  }
}

static void write_const(WasmWriteContext* ctx, WasmConst* const_) {
  switch (const_->type) {
    case WASM_TYPE_I32:
      out_open_space(ctx, s_opcode_name[WASM_OPCODE_I32_CONST]);
      out_printf(ctx, "%d", (int32_t)const_->u32);
      out_close_newline(ctx);
      break;

    case WASM_TYPE_I64:
      out_open_space(ctx, s_opcode_name[WASM_OPCODE_I64_CONST]);
      out_printf(ctx, "%" PRId64, (int64_t)const_->u64);
      out_close_newline(ctx);
      break;

    case WASM_TYPE_F32: {
      out_open_space(ctx, s_opcode_name[WASM_OPCODE_F32_CONST]);
      char buffer[128];
      wasm_write_float_hex(buffer, 128, const_->f32_bits);
      out_puts_space(ctx, buffer);
      out_close_newline(ctx);
      break;
    }

    case WASM_TYPE_F64: {
      out_open_space(ctx, s_opcode_name[WASM_OPCODE_F64_CONST]);
      char buffer[128];
      wasm_write_double_hex(buffer, 128, const_->f64_bits);
      out_puts_space(ctx, buffer);
      out_close_newline(ctx);
      break;
    }

    default:
      assert(0);
      break;
  }
}

static void write_expr(WasmWriteContext* ctx, WasmExpr* expr) {
  switch (expr->type) {
    case WASM_EXPR_TYPE_BINARY:
      out_open_newline(ctx, s_opcode_name[expr->binary.opcode]);
      write_expr(ctx, expr->binary.left);
      write_expr(ctx, expr->binary.right);
      out_close_newline(ctx);
      break;

    case WASM_EXPR_TYPE_BLOCK:
      write_block(ctx, &expr->block, s_opcode_name[WASM_OPCODE_BLOCK]);
      break;

    case WASM_EXPR_TYPE_BR:
      out_open_space(ctx, s_opcode_name[WASM_OPCODE_BR]);
      out_br_var(ctx, &expr->br.var, WASM_NEXT_CHAR_NEWLINE);
      if (expr->br.expr && expr->br.expr->type != WASM_EXPR_TYPE_NOP)
        write_expr(ctx, expr->br.expr);
      out_close_newline(ctx);
      break;

    case WASM_EXPR_TYPE_BR_IF:
      out_open_space(ctx, s_opcode_name[WASM_OPCODE_BR_IF]);
      out_br_var(ctx, &expr->br_if.var, WASM_NEXT_CHAR_NEWLINE);
      if (expr->br_if.expr && expr->br_if.expr->type != WASM_EXPR_TYPE_NOP)
        write_expr(ctx, expr->br_if.expr);
      write_expr(ctx, expr->br_if.cond);
      out_close_newline(ctx);
      break;

    case WASM_EXPR_TYPE_BR_TABLE: {
      out_open_newline(ctx, s_opcode_name[WASM_OPCODE_BR_TABLE]);
      size_t i;
      for (i = 0; i < expr->br_table.targets.size; ++i)
        out_br_var(ctx, &expr->br_table.targets.data[i], WASM_NEXT_CHAR_SPACE);
      out_br_var(ctx, &expr->br_table.default_target, WASM_NEXT_CHAR_NEWLINE);
      write_expr(ctx, expr->br_table.expr);
      out_close_newline(ctx);
      break;
    }

    case WASM_EXPR_TYPE_CALL: {
      out_open_space(ctx, s_opcode_name[WASM_OPCODE_CALL_FUNCTION]);
      out_var(ctx, &expr->call.var, WASM_NEXT_CHAR_NEWLINE);
      size_t i;
      for (i = 0; i < expr->call.args.size; ++i)
        write_expr(ctx, expr->call.args.data[i]);
      out_close_newline(ctx);
      break;
    }

    case WASM_EXPR_TYPE_CALL_IMPORT: {
      out_open_space(ctx, s_opcode_name[WASM_OPCODE_CALL_IMPORT]);
      out_var(ctx, &expr->call.var, WASM_NEXT_CHAR_NEWLINE);
      size_t i;
      for (i = 0; i < expr->call.args.size; ++i)
        write_expr(ctx, expr->call.args.data[i]);
      out_close_newline(ctx);
      break;
    }

    case WASM_EXPR_TYPE_CALL_INDIRECT: {
      out_open_space(ctx, s_opcode_name[WASM_OPCODE_CALL_INDIRECT]);
      out_var(ctx, &expr->call_indirect.var, WASM_NEXT_CHAR_NEWLINE);
      write_expr(ctx, expr->call_indirect.expr);
      size_t i;
      for (i = 0; i < expr->call_indirect.args.size; ++i)
        write_expr(ctx, expr->call_indirect.args.data[i]);
      out_close_newline(ctx);
      break;
    }

    case WASM_EXPR_TYPE_COMPARE:
      out_open_newline(ctx, s_opcode_name[expr->compare.opcode]);
      write_expr(ctx, expr->compare.left);
      write_expr(ctx, expr->compare.right);
      out_close_newline(ctx);
      break;

    case WASM_EXPR_TYPE_CONST:
      write_const(ctx, &expr->const_);
      break;

    case WASM_EXPR_TYPE_CONVERT:
      out_open_newline(ctx, s_opcode_name[expr->convert.opcode]);
      write_expr(ctx, expr->convert.expr);
      out_close_newline(ctx);
      break;

    case WASM_EXPR_TYPE_GET_LOCAL:
      out_open_space(ctx, s_opcode_name[WASM_OPCODE_GET_LOCAL]);
      out_var(ctx, &expr->get_local.var, WASM_NEXT_CHAR_NONE);
      out_close_newline(ctx);
      break;

    case WASM_EXPR_TYPE_GROW_MEMORY:
      out_open_newline(ctx, s_opcode_name[WASM_OPCODE_GROW_MEMORY]);
      write_expr(ctx, expr->grow_memory.expr);
      out_close_newline(ctx);
      break;

    case WASM_EXPR_TYPE_IF: {
      out_open_newline(ctx, s_opcode_name[WASM_OPCODE_IF]);
      write_expr(ctx, expr->if_.cond);
      WriteIfBranchType true_type = get_if_branch_type(expr->if_.true_);
      write_if_branch(ctx, true_type, expr->if_.true_, "then");
      out_close_newline(ctx);
      break;
    }

    case WASM_EXPR_TYPE_IF_ELSE: {
      out_open_newline(ctx, s_opcode_name[WASM_OPCODE_IF]);
      write_expr(ctx, expr->if_else.cond);

      /* silly dance to make sure that we don't use mismatching format for
       * if/then/else. We're allowed to do the short if form:
       *   (if (cond) (true) (false))
       * only when true and false are one non-block expression. Otherwise we
       * need to use the full
       *   (if (then (true1) (true2)) (else (false1) (false2)))
       * form. */
      WriteIfBranchType true_type = get_if_branch_type(expr->if_else.true_);
      WriteIfBranchType false_type = get_if_branch_type(expr->if_else.false_);
      if (true_type != false_type)
        true_type = false_type = WASM_IF_BRANCH_TYPE_BLOCK_EXPRESSION;

      write_if_branch(ctx, true_type, expr->if_else.true_, "then");
      write_if_branch(ctx, false_type, expr->if_else.false_, "else");
      out_close_newline(ctx);
      break;
    }

    case WASM_EXPR_TYPE_LOAD:
      out_open_space(ctx, s_opcode_name[expr->load.opcode]);
      if (expr->load.offset)
        out_printf(ctx, "offset=%" PRIu64, expr->load.offset);
      if (!wasm_is_naturally_aligned(expr->load.opcode, expr->load.align))
        out_printf(ctx, "align=%u", expr->load.align);
      out_newline(ctx, NO_FORCE_NEWLINE);
      write_expr(ctx, expr->load.addr);
      out_close_newline(ctx);
      break;

    case WASM_EXPR_TYPE_LOOP:
      out_open_space(ctx, s_opcode_name[WASM_OPCODE_LOOP]);
      out_string_slice_opt(ctx, &expr->loop.inner, WASM_NEXT_CHAR_SPACE);
      out_string_slice_opt(ctx, &expr->loop.outer, WASM_NEXT_CHAR_SPACE);
      out_printf(ctx, " ;; exit = @%d, cont = @%d", ctx->depth,
                 ctx->depth + 1);
      out_newline(ctx, FORCE_NEWLINE);
      ctx->depth += 2;
      write_exprs(ctx, &expr->loop.exprs);
      ctx->depth -= 2;
      out_close_newline(ctx);
      break;

    case WASM_EXPR_TYPE_MEMORY_SIZE:
      out_open_space(ctx, s_opcode_name[WASM_OPCODE_MEMORY_SIZE]);
      out_close_newline(ctx);
      break;

    case WASM_EXPR_TYPE_NOP:
      out_open_space(ctx, s_opcode_name[WASM_OPCODE_NOP]);
      out_close_newline(ctx);
      break;

    case WASM_EXPR_TYPE_RETURN:
      out_open_newline(ctx, s_opcode_name[WASM_OPCODE_RETURN]);
      if (expr->return_.expr)
        write_expr(ctx, expr->return_.expr);
      out_close_newline(ctx);
      break;

    case WASM_EXPR_TYPE_SELECT:
      out_open_newline(ctx, s_opcode_name[WASM_OPCODE_SELECT]);
      write_expr(ctx, expr->select.true_);
      write_expr(ctx, expr->select.false_);
      write_expr(ctx, expr->select.cond);
      out_close_newline(ctx);
      break;

    case WASM_EXPR_TYPE_SET_LOCAL:
      out_open_space(ctx, s_opcode_name[WASM_OPCODE_SET_LOCAL]);
      out_var(ctx, &expr->set_local.var, WASM_NEXT_CHAR_NEWLINE);
      write_expr(ctx, expr->set_local.expr);
      out_close_newline(ctx);
      break;

    case WASM_EXPR_TYPE_STORE:
      out_open_space(ctx, s_opcode_name[expr->store.opcode]);
      if (expr->store.offset)
        out_printf(ctx, "offset=%" PRIu64, expr->store.offset);
      if (!wasm_is_naturally_aligned(expr->store.opcode, expr->store.align))
        out_printf(ctx, "align=%u", expr->store.align);
      out_newline(ctx, NO_FORCE_NEWLINE);
      write_expr(ctx, expr->store.addr);
      write_expr(ctx, expr->store.value);
      out_close_newline(ctx);
      break;

    case WASM_EXPR_TYPE_UNARY:
      out_open_newline(ctx, s_opcode_name[expr->unary.opcode]);
      write_expr(ctx, expr->unary.expr);
      out_close_newline(ctx);
      break;

    case WASM_EXPR_TYPE_UNREACHABLE:
      out_open_space(ctx, s_opcode_name[WASM_OPCODE_UNREACHABLE]);
      out_close_newline(ctx);
      break;

    default:
      fprintf(stderr, "bad expr type: %d\n", expr->type);
      assert(0);
      break;
  }
}

static void write_exprs(WasmWriteContext* ctx, WasmExprPtrVector* exprs) {
  size_t i;
  for (i = 0; i < exprs->size; ++i)
    write_expr(ctx, exprs->data[i]);
}

static void write_type_bindings(WasmWriteContext* ctx,
                                const char* prefix,
                                WasmFunc* func,
                                WasmTypeBindings* type_bindings,
                                uint32_t index_offset) {
  size_t i;
  /* allocate memory for the index-to-name mapping */
  uint32_t num_names = type_bindings->types.size;
  if (num_names > ctx->index_to_name.size) {
    CHECK_ALLOC(ctx, wasm_reserve_string_slices(
                         ctx->allocator, &ctx->index_to_name, num_names));
  }
  ctx->index_to_name.size = num_names;
  memset(ctx->index_to_name.data, 0, num_names * sizeof(WasmStringSlice));

  /* map index to name */
  for (i = 0; i < type_bindings->bindings.entries.capacity; ++i) {
    WasmBindingHashEntry* entry = &type_bindings->bindings.entries.data[i];
    if (wasm_hash_entry_is_free(entry))
      continue;

    uint32_t index = entry->binding.index + index_offset;

    assert(index < ctx->index_to_name.size);
    ctx->index_to_name.data[index] = entry->binding.name;
  }

  /* named params/locals must be specified by themselves, but nameless
   * params/locals can be compressed, e.g.:
   *   (param $foo i32)
   *   (param i32 i64 f32)
   */
  int is_open = 0;
  for (i = 0; i < type_bindings->types.size; ++i) {
    if (!is_open) {
      out_open_space(ctx, prefix);
      is_open = 1;
    }

    WasmStringSlice* name = &ctx->index_to_name.data[i];
    int has_name = name->start != NULL;
    if (has_name)
      out_string_slice(ctx, name, WASM_NEXT_CHAR_SPACE);
    out_type(ctx, type_bindings->types.data[i], WASM_NEXT_CHAR_SPACE);
    if (has_name) {
      out_close_space(ctx);
      is_open = 0;
    }
  }
  if (is_open)
    out_close_space(ctx);
}

static void write_func(WasmWriteContext* ctx,
                       WasmModule* module,
                       int func_index,
                       WasmFunc* func) {
  out_open_space(ctx, "func");
  out_printf(ctx, "(; %d ;)", func_index);
  out_string_slice_opt(ctx, &func->name, WASM_NEXT_CHAR_SPACE);
  uint32_t num_params = 0;
  if (func->flags & WASM_FUNC_FLAG_HAS_FUNC_TYPE) {
    num_params =
        module->func_types.data[func->type_var.index]->sig.param_types.size;
    out_open_space(ctx, "type");
    out_var(ctx, &func->type_var, WASM_NEXT_CHAR_NONE);
    out_close_space(ctx);
  }
  if (func->flags & WASM_FUNC_FLAG_HAS_SIGNATURE) {
    num_params = func->params.types.size;
    write_type_bindings(ctx, "param", func, &func->params, 0);
    if (func->result_type != WASM_TYPE_VOID) {
      out_open_space(ctx, "result");
      out_type(ctx, func->result_type, WASM_NEXT_CHAR_NONE);
      out_close_space(ctx);
    }
  }
  out_newline(ctx, NO_FORCE_NEWLINE);
  if (func->locals.types.size) {
    write_type_bindings(ctx, "local", func, &func->locals, -num_params);
  }
  out_newline(ctx, NO_FORCE_NEWLINE);
  write_exprs(ctx, &func->exprs);
  out_close_newline(ctx);
}

static void write_import(WasmWriteContext* ctx,
                         int import_index,
                         WasmImport* import) {
  out_open_space(ctx, "import");
  out_printf(ctx, "(; %d ;)", import_index);
  out_string_slice_opt(ctx, &import->name, WASM_NEXT_CHAR_SPACE);
  out_quoted_string_slice(ctx, &import->module_name, WASM_NEXT_CHAR_SPACE);
  out_quoted_string_slice(ctx, &import->func_name, WASM_NEXT_CHAR_SPACE);
  if (import->import_type == WASM_IMPORT_HAS_TYPE) {
    out_open_space(ctx, "type");
    out_var(ctx, &import->type_var, WASM_NEXT_CHAR_NONE);
    out_close_space(ctx);
  } else {
    out_func_sig_space(ctx, &import->func_sig);
  }
  out_close_newline(ctx);
}

static void write_export(WasmWriteContext* ctx,
                         int export_index,
                         WasmExport* export) {
  out_open_space(ctx, "export");
  out_printf(ctx, "(; %d ;)", export_index);
  out_quoted_string_slice(ctx, &export->name, WASM_NEXT_CHAR_SPACE);
  out_var(ctx, &export->var, WASM_NEXT_CHAR_NONE);
  out_close_newline(ctx);
}

static void write_export_memory(WasmWriteContext* ctx,
                                WasmExportMemory* export) {
  out_open_space(ctx, "export");
  out_quoted_string_slice(ctx, &export->name, WASM_NEXT_CHAR_SPACE);
  out_puts_space(ctx, "memory");
  out_close_newline(ctx);
}

static void write_table(WasmWriteContext* ctx, WasmVarVector* table) {
  out_open_space(ctx, "table");
  size_t i;
  for (i = 0; i < table->size; ++i)
    out_var(ctx, &table->data[i], WASM_NEXT_CHAR_SPACE);
  out_close_newline(ctx);
}

static void write_segment(WasmWriteContext* ctx, WasmSegment* segment) {
  out_open_space(ctx, "segment");
  out_printf(ctx, "%u", segment->addr);
  out_quoted_data(ctx, segment->data, segment->size);
  out_close_newline(ctx);
}

static void write_memory(WasmWriteContext* ctx, WasmMemory* memory) {
  out_open_space(ctx, "memory");
  out_printf(ctx, "%u", memory->initial_pages);
  if (memory->initial_pages != memory->max_pages)
    out_printf(ctx, "%u", memory->max_pages);
  out_newline(ctx, NO_FORCE_NEWLINE);
  size_t i;
  for (i = 0; i < memory->segments.size; ++i) {
    WasmSegment* segment = &memory->segments.data[i];
    write_segment(ctx, segment);
  }
  out_close_newline(ctx);
}

static void write_func_type(WasmWriteContext* ctx,
                            int func_type_index,
                            WasmFuncType* func_type) {
  out_open_space(ctx, "type");
  out_printf(ctx, "(; %d ;)", func_type_index);
  out_string_slice_opt(ctx, &func_type->name, WASM_NEXT_CHAR_SPACE);
  out_open_space(ctx, "func");
  out_func_sig_space(ctx, &func_type->sig);
  out_close_space(ctx);
  out_close_newline(ctx);
}

static void write_start_function(WasmWriteContext* ctx, WasmVar* start) {
  out_open_space(ctx, "start");
  out_var(ctx, start, WASM_NEXT_CHAR_NONE);
  out_close_newline(ctx);
}

static void write_module(WasmWriteContext* ctx, WasmModule* module) {
  out_open_newline(ctx, "module");
  WasmModuleField* field;
  int func_index = 0;
  int import_index = 0;
  int export_index = 0;
  int func_type_index = 0;
  for (field = module->first_field; field != NULL; field = field->next) {
    switch (field->type) {
      case WASM_MODULE_FIELD_TYPE_FUNC:
        write_func(ctx, module, func_index++, &field->func);
        break;
      case WASM_MODULE_FIELD_TYPE_IMPORT:
        write_import(ctx, import_index++, &field->import);
        break;
      case WASM_MODULE_FIELD_TYPE_EXPORT:
        write_export(ctx, export_index++, &field->export_);
        break;
      case WASM_MODULE_FIELD_TYPE_EXPORT_MEMORY:
        write_export_memory(ctx, &field->export_memory);
        break;
      case WASM_MODULE_FIELD_TYPE_TABLE:
        write_table(ctx, &field->table);
        break;
      case WASM_MODULE_FIELD_TYPE_MEMORY:
        write_memory(ctx, &field->memory);
        break;
      case WASM_MODULE_FIELD_TYPE_FUNC_TYPE:
        write_func_type(ctx, func_type_index++, &field->func_type);
        break;
      case WASM_MODULE_FIELD_TYPE_START:
        write_start_function(ctx, &field->start);
        break;
    }
  }
  out_close_newline(ctx);
}

WasmResult wasm_write_ast(struct WasmAllocator* allocator,
                          struct WasmWriter* writer,
                          struct WasmModule* module) {
  WasmWriteContext ctx;
  WASM_ZERO_MEMORY(ctx);
  ctx.allocator = allocator;
  ctx.result = WASM_OK;
  ctx.writer = writer;
  write_module(&ctx, module);
  /* the memory for the actual string slice is shared with the module, so we
   * only need to free the vector */
  wasm_destroy_string_slice_vector(allocator, &ctx.index_to_name);
  return ctx.result;
}
