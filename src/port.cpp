#define __USE_GNU
#include "glad/glad.h"
#include "dyn_util.h"
#include <GLFW/glfw3.h>

#include <dirent.h>
#include <string.h>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <math.h>

#include "dynarec.h"
#include "so_util.h"
#include "thunk_gen.h"
#include "port.h"
#include "variadics.h"

#define AL_ALEXT_PROTOTYPES
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>

extern "C" {
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
}

/*
 * Custom imports implementations
 */
char *stpcpy(char *s1, char *s2) {
	strcpy(s1, s2);
	return &s1[strlen(s1)];
}

int __android_log_print(int prio, const char *tag, const char *fmt) {
	std::string s = parse_format(fmt, 3); // startReg is # of fixed function args + 1
	debugLog("[%s] %s\n", tag, s.c_str());
	
	return 0;
}

int __cxa_atexit_fake(void (*func) (void *), void *arg, void *dso_handle)
{
	return 0;
}

int pthread_once_fake(pthread_once_t *__once_control, void (*__init_routine) (void)) {
	if (*__once_control == PTHREAD_ONCE_INIT) {
		so_run_fiber(so_dynarec, (uintptr_t)__init_routine);
		*__once_control = !PTHREAD_ONCE_INIT;
	}

	return 0;
}

int pthread_create_fake (Dynarmic::A64::Jit *jit, pthread_t *__restrict __newthread,
			   const pthread_attr_t *__restrict __attr,
			   void *(*__start_routine) (void *),
			   void *__restrict __arg)
{
	debugLog("NOIMPL: pthread_create called\n");
	std::abort();
	return 0;
}

int pthread_mutex_init_fake(pthread_mutex_t** uid, const int* mutexattr) {
	pthread_mutex_t *m = (pthread_mutex_t *)calloc(1, sizeof(pthread_mutex_t));
	if (!m)
		return -1;

	const int recursive = (mutexattr && *mutexattr == 1);
	*m = recursive ? PTHREAD_RECURSIVE_MUTEX_INITIALIZER : PTHREAD_MUTEX_INITIALIZER;

	int ret = pthread_mutex_init(m, NULL);
	if (ret < 0) {
		free(m);
		return -1;
	}

	*uid = m;

	return 0;
}

int pthread_mutex_destroy_fake(pthread_mutex_t** uid) {
	if (uid && *uid && (uintptr_t)*uid > 0x8000) {
		pthread_mutex_destroy(*uid);
		free(*uid);
		*uid = NULL;
	}
	return 0;
}

int pthread_mutex_lock_fake(pthread_mutex_t** uid) {
	int ret = 0;
	if (!*uid) {
		ret = pthread_mutex_init_fake(uid, NULL);
	}
	else if ((uintptr_t)*uid == 0x4000) {
		int attr = 1; // recursive
		ret = pthread_mutex_init_fake(uid, &attr);
	}
	if (ret < 0) {
		return ret;
	}
	return pthread_mutex_lock(*uid);
}

int pthread_mutex_unlock_fake(pthread_mutex_t** uid) {
	int ret = 0;
	if (!*uid) {
		ret = pthread_mutex_init_fake(uid, NULL);
	}
	else if ((uintptr_t)*uid == 0x4000) {
		int attr = 1; // recursive
		ret = pthread_mutex_init_fake(uid, &attr);
	}
	if (ret < 0)
		return ret;
	return pthread_mutex_unlock(*uid);
}

FILE *fopen_fake(char *fname, char *mode) {
	FILE *f = fopen(fname, mode);
	debugLog("fopen(%s, %s) -> %llx\n", fname, mode, f);
	return f;
}

// qsort uses AARCH64 functions, so we map them to native variants
int ZIPFile_EntryCompare(const void *key, const void *element) {
	return strcasecmp(*((const char **)((uintptr_t)key + 8)), *((const char **)((uintptr_t)element + 8)));
}
int RASFileNameComp(const void *key, const void *element) {
	return strcasecmp(*(const char **)key, *(const char **)element);
}
int FontCmp(const void *key, const void *element) {
	return *(int *)((uintptr_t)element + 4) - *(int *)((uintptr_t)key + 4);
}
int AnimationMessageContainerCmp(const void *key, const void *element) {
	float *a2 = (float *)key;
	float *a1 = (float *)element;
	if (*a1 < *a2)
		return 1;
	else if (*a1 == *a2)
		return 0;
	return -1;
}
int PriorityCompHiToLow(const void *key, const void *element) {
	int64_t keyval = *(int64_t *)key;
	int64_t elemval = *(int64_t *)element;
	float keyfval = *(float *)(keyval + 524);
	float elemfval = *(float *)(elemval + 524);
	if (keyfval < elemfval)
		return 1;
	else if (keyfval == elemfval)
		return 0;
	return -1;
}
int cmpl(const void *key, const void *element) {
	double keyval = *(double *)key;
	double elemval = *(double *)element;
	double diff = keyval - elemval;
	if (diff <= 0.0) {
		if (diff < 0.0f) {
			return -1;
		} else {
			keyval = *((double *)key + 1);
			elemval = *((double *)element + 1);
			diff = elemval - keyval;
			if (diff == 0.0)
				return 0;
			else if (diff < 0.0)
				return 1;
			else
				return -1;
		}
	}
	return 1;
}
int cmph(const void *key, const void *element) {
	return cmpl(element, key);
}
std::unordered_map<uintptr_t, int (*)(const void *, const void *)> qsort_db;
void qsort_fake(void *base, size_t num, size_t width, int(*compare)(const void *key, const void *element)) {
	auto native_f = qsort_db.find((uintptr_t)compare);
	if (native_f == qsort_db.end()) {
		debugLog("Fatal error: Invalid qsort function: %llx\n", (uintptr_t)compare - (uintptr_t)dynarec_base_addr);
		abort();
	}
	qsort(base, num, width, native_f->second);
}

// bsearch uses AARCH64 functions, so we map them to native variants
std::unordered_map<uintptr_t, int (*)(const void *, const void *)> bsearch_db;
void *bsearch_fake(const void *key, const void *base, size_t num, size_t size, int (*compare)(const void *element1, const void *element2)) {
	auto native_f = bsearch_db.find((uintptr_t)compare);
	if (native_f == bsearch_db.end()) {
		debugLog("Fatal error: Invalid bsearch function: %llx\n", (uintptr_t)compare - (uintptr_t)dynarec_base_addr);
		abort();
	}
	return bsearch(key, base, num, size, native_f->second);
}

