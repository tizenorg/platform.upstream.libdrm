#ifndef __GEM_H__
#define __GEM_H__

extern int exynos_gem_create(int fd, struct drm_exynos_gem_create *gem);
extern int exynos_gem_mmap(int fd, struct drm_exynos_gem_mmap *in_mmap);
extern int exynos_gem_close(int fd, struct drm_gem_close *gem_close);

#endif
