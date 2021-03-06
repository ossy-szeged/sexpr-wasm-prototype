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

#ifndef WASM_LEXER_H_
#define WASM_LEXER_H_

#include <stddef.h>

#include "wasm-common.h"

struct WasmAllocator;
typedef void* WasmLexer;

WASM_EXTERN_C_BEGIN
WasmLexer wasm_new_lexer(struct WasmAllocator*, const char* filename);
void wasm_destroy_lexer(WasmLexer);
WASM_EXTERN_C_END

#endif /* WASM_LEXER_H_ */