int ret0() {
	return 0;
}

typedef struct {
	int64_t tv_sec;
	uint64_t tv_usec;
} aarch64_timeval;

typedef struct {
	int tz_minuteswest;
	int tz_dsttime;
} aarch64_timezone;

int gettimeofday_hook(aarch64_timeval *tv, aarch64_timezone *tz) {
	//debugLog("Entering gettimeofday\n");
	struct timeval t;
#ifdef __MINGW64__
	int ret = mingw_gettimeofday(&t, (struct timezone *)tz);
#else
	int ret = gettimeofday(&t, (struct timezone *)tz);
#endif
	tv->tv_sec = t.tv_sec;
	tv->tv_usec = t.tv_usec;
	//debugLog("Exiting gettimeofday\n");
	return ret;
}

const GLubyte *glGetString_fake(GLenum name) {
	// The game miscalculates buffer sizes for compressed textures, so we need to fakely report that we support no compressed format
	if (name == GL_EXTENSIONS)
		return (const GLubyte *)"";
	return glGetString(name);
}

// Some math functions in C++ have different prototypes and makes it messy with WRAP_FUNC, this workarounds the issue
double acosd(double n) { return acos(n); }
double cosd(double n) { return cos(n); }
double expd(double n) { return exp(n); }
double fmodd(double n, double n2) { return fmod(n, n2); }
double logd(double n) { return log(n); }
double powd(double n, double n2) { return pow(n, n2); }
double sind(double n) { return sin(n); }
double sqrtd(double n) { return sqrt(n); }

// BIONIC ctype implementation
size_t __ctype_get_mb_cur_max() {
	return 1;
}

/*
 * List of imports to be resolved with native variants
 */
