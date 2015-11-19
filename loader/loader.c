#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "loader.h"
#include <stdio.h>
#include <dlfcn.h>
#ifndef ARRAY_SIZE(ar)
#define ARRAY_SIZE(ar) (sizeof(ar) / sizeof((ar)[0]))
#endif
static const char * support_modules_list[] = {
	"libdrm_sprd.so",
	"libdrm_exynos.so",
};
static void* lib_handler;
typedef int (*init_device_t) (const char *);
typedef int (*close_device_t) (int);

void * open_module (const char * name, int dlopen_mode)
{
	void *ret_lib_handler = NULL;
	int i;
	char * buf[1024];
	if (ret_lib_handler = dlopen (name, dlopen_mode) != NULL) {
		return ret_lib_handler;
	}
	for (i = 0; i < 10; i++) {
		sprintf(buf, "%s.%d", name, i);
		if ((ret_lib_handler = dlopen (buf, dlopen_mode)) != NULL) {
			return ret_lib_handler;
		}
	}
	return NULL;
}

int loader_load_module (const char *name, const char *busid)
{
	int fd, i;
	char * dl_err;
	init_device_t init_device_func = NULL;
	if (lib_handler) {
		return -1;
	}
	for (i = 0; i < ARRAY_SIZE(support_modules_list); i++) {
		if ((lib_handler = open_module (support_modules_list[i], RTLD_LAZY)) == NULL) {
			continue;
		}
		if ((init_device_func =  dlsym(lib_handler, "init_func")) == NULL ||
			dlerror() != NULL) {
			dlclose(lib_handler);
			lib_handler = NULL;
			continue;
		}
		if ((fd = init_device_func(name)) > 0) {
			return fd;
		}
		dlclose(lib_handler);
		lib_handler = NULL;
	}
	return -1;
}
