/*
 * DRM based fimc test program
 * Copyright 2012 Samsung Electronics
 *   Eunchul Kim <chulspro.kim@sasmsung.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "exynos_drm.h"
#include "drm_fourcc.h"
#include "libkms.h"
#include "internal.h"
#include "gem.h"
#include "util.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/*
 * Formats
 */
struct color_component {
	unsigned int length;
	unsigned int offset;
};

struct rgb_info {
	struct color_component red;
	struct color_component green;
	struct color_component blue;
	struct color_component alpha;
};

enum yuv_order {
	YUV_YCbCr = 1,
	YUV_YCrCb = 2,
	YUV_YC = 4,
	YUV_CY = 8,
};

struct yuv_info {
	enum yuv_order order;
	unsigned int xsub;
	unsigned int ysub;
	unsigned int chroma_stride;
};

struct format_info {
	unsigned int format;
	const char *name;
	const struct rgb_info rgb;
	const struct yuv_info yuv;
};

#define MAKE_RGB_INFO(rl, ro, bl, bo, gl, go, al, ao) \
	.rgb = { { (rl), (ro) }, { (bl), (bo) }, { (gl), (go) }, { (al), (ao) } }

#define MAKE_YUV_INFO(order, xsub, ysub, chroma_stride) \
	.yuv = { (order), (xsub), (ysub), (chroma_stride) }