#define WRAP_FUNC(name, func) gen_wrapper<&func>(name)
dynarec_import dynarec_imports[] = {
	WRAP_FUNC("__android_log_print", __android_log_print),
	WRAP_FUNC("__ctype_get_mb_cur_max", __ctype_get_mb_cur_max),
	WRAP_FUNC("__cxa_atexit", __cxa_atexit_fake),
	WRAP_FUNC("__google_potentially_blocking_region_begin", ret0),
	WRAP_FUNC("__google_potentially_blocking_region_end", ret0),
	WRAP_FUNC("AAssetManager_open", ret0),
	WRAP_FUNC("AAssetManager_fromJava", ret0),
	WRAP_FUNC("AAsset_close", ret0),
	WRAP_FUNC("AAsset_getLength", ret0),
	WRAP_FUNC("AAsset_getRemainingLength", ret0),
	WRAP_FUNC("AAsset_read", ret0),
	WRAP_FUNC("AAsset_seek", ret0),
	WRAP_FUNC("abort", abort),
	WRAP_FUNC("acos", acosd),
	WRAP_FUNC("acosf", acosf),
	WRAP_FUNC("asinf", asinf),
	WRAP_FUNC("atan2f", atan2f),
	WRAP_FUNC("atanf", atanf),
	WRAP_FUNC("atof", atof),
	WRAP_FUNC("atoi", atoi),
	WRAP_FUNC("bsearch", bsearch_fake),
	WRAP_FUNC("btowc", btowc),
	WRAP_FUNC("calloc", calloc),
	WRAP_FUNC("close", close),
	WRAP_FUNC("closedir", closedir),
	WRAP_FUNC("cos", cosd),
	WRAP_FUNC("cosf", cosf),
	WRAP_FUNC("exp", expd),
	WRAP_FUNC("fclose", fclose),
	WRAP_FUNC("feof", feof),
	WRAP_FUNC("ferror", ferror),
	WRAP_FUNC("fflush", fflush),
	WRAP_FUNC("fgetc", fgetc),
	WRAP_FUNC("fgets", fgets),
	WRAP_FUNC("fmod", fmodd),
	WRAP_FUNC("fmodf", fmodf),
#if 1 // Debug variant with logging
	WRAP_FUNC("fopen", fopen_fake),
#else
	WRAP_FUNC("fopen", fopen),
#endif
	WRAP_FUNC("fprintf", __aarch64_fprintf),
	WRAP_FUNC("fputc", fputc),
	WRAP_FUNC("fputs", fputs),
	WRAP_FUNC("fread", fread),
	WRAP_FUNC("free", free),
	WRAP_FUNC("fseek", fseek),
	WRAP_FUNC("ftell", ftell),
	WRAP_FUNC("fwrite", fwrite),
	WRAP_FUNC("getc", getc),
	WRAP_FUNC("getenv", ret0),
	WRAP_FUNC("gettimeofday", gettimeofday_hook),
	WRAP_FUNC("getwc", getwc),
	WRAP_FUNC("glActiveTexture", _glActiveTexture),
	WRAP_FUNC("glAttachShader", _glAttachShader),
	WRAP_FUNC("glBindAttribLocation", _glBindAttribLocation),
	WRAP_FUNC("glBindBuffer", _glBindBuffer),
	WRAP_FUNC("glBindFramebuffer", _glBindFramebuffer),
	WRAP_FUNC("glBindRenderbuffer", _glBindRenderbuffer),
	WRAP_FUNC("glBindTexture", _glBindTexture),
	WRAP_FUNC("glBlendFunc", _glBlendFunc),
	WRAP_FUNC("glBlendFuncSeparate", _glBlendFuncSeparate),
	WRAP_FUNC("glBufferData", _glBufferData),
	WRAP_FUNC("glCheckFramebufferStatus", _glCheckFramebufferStatus),
	WRAP_FUNC("glClear", _glClear),
	WRAP_FUNC("glClearColor", _glClearColor),
	WRAP_FUNC("glClearDepthf", _glClearDepthf),
	WRAP_FUNC("glClearStencil", _glClearStencil),
	WRAP_FUNC("glCompileShader", _glCompileShader),
	WRAP_FUNC("glCompressedTexImage2D", _glCompressedTexImage2D),
	WRAP_FUNC("glCreateProgram", _glCreateProgram),
	WRAP_FUNC("glCreateShader", _glCreateShader),
	WRAP_FUNC("glCullFace", _glCullFace),
	WRAP_FUNC("glDeleteBuffers", _glDeleteBuffers),
	WRAP_FUNC("glDeleteFramebuffers", _glDeleteFramebuffers),
	WRAP_FUNC("glDeleteProgram", _glDeleteProgram),
	WRAP_FUNC("glDeleteRenderbuffers", _glDeleteRenderbuffers),
	WRAP_FUNC("glDeleteShader", _glDeleteShader),
	WRAP_FUNC("glDeleteTextures", _glDeleteTextures),
	WRAP_FUNC("glDepthFunc", _glDepthFunc),
	WRAP_FUNC("glDepthMask", _glDepthMask),
	WRAP_FUNC("glDepthRangef", _glDepthRangef),
	WRAP_FUNC("glDisable", _glDisable),
	WRAP_FUNC("glDisableVertexAttribArray", _glDisableVertexAttribArray),
	WRAP_FUNC("glDrawArrays", _glDrawArrays),
	WRAP_FUNC("glDrawElements", _glDrawElements),
	WRAP_FUNC("glEnable", _glEnable),
	WRAP_FUNC("glEnableVertexAttribArray", _glEnableVertexAttribArray),
	WRAP_FUNC("glFinish", _glFinish),
	WRAP_FUNC("glFramebufferRenderbuffer", _glFramebufferRenderbuffer),
	WRAP_FUNC("glFramebufferTexture2D", _glFramebufferTexture2D),
	WRAP_FUNC("glFrontFace", _glFrontFace),
	WRAP_FUNC("glGenBuffers", _glGenBuffers),
	WRAP_FUNC("glGenFramebuffers", _glGenFramebuffers),
	WRAP_FUNC("glGenRenderbuffers", _glGenRenderbuffers),
	WRAP_FUNC("glGenTextures", _glGenTextures),
	WRAP_FUNC("glGetAttribLocation", _glGetAttribLocation),
	WRAP_FUNC("glGetError", _glGetError),
	WRAP_FUNC("glGetBooleanv", _glGetBooleanv),
	WRAP_FUNC("glGetIntegerv", _glGetIntegerv),
	WRAP_FUNC("glGetProgramInfoLog", _glGetProgramInfoLog),
	WRAP_FUNC("glGetProgramiv", _glGetProgramiv),
	WRAP_FUNC("glGetShaderInfoLog", _glGetShaderInfoLog),
	WRAP_FUNC("glGetShaderiv", _glGetShaderiv),
	WRAP_FUNC("glGetString", _glGetString),
	WRAP_FUNC("glGetUniformLocation", _glGetUniformLocation),
	WRAP_FUNC("glHint", _glHint),
	WRAP_FUNC("glLinkProgram", _glLinkProgram),
	WRAP_FUNC("glPolygonOffset", _glPolygonOffset),
	WRAP_FUNC("glReadPixels", _glReadPixels),
	WRAP_FUNC("glRenderbufferStorage", _glRenderbufferStorage),
	WRAP_FUNC("glScissor", _glScissor),
	WRAP_FUNC("glShaderSource", _glShaderSource),
	WRAP_FUNC("glTexImage2D", _glTexImage2D),
	WRAP_FUNC("glTexParameterf", _glTexParameterf),
	WRAP_FUNC("glTexParameteri", _glTexParameteri),
	WRAP_FUNC("glUniform1f", _glUniform1f),
	WRAP_FUNC("glUniform1fv", _glUniform1fv),
	WRAP_FUNC("glUniform1i", _glUniform1i),
	WRAP_FUNC("glUniform2fv", _glUniform2fv),
	WRAP_FUNC("glUniform3f", _glUniform3f),
	WRAP_FUNC("glUniform3fv", _glUniform3fv),
	WRAP_FUNC("glUniform4fv", _glUniform4fv),
	WRAP_FUNC("glUniformMatrix3fv", _glUniformMatrix3fv),
	WRAP_FUNC("glUniformMatrix4fv", _glUniformMatrix4fv),
	WRAP_FUNC("glUseProgram", _glUseProgram),
	WRAP_FUNC("glVertexAttrib4fv", _glVertexAttrib4fv),
	WRAP_FUNC("glVertexAttribPointer", _glVertexAttribPointer),
	WRAP_FUNC("glViewport", _glViewport),
	WRAP_FUNC("isspace", isspace),
	WRAP_FUNC("log", logd),
	WRAP_FUNC("log10f", log10f),
	WRAP_FUNC("malloc", malloc),
	WRAP_FUNC("mbrtowc", mbrtowc),
	WRAP_FUNC("memchr", memchr),
	WRAP_FUNC("memcpy", memcpy),
	WRAP_FUNC("memcmp", memcmp),
	WRAP_FUNC("memmove", memmove),
	WRAP_FUNC("memset", memset),
	WRAP_FUNC("mkdir", mkdir),
	WRAP_FUNC("nanosleep", nanosleep),
	WRAP_FUNC("pow", powd),
	WRAP_FUNC("powf", powf),
	WRAP_FUNC("printf", __aarch64_printf),
	WRAP_FUNC("pthread_once", pthread_once_fake),
	WRAP_FUNC("pthread_create", pthread_create_fake),
	WRAP_FUNC("pthread_getspecific", ret0),
	WRAP_FUNC("pthread_join", pthread_join),
	WRAP_FUNC("pthread_key_create", ret0),
	WRAP_FUNC("pthread_key_delete", ret0),
	WRAP_FUNC("pthread_mutexattr_init", ret0),
	WRAP_FUNC("pthread_mutexattr_settype", ret0),
	WRAP_FUNC("pthread_mutexattr_destroy", ret0),
	WRAP_FUNC("pthread_mutex_destroy", pthread_mutex_destroy_fake),
	WRAP_FUNC("pthread_mutex_init", pthread_mutex_init_fake),
	WRAP_FUNC("pthread_mutex_lock", pthread_mutex_lock_fake),
	WRAP_FUNC("pthread_mutex_unlock", pthread_mutex_unlock_fake),
	WRAP_FUNC("pthread_self", pthread_self),
	WRAP_FUNC("pthread_setschedparam", ret0),
	WRAP_FUNC("pthread_setspecific", ret0),
	WRAP_FUNC("putc", putc),
	WRAP_FUNC("putwc", putwc),
	WRAP_FUNC("qsort", qsort_fake),
	WRAP_FUNC("rand", rand),
	WRAP_FUNC("readdir", readdir),
	WRAP_FUNC("realloc", realloc),
	WRAP_FUNC("remove", remove),
	WRAP_FUNC("setjmp", ret0),
	WRAP_FUNC("sin", sind),
	WRAP_FUNC("sinf", sinf),
	WRAP_FUNC("snprintf", __aarch64_snprintf),
	WRAP_FUNC("sprintf", __aarch64_sprintf),
	WRAP_FUNC("sqrt", sqrtd),
	WRAP_FUNC("sqrtf", sqrtf),
	WRAP_FUNC("srand", srand),
	WRAP_FUNC("sscanf", __aarch64_sscanf),
	WRAP_FUNC("stpcpy", stpcpy),
	WRAP_FUNC("strcasecmp", strcasecmp),
	WRAP_FUNC("strcat", strcat),
	WRAP_FUNC("strchr", strchr),
	WRAP_FUNC("strcmp", strcmp),
	WRAP_FUNC("strcoll", strcoll),
	WRAP_FUNC("strcpy", strcpy),
	WRAP_FUNC("strerror", strerror),
	WRAP_FUNC("strftime", strftime),
	WRAP_FUNC("strlen", strlen),
	WRAP_FUNC("strncasecmp", strncasecmp),
	WRAP_FUNC("strncat", strncat),
	WRAP_FUNC("strncpy", strncpy),
	WRAP_FUNC("strncmp", strncmp),
	WRAP_FUNC("strpbrk", strpbrk),
	WRAP_FUNC("strrchr", strrchr),
	WRAP_FUNC("strstr", strstr),
	WRAP_FUNC("strtod", strtod),
	WRAP_FUNC("strtof", strtof),
	WRAP_FUNC("strtok", strtok),
	WRAP_FUNC("strtol", strtol),
	WRAP_FUNC("strtold", strtold),
	WRAP_FUNC("strtoul", strtoul),
	WRAP_FUNC("tanf", tanf),
	WRAP_FUNC("tolower", tolower),
	WRAP_FUNC("toupper", toupper),
	WRAP_FUNC("towlower", towlower),
	WRAP_FUNC("towupper", towupper),
	WRAP_FUNC("ungetc", ungetc),
	WRAP_FUNC("ungetwc", ungetwc),
	WRAP_FUNC("usleep", usleep),
	WRAP_FUNC("vsnprintf", __aarch64_vsnprintf),
	WRAP_FUNC("vsprintf", __aarch64_vsprintf),
	WRAP_FUNC("wcrtomb", wcrtomb),
	WRAP_FUNC("wcscoll", wcscoll),
	WRAP_FUNC("wcslen", wcslen),
	WRAP_FUNC("wctob", wctob),
	WRAP_FUNC("wctype", wctype),
	WRAP_FUNC("wmemchr", wmemchr),
	WRAP_FUNC("wmemcmp", wmemcmp),
	WRAP_FUNC("wmemcpy", wmemcpy),
	WRAP_FUNC("wmemmove", wmemmove),
	WRAP_FUNC("wmemset", wmemset),
};
size_t dynarec_imports_num = sizeof(dynarec_imports) / sizeof(*dynarec_imports);

