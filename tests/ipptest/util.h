#ifndef __UTIL_H__
#define __UTIL_H__

/*
 * RGB value of YUV format
 */
#define FMT_Y_R_VALUE 0.257
#define FMT_Y_G_VALUE 0.504
#define FMT_Y_B_VALUE 0.098

#define FMT_U_R_VALUE 0.148
#define FMT_U_G_VALUE 0.291
#define FMT_U_B_VALUE 0.439

#define FMT_V_R_VALUE 0.439
#define FMT_V_G_VALUE 0.368
#define FMT_V_B_VALUE 0.071

struct kms_bo;
struct kms_driver;

unsigned int format_fourcc(const char *name);
extern int util_gem_create_mmap(int fd, struct drm_exynos_gem_create *gem,
					struct exynos_gem_mmap_data *mmap,
					unsigned int size);
extern struct kms_bo *util_kms_gem_create_mmap(struct kms_driver *kms,
			unsigned int format, unsigned int width,
			unsigned int height, unsigned int handles[4],
			unsigned int pitches[4], unsigned int offsets[4]);
extern void util_draw_buffer(void *addr, unsigned int stripe,
				unsigned int width, unsigned int height,
				unsigned int stride, unsigned int size);
extern void util_draw_buffer_yuv(void **addr, unsigned int width,
						unsigned int height);
extern int util_write_bmp(const char *file, const void *data,
				unsigned int width, unsigned int height);

#endif
