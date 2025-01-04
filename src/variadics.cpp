// Based on information available on: https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst#appendix-variable-argument-lists
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "variadics.h"
#include "dynarec.h"

#define MAX_SSCANF_OUTS (8)
extern FILE *stderr_fake;

#define parse_token(s) \
	switch (*s) { \
		case '0': \
		case '1': \
		case '2': \
		case '3': \
		case '4': \
		case '5': \
		case '6': \
		case '7': \
		case '8': \
		case '9': \
			s++; \
			goto PROCESS_VAR; \
			break; \
		case 'h': \
			var_size--; \
			s++; \
			goto PROCESS_VAR; \
			break; \
		case 'l': \
			var_size++; \
			s++; \
			goto PROCESS_VAR; \
			break; \
		case 'f': \
			var_type = VAR_FLOAT; \
			break; \
		case 'd': \
		case 'i': \
			var_type = VAR_SIGNED_INTEGER; \
			break; \
		case 'u': \
		case 'x': \
		case 'X': \
			var_type = VAR_UNSIGNED_INTEGER; \
			break; \
		case 's': \
			var_type = VAR_STRING; \
			break; \
		default: \
			debugLog("Unrecognized format token %c\n", *s); \
			break; \
		} \
		if (var_type == VAR_UNKNOWN) { \
			debugLog("Failure parsing va_list with format %s due to unknown var type\n", format); \
			abort(); \
		}

std::string parse_format(const char *format, int startReg) {
#ifdef USE_INTERPRETER
	uintptr_t sp;
	uc_reg_read(uc, UC_ARM64_REG_SP, &sp);
#else
	uintptr_t sp = so_dynarec->GetSP();
#endif
	std::string res = format;
	size_t orig_sz = res.size();
	int str_offs = 0;
	char *s = strstr(format, "%");
	while (s) {
		char *start = s;
		size_t start_offs = (size_t)(start - format);
		int var_size = VAR_DEFAULT;
		int var_type = VAR_UNKNOWN;
		if (s[1] == '%') {
			s += 2;
		}
		else
		{
			s++;
PROCESS_VAR:
			parse_token(s)
			s++;
			size_t end_offs = (size_t)(s - format);
			char token[8];
			memcpy(token, start, end_offs - start_offs);
			token[end_offs - start_offs] = 0;
			char tmp[1024];
			bool onStack = false;
			if (startReg > 7)
				onStack = true;
			uint64_t reg_val;
			switch (var_type) {
			case VAR_STRING:
#ifdef USE_INTERPRETER
				if (!onStack)
					uc_reg_read(uc, UC_ARM64_REG_X0 + startReg++, &reg_val);
				sprintf(tmp, token, onStack ? *(char **)sp : (char *)reg_val);
#else
				sprintf(tmp, token, onStack ? *(char **)sp : (char *)so_dynarec->GetRegister(startReg++));
#endif
				break;
			case VAR_SIGNED_INTEGER:
				switch (var_size) {
				case VAR_SHORT_SHORT:
#ifdef USE_INTERPRETER
					if (!onStack)
						uc_reg_read(uc, UC_ARM64_REG_X0 + startReg++, &reg_val);
					sprintf(tmp, token, onStack ? *(int8_t*)sp : (int8_t)reg_val);
#else
					sprintf(tmp, token, onStack ? *(int8_t*)sp : (int8_t)so_dynarec->GetRegister(startReg++));
#endif
					break;
				case VAR_SHORT:
#ifdef USE_INTERPRETER
					if (!onStack)
						uc_reg_read(uc, UC_ARM64_REG_X0 + startReg++, &reg_val);
					sprintf(tmp, token, onStack ? *(int16_t*)sp : (int16_t)reg_val);
#else
					sprintf(tmp, token, onStack ? *(int16_t*)sp : (int16_t)so_dynarec->GetRegister(startReg++));
#endif					
					break;
				case VAR_DEFAULT:
				case VAR_LONG:
#ifdef USE_INTERPRETER
					if (!onStack)
						uc_reg_read(uc, UC_ARM64_REG_X0 + startReg++, &reg_val);
					sprintf(tmp, token, onStack ? *(int32_t*)sp : (int32_t)reg_val);
#else
					sprintf(tmp, token, onStack ? *(int32_t*)sp : (int32_t)so_dynarec->GetRegister(startReg++));
#endif					
					break;
				case VAR_LONG_LONG:
					sprintf(tmp, token, onStack ? *(int64_t*)sp : (int64_t)so_dynarec->GetRegister(startReg++));
					break;
				}
				break;
			case VAR_UNSIGNED_INTEGER:
				switch (var_size) {
				case VAR_SHORT_SHORT:
#ifdef USE_INTERPRETER
					if (!onStack)
						uc_reg_read(uc, UC_ARM64_REG_X0 + startReg++, &reg_val);
					sprintf(tmp, token, onStack ? *(uint8_t*)sp : (uint8_t)reg_val);
#else
					sprintf(tmp, token, onStack ? *(uint8_t*)sp : (uint8_t)so_dynarec->GetRegister(startReg++));
#endif					
					break;
				case VAR_SHORT:
#ifdef USE_INTERPRETER
					if (!onStack)
						uc_reg_read(uc, UC_ARM64_REG_X0 + startReg++, &reg_val);
					sprintf(tmp, token, onStack ? *(uint16_t*)sp : (uint16_t)reg_val);
#else
					sprintf(tmp, token, onStack ? *(uint16_t*)sp : (uint16_t)so_dynarec->GetRegister(startReg++));
#endif					
					break;
				case VAR_DEFAULT:
				case VAR_LONG:
#ifdef USE_INTERPRETER
					if (!onStack)
						uc_reg_read(uc, UC_ARM64_REG_X0 + startReg++, &reg_val);
					sprintf(tmp, token, onStack ? *(uint32_t*)sp : (uint32_t)reg_val);
#else
					sprintf(tmp, token, onStack ? *(uint32_t*)sp : (uint32_t)so_dynarec->GetRegister(startReg++));
#endif					
					break;
				case VAR_LONG_LONG:
#ifdef USE_INTERPRETER
					if (!onStack)
						uc_reg_read(uc, UC_ARM64_REG_X0 + startReg++, &reg_val);
					sprintf(tmp, token, onStack ? *(uint64_t*)sp : (uint64_t)reg_val);
#else
					sprintf(tmp, token, onStack ? *(uint64_t*)sp : (uint64_t)so_dynarec->GetRegister(startReg++));
#endif						
					break;
				}
				break;
			case VAR_FLOAT:
#ifdef USE_INTERPRETER
				if (!onStack)
					uc_reg_read(uc, UC_ARM64_REG_X0 + startReg++, &reg_val);
				sprintf(tmp, token, onStack ? *(double*)sp : (double)reg_val);
#else
				sprintf(tmp, token, onStack ? *(double*)sp : (double)so_dynarec->GetRegister(startReg++));
#endif
				break;
			}
			if (onStack)
				sp -= 8;
			//debugLog("Argument decoded: %s\n", tmp);
			res.replace(str_offs + start_offs, end_offs - start_offs, tmp);
			str_offs = res.size() - orig_sz;
		}
		s = strstr(s, "%");
	}
	
	//debugLog("Finished parsing va_list %s -> %s\n", format, res.c_str());
	return res;
}