/*
 * All the game specific code that needs to be executed right after executable is loaded in mem must be put here
 */
int exec_booting_sequence(void *dynarec_base_addr) {
	int numRASFiles = *(int *)((uintptr_t)dynarec_base_addr + so_find_addr_rx("numRASFiles"));
	if (numRASFiles != 6) {
		debugLog("numRASFiles is not 6, is %d (addr %llx)!\n", numRASFiles, so_find_addr_rx("numRASFiles"));
		abort();
	}
	
	glClearColor(0, 1, 0, 0);
	
	uintptr_t initGraphics = (uintptr_t)so_find_addr_rx("_Z12initGraphicsv"); // void -> uint64_t
	uintptr_t ShowJoystick = (uintptr_t)so_find_addr_rx("_Z12ShowJoystickb"); // int -> uint64_t
	uintptr_t NVEventAppMain = (uintptr_t)so_find_addr_rx("_Z14NVEventAppMainiPPc"); // int, char *[] -> int
	
	debugLog("----------------------\n");
	debugLog("Max Payne Loader:\n");
	debugLog("----------------------\n");
	
	if (!initGraphics || !ShowJoystick || !NVEventAppMain) {
		debugLog("Failed to find required symbols\n");
		return -1;
	}
	
	debugLog("initGraphics: 0x%llx\n", (uint64_t)initGraphics);
	debugLog("ShowJoystick: 0x%llx\n", (uint64_t)ShowJoystick);
	debugLog("NVEventAppMain: 0x%llx\n", (uint64_t)NVEventAppMain);
	
	debugLog("Executing initGraphics...\n");
	so_run_fiber(so_dynarec, (uintptr_t)dynarec_base_addr + initGraphics);
	
	debugLog("Executing ShowJoystick...\n");
#ifdef USE_INTERPRETER
	uint64_t zero = 0;
	uc_reg_write(uc, UC_ARM64_REG_X0, &zero);
#else
	so_dynarec->SetRegister(0, 0); // Set first arg of ShowJoystick function call to 0
#endif
	so_run_fiber(so_dynarec, (uintptr_t)dynarec_base_addr + ShowJoystick);
	
	debugLog("Executing NVEventAppMain...\n");
#ifdef USE_INTERPRETER
	uc_reg_write(uc, UC_ARM64_REG_X0, &zero);
	uc_reg_write(uc, UC_ARM64_REG_X1, &zero);
#else
	so_dynarec->SetRegister(0, 0);
	so_dynarec->SetRegister(1, 0);
#endif
	so_run_fiber(so_dynarec, (uintptr_t)dynarec_base_addr + NVEventAppMain);
	
	return 0;
}

uint8_t NVEventEGLInit(void) {
	debugLog("Initing GL context\n");
	return 1;
}

void NVEventEGLSwapBuffers(void) {
	debugLog("Swapping backbuffer\n");
	glfwSwapBuffers(glfw_window);
}

void NVEventEGLMakeCurrent(void) {
}

void NVEventEGLUnmakeCurrent(void) {
}

