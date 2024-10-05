#ifndef _PORT_H_
#define _PORT_H_

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

#define MAIN_ELF_PATH "libMaxPayne.so"

extern DynLibFunction dynlib_functions[];
extern size_t dynlib_numfunctions;

extern int exec_booting_sequence(void *dynarec_base_addr);

#endif