static const struct format_info format_info[] = {
	/* YUV packed */
	{ DRM_FORMAT_UYVY, "UYVY", MAKE_YUV_INFO(YUV_YCbCr | YUV_CY, 2, 2, 2) },
	{ DRM_FORMAT_VYUY, "VYUY", MAKE_YUV_INFO(YUV_YCrCb | YUV_CY, 2, 2, 2) },
	{ DRM_FORMAT_YUYV, "YUYV", MAKE_YUV_INFO(YUV_YCbCr | YUV_YC, 2, 2, 2) },
	{ DRM_FORMAT_YVYU, "YVYU", MAKE_YUV_INFO(YUV_YCrCb | YUV_YC, 2, 2, 2) },
	/* YUV semi-planar */
	{ DRM_FORMAT_NV12, "NV12", MAKE_YUV_INFO(YUV_YCbCr, 2, 2, 2) },
	{ DRM_FORMAT_NV21, "NV21", MAKE_YUV_INFO(YUV_YCrCb, 2, 2, 2) },
	{ DRM_FORMAT_NV16, "NV16", MAKE_YUV_INFO(YUV_YCbCr, 2, 1, 2) },
	{ DRM_FORMAT_NV61, "NV61", MAKE_YUV_INFO(YUV_YCrCb, 2, 1, 2) },
	/* YUV planar */
	{ DRM_FORMAT_YUV420, "YU12", MAKE_YUV_INFO(YUV_YCbCr, 2, 2, 1) },
	{ DRM_FORMAT_YVU420, "YV12", MAKE_YUV_INFO(YUV_YCrCb, 2, 2, 1) },
	/* RGB16 */
	{ DRM_FORMAT_ARGB4444, "AR12", MAKE_RGB_INFO(4, 8, 4, 4, 4, 0, 4, 12) },
	{ DRM_FORMAT_XRGB4444, "XR12", MAKE_RGB_INFO(4, 8, 4, 4, 4, 0, 0, 0) },
	{ DRM_FORMAT_ABGR4444, "AB12", MAKE_RGB_INFO(4, 0, 4, 4, 4, 8, 4, 12) },
	{ DRM_FORMAT_XBGR4444, "XB12", MAKE_RGB_INFO(4, 0, 4, 4, 4, 8, 0, 0) },
	{ DRM_FORMAT_RGBA4444, "RA12", MAKE_RGB_INFO(4, 12, 4, 8, 4, 4, 4, 0) },
	{ DRM_FORMAT_RGBX4444, "RX12", MAKE_RGB_INFO(4, 12, 4, 8, 4, 4, 0, 0) },
	{ DRM_FORMAT_BGRA4444, "BA12", MAKE_RGB_INFO(4, 4, 4, 8, 4, 12, 4, 0) },
	{ DRM_FORMAT_BGRX4444, "BX12", MAKE_RGB_INFO(4, 4, 4, 8, 4, 12, 0, 0) },
	{ DRM_FORMAT_ARGB1555, "AR15", MAKE_RGB_INFO(5, 10, 5, 5, 5, 0, 1, 15) },
	{ DRM_FORMAT_XRGB1555, "XR15", MAKE_RGB_INFO(5, 10, 5, 5, 5, 0, 0, 0) },
	{ DRM_FORMAT_ABGR1555, "AB15", MAKE_RGB_INFO(5, 0, 5, 5, 5, 10, 1, 15) },
	{ DRM_FORMAT_XBGR1555, "XB15", MAKE_RGB_INFO(5, 0, 5, 5, 5, 10, 0, 0) },
	{ DRM_FORMAT_RGBA5551, "RA15", MAKE_RGB_INFO(5, 11, 5, 6, 5, 1, 1, 0) },
	{ DRM_FORMAT_RGBX5551, "RX15", MAKE_RGB_INFO(5, 11, 5, 6, 5, 1, 0, 0) },
	{ DRM_FORMAT_BGRA5551, "BA15", MAKE_RGB_INFO(5, 1, 5, 6, 5, 11, 1, 0) },
	{ DRM_FORMAT_BGRX5551, "BX15", MAKE_RGB_INFO(5, 1, 5, 6, 5, 11, 0, 0) },
	{ DRM_FORMAT_RGB565, "RG16", MAKE_RGB_INFO(5, 11, 6, 5, 5, 0, 0, 0) },
	{ DRM_FORMAT_BGR565, "BG16", MAKE_RGB_INFO(5, 0, 6, 5, 5, 11, 0, 0) },
	/* RGB24 */
	{ DRM_FORMAT_BGR888, "BG24", MAKE_RGB_INFO(8, 0, 8, 8, 8, 16, 0, 0) },
	{ DRM_FORMAT_RGB888, "RG24", MAKE_RGB_INFO(8, 16, 8, 8, 8, 0, 0, 0) },
	/* RGB32 */
	{ DRM_FORMAT_ARGB8888, "AR24", MAKE_RGB_INFO(8, 16, 8, 8, 8, 0, 8, 24) },
	{ DRM_FORMAT_XRGB8888, "XR24", MAKE_RGB_INFO(8, 16, 8, 8, 8, 0, 0, 0) },
	{ DRM_FORMAT_ABGR8888, "AB24", MAKE_RGB_INFO(8, 0, 8, 8, 8, 16, 8, 24) },
	{ DRM_FORMAT_XBGR8888, "XB24", MAKE_RGB_INFO(8, 0, 8, 8, 8, 16, 0, 0) },
	{ DRM_FORMAT_RGBA8888, "RA24", MAKE_RGB_INFO(8, 24, 8, 16, 8, 8, 8, 0) },
	{ DRM_FORMAT_RGBX8888, "RX24", MAKE_RGB_INFO(8, 24, 8, 16, 8, 8, 0, 0) },
	{ DRM_FORMAT_BGRA8888, "BA24", MAKE_RGB_INFO(8, 8, 8, 16, 8, 24, 8, 0) },
	{ DRM_FORMAT_BGRX8888, "BX24", MAKE_RGB_INFO(8, 8, 8, 16, 8, 24, 0, 0) },
	{ DRM_FORMAT_ARGB2101010, "AR30", MAKE_RGB_INFO(10, 20, 10, 10, 10, 0, 2, 30) },
	{ DRM_FORMAT_XRGB2101010, "XR30", MAKE_RGB_INFO(10, 20, 10, 10, 10, 0, 0, 0) },
	{ DRM_FORMAT_ABGR2101010, "AB30", MAKE_RGB_INFO(10, 0, 10, 10, 10, 20, 2, 30) },
	{ DRM_FORMAT_XBGR2101010, "XB30", MAKE_RGB_INFO(10, 0, 10, 10, 10, 20, 0, 0) },
	{ DRM_FORMAT_RGBA1010102, "RA30", MAKE_RGB_INFO(10, 22, 10, 12, 10, 2, 2, 0) },
	{ DRM_FORMAT_RGBX1010102, "RX30", MAKE_RGB_INFO(10, 22, 10, 12, 10, 2, 0, 0) },
	{ DRM_FORMAT_BGRA1010102, "BA30", MAKE_RGB_INFO(10, 2, 10, 12, 10, 22, 2, 0) },
	{ DRM_FORMAT_BGRX1010102, "BX30", MAKE_RGB_INFO(10, 2, 10, 12, 10, 22, 0, 0) },
};

unsigned int format_fourcc(const char *name)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(format_info); i++) {
		if (!strcmp(format_info[i].name, name))
			return format_info[i].format;
	}
	return 0;
}