int ProcessEvents(void) {
	int ret = glfwWindowShouldClose(glfw_window) ? 1 : 0;
	if (!ret) {
		glfwPollEvents();
	}
	return ret;
}

int AND_DeviceType(void) {
	// 0x1: phone
	// 0x2: tegra
	// Low memory is < 256
	return (512 << 6) | (3 << 2) | 0x2;
}

int AND_DeviceLocale(void) {
	return 0; // Defaulting to English for now
}

static int *deviceChip;
static int *deviceForm;
static int *definedDevice;
int AND_SystemInitialize(void) {
	*deviceForm = 1;
	*deviceChip = 14;
	*definedDevice = 27;
	return 0;
}

char *OS_FileGetArchiveName(unsigned int mode) {
	if (mode == 1) { // main.obb
		return strdup("main.obb");
	}
	return NULL;
}

// Game doesn't properly stop/delete buffers, so we fix it with some hooks
static ALuint last_stopped_src = 0;
void alSourceStop_hook(ALuint src) {
	last_stopped_src = src;
	alSourceStop(src);
}
void alDeleteBuffers_hook(ALsizei n, ALuint *bufs) {
	if (last_stopped_src) {
		ALint type = 0;
		alGetSourcei(last_stopped_src, AL_SOURCE_TYPE, &type);
		if (type == AL_STREAMING)
			alSourceUnqueueBuffers(last_stopped_src, n, bufs);
		else
			alSourcei(last_stopped_src, AL_BUFFER, 0);
		last_stopped_src = 0;
	}
	alDeleteBuffers(n, bufs);
}

ALCcontext *alcCreateContext_hook(ALCdevice *dev, const ALCint *unused) {
	const ALCint attr[] = { ALC_FREQUENCY, 44100, 0 };
	return alcCreateContext(dev, attr);
}

int NVThreadGetCurrentJNIEnv() {
#ifdef USE_INTERPRETER
	uintptr_t addr_next;
	uc_reg_read(uc, REG_FP, &addr_next);
#else
	uintptr_t addr_next = so_dynarec->GetRegister(REG_FP);
#endif
	debugLog("GetCurrentJNIENv called from %x\n", (uintptr_t)addr_next - (uintptr_t)dynarec_base_addr);

	return 0x1337;
}

int WarGamepad_GetGamepadType(int padnum) {
	// Fake to a regular controller
	return 0;
}

int WarGamepad_GetGamepadButtons(int padnum) {
	int mask = 0;
	int count;
	const unsigned char *buttons = glfwGetJoystickButtons(GLFW_JOYSTICK_1, &count);
	if (!buttons) {
		return mask;
	}

	if (buttons[GLFW_GAMEPAD_BUTTON_A] == GLFW_PRESS)
		mask |= 0x1;
	if (buttons[GLFW_GAMEPAD_BUTTON_B] == GLFW_PRESS)
		mask |= 0x2;
	if (buttons[GLFW_GAMEPAD_BUTTON_X] == GLFW_PRESS)
		mask |= 0x4;
	if (buttons[GLFW_GAMEPAD_BUTTON_Y] == GLFW_PRESS)
		mask |= 0x8;
	if (buttons[GLFW_GAMEPAD_BUTTON_START] == GLFW_PRESS)
		mask |= 0x10;
	if (buttons[GLFW_GAMEPAD_BUTTON_GUIDE] == GLFW_PRESS)
		mask |= 0x20;
	if (buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER] == GLFW_PRESS)
		mask |= 0x40;
	if (buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER] == GLFW_PRESS)
		mask |= 0x80;
	if (buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP] == GLFW_PRESS)
		mask |= 0x100;
	if (buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN] == GLFW_PRESS)
		mask |= 0x200;
	if (buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT] == GLFW_PRESS)
		mask |= 0x400;
	if (buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] == GLFW_PRESS)
		mask |= 0x800;
	if (buttons[GLFW_GAMEPAD_BUTTON_LEFT_THUMB] == GLFW_PRESS)
		mask |= 0x1000;
	if (buttons[GLFW_GAMEPAD_BUTTON_RIGHT_THUMB] == GLFW_PRESS)
		mask |= 0x2000;

	return mask;
}

float WarGamepad_GetGamepadAxis(int padnum, int axis) {
	int count;
	const float *axes = glfwGetJoystickAxes(GLFW_JOYSTICK_1, &count); 	

	if (fabsf(axes[axis]) > 0.2f)
		return axes[axis];

	return 0.0f;
}

int GetAndroidCurrentLanguage(void) {
	debugLog("GetAndroidCurrentLanguage returning English\n");
	return 0; // English
}

void SetAndroidCurrentLanguage(int lang) {
}

int ReadDataFromPrivateStorage(const char *file, void **data, int *size) {
	debugLog("ReadDataFromPrivateStorage %s\n", file);

	FILE *f = fopen(file, "rb");
	if (!f)
		return 0;

	fseek(f, 0, SEEK_END);
	size_t sz = ftell(f);
	fseek(f, 0, SEEK_SET);

	int ret = 0;

	if (sz > 0) {
		void *buf = malloc(sz);
		if (buf && fread(buf, sz, 1, f)) {
			ret = 1;
			*size = sz;
			*data = buf;
		} else {
			free(buf);
		}
	}

	fclose(f);

	return ret;
}

int WriteDataToPrivateStorage(const char *file, const void *data, int size) {
	debugLog("WriteDataToPrivateStorage %s\n", file);

	FILE *f = fopen(file, "wb");
	if (!f)
		return 0;

	const int ret = fwrite(data, size, 1, f);
	fclose(f);

	return ret;
}

typedef struct  {
	uint8_t *buffer;
	uint32_t offset;
} tga_write_struct;
tga_write_struct tga_ctx;

void tga_write_func(void *context, void *data, int size) {
	tga_write_struct *ctx = (tga_write_struct *)context;
	memcpy(&ctx->buffer[ctx->offset], data, size);
	ctx->offset += size;
}

