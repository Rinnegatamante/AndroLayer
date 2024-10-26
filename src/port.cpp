#define __USE_GNU
#include "glad/glad.h"
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>

#include "dynarec.h"
#include "so_util.h"
#include "thunk_gen.h"
#include "port.h"

/*
 * Custom imports implementations
 */
int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
	va_list list;
	static char string[0x1000];

	va_start(list, fmt);
	vsnprintf(string, sizeof(string), fmt, list);
	va_end(list);

	printf("[LOG] %s: %s\n", tag, string);

	return 0;
}

int __cxa_atexit_fake(void (*func) (void *), void *arg, void *dso_handle)
{
	return 0;
}

thread_local void (*_tls__init_func)(void) = nullptr;
thread_local Dynarmic::A64::Jit *_tls__init_func_jit = nullptr;

int pthread_once_fake (Dynarmic::A64::Jit *jit, pthread_once_t *__once_control, void (*__init_routine) (void))
{
	// Store the current pthread_once elements
	_tls__init_func = __init_routine;
	_tls__init_func_jit = jit;
	// call pthread_once with a custom routine to callback the jit
	return pthread_once(__once_control, []() {
		uintptr_t entry_point = (uintptr_t)_tls__init_func;
		Dynarmic::A64::Jit *jit = _tls__init_func_jit;
		so_run_fiber(jit, entry_point);
	});
}

int pthread_create_fake (Dynarmic::A64::Jit *jit, pthread_t *__restrict __newthread,
			   const pthread_attr_t *__restrict __attr,
			   void *(*__start_routine) (void *),
			   void *__restrict __arg)
{
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
	if (ret < 0)
		return ret;
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

int ret0() {
	return 0;
}

/*
 * List of imports to be resolved with native variants
 */
#define WRAP_FUNC(name, func) gen_wrapper<&func>(name)
dynarec_import dynarec_imports[] = {
	WRAP_FUNC("__cxa_atexit", __cxa_atexit_fake),
	WRAP_FUNC("AAssetManager_open", ret0),
	WRAP_FUNC("AAssetManager_fromJava", ret0),
	WRAP_FUNC("AAsset_close", ret0),
	WRAP_FUNC("AAsset_getLength", ret0),
	WRAP_FUNC("AAsset_getRemainingLength", ret0),
	WRAP_FUNC("AAsset_read", ret0),
	WRAP_FUNC("AAsset_seek", ret0),
	WRAP_FUNC("btowc", btowc),
	WRAP_FUNC("calloc", calloc),
	WRAP_FUNC("getenv", getenv),
#ifdef __MINGW64__
	WRAP_FUNC("gettimeofday", mingw_gettimeofday),
#else
	WRAP_FUNC("gettimeofday", gettimeofday),
#endif
	WRAP_FUNC("malloc", malloc),
	WRAP_FUNC("memcpy", memcpy),
	WRAP_FUNC("memset", memset),
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
	WRAP_FUNC("pthread_once", pthread_once_fake),
	WRAP_FUNC("pthread_self", pthread_self),
	WRAP_FUNC("pthread_setschedparam", ret0),
	WRAP_FUNC("pthread_setspecific", ret0),
	WRAP_FUNC("srand", srand),
	WRAP_FUNC("strcasecmp", strcasecmp),
	WRAP_FUNC("strcmp", strcmp),
	WRAP_FUNC("strcpy", strcpy),
	WRAP_FUNC("strlen", strlen),
	WRAP_FUNC("strncmp", strncmp),
	WRAP_FUNC("wctob", wctob),
	WRAP_FUNC("wctype", wctype),
};
size_t dynarec_imports_num = sizeof(dynarec_imports) / sizeof(*dynarec_imports);

/*
 * All the game specific code that needs to be executed right after executable is loaded in mem must be put here
 */
int exec_booting_sequence(void *dynarec_base_addr) {
	glClearColor(0,1,0,0);
	
	uint32_t (* initGraphics)(void) = (uint32_t (*)())so_find_addr_rx("_Z12initGraphicsv");
	uint32_t (* ShowJoystick)(int show) = (uint32_t (*)(int))so_find_addr_rx("_Z12ShowJoystickb");
	int (* NVEventAppMain)(int argc, char *argv[]) = (int (*)(int, char *[]))so_find_addr_rx("_Z14NVEventAppMainiPPc");
	
	printf("----------------------\n");
	printf("Max Payne Loader:\n");
	printf("----------------------\n");
	
	if (!initGraphics || !ShowJoystick || !NVEventAppMain) {
		printf("Failed to find required symbols\n");
		return -1;
	}
	
	printf("initGraphics: 0x%llx\n", (uint64_t)initGraphics);
	printf("ShowJoystick: 0x%llx\n", (uint64_t)ShowJoystick);
	printf("NVEventAppMain: 0x%llx\n", (uint64_t)NVEventAppMain);
	
	
	so_dynarec->SetPC((uint64_t)initGraphics);
	so_dynarec->Run();
	
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
