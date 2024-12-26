#define __USE_GNU
#include "glad/glad.h"
#include "dyn_util.h"
#include <GLFW/glfw3.h>

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

FILE *fopen_fake(char *fname, char *mode) {
	printf("fopen(%s, %s)\n", fname, mode);
	return fopen(fname, mode);
}

// qsort uses AARCH64 functions, so we map them to native variants
int ZIPFile_EntryCompare(const void *key, const void *element) {
	return strcasecmp(*((const char **)key + 1), *((const char **)element + 1));
}
std::unordered_map<uintptr_t, int (*)(const void *, const void *)> qsort_db;
void qsort_fake(void *base, size_t num, size_t width, int(*compare)(const void *key, const void *element)) {
	auto native_f = qsort_db.find((uintptr_t)compare);
	if (native_f == qsort_db.end()) {
		printf("Fatal error: Invalid qsort function: %llx\n", (uintptr_t)compare - (uintptr_t)dynarec_base_addr);
		abort();
	}
	return qsort(base, num, width, native_f->second);
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
	WRAP_FUNC("fclose", fclose),
	WRAP_FUNC("fopen", fopen_fake),
	WRAP_FUNC("fread", fread),
	WRAP_FUNC("free", free),
	WRAP_FUNC("fseek", fseek),
	WRAP_FUNC("ftell", ftell),
	WRAP_FUNC("fwrite", fwrite),
	WRAP_FUNC("getenv", ret0),
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
	WRAP_FUNC("qsort", qsort_fake),
	WRAP_FUNC("snprintf", __aarch64_snprintf),
	WRAP_FUNC("sprintf", __aarch64_sprintf),
	WRAP_FUNC("sqrtf", sqrtf),
	WRAP_FUNC("srand", srand),
	WRAP_FUNC("strcasecmp", strcasecmp),
	WRAP_FUNC("strchr", strchr),
	WRAP_FUNC("strcmp", strcmp),
	WRAP_FUNC("strcpy", strcpy),
	WRAP_FUNC("strlen", strlen),
	WRAP_FUNC("strncpy", strncpy),
	WRAP_FUNC("strncmp", strncmp),
	WRAP_FUNC("strstr", strstr),
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
	
	printf("Executing initGraphics...\n");
	so_run_fiber(so_dynarec, (uintptr_t)dynarec_base_addr + initGraphics);
	
	printf("Executing ShowJoystick...\n");
	so_dynarec->SetRegister(0, 0); // Set first arg of ShowJoystick function call to 0
	so_run_fiber(so_dynarec, (uintptr_t)dynarec_base_addr + ShowJoystick);
	
	printf("Executing NVEventAppMain...\n");
	so_dynarec->SetRegister(0, 0);
	so_dynarec->SetRegister(1, 0);
	so_run_fiber(so_dynarec, (uintptr_t)dynarec_base_addr + NVEventAppMain);
	
	return 0;
}

uint8_t NVEventEGLInit(void) {
	printf("Initing GL context\n");
	return 1;
}

void NVEventEGLSwapBuffers(void) {
	printf("Swapping backbuffer\n");
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
	uintptr_t addr_next = so_dynarec->GetRegister(REG_FP);
	printf("GetCurrentJNIENv called from %x\n", (uintptr_t)addr_next - (uintptr_t)dynarec_base_addr);

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
	printf("GetAndroidCurrentLanguage returning English\n");
	return 0; // English
}

void SetAndroidCurrentLanguage(int lang) {
}

int exec_patch_hooks(void *dynarec_base_addr) {
	strcpy((char *)(dynarec_base_addr + so_find_addr_rx("StorageRootBuffer")), "./gamefiles");
	*(int *)(dynarec_base_addr + so_find_addr_rx("IsAndroidPaused")) = 0;
	*(uint8_t *)(dynarec_base_addr + so_find_addr_rx("UseRGBA8")) = 1; // Game defaults to RGB565 which is lower quality
	
	// Filling qsort native functions database
	qsort_db.insert({(uintptr_t)(dynarec_base_addr + so_find_addr_rx("_ZN7ZIPFile12EntryCompareEPKvS1_")), ZIPFile_EntryCompare});
	
	// Vars used in AND_SystemInitialize
	deviceChip = (int *)(dynarec_base_addr + so_find_addr_rx("deviceChip"));
	deviceForm = (int *)(dynarec_base_addr + so_find_addr_rx("deviceForm"));
	definedDevice = (int *)(dynarec_base_addr + so_find_addr_rx("definedDevice"));
	
	HOOK_FUNC("_Z24NVThreadGetCurrentJNIEnvv", NVThreadGetCurrentJNIEnv);
	
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
	*(int64_t *)(dynarec_base_addr + so_find_addr_rx("windowSize")) = ((int64_t)WINDOW_HEIGHT << 32) | (int64_t)WINDOW_WIDTH;
	
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
