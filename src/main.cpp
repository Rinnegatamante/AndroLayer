#include "glad/glad.h"
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "dynarec.h"
#include "so_util.h"
#include "port.h"

GLFWwindow *window = nullptr;
void *dynarec_base_addr = nullptr;

void framebuffer_size_callback(GLFWwindow* window, int width, int height){
    glViewport(0, 0, width, height);
} 

bool initOpenGL(int major_ver, int minor_ver) {
	// Initialize OpenGL
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major_ver);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor_ver);

    // Create a window
    window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "AndroLayer", NULL, NULL);
    if (window == NULL) {
        printf("Failed to create glfw3 window\n");
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window);

    // Load GL functions via glfw3
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        printf("Failed to initialize glad\n");
        return false;
    }    

    // Adjust viewport size to window size
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	
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

void setupDynarec() {
	Dynarmic::A64::UserConfig user_config;
    user_config.callbacks = &so_dynarec_env;
	so_dynarec = new Dynarmic::A64::Jit(user_config);
	printf("AARCH64 dynarec inited with address: 0x%x\n", so_dynarec);
}

int main() {
	// Initialize OpenGL
    if (!initOpenGL(2, 1)) {
		printf("FATAL ERROR: OpenGL failed to be inited.\n");
		return -1;
	}
	
	// Load main game elf
	int ret = so_load(MAIN_ELF_PATH, &dynarec_base_addr);
	if (ret) {
		printf("FATAL ERROR: Failed to load %s. (Errorcode: %d)\n", MAIN_ELF_PATH, ret);
		return -1;		
	}
	
	// Resolve imports with native implementations
	so_resolve(dynlib_functions, dynlib_numfunctions, 1);
	
	// Setup dynarec
	setupDynarec();
	
	// Start dynarec
	ret = exec_booting_sequence(dynarec_base_addr);
	if (ret) {
		printf("FATAL ERROR: Booting sequence failed. (Errorcode: %d)\n", ret);
		return -1;	
	}
	
    // Main loop
	glClearColor(0,1,0,0);
    while (!glfwWindowShouldClose(window)) {

        // render process
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // clear the depth buffer and the color buffer

        // check call events
        glfwSwapBuffers(window);
        glfwPollEvents();    
    }
  
    glfwTerminate();    
    return 0;
}
