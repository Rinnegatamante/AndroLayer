#define __USE_GNU
#include "glad/glad.h"
#include "dyn_util.h"
#include <GLFW/glfw3.h>

#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>

#include "dynarec.h"
#include "so_util.h"
#include "thunk_gen.h"
#include "port.h"
#include "variadics.h"

/*
 * Custom imports implementations
 */
int __android_log_print(int prio, const char *tag, const char *fmt) {
	std::string s = parse_format(fmt, 3); // startReg is # of fixed function args + 1
	printf("[%s] %s\n", tag, s.c_str());
	
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
	WRAP_FUNC("__android_log_print", __android_log_print),
	WRAP_FUNC("__google_potentially_blocking_region_begin", ret0),
	WRAP_FUNC("__google_potentially_blocking_region_end", ret0),
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
	WRAP_FUNC("free", free),
#ifdef __MINGW64__
	WRAP_FUNC("gettimeofday", mingw_gettimeofday),
#else
	WRAP_FUNC("gettimeofday", gettimeofday),
#endif
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
	WRAP_FUNC("malloc", malloc),
	WRAP_FUNC("memcpy", memcpy),
	WRAP_FUNC("memcmp", memcmp),
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
	WRAP_FUNC("strncpy", strncpy),
	WRAP_FUNC("strlen", strlen),
	WRAP_FUNC("strncmp", strncmp),
	WRAP_FUNC("tolower", tolower),
	WRAP_FUNC("vsnprintf", __aarch64_vsnprintf),
	WRAP_FUNC("wctob", wctob),
	WRAP_FUNC("wctype", wctype),
};
size_t dynarec_imports_num = sizeof(dynarec_imports) / sizeof(*dynarec_imports);

/*
 * All the game specific code that needs to be executed right after executable is loaded in mem must be put here
 */
int exec_booting_sequence(void *dynarec_base_addr) {
	glClearColor(0, 1, 0, 0);
	
	uintptr_t initGraphics = (uintptr_t)so_find_addr_rx("_Z12initGraphicsv"); // void -> uint64_t
	uintptr_t ShowJoystick = (uintptr_t)so_find_addr_rx("_Z12ShowJoystickb"); // int -> uint64_t
	uintptr_t NVEventAppMain = (uintptr_t)so_find_addr_rx("_Z14NVEventAppMainiPPc"); // int, char *[] -> int
	
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

	so_run_fiber(so_dynarec, (uintptr_t)dynarec_base_addr + initGraphics);
	
	return 0;
}

int exec_patch_hooks(void *dynarec_base_addr) {
	HOOK_FUNC("__cxa_guard_acquire", __cxa_guard_acquire);
	HOOK_FUNC("__cxa_guard_release", __cxa_guard_release);
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