size_t count_args(const char *format) {
	size_t res = 0;
	char *s = strstr(format, "%");
	while (s) {
		int var_size = VAR_DEFAULT;
		int var_type = VAR_UNKNOWN;
		if (s[1] == '%') {
			s += 2;
		}
		else
		{
			s++;
PROCESS_VAR:
			parse_token(s)
			s++;
			res++;
		}
		s = strstr(s, "%");
	}
	
	//debugLog("Finished counting arguments in %s -> %u\n", format, res);
	return res;
}

std::string parse_va_list(const char *format, __aarch64_va_list *va) {
	std::string res = format;
	size_t orig_sz = res.size();
	int str_offs = 0;
	char *s = strstr(format, "%");
	while (s) {
		char *start = s;
		size_t start_offs = (size_t)(start - format);
		int var_size = VAR_DEFAULT;
		int var_type = VAR_UNKNOWN;
		if (s[1] == '%') {
			s += 2;
		}
		else
		{
			s++;
PROCESS_VAR:
			parse_token(s)
			s++;
			size_t end_offs = (size_t)(s - format);
			char token[8];
			memcpy(token, start, end_offs - start_offs);
			token[end_offs - start_offs] = 0;
			char tmp[1024];
			switch (var_type) {
			case VAR_STRING:
				sprintf(tmp, token, __aarch64_va_arg(char *, va, 0));
				break;
			case VAR_SIGNED_INTEGER:
				switch (var_size) {
				case VAR_SHORT_SHORT:
					sprintf(tmp, token, __aarch64_va_arg(int8_t, va, 0));
					break;
				case VAR_SHORT:
					sprintf(tmp, token, __aarch64_va_arg(int16_t, va, 0));
					break;
				case VAR_DEFAULT:
				case VAR_LONG:
					sprintf(tmp, token, __aarch64_va_arg(int32_t, va, 0));
					break;
				case VAR_LONG_LONG:
					sprintf(tmp, token, __aarch64_va_arg(int64_t, va, 0));
					break;
				}
				break;
			case VAR_UNSIGNED_INTEGER:
				switch (var_size) {
				case VAR_SHORT_SHORT:
					sprintf(tmp, token, __aarch64_va_arg(uint8_t, va, 0));
					break;
				case VAR_SHORT:
					sprintf(tmp, token, __aarch64_va_arg(uint16_t, va, 0));
					break;
				case VAR_DEFAULT:
				case VAR_LONG:
					sprintf(tmp, token, __aarch64_va_arg(uint32_t, va, 0));
					break;
				case VAR_LONG_LONG:
					sprintf(tmp, token, __aarch64_va_arg(uint64_t, va, 0));
					break;
				}
				break;
			case VAR_FLOAT:
				sprintf(tmp, token, __aarch64_va_arg(double, va, 1));
				break;
			}
			//debugLog("Argument decoded: %s\n", tmp);
			res.replace(str_offs + start_offs, end_offs - start_offs, tmp);
			str_offs = res.size() - orig_sz;
		}
		s = strstr(s, "%");
	}
	
	debugLog("Finished parsing va_list %s -> %s\n", format, res.c_str());
	return res;
}

