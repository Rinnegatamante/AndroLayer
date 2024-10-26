/* so_util.cpp -- utils to load and hook .so modules
 *
 * Copyright (C) 2024 Alessio Tosto, Andy Nguyen, fgsfds
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.	See the LICENSE file for details.
 */

#include <stdlib.h>
#include <string.h>
#include <map>

#include "elf.h"
#include "dynarec.h"
#include "so_util.h"

so_env so_dynarec_env;
Dynarmic::A64::Jit *so_dynarec = nullptr;
Dynarmic::ExclusiveMonitor *so_monitor = nullptr;
Dynarmic::A64::UserConfig so_dynarec_cfg;
uint8_t so_stack[1024 * 1024 * 8];

void *text_base;
size_t text_size;

void *data_base;
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

void hook_thumb(uintptr_t addr, uintptr_t dst) {
	if (addr == 0)
		return;
	addr &= ~1;
	if (addr & 2) {
		uint16_t nop = 0xbf00;
		memcpy((void *)addr, &nop, sizeof(nop));
		addr += 2;
	}
	uint32_t hook[2];
	hook[0] = 0xf000f8df; // LDR PC, [PC]
	hook[1] = dst;
	memcpy((void *)addr, hook, sizeof(hook));
}

void hook_arm(uintptr_t addr, uintptr_t dst) {
	if (addr == 0)
		return;
	uint32_t hook[2];
	hook[0] = 0xe51ff004; // LDR PC, [PC, #-0x4]
	hook[1] = dst;
	memcpy((void *)addr, hook, sizeof(hook));
}

void hook_arm64(uintptr_t addr, uintptr_t dst) {
	if (addr == 0)
		return;
	uint32_t *hook = (uint32_t *)addr;
	hook[0] = 0x58000051u; // LDR X17, #0x8
	hook[1] = 0xd61f0220u; // BR X17
	*(uint64_t *)(hook + 2) = dst;
}

void so_flush_caches(void) {
	so_dynarec->InvalidateCacheRange((uint64_t)load_base, load_size);
}

void so_free_temp(void) {
	free(so_base);
	so_base = NULL;
}

int so_load(const char *filename, void **base_addr) {
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
	printf("Total LOAD size: %llu bytes\n", load_size);

	// allocate space for all load segments (align to page size)
	printf("Allocating dynarec memblock of %llu bytes\n", DYNAREC_MEMBLK_SIZE);
	load_base = malloc(DYNAREC_MEMBLK_SIZE);
	*base_addr = load_base;
	if (!load_base)
		goto err_free_so;
	memset(load_base, 0, DYNAREC_MEMBLK_SIZE);
	
	// copy segments to where they belong

	// text
	text_size = prog_hdr[text_segno].p_memsz;
	text_base = (void *)(prog_hdr[text_segno].p_vaddr + (Elf64_Addr)load_base);
	text_base = (void *)ALIGN_MEM((uintptr_t)text_base, prog_hdr[text_segno].p_align);
	prog_hdr[text_segno].p_vaddr = (Elf64_Addr)text_base;
	memcpy(text_base, (void *)((uintptr_t)so_base + prog_hdr[text_segno].p_offset), prog_hdr[text_segno].p_filesz);

	// data
	data_size = prog_hdr[data_segno].p_memsz;
	data_base = (void *)(prog_hdr[data_segno].p_vaddr + (Elf64_Addr)load_base);
	data_base = (void *)ALIGN_MEM((uintptr_t)data_base, prog_hdr[data_segno].p_align);
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

	if (syms == NULL || dynstrtab == NULL) {
		res = -2;
		goto err_free_load;
	}

	return 0;

err_free_load:
	free(load_base);
err_free_so:
	free(so_base);

	return res;
}

uintptr_t get_trampoline(const char *name, dynarec_import *funcs, int num_funcs)
{
	for (int k = 0; k < num_funcs; k++) {
		if (strcmp(name, funcs[k].symbol) == 0) {
			if (funcs[k].ptr == NULL)
				return (uintptr_t)funcs[k].trampoline;
			else
				return (uintptr_t)funcs[k].symbol;
		}
	}
	return (uintptr_t)unresolved_stub_token;
}

