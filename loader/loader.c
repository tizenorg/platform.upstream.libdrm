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


int loader_load_module (const char *name, const char *busid)
{
	int fd, i;
	if (lib_handler) {
		return -1;
	}
	for (i = 0; i < ARRAY_SIZE(support_modules_list); i++) {
		if ((lib_handler = dlopen (support_modules_list[i], RTLD_LAZY)) == NULL) {
			continue;
		}
		init_device_t init_device_func =  dlsym(lib_handler, "init_func");
		if (init_device_func && !dlerror()) {
			if ((fd = init_device_func(name)) > 0) {
				return fd;
			}
		}
		dlclose(lib_handler);
		lib_handler = NULL;
	}
	return -1;
}