uint8_t decomp_buffer[1024 * 1024 * 8];
uintptr_t loadTGA;
void loadJPG(void *_this, uint8_t *buf, int size) {
	// Max Payne doesn't support RLE'd TGA files
	stbi_write_tga_with_rle = 0;

	//debugLog("loadJPG called with buf: %llx and size: %d\n", buf, size);
	//FILE *f = fopen("dump.jpg", "wb");
	//fwrite(buf, 1, size, f);
	//fclose(f);
	int w, h;
	uint8_t *data = stbi_load_from_memory(buf, size, &w, &h, NULL, 4);
	if (!data) {
		debugLog("Failed to load jpg file\n");
		abort();
		return;
	}
	tga_ctx.buffer = decomp_buffer;
	tga_ctx.offset = 0;
	stbi_write_tga_to_func(tga_write_func, &tga_ctx, w, h, 4, data);
	free(data);
	
	//f = fopen("dump.tga", "wb");
	//fwrite(decomp_buffer, 1, tga_ctx.offset, f);
	//fclose(f);
	
	//debugLog("Running loadTGA\n");
#ifdef USE_INTERPRETER
	uintptr_t ptr = (uintptr_t)_this;
	uc_reg_write(uc, UC_ARM64_REG_X0, &ptr);
	ptr = (uintptr_t)&decomp_buffer[0];
	uc_reg_write(uc, UC_ARM64_REG_X1, &ptr);
	ptr = (uintptr_t)&tga_ctx.offset;
	uc_reg_write(uc, UC_ARM64_REG_X2, &ptr);
#else
	so_dynarec->SetRegister(0, (uintptr_t)_this);
	so_dynarec->SetRegister(1, (uintptr_t)decomp_buffer);
	so_dynarec->SetRegister(2, tga_ctx.offset);
#endif
	so_run_fiber(so_dynarec, loadTGA);
	
	//debugLog("Returning from loadJPG\n");
}

int OS_ScreenGetHeight(void) {
	return WINDOW_HEIGHT;
}

int OS_ScreenGetWidth(void) {
	return WINDOW_WIDTH;
}

