/* so_util.h -- utils to load and hook .so modules
 *
 * Copyright (C) 2024 Alessio Tosto, Andy Nguyen, fgsfds
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __SO_UTIL_H__
#define __SO_UTIL_H__

#ifdef __MINGW64__
#include <intrin.h>
#include <malloc.h>
#include <windows.h>
#define memalign(x, y) _aligned_malloc(y, x)
#else
#include <malloc.h>
#endif

#include <stdint.h>

#define ALIGN_MEM(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

typedef struct {
#ifdef USE_INTERPRETER
	bool mapped;
#endif
	char *symbol;
	uintptr_t ptr; /* NULL means using the trampoline instead. */
#ifdef USE_INTERPRETER
	uint32_t trampoline;
#else
	uint32_t trampoline[2];
#endif
} dynarec_import;

typedef struct {
#ifdef USE_INTERPRETER
	bool mapped;
	uint32_t trampoline;
#else
	uint32_t trampoline[2];
#endif
} dynarec_hook;

extern dynarec_import dynarec_imports[];
extern size_t dynarec_imports_num;

extern void *text_base, *data_base;
extern size_t text_size, data_size;

void hook_arm64(uintptr_t addr, dynarec_hook *dst);

void so_flush_caches(void);
void so_free_temp(void);
int so_load(const char *filename, void **base_addr);
int so_relocate();
int so_resolve(dynarec_import *funcs, int num_funcs);
void so_execute_init_array(void);
uintptr_t so_find_addr(const char *symbol);
uintptr_t so_find_addr_rx(const char *symbol);
uintptr_t so_find_rel_addr(const char *symbol);
dynarec_import *so_find_import(dynarec_import *funcs, int num_funcs, const char *name);
int so_unload(void);
void so_run_fiber(Dynarmic::A64::Jit *jit, uintptr_t entry);

#define HOOK_FUNC(symname, func) \
	{ \
		dynarec_hook hook = gen_trampoline<&func>(symname); \
		hook_arm64((uintptr_t)dynarec_base_addr + so_find_addr_rx(symname), &hook); \
	}

#ifdef NDEBUG
#define debugLog
#else
#define debugLog printf
#endif

#endif
