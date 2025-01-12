/*
 * pthread, depending on the pthread implementation used on host machine (pthread-embedded, BIONIC, etc) may have different struct sizes causing incompatibility
 * with the guest application. In order to fix this, we abstract the pthread object accesses and making the implementation agnostic to struct sizes.
 */

#include "dynarec.h"
#include "so_util.h"
#include "aarch64_pthread.h"

int __aarch64_pthread_once(pthread_once_t *__once_control, void (*__init_routine) (void)) {
	if (*__once_control == PTHREAD_ONCE_INIT) {
		so_run_fiber(so_dynarec, (uintptr_t)__init_routine);
		*__once_control = !PTHREAD_ONCE_INIT;
	}

	return 0;
}

int __aarch64_pthread_create(Dynarmic::A64::Jit *jit, pthread_t *__restrict __newthread, const pthread_attr_t *__restrict __attr, void *(*__start_routine) (void *), void *__restrict __arg) {
	printf("NOIMPL: pthread_create called\n");
	std::abort();
	return 0;
}

int __aarch64_pthread_mutex_init(pthread_mutex_t** uid, const int* mutexattr) {
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

int __aarch64_pthread_mutex_destroy(pthread_mutex_t** uid) {
	if (uid && *uid && (uintptr_t)*uid > 0x8000) {
		pthread_mutex_destroy(*uid);
		free(*uid);
		*uid = NULL;
	}
	return 0;
}

int __aarch64_pthread_mutex_lock(pthread_mutex_t** uid) {
	int ret = 0;
	if (!*uid) {
		ret = __aarch64_pthread_mutex_init(uid, NULL);
	}
	else if ((uintptr_t)*uid == 0x4000) {
		int attr = 1; // recursive
		ret = __aarch64_pthread_mutex_init(uid, &attr);
	}
	if (ret < 0) {
		return ret;
	}
	return pthread_mutex_lock(*uid);
}

int __aarch64_pthread_mutex_unlock(pthread_mutex_t** uid) {
	int ret = 0;
	if (!*uid) {
		ret = __aarch64_pthread_mutex_init(uid, NULL);
	}
	else if ((uintptr_t)*uid == 0x4000) {
		int attr = 1; // recursive
		ret = __aarch64_pthread_mutex_init(uid, &attr);
	}
	if (ret < 0)
		return ret;
	return pthread_mutex_unlock(*uid);
}