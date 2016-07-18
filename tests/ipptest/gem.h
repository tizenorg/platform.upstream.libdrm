#ifndef __GEM_H__
#define __GEM_H__

struct exynos_gem_mmap_data {
	uint32_t handle;
	uint64_t size;
	uint64_t offset;
	void *addr;
};

struct kms_bo;
struct kms_driver;

extern int exynos_gem_create(int fd, struct drm_exynos_gem_create *gem);
extern int exynos_gem_mmap(int fd, struct exynos_gem_mmap_data *in_mmap);
extern int exynos_gem_close(int fd, struct drm_gem_close *gem_close);
extern struct kms_bo* exynos_kms_gem_create(struct kms_driver *kms,
		unsigned int width, unsigned int height, unsigned int *stride);

#endif
