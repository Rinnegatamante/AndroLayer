// Based on information available on: https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst#appendix-variable-argument-lists
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "variadics.h"

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
			switch (*s) {
			case 'h':
				var_size--;
				s++;
				goto PROCESS_VAR;
				break;
			case 'l':
				var_size++;
				s++;
				goto PROCESS_VAR;
				break;
			case 'f':
				var_type = VAR_FLOAT;
				break;
			case 'd':
				var_type = VAR_SIGNED_INTEGER;
				break;
			case 'u':
			case 'x':
			case 'X':
				var_type = VAR_UNSIGNED_INTEGER;
				break;
			case 's':
				var_type = VAR_STRING;
				break;
			}
			if (var_type == VAR_UNKNOWN) {
				printf("Failure parsing va_list with format %s due to unknown var type\n", format);
				abort();
			}
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
			printf("Argument decoded: %s\n", tmp);
			res.replace(str_offs + start_offs, end_offs - start_offs, tmp);
			str_offs = res.size() - orig_sz;
		}
		s = strstr(s, "%");
	}
	
	printf("Finished parsing va_list %s -> %s\n", format, res.c_str());
	return res;
}