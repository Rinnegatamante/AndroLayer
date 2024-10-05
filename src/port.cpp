#include "glad/glad.h"
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>

#include "dynarec.h"
#include "so_util.h"
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

/*
 * List of imports to be resolved with native variants
 */
dynarec_import dynarec_imports[] = {
	{"__android_log_print", __android_log_print},
	{"glDeleteShader", glDeleteShader}
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
	
	so_dynarec->SetPC(initGraphics);
	so_dynarec_env.ticks_left = 1;
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
