// Based on information available on: https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst#appendix-variable-argument-lists
#ifndef _VARIADICS_H_
#define _VARIADICS_H_

#define MAX_VARIADICS_ARGS (8)

typedef struct 
{ 
	void *__stack; 
	void *__gr_top; 
	void *__vr_top;
	int   __gr_offs;
	int   __vr_offs;
} __aarch64_va_list;

enum {
	VAR_SHORT_SHORT,
	VAR_SHORT,
	VAR_DEFAULT,
	VAR_LONG,
	VAR_LONG_LONG
};

enum {
	VAR_UNKNOWN,
	VAR_SIGNED_INTEGER,
	VAR_UNSIGNED_INTEGER,
	VAR_FLOAT,
	VAR_STRING
};

#define __aarch64_va_arg(type, va, is_float) \
	({ \
		type r; \
		int done = 0; \
		if (is_float) { \
			if (va->__vr_offs < 0) { \
				int nreg = (sizeof(type) + 15) / 16; \
				int offs = va->__vr_offs; \
				va->__vr_offs += nreg * 16; \
				if (va->__vr_offs < 0) { \
					r = *(type *)(va->__vr_top + offs); \
					printf("arg is on VR on %llx + %d\n", va->__vr_top, offs); \
					done = 1; \
				} \
			} \
		} else { \
			if (va->__gr_offs < 0) { \
				int nreg = (sizeof(type) + 7) / 8; \
				int offs = va->__gr_offs; \
				va->__gr_offs += nreg * 8; \
				if (va->__gr_offs < 0) { \
					r = *(type *)(va->__gr_top + offs); \
					printf("arg is on GR on %llx + %d\n", va->__gr_top, offs); \
					done = 1; \
				} \
			} \
		} \
		if (!done) { \
			intptr_t arg = (intptr_t)va->__stack; \
			if (alignof(type) > 8) \
				arg = (arg + 15) & -16; \
			va->__stack = (void *)((arg + sizeof(type) + 7) & -8); \
			printf("arg is on stack on %llx\n", arg); \
			r = *(type *)arg; \
		} \
		r; \
	})

std::string parse_va_list(const char *format, __aarch64_va_list *va);

#endif
