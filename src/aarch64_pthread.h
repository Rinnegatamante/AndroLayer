#ifndef _AARCH64_PTHREAD_H_
#define _AARCH64_PTHREAD_H_

#include <pthread.h>

int __aarch64_pthread_once(pthread_once_t *__once_control, void (*__init_routine) (void));
int __aarch64_pthread_create(Dynarmic::A64::Jit *jit, pthread_t *__restrict __newthread, const pthread_attr_t *__restrict __attr, void *(*__start_routine) (void *), void *__restrict __arg);
int __aarch64_pthread_mutex_init(pthread_mutex_t** uid, const int* mutexattr);
int __aarch64_pthread_mutex_destroy(pthread_mutex_t** uid);
int __aarch64_pthread_mutex_lock(pthread_mutex_t** uid);
int __aarch64_pthread_mutex_unlock(pthread_mutex_t** uid);

#endif
