/* so_util.h -- utils to load and hook .so modules
 *
 * Copyright (C) 2024 Alessio Tosto, Andy Nguyen, fgsfds
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __SO_UTIL_H__
#define __SO_UTIL_H__

#include <stdint.h>

#define ALIGN_MEM(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

typedef struct {
	char *symbol;
	uintptr_t ptr; /* NULL means using the trampoline instead. */
	uint32_t trampoline[10];
} dynarec_import;

extern dynarec_import dynarec_imports[];
extern size_t dynarec_imports_num;

extern void *text_base, *data_base;
extern size_t text_size, data_size;

void hook_thumb(uintptr_t addr, uintptr_t dst);
void hook_arm(uintptr_t addr, uintptr_t dst);
void hook_arm64(uintptr_t addr, uintptr_t dst);

void so_flush_caches(void);
void so_free_temp(void);
int so_load(const char *filename, void **base_addr);
int so_relocate(dynarec_import *funcs, int num_funcs);
void so_execute_init_array(void);
uintptr_t so_find_addr(const char *symbol);
uintptr_t so_find_addr_rx(const char *symbol);
uintptr_t so_find_rel_addr(const char *symbol);
dynarec_import *so_find_import(dynarec_import *funcs, int num_funcs, const char *name);
int so_unload(void);
void so_run_fiber(Dynarmic::A64::Jit *jit, uintptr_t entry);

#endif
