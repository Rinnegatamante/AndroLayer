#include "clib.h"
#include "dynarec.h"

int __aarch64_gettimeofday(aarch64_timeval *tv, aarch64_timezone *tz) {
	struct timeval t;
#ifdef __MINGW64__
	int ret = mingw_gettimeofday(&t, (struct timezone *)tz);
#else
	int ret = gettimeofday(&t, (struct timezone *)tz);
#endif
	tv->tv_sec = t.tv_sec;
	tv->tv_usec = t.tv_usec;

	return ret;
}

size_t __aarch64_fwrite(void *ptr, size_t dim, size_t num, FILE *fp) {
	// Redirecting stderr to native one
	if (fp == stderr_fake) {
		return printf("[stderr] %s\n", ptr);
	}
	return fwrite(ptr, dim, num, fp);
}

// Windows has RAND_MAX set to only 0x7fff and games might rely on larger range like on linux machines (0x7fffffff), so we reimplement it
static uint32_t rand_state = 0;
void __aarch64_srand(unsigned int seed) {
	rand_state = seed;
}

int __aarch64_rand() {
	rand_state = ((rand_state * 1103515245) + 12345) & 0x7fffffff;
	return rand_state;
}

size_t __ctype_get_mb_cur_max() {
	return 1;
}

// Some math functions in C++ have different prototypes and makes it messy with WRAP_FUNC, this workarounds the issue
double __aarch64_acos(double n) { return acos(n); }
double __aarch64_cos(double n) { return cos(n); }
double __aarch64_exp(double n) { return exp(n); }
double __aarch64_fmod(double n, double n2) { return fmod(n, n2); }
double __aarch64_log(double n) { return log(n); }
double __aarch64_pow(double n, double n2) { return pow(n, n2); }
double __aarch64_sin(double n) { return sin(n); }
double __aarch64_sqrt(double n) { return sqrt(n); }

int __aarch64__cxa_atexit(void (*func) (void *), void *arg, void *dso_handle)
{
	return 0;
}

char *stpcpy(char *s1, char *s2) {
	strcpy(s1, s2);
	return &s1[strlen(s1)];
}

// qsort and bsearch will receive AARCH64 functions for 'compare'. So we keep a database of the game functions and matching reimplementation in native host code that we'll call instead
std::unordered_map<uintptr_t, int (*)(const void *, const void *)> qsort_db;
void __aarch64_qsort(void *base, size_t num, size_t width, int(*compare)(const void *key, const void *element)) {
	auto native_f = qsort_db.find((uintptr_t)compare);
	if (native_f == qsort_db.end()) {
		printf("Fatal error: Invalid qsort function: %llx\n", (uintptr_t)compare - (uintptr_t)dynarec_base_addr);
		abort();
	}
	qsort(base, num, width, native_f->second);
}
void *__aarch64_bsearch(const void *key, const void *base, size_t num, size_t size, int (*compare)(const void *element1, const void *element2)) {
	auto native_f = qsort_db.find((uintptr_t)compare);
	if (native_f == qsort_db.end()) {
		printf("Fatal error: Invalid bsearch function: %llx\n", (uintptr_t)compare - (uintptr_t)dynarec_base_addr);
		abort();
	}
	return bsearch(key, base, num, size, native_f->second);
}
