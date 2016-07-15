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
#include "gem.h"

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

void util_draw_buffer_yuv(void **addr, unsigned int width, unsigned int height)
{
	int i, j;
	void *img_ptr;
	unsigned char *in_img;
	unsigned char *y, *u, *v, *u422, *v422;

	img_ptr = malloc(width * height * 4);
	y = malloc(width * height);
	u = malloc(width * height);
	v = malloc(width * height);

	u422 = malloc(width * (height / 2));
	v422 = malloc(width * (height / 2));

	/* Get RGB fmt Image */
	util_draw_buffer(img_ptr, 1, width, height, width * 4, 0);

	i = 0;
	in_img = (unsigned char *)img_ptr;

	/* RGB to YCbCr Conversion  */
	for (j = 0; j<width * height * 4; j += 4) {
		y[j / 4] = (unsigned char)(62.481 / 255 *
			in_img[width * height * 4 - j - 1] + 128.553 / 255 *
			in_img[width * height * 4 - j - 2] + 24.966 / 255 *
			in_img[width * height * 4 - j - 3]);
		u[j / 4] = (unsigned char)(-37.797 / 255 *
			in_img[width * height * 4 - j - 1] - 74.203 / 255 *
			in_img[width * height * 4 - j - 2] + 112.000 / 255 *
			in_img[width * height * 4 - j - 3]) + 128;
		v[j / 4] = (unsigned char)(112.000 / 255 *
			in_img[width * height * 4 - j - 1] - 93.786 / 255 *
			in_img[width * height * 4 - j - 2] - 18.214 / 255 *
			in_img[width * height * 4 - j - 3]) + 128;

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