int exec_patch_hooks(void *dynarec_base_addr) {
	mkdir("./savegames");

	strcpy((char *)((uintptr_t)dynarec_base_addr + so_find_addr_rx("StorageRootBuffer")), ".");
	*(int *)((uintptr_t)dynarec_base_addr + so_find_addr_rx("IsAndroidPaused")) = 0;
	*(uint8_t *)((uintptr_t)dynarec_base_addr + so_find_addr_rx("UseRGBA8")) = 1; // Game defaults to RGB565 which is lower quality
	
	// Filling qsort native functions database
	qsort_db.insert({(uintptr_t)((uintptr_t)dynarec_base_addr + so_find_addr_rx("_ZN7ZIPFile12EntryCompareEPKvS1_")), ZIPFile_EntryCompare});
	qsort_db.insert({(uintptr_t)((uintptr_t)dynarec_base_addr + so_find_addr_rx("_Z15RASFileNameCompPKvS0_")), RASFileNameComp});
	qsort_db.insert({(uintptr_t)((uintptr_t)dynarec_base_addr + so_find_addr_rx("_ZNK6P_Text11getPositionEv") + 4), FontCmp});
	qsort_db.insert({(uintptr_t)((uintptr_t)dynarec_base_addr + so_find_addr_rx("_ZN27X_AnimationMessageContainer8destructEv") - 16), AnimationMessageContainerCmp});
	qsort_db.insert({(uintptr_t)((uintptr_t)dynarec_base_addr + so_find_addr_rx("_Z19priorityCompHiToLowPKvS0_")), PriorityCompHiToLow});
	qsort_db.insert({(uintptr_t)((uintptr_t)dynarec_base_addr + so_find_addr_rx("_Z4cmplPKvS0_")), cmpl});
	qsort_db.insert({(uintptr_t)((uintptr_t)dynarec_base_addr + so_find_addr_rx("_Z4cmphPKvS0_")), cmph});
	
	// Filling bsearch native functions database
	bsearch_db.insert({(uintptr_t)((uintptr_t)dynarec_base_addr + so_find_addr_rx("_Z15RASFileNameCompPKvS0_")), RASFileNameComp});
	
	// Vars used in AND_SystemInitialize
	deviceChip = (int *)((uintptr_t)dynarec_base_addr + so_find_addr_rx("deviceChip"));
	deviceForm = (int *)((uintptr_t)dynarec_base_addr + so_find_addr_rx("deviceForm"));
	definedDevice = (int *)((uintptr_t)dynarec_base_addr + so_find_addr_rx("definedDevice"));
	
#ifndef USE_INTERPRETER
	// FIXME: There is some issue with libjpeg that causes random mem corruptions. For now we do JPEG->TGA conversion natively and load images as TGA
	HOOK_FUNC("_ZN7G_Image7loadJPGEPci", loadJPG);
	loadTGA = (uintptr_t)dynarec_base_addr + so_find_addr_rx("_ZN7G_Image7loadTGAEPc");
#endif

	// This hook exists just as a guard to know if we're reaching some code we should patch instead
	HOOK_FUNC("_Z24NVThreadGetCurrentJNIEnvv", NVThreadGetCurrentJNIEnv);
	
	// Hooking these two functions since they rely on JNI which we avoid reimplementing
	HOOK_FUNC("_Z26ReadDataFromPrivateStoragePKcRPcRi", ReadDataFromPrivateStorage);
	HOOK_FUNC("_Z25WriteDataToPrivateStoragePKcS0_i", WriteDataToPrivateStorage);
	
	// Language override
	HOOK_FUNC("_Z25GetAndroidCurrentLanguagev", GetAndroidCurrentLanguage);
	HOOK_FUNC("_Z25SetAndroidCurrentLanguagei", SetAndroidCurrentLanguage);
	
	// Redirect gamepad code to our own implementation
	HOOK_FUNC("_Z25WarGamepad_GetGamepadTypei", WarGamepad_GetGamepadType);
	HOOK_FUNC("_Z28WarGamepad_GetGamepadButtonsi", WarGamepad_GetGamepadButtons);
	HOOK_FUNC("_Z25WarGamepad_GetGamepadAxisii", WarGamepad_GetGamepadAxis);

	HOOK_FUNC("__cxa_guard_acquire", __cxa_guard_acquire);
	HOOK_FUNC("__cxa_guard_release", __cxa_guard_release);
	HOOK_FUNC("__cxa_throw", __cxa_throw);
	
	// Disable movies playback for now
	HOOK_FUNC("_Z12OS_MoviePlayPKcbbf", ret0);
	HOOK_FUNC("_Z13AND_StopMoviev", ret0);
	HOOK_FUNC("_Z20AND_MovieIsSkippableb", ret0);
	HOOK_FUNC("_Z18AND_MovieTextScalei", ret0);
	HOOK_FUNC("_Z18AND_IsMoviePlayingv", ret0);
	HOOK_FUNC("_Z20OS_MoviePlayinWindowPKciiiibbf", ret0);
	
	// We don't use the original apk but extracted files
	HOOK_FUNC("_Z9NvAPKOpenPKc", ret0);
	
	// Disabling some checks we don't need
	HOOK_FUNC("_Z20OS_ServiceAppCommandPKcS0_", ret0);
	HOOK_FUNC("_Z23OS_ServiceAppCommandIntPKci", ret0);
	HOOK_FUNC("_Z25OS_ServiceIsWifiAvailablev", ret0);
	HOOK_FUNC("_Z28OS_ServiceIsNetworkAvailablev", ret0);
	HOOK_FUNC("_Z12AND_OpenLinkPKc", ret0);
	
	// Inject OpenGL context
	HOOK_FUNC("_Z14NVEventEGLInitv", NVEventEGLInit);
	HOOK_FUNC("_Z21NVEventEGLMakeCurrentv", NVEventEGLMakeCurrent);
	HOOK_FUNC("_Z23NVEventEGLUnmakeCurrentv", NVEventEGLUnmakeCurrent);
	HOOK_FUNC("_Z21NVEventEGLSwapBuffersv", NVEventEGLSwapBuffers);
	
	// Override screen size
	HOOK_FUNC("_Z17OS_ScreenGetWidthv", OS_ScreenGetWidth);
	HOOK_FUNC("_Z18OS_ScreenGetHeightv", OS_ScreenGetHeight);
	
	// Disable vibration
	HOOK_FUNC("_Z12VibratePhonei", ret0);
	HOOK_FUNC("_Z14Mobile_Vibratei", ret0);
	
	HOOK_FUNC("_Z14AND_DeviceTypev", AND_DeviceType);
	HOOK_FUNC("_Z16AND_DeviceLocalev", AND_DeviceLocale);
	HOOK_FUNC("_Z20AND_SystemInitializev", AND_SystemInitialize);
	HOOK_FUNC("_Z21AND_ScreenSetWakeLockb", ret0);
	HOOK_FUNC("_Z22AND_FileGetArchiveName13OSFileArchive", OS_FileGetArchiveName);

	// Redirect OpenAL to native version
	HOOK_FUNC("InitializeCriticalSection", ret0);
	HOOK_FUNC("alAuxiliaryEffectSlotf", alAuxiliaryEffectSlotf);
	HOOK_FUNC("alAuxiliaryEffectSlotfv", alAuxiliaryEffectSlotfv);
	HOOK_FUNC("alAuxiliaryEffectSloti", alAuxiliaryEffectSloti);
	HOOK_FUNC("alAuxiliaryEffectSlotiv", alAuxiliaryEffectSlotiv);
	HOOK_FUNC("alBuffer3f", alBuffer3f);
	HOOK_FUNC("alBuffer3i", alBuffer3i);
	HOOK_FUNC("alBufferData", alBufferData);
	HOOK_FUNC("alBufferf", alBufferf);
	HOOK_FUNC("alBufferfv", alBufferfv);
	HOOK_FUNC("alBufferi", alBufferi);
	HOOK_FUNC("alBufferiv", alBufferiv);
	HOOK_FUNC("alDeleteAuxiliaryEffectSlots", alDeleteAuxiliaryEffectSlots);
	HOOK_FUNC("alDeleteBuffers", alDeleteBuffers_hook);
	HOOK_FUNC("alDeleteEffects", alDeleteEffects);
	HOOK_FUNC("alDeleteFilters", alDeleteFilters);
	HOOK_FUNC("alDeleteSources", alDeleteSources);
	HOOK_FUNC("alDisable", alDisable);
	HOOK_FUNC("alDistanceModel", alDistanceModel);
	HOOK_FUNC("alDopplerFactor", alDopplerFactor);
	HOOK_FUNC("alDopplerVelocity", alDopplerVelocity);
	HOOK_FUNC("alEffectf", alEffectf);
	HOOK_FUNC("alEffectfv", alEffectfv);
	HOOK_FUNC("alEffecti", alEffecti);
	HOOK_FUNC("alEffectiv", alEffectiv);
	HOOK_FUNC("alEnable", alEnable);
	HOOK_FUNC("alFilterf", alFilterf);
	HOOK_FUNC("alFilterfv", alFilterfv);
	HOOK_FUNC("alFilteri", alFilteri);
	HOOK_FUNC("alFilteriv", alFilteriv);
	HOOK_FUNC("alGenAuxiliaryEffectSlots", alGenAuxiliaryEffectSlots);
	HOOK_FUNC("alGenBuffers", alGenBuffers);
	HOOK_FUNC("alGenEffects", alGenEffects);
	HOOK_FUNC("alGenFilters", alGenFilters);
	HOOK_FUNC("alGenSources", alGenSources);
	HOOK_FUNC("alGetAuxiliaryEffectSlotf", alGetAuxiliaryEffectSlotf);
	HOOK_FUNC("alGetAuxiliaryEffectSlotfv", alGetAuxiliaryEffectSlotfv);
	HOOK_FUNC("alGetAuxiliaryEffectSloti", alGetAuxiliaryEffectSloti);
	HOOK_FUNC("alGetAuxiliaryEffectSlotiv", alGetAuxiliaryEffectSlotiv);
	HOOK_FUNC("alGetBoolean", alGetBoolean);
	HOOK_FUNC("alGetBooleanv", alGetBooleanv);
	HOOK_FUNC("alGetBuffer3f", alGetBuffer3f);
	HOOK_FUNC("alGetBuffer3i", alGetBuffer3i);
	HOOK_FUNC("alGetBufferf", alGetBufferf);
	HOOK_FUNC("alGetBufferfv", alGetBufferfv);
	HOOK_FUNC("alGetBufferi", alGetBufferi);
	HOOK_FUNC("alGetBufferiv", alGetBufferiv);
	HOOK_FUNC("alGetDouble", alGetDouble);
	HOOK_FUNC("alGetDoublev", alGetDoublev);
	HOOK_FUNC("alGetEffectf", alGetEffectf);
	HOOK_FUNC("alGetEffectfv", alGetEffectfv);
	HOOK_FUNC("alGetEffecti", alGetEffecti);
	HOOK_FUNC("alGetEffectiv", alGetEffectiv);
	HOOK_FUNC("alGetEnumValue", alGetEnumValue);
	HOOK_FUNC("alGetError", alGetError);
	HOOK_FUNC("alGetFilterf", alGetFilterf);
	HOOK_FUNC("alGetFilterfv", alGetFilterfv);
	HOOK_FUNC("alGetFilteri", alGetFilteri);
	HOOK_FUNC("alGetFilteriv", alGetFilteriv);
	HOOK_FUNC("alGetFloat", alGetFloat);
	HOOK_FUNC("alGetFloatv", alGetFloatv);
	HOOK_FUNC("alGetInteger", alGetInteger);
	HOOK_FUNC("alGetIntegerv", alGetIntegerv);
	HOOK_FUNC("alGetListener3f", alGetListener3f);
	HOOK_FUNC("alGetListener3i", alGetListener3i);
	HOOK_FUNC("alGetListenerf", alGetListenerf);
	HOOK_FUNC("alGetListenerfv", alGetListenerfv);
	HOOK_FUNC("alGetListeneri", alGetListeneri);
	HOOK_FUNC("alGetListeneriv", alGetListeneriv);
	HOOK_FUNC("alGetProcAddress", alGetProcAddress);
	HOOK_FUNC("alGetSource3f", alGetSource3f);
	HOOK_FUNC("alGetSource3i", alGetSource3i);
	HOOK_FUNC("alGetSourcef", alGetSourcef);
	HOOK_FUNC("alGetSourcefv", alGetSourcefv);
	HOOK_FUNC("alGetSourcei", alGetSourcei);
	HOOK_FUNC("alGetSourceiv", alGetSourceiv);
	HOOK_FUNC("alGetString", alGetString);
	HOOK_FUNC("alIsAuxiliaryEffectSlot", alIsAuxiliaryEffectSlot);
	HOOK_FUNC("alIsBuffer", alIsBuffer);
	HOOK_FUNC("alIsEffect", alIsEffect);
	HOOK_FUNC("alIsEnabled", alIsEnabled);
	HOOK_FUNC("alIsExtensionPresent", alIsExtensionPresent);
	HOOK_FUNC("alIsFilter", alIsFilter);
	HOOK_FUNC("alIsSource", alIsSource);
	HOOK_FUNC("alListener3f", alListener3f);
	HOOK_FUNC("alListener3i", alListener3i);
	HOOK_FUNC("alListenerf", alListenerf);
	HOOK_FUNC("alListenerfv", alListenerfv);
	HOOK_FUNC("alListeneri", alListeneri);
	HOOK_FUNC("alListeneriv", alListeneriv);
	HOOK_FUNC("alSource3f", alSource3f);
	HOOK_FUNC("alSource3i", alSource3i);
	HOOK_FUNC("alSourcePause", alSourcePause);
	HOOK_FUNC("alSourcePausev", alSourcePausev);
	HOOK_FUNC("alSourcePlay", alSourcePlay);
	HOOK_FUNC("alSourcePlayv", alSourcePlayv);
	HOOK_FUNC("alSourceQueueBuffers", alSourceQueueBuffers);
	HOOK_FUNC("alSourceRewind", alSourceRewind);
	HOOK_FUNC("alSourceRewindv", alSourceRewindv);
	HOOK_FUNC("alSourceStop", alSourceStop_hook);
	HOOK_FUNC("alSourceStopv", alSourceStopv);
	HOOK_FUNC("alSourceUnqueueBuffers", alSourceUnqueueBuffers);
	HOOK_FUNC("alSourcef", alSourcef);
	HOOK_FUNC("alSourcefv", alSourcefv);
	HOOK_FUNC("alSourcei", alSourcei);
	HOOK_FUNC("alSourceiv", alSourceiv);
	HOOK_FUNC("alSpeedOfSound", alSpeedOfSound);
	HOOK_FUNC("alcCaptureCloseDevice", alcCaptureCloseDevice);
	HOOK_FUNC("alcCaptureOpenDevice", alcCaptureOpenDevice);
	HOOK_FUNC("alcCaptureSamples", alcCaptureSamples);
	HOOK_FUNC("alcCaptureStart", alcCaptureStart);
	HOOK_FUNC("alcCaptureStop", alcCaptureStop);
	HOOK_FUNC("alcCloseDevice", alcCloseDevice);
	HOOK_FUNC("alcCreateContext", alcCreateContext_hook);
	HOOK_FUNC("alcDestroyContext", alcDestroyContext);
	HOOK_FUNC("alcGetContextsDevice", alcGetContextsDevice);
	HOOK_FUNC("alcGetCurrentContext", alcGetCurrentContext);
	HOOK_FUNC("alcGetEnumValue", alcGetEnumValue);
	HOOK_FUNC("alcGetError", alcGetError);
	HOOK_FUNC("alcGetIntegerv", alcGetIntegerv);
	HOOK_FUNC("alcGetProcAddress", alcGetProcAddress);
	HOOK_FUNC("alcGetString", alcGetString);
	HOOK_FUNC("alcGetThreadContext", alcGetThreadContext);
	HOOK_FUNC("alcIsExtensionPresent", alcIsExtensionPresent);
	HOOK_FUNC("alcMakeContextCurrent", alcMakeContextCurrent);
	HOOK_FUNC("alcOpenDevice", alcOpenDevice);
	HOOK_FUNC("alcProcessContext", alcProcessContext);
	HOOK_FUNC("alcSetThreadContext", alcSetThreadContext);
	HOOK_FUNC("alcSuspendContext", alcSuspendContext);

	// Events processing
	HOOK_FUNC("_Z13ProcessEventsb", ProcessEvents);

	return 0;
}

int exec_main_loop(void *dynarec_base_addr) {
	if (!glfwWindowShouldClose(glfw_window)) {

		// render process
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // clear the depth buffer and the color buffer

		// check call events
		glfwSwapBuffers(glfw_window);
		glfwPollEvents();
		
		return 0;
	}
	
	return -1;
}
