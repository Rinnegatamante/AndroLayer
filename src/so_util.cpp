/* so_util.c -- utils to load and hook .so modules
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

void *text_base, *text_virtbase;
size_t text_size;

void *data_base, *data_virtbase;
size_t data_size;

static void *load_base, *load_virtbase;
static size_t load_size;

static void *so_base;

static Elf64_Ehdr *elf_hdr;
static Elf64_Phdr *prog_hdr;
static Elf64_Shdr *sec_hdr;
static Elf64_Sym *syms;
static int num_syms;

static char *shstrtab;
static char *dynstrtab;

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
	so_dynarec->InvalidateCacheRange((uint64_t)load_virtbase, load_size);
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

	// allocate space for all load segments (align to page size)
	load_base = malloc(DYNAREC_MEMBLK_SIZE);
	*base_addr = load_base;
	if (!load_base)
		goto err_free_so;
	memset(load_base, 0, DYNAREC_MEMBLK_SIZE);

	// reserve virtual memory space for the entire LOAD zone while we're dealing with the ELF
	if (so_dynarec_env.mem_size == 0) {
		printf("Allocating dynarec memblock of %llu bytes\n", DYNAREC_MEMBLK_SIZE);
		so_dynarec_env.mem_size = DYNAREC_MEMBLK_SIZE;
		so_dynarec_env.memory = load_base;
	}
	
	load_virtbase = load_base;

	printf("load base = %p\n", load_virtbase);

	// copy segments to where they belong

	// text
	text_size = prog_hdr[text_segno].p_memsz;
	text_virtbase = (void *)(prog_hdr[text_segno].p_vaddr + (Elf64_Addr)load_virtbase);
	text_base = (void *)(prog_hdr[text_segno].p_vaddr + (Elf64_Addr)load_base);
	prog_hdr[text_segno].p_vaddr = (Elf64_Addr)text_virtbase;
	memcpy(text_base, (void *)((uintptr_t)so_base + prog_hdr[text_segno].p_offset), prog_hdr[text_segno].p_filesz);

	// data
	data_size = prog_hdr[data_segno].p_memsz;
	data_virtbase = (void *)(prog_hdr[data_segno].p_vaddr + (Elf64_Addr)load_virtbase);
	data_base = (void *)(prog_hdr[data_segno].p_vaddr + (Elf64_Addr)load_base);
	prog_hdr[data_segno].p_vaddr = (Elf64_Addr)data_virtbase;
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

int so_relocate(void) {
	for (int i = 0; i < elf_hdr->e_shnum; i++) {
		char *sh_name = shstrtab + sec_hdr[i].sh_name;
		if (strcmp(sh_name, ".rela.dyn") == 0 || strcmp(sh_name, ".rela.plt") == 0) {
			Elf64_Rela *rels = (Elf64_Rela *)((uintptr_t)text_base + sec_hdr[i].sh_addr);
			for (int j = 0; j < sec_hdr[i].sh_size / sizeof(Elf64_Rela); j++) {
				uintptr_t *ptr = (uintptr_t *)((uintptr_t)text_base + rels[j].r_offset);
				Elf64_Sym *sym = &syms[ELF64_R_SYM(rels[j].r_info)];

				int type = ELF64_R_TYPE(rels[j].r_info);
				switch (type) {
					case R_AARCH64_ABS64:
						// FIXME: = or += ?
						*ptr = (uintptr_t)text_virtbase + sym->st_value + rels[j].r_addend;
						break;

					case R_AARCH64_RELATIVE:
						// sometimes the value of r_addend is also at *ptr
						*ptr = (uintptr_t)text_virtbase + rels[j].r_addend;
						break;

					case R_AARCH64_GLOB_DAT:
					case R_AARCH64_JUMP_SLOT:
					{
						if (sym->st_shndx != SHN_UNDEF)
							*ptr = (uintptr_t)text_virtbase + sym->st_value + rels[j].r_addend;
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

typedef struct {
	uint64_t orig_addr;
} trampoline;
std::map<uint64_t, trampoline> trampolines;

int so_resolve(dynarec_import *funcs, int num_funcs, int taint_missing_imports) {
	for (int i = 0; i < elf_hdr->e_shnum; i++) {
		char *sh_name = shstrtab + sec_hdr[i].sh_name;
		if (strcmp(sh_name, ".rela.dyn") == 0 || strcmp(sh_name, ".rela.plt") == 0) {
			Elf64_Rela *rels = (Elf64_Rela *)((uintptr_t)text_base + sec_hdr[i].sh_addr);
			for (int j = 0; j < sec_hdr[i].sh_size / sizeof(Elf64_Rela); j++) {
				bool resolved = false;
				uintptr_t *ptr = (uintptr_t *)((uintptr_t)text_base + rels[j].r_offset);
				Elf64_Sym *sym = &syms[ELF64_R_SYM(rels[j].r_info)];

				int type = ELF64_R_TYPE(rels[j].r_info);
				switch (type) {
					case R_AARCH64_GLOB_DAT:
					case R_AARCH64_JUMP_SLOT:
					{
						if (sym->st_shndx == SHN_UNDEF) {
							// make it crash for debugging
							if (taint_missing_imports)
								*ptr = rels[j].r_offset;

							char *name = dynstrtab + sym->st_name;
							for (int k = 0; k < num_funcs; k++) {
								if (strcmp(name, funcs[k].symbol) == 0) {
									*ptr = funcs[k].func;
									resolved = true;
									break;
								}
							}
							
							if (!resolved) {
								printf("Unresolved import: %s\n", name);
							}
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

void so_execute_init_array(void) {
	for (int i = 0; i < elf_hdr->e_shnum; i++) {
		char *sh_name = shstrtab + sec_hdr[i].sh_name;
		if (strcmp(sh_name, ".init_array") == 0) {
			int (** init_array)() = (int (**)())((uintptr_t)text_virtbase + sec_hdr[i].sh_addr);
			for (int j = 0; j < sec_hdr[i].sh_size / 8; j++) {
				if (init_array[j] != 0) {
					//printf("Initing array at 0x%x\n", (uint64_t)init_array[j] - (uint64_t)text_virtbase);
					so_dynarec->SetPC((uint64_t)init_array[j] - (uint64_t)text_virtbase);
					so_dynarec->Run();
					//printf("PC is: %x\n", so_dynarec->GetPC());
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