int util_gem_create_mmap(int fd, struct drm_exynos_gem_create *gem,
					struct exynos_gem_mmap_data *mmap,
					unsigned int size)
{
	/* initialize structure for gem create */
	memset(gem, 0x00, sizeof(struct drm_exynos_gem_create));
	gem->size = size;

	if (exynos_gem_create(fd, gem) < 0) {
		fprintf(stderr, "failed to gem create: %s\n", strerror(errno));
		return -1;
	}

	/* initialize structure for gem mmap */
	memset(mmap, 0x00, sizeof(struct exynos_gem_mmap_data));
	mmap->handle = gem->handle;
	mmap->size = gem->size;

	if (exynos_gem_mmap(fd, mmap) < 0) {
		fprintf(stderr, "failed to gem mmap: %s\n", strerror(errno));
		return -2;
	}

	return 0;
}

struct kms_bo *util_kms_gem_create_mmap(struct kms_driver *kms,
			unsigned int format, unsigned int width,
			unsigned int height, unsigned int handles[4],
			unsigned int pitches[4], unsigned int offsets[4])
{
	unsigned int virtual_height;
	void *planes[3] = { 0, };
	void *virtual;
	struct kms_bo *bo;
	int ret;

	switch (format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		virtual_height = height * 3 / 2;
		break;

	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
		virtual_height = height * 2;
		break;

	default:
		virtual_height = height;
		break;
	}

	bo = exynos_kms_gem_create(kms, width, virtual_height, &pitches[0]);
	if (!bo)
		return NULL;

	ret = kms_bo_map(bo, &virtual);
	if (ret) {
		fprintf(stderr, "failed to map buffer: %s\n",
			strerror(-ret));
		kms_bo_destroy(&bo);
		return NULL;
	}

	/* just testing a limited # of formats to test single
	 * and multi-planar path.. would be nice to add more..
	 */
	switch (format) {
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
		offsets[0] = 0;
		kms_bo_get_prop(bo, KMS_HANDLE, &handles[0]);
		kms_bo_get_prop(bo, KMS_PITCH, &pitches[0]);

		planes[0] = virtual;
		break;

	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
		offsets[0] = 0;
		kms_bo_get_prop(bo, KMS_HANDLE, &handles[0]);
		kms_bo_get_prop(bo, KMS_PITCH, &pitches[0]);
		pitches[1] = pitches[0];
		offsets[1] = pitches[0] * height;
		kms_bo_get_prop(bo, KMS_HANDLE, &handles[1]);

		planes[0] = virtual;
		planes[1] = virtual + offsets[1];
		break;

	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
		offsets[0] = 0;
		kms_bo_get_prop(bo, KMS_HANDLE, &handles[0]);
		kms_bo_get_prop(bo, KMS_PITCH, &pitches[0]);
		pitches[1] = pitches[0] / 2;
		offsets[1] = pitches[0] * height;
		kms_bo_get_prop(bo, KMS_HANDLE, &handles[1]);
		pitches[2] = pitches[1];
		offsets[2] = offsets[1] + pitches[1] * height / 2;
		kms_bo_get_prop(bo, KMS_HANDLE, &handles[2]);

		planes[0] = virtual;
		planes[1] = virtual + offsets[1];
		planes[2] = virtual + offsets[2];
		break;

	case DRM_FORMAT_ARGB4444:
	case DRM_FORMAT_XRGB4444:
	case DRM_FORMAT_ABGR4444:
	case DRM_FORMAT_XBGR4444:
	case DRM_FORMAT_RGBA4444:
	case DRM_FORMAT_RGBX4444:
	case DRM_FORMAT_BGRA4444:
	case DRM_FORMAT_BGRX4444:
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_XBGR1555:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_RGBX5551:
	case DRM_FORMAT_BGRA5551:
	case DRM_FORMAT_BGRX5551:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_RGBX1010102:
	case DRM_FORMAT_BGRA1010102:
	case DRM_FORMAT_BGRX1010102:
		offsets[0] = 0;
		kms_bo_get_prop(bo, KMS_HANDLE, &handles[0]);
		kms_bo_get_prop(bo, KMS_PITCH, &pitches[0]);

		planes[0] = virtual;
		break;
	}

	kms_bo_unmap(bo);

	return bo;
}

void util_draw_buffer(void *addr, unsigned int stripe,
				unsigned int width, unsigned int height,
				unsigned int stride, unsigned int size)
{
	if (stripe == 1) {
		int i, j;
		unsigned int *fb_ptr;
		div_t d;

		for (j = 0; j < height; j++) {
			fb_ptr = (unsigned int *)((char *)addr + j * stride);
			for (i = 0; i < width; i++) {
				d = div(i, width);
				fb_ptr[i] = 0x00130502 * (d.quot >> 6)
						+ 0x000a1120 * (d.rem >> 6);
			}
		}
	} else
		memset(addr, 0x77, size);
}

