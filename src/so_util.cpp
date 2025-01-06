/* so_util.cpp -- utils to load and hook .so modules
 *
 * Copyright (C) 2024 Alessio Tosto, Andy Nguyen, fgsfds
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.	See the LICENSE file for details.
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <bits/stdc++.h>

#include "elf.h"
#include "dynarec.h"
#include "so_util.h"

#ifdef USE_INTERPRETER
#include "interpreter.h"
uc_engine *uc;
std::vector<uc_hook> hooks;
#define HOOKS_BASE_ADDRESS ((uintptr_t)load_base + load_size)
#define HOOKS_BLOCK_SIZE (65536)
#endif

std::vector<uintptr_t> native_funcs;
std::vector<std::string> native_funcs_names;

extern uintptr_t __stack_chk_fail;
static uint64_t __stack_chk_guard_fake = 0x4242424242424242;
FILE *stderr_fake = (FILE*)0xDEADBEEFDEADBEEF;

#define	_U	(char)01
#define	_L	(char)02
#define	_N	(char)04
#define	_S	(char)010
#define _P	(char)020
#define _C	(char)040
#define _X	(char)0100
#define	_B	(char)0200

static const char __BIONIC_ctype_[257] = {0,
	_C,    _C,    _C,    _C,    _C,    _C,    _C,    _C,
	_C,    _C|_S, _C|_S, _C|_S, _C|_S, _C|_S, _C,    _C,
	_C,    _C,    _C,    _C,    _C,    _C,    _C,    _C,
	_C,    _C,    _C,    _C,    _C,    _C,    _C,    _C,
	_S|_B, _P,    _P,    _P,    _P,    _P,    _P,    _P,
	_P,    _P,    _P,    _P,    _P,    _P,    _P,    _P,
	_N,    _N,    _N,    _N,    _N,    _N,    _N,    _N,
	_N,    _N,    _P,    _P,    _P,    _P,    _P,    _P,
	_P,    _U|_X, _U|_X, _U|_X, _U|_X, _U|_X, _U|_X, _U,
	_U,    _U,    _U,    _U,    _U,    _U,    _U,    _U,
	_U,    _U,    _U,    _U,    _U,    _U,    _U,    _U,
	_U,    _U,    _U,    _P,    _P,    _P,    _P,    _P,
	_P,    _L|_X, _L|_X, _L|_X, _L|_X, _L|_X, _L|_X, _L,
	_L,    _L,    _L,    _L,    _L,    _L,    _L,    _L,
	_L,    _L,    _L,    _L,    _L,    _L,    _L,    _L,
	_L,    _L,    _L,    _P,    _P,    _P,    _P,    _C,
	0,     0,     0,     0,     0,     0,     0,     0,
	0,     0,     0,     0,     0,     0,     0,     0,
	0,     0,     0,     0,     0,     0,     0,     0,
	0,     0,     0,     0,     0,     0,     0,     0,
	0,     0,     0,     0,     0,     0,     0,     0,
	0,     0,     0,     0,     0,     0,     0,     0,
	0,     0,     0,     0,     0,     0,     0,     0,
	0,     0,     0,     0,     0,     0,     0,     0,
	0,     0,     0,     0,     0,     0,     0,     0,
	0,     0,     0,     0,     0,     0,     0,     0,
	0,     0,     0,     0,     0,     0,     0,     0,
	0,     0,     0,     0,     0,     0,     0,     0,
	0,     0,     0,     0,     0,     0,     0,     0,
	0,     0,     0,     0,     0,     0,     0,     0,
	0,     0,     0,     0,     0,     0,     0,     0,
	0,     0,     0,     0,     0,     0,     0,     0 
};

so_env so_dynarec_env;
Dynarmic::A64::Jit *so_dynarec = nullptr;
Dynarmic::ExclusiveMonitor *so_monitor = nullptr;
Dynarmic::A64::UserConfig so_dynarec_cfg;
uint8_t *so_stack;
uint8_t *tpidr_el0;

void *text_base;
void *aligned_text_base;
size_t text_size;

void *data_base;
void *aligned_data_base;
size_t data_size;

static void *load_base;
static size_t load_size;

static void *so_base;

static Elf64_Ehdr *elf_hdr;
static Elf64_Phdr *prog_hdr;
static Elf64_Shdr *sec_hdr;
static Elf64_Sym *syms;
static int num_syms;

static char *shstrtab;
static char *dynstrtab;

void end_program_token() { }
void unresolved_stub_token() { }

#ifdef USE_INTERPRETER
static void hook_import(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
	address -= HOOKS_BASE_ADDRESS;
	//debugLog(">>> Import called %llx (%s)\n", address / 4, native_funcs_names[address / 4].c_str());
	uc_emu_stop(uc);
	auto host_next = (void (*)(void *))(native_funcs[address / 4]);
	host_next((void*)so_dynarec);
}

static void hook_patch(uc_engine *uc, uint64_t mem_address, uint32_t size, void *user_data) {
	//debugLog("hook_patch %llx\n", mem_address);
	uint32_t address = *(uint32_t *)mem_address / 4;
	//debugLog(">>> Hooked function called %llx (%s)\n", address, native_funcs_names[address].c_str());
	uc_emu_stop(uc);
	auto host_next = (void (*)(void *))(native_funcs[address]);
	host_next((void*)so_dynarec);
}
#endif

void hook_arm64(uintptr_t addr, dynarec_hook *dst) {
#ifdef USE_INTERPRETER
	//debugLog("hook_arm64 %llx, %s\n", addr, native_funcs_names[(uint32_t)dst->trampoline / 4].c_str());
	auto hook = hooks.emplace_back();
	*(uint32_t *)addr = dst->trampoline;
	uc_hook_add(uc, &hook, UC_HOOK_CODE, (void *)hook_patch, NULL, addr, addr);
#else
	if (addr == 0)
		return;
	memcpy((void *)addr, (void *)dst->trampoline, sizeof(uint32_t) * 5);
#endif
}

void so_flush_caches(void) {
#ifndef USE_INTERPRETER
	so_dynarec->InvalidateCacheRange((uintptr_t)load_base, load_size);
#endif
}

void so_free_temp(void) {
	free(so_base);
	so_base = NULL;
}

#ifdef USE_INTERPRETER
// Hooks to deal with dynamically allocated memory mapping
uc_hook mem_invalid_hook;

struct mem_span {
	uint64_t start;
	uint64_t end;
	mem_span *next;
	mem_span *prev;
};

mem_span *span_list = nullptr;
static int span_count = 0;

bool unmappedMemoryHook(uc_engine* uc, uc_mem_type type, u64 start_address, int size, u64 value, void* user_data) {
	const auto do_map = [&](const uint64_t start, const uint64_t end) {
		if (const auto err = uc_mem_map_ptr(uc, start, end - start, UC_PROT_ALL, (void *)start)) {
			printf("Failed to map unmapped memory at %p: %u (%s)\n", (void*)start, err, uc_strerror(err));
			abort();
			return false;
		}
		return true;
	};

	const auto do_unmap = [&](const uint64_t start, const uint64_t end) {
		if (const auto err = uc_mem_unmap(uc, start, end - start)) {
			printf("Failed to unmap mapped memory at %p: %u (%s)\n", (void*)start, err, uc_strerror(err));
			abort();
			return false;
		}
		return true;
	};

	uint64_t a_start = start_address & ~0xFFFULL;
	uint64_t a_end = (start_address + size + 0xFFFULL) & ~0xFFFULL;

	if (a_start < 0x1000ULL)
		return false;

	if (span_list == nullptr) {
		// list is empty; become head
		span_list = new mem_span { a_start, a_end, nullptr, nullptr };
		++span_count;
		do_map(a_start, a_end);
		return true;
	}

	if (a_start <= span_list->start) {
		// we're at the beginning of the list; check if we can merge
		if (a_end >= span_list->start) {
			// yes; just expand the head node
			do_unmap(span_list->start, span_list->end);
			span_list->start = a_start;
			span_list->end = std::max(a_end, span_list->end);
			do_map(span_list->start, span_list->end);
		} else {
			// no; insert ourselves at the start
			span_list = new mem_span { a_start, a_end, span_list, nullptr };
			++span_count;
			span_list->next->prev = span_list;
			do_map(a_start, a_end);
		}
		return true;
	}

	// find where we fit in
	mem_span *it = span_list;
	while (it->next && it->next->start < a_start) {
		it = it->next;
	}

	if (it->next == nullptr) {
		// reached end of list; check if we can merge with the last node
		if (it->end >= a_start) {
			// yes; just expand it then
			do_unmap(it->start, it->end);
			do_map(it->start, a_end);
			it->end = a_end;
		} else {
			// no; add ourselves to the end
			it->next = new mem_span { a_start, a_end, nullptr, it };
			++span_count;
			do_map(a_start, a_end);
		}
		return true;
	}

	// in the middle somewhere
	if (it->end >= a_start && it->next->start <= a_end) {
		// double merge; bridge the gap and remove the right node
		do_unmap(it->start, it->end);
		do_unmap(it->next->start, it->next->end);
		it->end = it->next->end;
		mem_span *new_next = it->next->next;
		if (new_next)
			new_next->prev = it;
		delete it->next;
		--span_count;
		it->next = new_next;
		do_map(it->start, it->end);
	} else if (it->end >= a_start) {
		// left merge; extend left node to include us
		do_unmap(it->start, it->end);
		do_map(it->start, a_end);
		it->end = a_end;
	} else if (it->next->start <= a_end) {
		// right merge; extend right node to include us
		do_unmap(it->next->start, it->next->end);
		do_map(a_start, it->next->end);
		it->next->start = a_start;
	} else {
		// no merge; insert ourselves in the middle
		mem_span *new_node = new mem_span { a_start, a_end, it->next, it };
		++span_count;
		it->next->prev = new_node;
		it->next = new_node;
		do_map(a_start, a_end);
	}

	return true;
}
#endif

int so_load(const char *filename, void **base_addr) {
#ifdef USE_INTERPRETER
	// Set up dynamic memory mapping hooks
	uc_err err = uc_hook_add(uc, &mem_invalid_hook, UC_HOOK_MEM_INVALID, (void*)unmappedMemoryHook, NULL, 0, ~u64(0));
	if (err) {
		debugLog("Failed to setup dynamic memory handler %u (%s)\n", err, uc_strerror(err));
		return -1;
	}
#endif
	int res = 0;
	size_t so_size = 0;
	int text_segno = -1;
	int data_segno = -1;

	FILE *fd = fopen(filename, "rb");
	if (fd == NULL)
		return -1;

	fseek(fd, 0, SEEK_END);
	so_size = ftell(fd);
	fseek(fd, 0, SEEK_SET);

	so_base = malloc(so_size);
	if (!so_base) {
		fclose(fd);
		return -2;
	}

	fread(so_base, so_size, 1, fd);
	fclose(fd);

	if (memcmp(so_base, ELFMAG, SELFMAG) != 0) {
		res = -1;
		goto err_free_so;
	}

	elf_hdr = (Elf64_Ehdr *)so_base;
	prog_hdr = (Elf64_Phdr *)((uintptr_t)so_base + elf_hdr->e_phoff);
	sec_hdr = (Elf64_Shdr *)((uintptr_t)so_base + elf_hdr->e_shoff);
	shstrtab = (char *)((uintptr_t)so_base + sec_hdr[elf_hdr->e_shstrndx].sh_offset);

	// calculate total size of the LOAD segments
	for (int i = 0; i < elf_hdr->e_phnum; i++) {
		if (prog_hdr[i].p_type == PT_LOAD) {
			const size_t prog_size = ALIGN_MEM(prog_hdr[i].p_memsz, prog_hdr[i].p_align);
			// get the segment numbers of text and data segments
			if ((prog_hdr[i].p_flags & PF_X) == PF_X) {
				text_segno = i;
			} else {
				// assume data has to be after text
				if (text_segno < 0)
					goto err_free_so;
				data_segno = i;
				// since data is after text, total program size = last_data_offset + last_data_aligned_size
				load_size = prog_hdr[i].p_vaddr + prog_size;
			}
		}
	}

	// align total size to page size
	load_size = ALIGN_MEM(load_size, 0x1000);
	if (load_size > DYNAREC_MEMBLK_SIZE) {
		res = -3;
		goto err_free_so;
	}
	debugLog("Total LOAD size: %llu bytes\n", load_size);

	// allocate space for all load segments (align to page size)
	debugLog("Allocating dynarec memblock of %llu bytes\n", load_size);
	load_base = memalign(0x1000, load_size);
	if (!load_base)
		goto err_free_so;
	memset(load_base, 0, load_size);

#ifdef USE_INTERPRETER
	err = uc_mem_map_ptr(uc, (uintptr_t)load_base, load_size, UC_PROT_ALL, load_base);
	if (err) {
		debugLog("Failed to map ELF memory %u (%s)\n", err, uc_strerror(err));
		return -1;
	}

	err = uc_mem_map(uc, (uintptr_t)HOOKS_BASE_ADDRESS, HOOKS_BLOCK_SIZE, UC_PROT_ALL);
	if (err) {
		debugLog("Failed to allocate region for function hooks\n");
		return -1;
	}
#endif
	
	// copy segments to where they belong

	// text
	text_size = prog_hdr[text_segno].p_memsz;
	text_base = (void *)(prog_hdr[text_segno].p_vaddr + (Elf64_Addr)load_base);
	prog_hdr[text_segno].p_vaddr = (Elf64_Addr)text_base;
	memcpy(text_base, (void *)((uintptr_t)so_base + prog_hdr[text_segno].p_offset), prog_hdr[text_segno].p_filesz);

	// data
	data_size = prog_hdr[data_segno].p_memsz;
	data_base = (void *)(prog_hdr[data_segno].p_vaddr + (Elf64_Addr)load_base);
	prog_hdr[data_segno].p_vaddr = (Elf64_Addr)data_base;
	memcpy(data_base, (void *)((uintptr_t)so_base + prog_hdr[data_segno].p_offset), prog_hdr[data_segno].p_filesz);

	syms = NULL;
	dynstrtab = NULL;

	for (int i = 0; i < elf_hdr->e_shnum; i++) {
		char *sh_name = shstrtab + sec_hdr[i].sh_name;
		if (strcmp(sh_name, ".dynsym") == 0) {
			syms = (Elf64_Sym *)((uintptr_t)text_base + sec_hdr[i].sh_addr);
			num_syms = sec_hdr[i].sh_size / sizeof(Elf64_Sym);
		} else if (strcmp(sh_name, ".dynstr") == 0) {
			dynstrtab = (char *)((uintptr_t)text_base + sec_hdr[i].sh_addr);
		}
	}
	
	*base_addr = load_base;

	if (syms == NULL || dynstrtab == NULL) {
		res = -2;
		goto err_free_load;
	}

	return 0;

err_free_load:
#ifdef __MINGW64__
	_aligned_free(load_base);
#else
	free(load_base);
#endif
err_free_so:
	free(so_base);

	return res;
}

#ifdef USE_INTERPRETER
static void unresolved_symbol_hook(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
	uint64_t ra;
	uc_reg_read(uc, REG_FP, &ra);
	debugLog("Unresolved symbol called from %llx\n", ra - (uintptr_t)dynarec_base_addr);
	abort();
}
#endif


uintptr_t get_trampoline(const char *name, dynarec_import *funcs, int num_funcs)
{
#ifdef USE_INTERPRETER
	static bool unresolved_symbol_mapped = false;
	if (!unresolved_symbol_mapped) {
		unresolved_symbol_mapped = true;
		auto hook = hooks.emplace_back();
		uc_hook_add(uc, &hook, UC_HOOK_CODE, (void *)unresolved_symbol_hook, NULL, HOOKS_BASE_ADDRESS + HOOKS_BLOCK_SIZE - 4, HOOKS_BASE_ADDRESS + HOOKS_BLOCK_SIZE - 4);
	}
#endif

	for (int k = 0; k < num_funcs; k++) {
		if (strcmp(name, funcs[k].symbol) == 0) {
			if (funcs[k].ptr == (uintptr_t)NULL) {
#ifdef USE_INTERPRETER
				if (!funcs[k].mapped) {
					uint32_t nop = 0xD503201F;
					auto hook = hooks.emplace_back();
					//debugLog("%s %llx hook on %llx\n", name, (uintptr_t)funcs[k].trampoline / 4, HOOKS_BASE_ADDRESS + (uintptr_t)funcs[k].trampoline - (uintptr_t)load_base);
					uc_hook_add(uc, &hook, UC_HOOK_CODE, (void *)hook_import, NULL, HOOKS_BASE_ADDRESS + (uintptr_t)funcs[k].trampoline, HOOKS_BASE_ADDRESS + (uintptr_t)funcs[k].trampoline);
					uc_mem_write(uc, HOOKS_BASE_ADDRESS + (uintptr_t)funcs[k].trampoline, &nop, 4);
					funcs[k].mapped = true;
				}
				return HOOKS_BASE_ADDRESS + (uintptr_t)funcs[k].trampoline;
#else
				return (uintptr_t)funcs[k].trampoline;
#endif
			} else {	
				return (uintptr_t)funcs[k].symbol;
			}
		}
	}
	
	// Redirect _ctype_ to BIONIC variant
	if (strcmp(name, "_ctype_") == 0) {
#ifdef USE_INTERPRETER
		uc_mem_write(uc, HOOKS_BASE_ADDRESS + (HOOKS_BLOCK_SIZE - 0x1000), __BIONIC_ctype_, sizeof(__BIONIC_ctype_) * sizeof(*__BIONIC_ctype_));
		return HOOKS_BASE_ADDRESS + (HOOKS_BLOCK_SIZE - 0x1000);
#else
		return (uintptr_t)__BIONIC_ctype_;
#endif
	// Redirect stack guard related pointers
	} else if (strcmp(name, "__stack_chk_guard") == 0) {
#ifdef USE_INTERPRETER
		uc_mem_write(uc, HOOKS_BASE_ADDRESS + (HOOKS_BLOCK_SIZE - 0x1000 - 8), &__stack_chk_guard_fake, 8);
		return HOOKS_BASE_ADDRESS + (HOOKS_BLOCK_SIZE - 0x1000 - 8);
#else
		return (uintptr_t)&__stack_chk_guard_fake;
#endif
	} else if (strcmp(name, "__stack_chk_fail") == 0) {
#ifdef USE_INTERPRETER
		uc_mem_write(uc, HOOKS_BASE_ADDRESS + (HOOKS_BLOCK_SIZE - 0x1000 - 16), &__stack_chk_fail, 8);
		return HOOKS_BASE_ADDRESS + (HOOKS_BLOCK_SIZE - 0x1000 - 16);
#else
		return __stack_chk_fail;
#endif
	// Redirect stderr to fake one so that we can intercept it in __aarch64_fprintf
	} else if (strcmp(name, "stderr") == 0) {
#ifdef USE_INTERPRETER
		uc_mem_write(uc, HOOKS_BASE_ADDRESS + (HOOKS_BLOCK_SIZE - 0x1000 - 24), &stderr_fake, 8);
		return HOOKS_BASE_ADDRESS + (HOOKS_BLOCK_SIZE - 0x1000 - 24);
#else
		return (uintptr_t)&stderr_fake;
#endif
	}
	
	debugLog("Unresolved import: %s\n", name);
#ifdef USE_INTERPRETER
	return (uintptr_t)HOOKS_BASE_ADDRESS + HOOKS_BLOCK_SIZE - 4;
#else
	return (uintptr_t)unresolved_stub_token;
#endif
}

int so_relocate() {
	for (int i = 0; i < elf_hdr->e_shnum; i++) {
		char *sh_name = shstrtab + sec_hdr[i].sh_name;
		if (strcmp(sh_name, ".rela.dyn") == 0 || strcmp(sh_name, ".rela.plt") == 0) {
			Elf64_Rela *rels = (Elf64_Rela *)((uintptr_t)text_base + sec_hdr[i].sh_addr);
			for (int j = 0; j < sec_hdr[i].sh_size / sizeof(Elf64_Rela); j++) {
				uintptr_t *ptr = (uintptr_t *)((uintptr_t)text_base + rels[j].r_offset);
				Elf64_Sym *sym = &syms[ELF64_R_SYM(rels[j].r_info)];

				int type = ELF64_R_TYPE(rels[j].r_info);
				uintptr_t target;
				switch (type) {
					case R_AARCH64_RELATIVE:
						target = (uintptr_t)text_base + rels[j].r_addend;
						memcpy(ptr, &target, sizeof(uintptr_t));
						break;
					case R_AARCH64_ABS64:
						target = *ptr + (uintptr_t)text_base + sym->st_value + rels[j].r_addend;
						memcpy(ptr, &target, sizeof(uintptr_t));
						break;
					case R_AARCH64_GLOB_DAT:
					case R_AARCH64_JUMP_SLOT:
					{
						if (sym->st_shndx != SHN_UNDEF) {
							target = (uintptr_t)text_base + sym->st_value + rels[j].r_addend;
							memcpy(ptr, &target, sizeof(uintptr_t));
						}
						break;
					}

					default:
						debugLog("Error: unknown relocation type:\n%x\n", type);
						break;
				}
			}
		}
	}
	return 0;
}

int so_resolve(dynarec_import *funcs, int num_funcs) {
	for (int i = 0; i < elf_hdr->e_shnum; i++) {
		char *sh_name = shstrtab + sec_hdr[i].sh_name;
		if (strcmp(sh_name, ".rela.dyn") == 0 || strcmp(sh_name, ".rela.plt") == 0) {
			Elf64_Rela *rels = (Elf64_Rela *)((uintptr_t)text_base + sec_hdr[i].sh_addr);
			for (int j = 0; j < sec_hdr[i].sh_size / sizeof(Elf64_Rela); j++) {
				uintptr_t *ptr = (uintptr_t *)((uintptr_t)text_base + rels[j].r_offset);
				Elf64_Sym *sym = &syms[ELF64_R_SYM(rels[j].r_info)];

				int type = ELF64_R_TYPE(rels[j].r_info);
				switch (type) {
					case R_AARCH64_GLOB_DAT:
					case R_AARCH64_JUMP_SLOT:
					{
						if (sym->st_shndx == SHN_UNDEF) {
							char *name = dynstrtab + sym->st_name;
							uintptr_t link = get_trampoline(name, funcs, num_funcs);
							*ptr = (uintptr_t)link;
						}
						break;
					}

					default:
						break;
				}
			}
		}
	}

	return 0;
}

#ifdef GDB_ENABLED
uintptr_t gdb_fiber_pc;
uintptr_t gdb_fiber_fp;
uintptr_t gdb_thunk_fp;
#endif

void so_run_fiber(Dynarmic::A64::Jit *jit, uintptr_t entry) {
	//debugLog("Run 0x%llx with end_program_token %llx\n", entry - (uintptr_t)text_base, end_program_token);
#ifdef USE_INTERPRETER
	uintptr_t exit_token = (uintptr_t)load_base;
	uc_reg_write(uc, REG_FP, &exit_token);
	uc_err err;
	uintptr_t sp, pc, fp;
	do {
		uc_reg_read(uc, REG_FP, &fp);
		//debugLog("Run 0x%llx with FP: %llx (%llx)\n", entry - (uintptr_t)text_base, fp, fp - exit_token);
#ifdef GDB_ENABLED
		gdb_fiber_pc = entry;
		gdb_fiber_fp = fp;
#endif
		err = uc_emu_start(uc, entry, fp, 0, 0);
		uc_reg_read(uc, UC_ARM64_REG_PC, &entry);
		if (entry == exit_token)
			break;
	} while (!err);
	if (err) {
		uc_reg_read(uc, UC_ARM64_REG_SP, &sp);
		uc_reg_read(uc, UC_ARM64_REG_PC, &pc);
		uc_reg_read(uc, REG_FP, &fp);
		debugLog("Fatal error in Unicorn: %u %s on PC: %llx, SP: %llx, FP: %llx\n", err, uc_strerror(err), pc - (uintptr_t)load_base, sp, fp - (uintptr_t)load_base);
		std::abort();
	}
#else
	jit->SetRegister(REG_FP, (uintptr_t)end_program_token);
	jit->SetPC(entry);
	Dynarmic::HaltReason reason = {};
	while ((reason = jit->Run()) == Dynarmic::HaltReason::UserDefined2) {
		auto host_next = (void (*)(void *))(native_funcs[*(uint32_t *)jit->GetPC()]);
		//debugLog("host_next 0x%llx\n", host_next);
		host_next((void*)jit);
	}
	if (reason != Dynarmic::HaltReason::UserDefined1) {
		debugLog("fiber: Execution ended with failure.\n");
		std::abort();
	}
#endif
}

void so_execute_init_array(void) {
	debugLog("so_execute_init_array called\n");
	for (int i = 0; i < elf_hdr->e_shnum; i++) {
		char *sh_name = shstrtab + sec_hdr[i].sh_name;
		if (strcmp(sh_name, ".init_array") == 0) {
			int (** init_array)() = (int (**)())((uintptr_t)text_base + sec_hdr[i].sh_addr);
			for (int j = 0; j < sec_hdr[i].sh_size / 8; j++) {
				if (init_array[j] != 0) {
					debugLog("init_array on 0x%llx (%llx)\n", (uintptr_t)init_array[j], (uintptr_t)init_array[j] - (uintptr_t)text_base);
					so_run_fiber(so_dynarec, (uintptr_t)init_array[j]);
				}
			}
		}
	}
}

uintptr_t so_find_addr(const char *symbol) {
	for (int i = 0; i < num_syms; i++) {
		char *name = dynstrtab + syms[i].st_name;
		if (strcmp(name, symbol) == 0)
			return (uintptr_t)text_base + syms[i].st_value;
	}

	debugLog("Error: could not find symbol:\n%s\n", symbol);
	return 0;
}

uintptr_t so_find_rel_addr(const char *symbol) {
	for (int i = 0; i < elf_hdr->e_shnum; i++) {
		char *sh_name = shstrtab + sec_hdr[i].sh_name;
		if (strcmp(sh_name, ".rela.dyn") == 0 || strcmp(sh_name, ".rela.plt") == 0) {
			Elf64_Rela *rels = (Elf64_Rela *)((uintptr_t)text_base + sec_hdr[i].sh_addr);
			for (int j = 0; j < sec_hdr[i].sh_size / sizeof(Elf64_Rela); j++) {
				Elf64_Sym *sym = &syms[ELF64_R_SYM(rels[j].r_info)];

				int type = ELF64_R_TYPE(rels[j].r_info);
				if (type == R_AARCH64_GLOB_DAT || type == R_AARCH64_JUMP_SLOT) {
					char *name = dynstrtab + sym->st_name;
					if (strcmp(name, symbol) == 0)
						return (uintptr_t)text_base + rels[j].r_offset;
				}
			}
		}
	}

	debugLog("Error: could not find symbol:\n%s\n", symbol);
	return 0;
}

const char *so_find_rela_name(uintptr_t rela_ptr) {
	for (int i = 0; i < elf_hdr->e_shnum; i++) {
		char *sh_name = shstrtab + sec_hdr[i].sh_name;
		if (strcmp(sh_name, ".rela.dyn") == 0 || strcmp(sh_name, ".rela.plt") == 0) {
			Elf64_Rela *rels = (Elf64_Rela *)((uintptr_t)text_base + sec_hdr[i].sh_addr);
			for (int j = 0; j < sec_hdr[i].sh_size / sizeof(Elf64_Rela); j++) {
				Elf64_Sym *sym = &syms[ELF64_R_SYM(rels[j].r_info)];
				uintptr_t *ptr = (uintptr_t *)((uintptr_t)text_base + rels[j].r_offset);
				int type = ELF64_R_TYPE(rels[j].r_info);
				if (type == R_AARCH64_GLOB_DAT || type == R_AARCH64_JUMP_SLOT) {
					char *name = dynstrtab + sym->st_name;
					if ((uintptr_t)ptr == rela_ptr)
						return name;
				}
			}
		}
	}
	return "Unknown symbol";
}

uintptr_t so_find_addr_rx(const char *symbol) {
	for (int i = 0; i < num_syms; i++) {
		char *name = dynstrtab + syms[i].st_name;
		if (strcmp(name, symbol) == 0)
			return (uintptr_t)syms[i].st_value;
	}

	debugLog("Error: could not find symbol:\n%s\n", symbol);
	return 0;
}

dynarec_import *so_find_import(dynarec_import *funcs, int num_funcs, const char *name) {
	for (int i = 0; i < num_funcs; ++i)
		if (!strcmp(funcs[i].symbol, name))
			return &funcs[i];
	return NULL;
}

int so_unload(void) {
	if (load_base == NULL)
		return -1;

	if (so_base) {
		// someone forgot to free the temp data
		so_free_temp();
	}

	return 0;
}

// Dynarmic overrides

std::optional<std::uint32_t> so_env::MemoryReadCode(std::uint64_t vaddr) 
{
	// found the canary token for unresolved symbols
	if (vaddr == (uintptr_t)unresolved_stub_token) {
		uintptr_t f1 = so_dynarec->GetRegister(16);
		uintptr_t f2 = so_dynarec->GetRegister(17);
		return 0xD4000001 | (2 << 5);
	// found the canary token for returning from top-level function
	} else if (vaddr == (uintptr_t)end_program_token) {
		debugLog("vaddr %p: emitting end_program_token\n", vaddr);
		return 0xD4000001 | (0 << 5);
	}
	
#ifdef TPIDR_EL0_HACK
		if ((uintptr_t)vaddr < 0x1000)
			vaddr += (uintptr_t)tpidr_el0;
#endif
		return *(std::uint32_t *)(vaddr);
}
void so_env::CallSVC(std::uint32_t swi)
{
	uintptr_t pc = so_dynarec->GetPC() - 0x4;
	switch (swi) {
	case 0:
		// Execution done
		so_dynarec->HaltExecution();
		break;
	case 1:
		// Yield from guest for a moment to handle host code requested,
		// leaving this instance available for nested callbacks and whatnot
		//debugLog("Halting for host function execution: %p\n", so_dynarec->GetPC());
		so_dynarec->HaltExecution(Dynarmic::HaltReason::UserDefined2);
		break;
	case 2:
		{
			// Let's find the .got entry for this function, so we can display an error
			// message.
			uintptr_t f1 = so_dynarec->GetRegister(16);
			debugLog("Unresolved symbol %s\n", so_find_rela_name(f1));
			so_dynarec->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
		}
		break;
	default:
		debugLog("Unknown SVC %d\n", swi);
		break;
	}
}