int so_relocate(dynarec_import *funcs, int num_funcs) {
	for (int i = 0; i < elf_hdr->e_shnum; i++) {
		char *sh_name = shstrtab + sec_hdr[i].sh_name;
		if (strcmp(sh_name, ".rela.dyn") == 0 || strcmp(sh_name, ".rela.plt") == 0) {
			Elf64_Rela *rels = (Elf64_Rela *)((uintptr_t)text_base + sec_hdr[i].sh_addr);
			for (int j = 0; j < sec_hdr[i].sh_size / sizeof(Elf64_Rela); j++) {
				uintptr_t *ptr = (uintptr_t *)((uintptr_t)text_base + rels[j].r_offset);
				Elf64_Sym *sym = &syms[ELF64_R_SYM(rels[j].r_info)];

				int type = ELF64_R_TYPE(rels[j].r_info);
				switch (type) {
					case R_AARCH64_RELATIVE:
						// sometimes the value of r_addend is also at *ptr
						*ptr = (uintptr_t)text_base + rels[j].r_addend;
						break;
					case R_ARM_RELATIVE:
						*ptr += (uintptr_t)text_base;
						break;
					case R_AARCH64_ABS64:
					case R_AARCH64_GLOB_DAT:
					case R_AARCH64_JUMP_SLOT:
					{
						if (sym->st_shndx != SHN_UNDEF) {
							if (type == R_AARCH64_ABS64)
								*ptr += (uintptr_t)text_base + sym->st_value;
							else
								*ptr = (uintptr_t)text_base + sym->st_value + rels[j].r_addend;
						} else {
							char *name = dynstrtab + sym->st_name;
							uintptr_t link = get_trampoline(name, funcs, num_funcs);
							*ptr = (uintptr_t)link;
						}
						break;
					}

					default:
						printf("Error: unknown relocation type:\n%x\n", type);
						break;
				}
			}
		}
	}

	return 0;
}

void so_run_fiber(Dynarmic::A64::Jit *jit, uintptr_t entry)
{
	jit->SetRegister(REG_RA, (uintptr_t)end_program_token);
	jit->SetPC(entry);
	Dynarmic::HaltReason reason = {};
	while ((reason = jit->Run()) == Dynarmic::HaltReason::UserDefined2) {
		auto host_next = (void (*)(void *))jit->GetRegister(16);
		host_next((void*)jit);
	}
	if (reason != Dynarmic::HaltReason::UserDefined1) {
		printf("fiber: Execution ended with failure.\n");
		std::abort();
	}
}

void so_execute_init_array(void) {
	for (int i = 0; i < elf_hdr->e_shnum; i++) {
		char *sh_name = shstrtab + sec_hdr[i].sh_name;
		if (strcmp(sh_name, ".init_array") == 0) {
			int (** init_array)() = (int (**)())((uintptr_t)text_base + sec_hdr[i].sh_addr);
			for (int j = 0; j < sec_hdr[i].sh_size / 8; j++) {
				if (init_array[j] != 0) {
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

	printf("Error: could not find symbol:\n%s\n", symbol);
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

	printf("Error: could not find symbol:\n%s\n", symbol);
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

	printf("Error: could not find symbol:\n%s\n", symbol);
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
		uintptr_t f1 = parent->GetRegister(16);
		uintptr_t f2 = parent->GetRegister(17);
		return 0xD4000001 | (2 << 5);
	}
	// found the canary token for returning from top-level function
	else if (vaddr == (uintptr_t)end_program_token) {
		printf("vaddr %p: emitting end_program_token\n", vaddr);
		return 0xD4000001 | (0 << 5);
	}
	
	return MemoryRead32(vaddr);
}
void so_env::CallSVC(std::uint32_t swi)
{
	uintptr_t pc = parent->GetPC() - 0x4;
	switch (swi) {
	case 0:
		// Execution done
		parent->HaltExecution();
		break;
	case 1:
		// Yield from guest for a moment to handle host code requested,
		// leaving this instance available for nested callbacks and whatnot
		printf("Halting for host function execution: %p\n", parent->GetRegister(16));
		parent->HaltExecution(Dynarmic::HaltReason::UserDefined2);
		break;
	case 2:
		{
			// Let's find the .got entry for this function, so we can display an error
			// message.
			uintptr_t f1 = parent->GetRegister(16);
			printf("Unresolved symbol %s\n", so_find_rela_name(f1));
			parent->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
		}
		break;
	default:
		printf("Unknown SVC %d\n", swi);
		break;
	}
}