void util_draw_buffer_yuv(void **addr, unsigned int width, unsigned int height)
{
	int i, j;
	unsigned char *img_ptr;
	unsigned char *y, *u, *v, *u422, *v422;

	img_ptr = malloc(width * height * 4);
	y = malloc(width * height);
	u = malloc(width * height);
	v = malloc(width * height);

	u422 = malloc(width * (height / 2));
	v422 = malloc(width * (height / 2));

	/* Get RGB fmt Image */
	util_draw_buffer(img_ptr, 1, width, height, width * 4, 0);

	/* RGB888 to YUV422 Conversion */
	i = 0;
	for (j = 0; j < width * height * 4; j += 4) {
		y[j / 4] = (unsigned char)(
			FMT_Y_R_VALUE * img_ptr[width * height * 4 - j - 1] +
			FMT_Y_G_VALUE * img_ptr[width * height * 4 - j - 2] +
			FMT_Y_B_VALUE * img_ptr[width * height * 4 - j - 3]
			) + 16;
		u[j / 4] = (unsigned char)(-1 *
			FMT_U_R_VALUE * img_ptr[width * height * 4 - j - 1] -
			FMT_U_G_VALUE * img_ptr[width * height * 4 - j - 2] +
			FMT_U_B_VALUE * img_ptr[width * height * 4 - j - 3]
			) + 128;
		v[j / 4] = (unsigned char)(
			FMT_V_R_VALUE * img_ptr[width * height * 4 - j - 1] -
			FMT_V_G_VALUE * img_ptr[width * height * 4 - j - 2] -
			FMT_V_B_VALUE * img_ptr[width * height * 4 - j - 3]
			) + 128;

		if ((j / 4) % 2 == 0) {
			u422[i] = u[j / 4];
			v422[i] = v[j / 4];
			i++;
		}
	}

	memcpy(addr[EXYNOS_DRM_PLANAR_Y], y, width * height);
	memcpy(addr[EXYNOS_DRM_PLANAR_CB], u422, width * (height / 2));
	memcpy(addr[EXYNOS_DRM_PLANAR_CR], v422, width * (height / 2));

	free(img_ptr); free(y); free(u); free(v); free(u422); free(v422);
}

int util_write_bmp(const char *file, const void *data, unsigned int width,
							unsigned int height)
{
	int i;
	unsigned int * blocks;
	FILE *fp;
	struct {
		unsigned char magic[2];
	} bmpfile_magic = { {'B', 'M'} };
	struct {
		unsigned int filesz;
		unsigned short creator1;
		unsigned short creator2;
		unsigned int bmp_offset;
	} bmpfile_header = { 0, 0, 0, 0x36 };
	struct {
		unsigned int header_sz;
		unsigned int width;
		unsigned int height;
		unsigned short nplanes;
		unsigned short bitspp;
		unsigned int compress_type;
		unsigned int bmp_bytesz;
		unsigned int hres;
		unsigned int vres;
		unsigned int ncolors;
		unsigned int nimpcolors;
	} bmp_dib_v3_header_t = { 0x28, 0, 0, 1, 32, 0, 0, 0, 0, 0, 0 };

	fp = fopen(file, "wb");
	if (fp == NULL) return -1;

	bmpfile_header.filesz = sizeof(bmpfile_magic) + sizeof(bmpfile_header)
			+ sizeof(bmp_dib_v3_header_t) + width * height * 4;
	bmp_dib_v3_header_t.header_sz = sizeof(bmp_dib_v3_header_t);
	bmp_dib_v3_header_t.width = width;
	bmp_dib_v3_header_t.height = -height;
	bmp_dib_v3_header_t.nplanes = 1;
	bmp_dib_v3_header_t.bmp_bytesz = width * height * 4;

	fwrite(&bmpfile_magic, sizeof(bmpfile_magic), 1, fp);
	fwrite(&bmpfile_header, sizeof(bmpfile_header), 1, fp);
	fwrite(&bmp_dib_v3_header_t, sizeof(bmp_dib_v3_header_t), 1, fp);

	blocks = (unsigned int*)data;
	for (i = 0; i < height * width; i++)
		fwrite(&blocks[i], 4, 1, fp);

	fclose(fp);
	return 0;
}