int __aarch64_vsnprintf(char *target, size_t n, const char *format, __aarch64_va_list *v) {
	// GCC stores actual va_list struct in va_list[1]...
	v++;
	std::string s = parse_va_list(format, v);
	
	return snprintf(target, n, "%s", s.c_str());
}

int __aarch64_vsprintf(char *target, const char *format, __aarch64_va_list *v) {
	// GCC stores actual va_list struct in va_list[1]...
	v++;
	std::string s = parse_va_list(format, v);
	
	return sprintf(target, "%s", s.c_str());	
}

int __aarch64_sprintf(char *buffer, const char *format) {
	std::string s = parse_format(format, 2);
	return sprintf(buffer, "%s", s.c_str());
}

int __aarch64_snprintf(char *buffer, size_t n, const char *format) {
	std::string s = parse_format(format, 3);
	return snprintf(buffer, n, "%s", s.c_str());
}

int __aarch64_fprintf(FILE *fp, char *format) {
	std::string s = parse_format(format, 2);
	if (fp == stderr_fake) {
		return printf("[stderr] %s\n", s.c_str());
	}
	return fprintf(fp, "%s", s.c_str());
}

int __aarch64_printf(const char *format) {
	std::string s = parse_format(format, 1);
	return printf("%s", s.c_str());
}

int __aarch64_sscanf(const char *buffer, const char *format) {
	size_t num_args = count_args(format);
#ifdef USE_INTERPRETER
	uintptr_t sp;
	uc_reg_read(uc, UC_ARM64_REG_SP, &sp);
#else
	uintptr_t sp = so_dynarec->GetSP();
#endif
	int startReg = 2;
	uintptr_t out_ptrs[MAX_SSCANF_OUTS];
	bool onStack = false;
	for (size_t i = 0; i < num_args; i++) {
		if (startReg > 7)
			onStack = true;
#ifdef USE_INTERPRETER
		if (!onStack)
			uc_reg_read(uc, UC_ARM64_REG_X0 + startReg++, &out_ptrs[i]);
		else
			out_ptrs[i] = *(uintptr_t *)sp;
#else
		out_ptrs[i] = onStack ? *(uintptr_t *)sp : (uintptr_t)so_dynarec->GetRegister(startReg++);
#endif
		if (onStack)
			sp -= 8;
	}

	switch (num_args) {
	case 0:
		return sscanf(buffer, format);
	case 1:
		return sscanf(buffer, format, out_ptrs[0]);
	case 2:
		return sscanf(buffer, format, out_ptrs[0], out_ptrs[1]);
	case 3:
		return sscanf(buffer, format, out_ptrs[0], out_ptrs[1], out_ptrs[2]);
	case 4:
		return sscanf(buffer, format, out_ptrs[0], out_ptrs[1], out_ptrs[2], out_ptrs[3]);
	case 5:
		return sscanf(buffer, format, out_ptrs[0], out_ptrs[1], out_ptrs[2], out_ptrs[3], out_ptrs[4]);
	case 6:
		return sscanf(buffer, format, out_ptrs[0], out_ptrs[1], out_ptrs[2], out_ptrs[3], out_ptrs[4], out_ptrs[5]);
	case 7:
		return sscanf(buffer, format, out_ptrs[0], out_ptrs[1], out_ptrs[2], out_ptrs[3], out_ptrs[4], out_ptrs[5], out_ptrs[6]);
	case 8:
		return sscanf(buffer, format, out_ptrs[0], out_ptrs[1], out_ptrs[2], out_ptrs[3], out_ptrs[4], out_ptrs[5], out_ptrs[6], out_ptrs[7]);
	default:
		debugLog("Failure running sscanf on %s. Too many arguments\n", format);
		abort();
		break;
	}
	return 0;
}
