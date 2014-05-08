/*
 * DRM based rotator test program
 * Copyright 2012 Samsung Electronics
 *   YoungJun Cho <yj44.cho@samsung.com>
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "xf86drm.h"
#include "xf86drmMode.h"
#include "libkms.h"

#include "exynos_drm.h"

int exynos_gem_create(int fd, struct drm_exynos_gem_create *gem)
{
	int ret = 0;

	if (!gem) {
		fprintf(stderr, "gem object is null.\n");
		return -EFAULT;
	}
	ret = ioctl(fd, DRM_IOCTL_EXYNOS_GEM_CREATE, gem);
	if (ret < 0)
		fprintf(stderr, "failed to create gem buffer: %s\n",
								strerror(-ret));
	return ret;
}

int exynos_gem_mmap(int fd, struct drm_exynos_gem_mmap *in_mmap)
{
	int ret = 0;

	ret = ioctl(fd, DRM_IOCTL_EXYNOS_GEM_MMAP, in_mmap);
	if (ret < 0)
		fprintf(stderr, "failed to mmap: %s\n", strerror(-ret));
	return ret;
}

int exynos_gem_close(int fd, struct drm_gem_close *gem_close)
{
	int ret = 0;

	ret = ioctl(fd, DRM_IOCTL_GEM_CLOSE, gem_close);
	if (ret < 0)
		fprintf(stderr, "failed to close: %s\n", strerror(-ret));
	return ret;
}
