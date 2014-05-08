#ifndef __UTIL_H__
#define __UTIL_H__

extern int util_gem_create_mmap(int fd, struct drm_exynos_gem_create *gem,
					struct drm_exynos_gem_mmap *mmap,
					void **addr,
					unsigned int size);
extern void util_draw_buffer(void *addr, unsigned int stripe,
				unsigned int width, unsigned int height,
				unsigned int stride, unsigned int size);
extern int util_write_bmp(const char *file, const void *data,
				unsigned int width, unsigned int height);
#endif
