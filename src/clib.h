#ifndef _AARCH64_CLIB_H_
#define _AARCH64_CLIB_H_

#include <unordered_map>
#include <string.h>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <math.h>
#include <stdint.h>

typedef struct {
	int64_t tv_sec;
	uint64_t tv_usec;
} aarch64_timeval;

typedef struct {
	int tz_minuteswest;
	int tz_dsttime;
} aarch64_timezone;

extern FILE *stderr_fake;

extern std::unordered_map<uintptr_t, int (*)(const void *, const void *)> qsort_db;

// Missing libc symbols
int __aarch64__cxa_atexit(void (*func) (void *), void *arg, void *dso_handle);
char *stpcpy(char *s1, char *s2);

// libc patches
void *__aarch64_bsearch(const void *key, const void *base, size_t num, size_t size, int (*compare)(const void *element1, const void *element2));
size_t __aarch64_fwrite(void *ptr, size_t dim, size_t num, FILE *fp);
int __aarch64_gettimeofday(aarch64_timeval *tv, aarch64_timezone *tz);
void __aarch64_qsort(void *base, size_t num, size_t width, int(*compare)(const void *key, const void *element));
int __aarch64_rand();
void __aarch64_srand(unsigned int seed);

// BIONIC ctype implementation
size_t __ctype_get_mb_cur_max();

// libmath patches
double __aarch64_acos(double n);
double __aarch64_cos(double n);
double __aarch64_exp(double n);
double __aarch64_fmod(double n, double n2);
double __aarch64_log(double n);
double __aarch64_pow(double n, double n2);
double __aarch64_sin(double n);
double __aarch64_sqrt(double n);

#endif
