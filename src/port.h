#ifndef _PORT_H_
#define _PORT_H_

// OpenGL config
#define OPENGL_MAJOR_VER 2
#define OPENGL_MINOR_VER 1

// Game window setup
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

// Game elfs path
#define MAIN_ELF_PATH "libMaxPayne.so"

/* Functions */
extern GLFWwindow *glfw_window;
extern dynarec_import dynarec_imports[];
extern size_t dynarec_imports_num;

extern int exec_booting_sequence(void *dynarec_base_addr);
extern int exec_patch_hooks(void *dynarec_base_addr);
extern int exec_main_loop(void *dynarec_base_addr);

#endif
