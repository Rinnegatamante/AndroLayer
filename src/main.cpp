#include "glad/glad.h"
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "dynarec.h"
#include "so_util.h"
#include "port.h"

GLFWwindow *glfw_window = nullptr;
void *dynarec_base_addr = nullptr;

void framebuffer_size_callback(GLFWwindow *window, int width, int height) {
	glViewport(0, 0, width, height);
} 

bool initOpenGL(int major_ver, int minor_ver) {
	// Initialize OpenGL
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major_ver);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor_ver);

	// Create a window
	glfw_window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "AndroLayer", NULL, NULL);
	if (glfw_window == NULL) {
		printf("Failed to create glfw3 window\n");
		glfwTerminate();
		return false;
	}
	glfwMakeContextCurrent(glfw_window);

	// Load GL functions via glfw3
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		printf("Failed to initialize glad\n");
		return false;
	}	

	// Adjust viewport size to window size
	glfwSetFramebufferSizeCallback(glfw_window, framebuffer_size_callback);
	
	return true;
}

/*
int main(int argc, char** argv) {
	// Set up our memblock for loading the elf onto
	SoEnv env;
	env.memory = (u8 *)malloc(MEMBLK_SIZE);
	env.mem_size = MEMBLK_SIZE;
	
	Dynarmic::A32::UserConfig user_config;
	user_config.callbacks = &env;
	Dynarmic::A32::Jit cpu{user_config};

	// Execute at least 1 instruction.
	// (Note: More than one instruction may be executed.)
	env.ticks_left = 1;

	// Write some code to memory.
	env.MemoryWrite16(0, 0x0088); // lsls r0, r1, #2
	env.MemoryWrite16(2, 0xE7FE); // b +#0 (infinite loop)

	// Setup registers.
	cpu.Regs()[0] = 1;
	cpu.Regs()[1] = 2;
	cpu.Regs()[15] = 0; // PC = 0
	cpu.SetCpsr(0x00000030); // Thumb mode

	// Execute!
	cpu.Run();

	// Here we would expect cpu.Regs()[0] == 8
	printf("R0: %u\n", cpu.Regs()[0]);

	return 0;
}*/

uint64_t tpidr_el0[0x40];

void setupDynarec() {
	memset(tpidr_el0, 0, sizeof(uint64_t) * 0x40);
	so_monitor = new Dynarmic::ExclusiveMonitor(1);
	so_dynarec_cfg.fastmem_pointer = (uintptr_t)nullptr;
	so_dynarec_cfg.enable_cycle_counting = false;
	so_dynarec_cfg.global_monitor = so_monitor;
	so_dynarec_cfg.callbacks = &so_dynarec_env;
	so_dynarec_cfg.tpidrro_el0 = (uint64_t *)tpidr_el0;
	so_dynarec_cfg.tpidr_el0 = (uint64_t *)tpidr_el0;
	so_dynarec = new Dynarmic::A64::Jit(so_dynarec_cfg);
	printf("AARCH64 dynarec inited with address: 0x%llx and TPIDR EL0 pointing at: 0x%llx\n", so_dynarec, tpidr_el0);
	so_dynarec->SetSP((uintptr_t)&so_stack[sizeof(so_stack)]);
	so_dynarec_env.parent = so_dynarec;
}

int main() {
	// Initialize OpenGL
	printf("Initializing OpenGL %d.%d...\n", OPENGL_MAJOR_VER, OPENGL_MINOR_VER);
	if (!initOpenGL(OPENGL_MAJOR_VER, OPENGL_MINOR_VER)) {
		printf("FATAL ERROR: OpenGL failed to be inited.\n");
		return -1;
	}
	
	// Setup dynarec
	printf("Setting up dynarec...\n");
	setupDynarec();
	
	// Entering game folder
	chdir("./gamefiles");
	
	// Load main game elf
	printf("Loading %s...\n", MAIN_ELF_PATH);
	int ret = so_load(MAIN_ELF_PATH, &dynarec_base_addr);
	if (ret) {
		printf("FATAL ERROR: Failed to load %s. (Errorcode: %d)\n", MAIN_ELF_PATH, ret);
		return -1;		
	}
	
	// Relocate jumps and function calls to our dynarec virtual addresses
	printf("Executing relocations and imports resolving...\n");
	so_relocate(dynarec_imports, dynarec_imports_num);
	
	// Execute hook patches
	printf("Applying hook patches...\n");
	exec_patch_hooks(dynarec_base_addr);
	
	// Flush dynarec cache
	printf("Flushing dynarec code cache...\n");
	so_flush_caches();
	
	// Init static arrays
	printf("Initing static arrays...\n");
	so_execute_init_array();
	
	// Start dynarec
	printf("Starting dynarec...\n");
	ret = exec_booting_sequence(dynarec_base_addr);
	if (ret) {
		printf("FATAL ERROR: Booting sequence failed. (Errorcode: %d)\n", ret);
		return -1;	
	}
	
	// Main loop
	printf("Entering main loop...\n");
	while (!ret) {
		ret = exec_main_loop(dynarec_base_addr);
	}
  
	printf("Exiting with code %d\n", ret);
	glfwTerminate();	
	return 0;
}
