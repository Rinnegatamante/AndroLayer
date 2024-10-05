#include "glad/glad.h"
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "dynarec.h"
#include "so_util.h"
#include "port.h"

/*
 * List of imports to be resolved with native variants
 */
DynLibFunction dynlib_functions[] = {	
};
size_t dynlib_numfunctions = sizeof(dynlib_functions) / sizeof(*dynlib_functions);

/*
 * All the game specific code that needs to be executed right after executable is loaded in mem must be put here
 */
int exec_booting_sequence(void *dynarec_base_addr) {
	uint32_t (* initGraphics)(void) = (uint32_t (*)())so_find_addr_rx("_Z12initGraphicsv");
	uint32_t (* ShowJoystick)(int show) = (uint32_t (*)(int))so_find_addr_rx("_Z12ShowJoystickb");
    int (* NVEventAppMain)(int argc, char *argv[]) = (int (*)(int, char *[]))so_find_addr_rx("_Z14NVEventAppMainiPPc");

	printf("Max Payne Loader:\n");
	printf("initGraphics: %llx\n", initGraphics);
	printf("ShowJoystick: %llx\n", ShowJoystick);
	printf("NVEventAppMain: %llx\n", NVEventAppMain);
	if (!initGraphics || !ShowJoystick || !NVEventAppMain) {
		printf("Failed to find required symbols\n");
		return -1;
	}

	so_dynarec->SetSP(0xffff0000);
	so_dynarec->SetPC((uint64_t)initGraphics - (uint64_t)dynarec_base_addr);
	so_dynarec->Run();
	
	return 0;
}
