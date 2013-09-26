/* vigs.c
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Authors:
 * Stanislav Vorobiov <s.vorobiov@samsung.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <sys/mman.h>
#include <linux/stddef.h>

#include <xf86drm.h>
#include <xf86atomic.h>

#include "vigs.h"
#include "vigs_drm.h"

#define vigs_offsetof(type, member) ((size_t)&((type*)0)->member)

#define vigs_containerof(ptr, type, member) ((type*)((char*)(ptr) - vigs_offsetof(type, member)))

struct vigs_drm_gem_info
{
    atomic_t ref_count;
};

struct vigs_drm_gem_impl
{
    struct vigs_drm_gem_info info;

    struct vigs_drm_gem gem;
};

struct vigs_drm_surface_impl
{
    struct vigs_drm_gem_info gem_info;

    struct vigs_drm_surface base;
};

struct vigs_drm_execbuffer_impl
{
    struct vigs_drm_gem_info gem_info;

    struct vigs_drm_execbuffer base;
};

static void vigs_drm_gem_close(struct vigs_drm_device *dev, uint32_t handle)
{
    struct drm_gem_close req =
    {
        .handle = handle,
    };

    if (handle) {
        drmIoctl(dev->fd, DRM_IOCTL_GEM_CLOSE, &req);
    }
}

static void vigs_drm_gem_impl_init(struct vigs_drm_gem_impl *gem_impl,
                                   struct vigs_drm_device *dev,
                                   uint32_t handle,
                                   uint32_t size,
                                   uint32_t name)
{
    atomic_set(&gem_impl->info.ref_count, 1);
    gem_impl->gem.dev = dev;
    gem_impl->gem.size = size;
    gem_impl->gem.handle = handle;
    gem_impl->gem.name = name;
}

int vigs_drm_device_create(int fd, struct vigs_drm_device **dev)
{
    drmVersionPtr version;
    uint32_t major;
    int ret;

    *dev = calloc(sizeof(**dev), 1);

    if (!*dev) {
        ret = -ENOMEM;
        goto fail1;
    }

    version = drmGetVersion(fd);

    if (!version) {
        ret = -EINVAL;
        goto fail2;
    }

    major = version->version_major;

    drmFreeVersion(version);

    if (major != DRM_VIGS_DRIVER_VERSION) {
        ret = -EINVAL;
        goto fail2;
    }

    (*dev)->fd = fd;

    return 0;

fail2:
    free(*dev);
fail1:
    *dev = NULL;

    return ret;
}

void vigs_drm_device_destroy(struct vigs_drm_device *dev)
{
    free(dev);
}

int vigs_drm_device_get_protocol_version(struct vigs_drm_device *dev,
                                         uint32_t *protocol_version)
{
    struct drm_vigs_get_protocol_version req;
    int ret;

    ret = drmIoctl(dev->fd, DRM_IOCTL_VIGS_GET_PROTOCOL_VERSION, &req);

    if (ret != 0) {
        return -errno;
    }

    if (protocol_version) {
        *protocol_version = req.version;
    }

    return 0;
}

void vigs_drm_gem_ref(struct vigs_drm_gem *gem)
{
    struct vigs_drm_gem_impl *gem_impl;

    if (!gem) {
        return;
    }

    gem_impl = vigs_containerof(gem, struct vigs_drm_gem_impl, gem);

    atomic_inc(&gem_impl->info.ref_count);
}

void vigs_drm_gem_unref(struct vigs_drm_gem *gem)
{
    struct vigs_drm_gem_impl *gem_impl;

    if (!gem) {
        return;
    }

    gem_impl = vigs_containerof(gem, struct vigs_drm_gem_impl, gem);

    assert(atomic_read(&gem_impl->info.ref_count) > 0);
    if (!atomic_dec_and_test(&gem_impl->info.ref_count)) {
        return;
    }

    if (gem->vaddr) {
        munmap(gem->vaddr, gem->size);
    }

    vigs_drm_gem_close(gem->dev, gem->handle);

    free(gem_impl);
}

int vigs_drm_gem_get_name(struct vigs_drm_gem *gem)
{
    struct drm_gem_flink req =
    {
        .handle = gem->handle,
    };
    int ret;

    if (gem->name) {
        return 0;
    }

    ret = drmIoctl(gem->dev->fd, DRM_IOCTL_GEM_FLINK, &req);

    if (ret != 0) {
        return -errno;
    }

    gem->name = req.name;

    return 0;
}

int vigs_drm_gem_map(struct vigs_drm_gem *gem, int track_access)
{
    struct drm_vigs_gem_map req =
    {
        .handle = gem->handle,
        .track_access = track_access
    };
    int ret;

    if (gem->vaddr) {
        return 0;
    }

    ret = drmIoctl(gem->dev->fd, DRM_IOCTL_VIGS_GEM_MAP, &req);

    if (ret != 0) {
        return -errno;
    }

    gem->vaddr = (void*)req.address;

    return 0;
}

void vigs_drm_gem_unmap(struct vigs_drm_gem *gem)
{
    if (!gem->vaddr) {
        return;
    }

    munmap(gem->vaddr, gem->size);
    gem->vaddr = NULL;
}

int vigs_drm_surface_create(struct vigs_drm_device *dev,
                            uint32_t width,
                            uint32_t height,
                            uint32_t stride,
                            uint32_t format,
                            struct vigs_drm_surface **sfc)
{
    struct vigs_drm_surface_impl *sfc_impl;
    struct drm_vigs_create_surface req =
    {
        .width = width,
        .height = height,
        .stride = stride,
        .format = format,
    };
    int ret;

    sfc_impl = calloc(sizeof(*sfc_impl), 1);

    if (!sfc_impl) {
        ret = -ENOMEM;
        goto fail1;
    }

    ret = drmIoctl(dev->fd, DRM_IOCTL_VIGS_CREATE_SURFACE, &req);

    if (ret != 0) {
        ret = -errno;
        goto fail2;
    }

    vigs_drm_gem_impl_init((struct vigs_drm_gem_impl*)sfc_impl,
                           dev,
                           req.handle,
                           req.size,
                           0);

    sfc_impl->base.width = width;
    sfc_impl->base.height = height;
    sfc_impl->base.stride = stride;
    sfc_impl->base.format = format;
    sfc_impl->base.id = req.id;

    *sfc = &sfc_impl->base;

    return 0;

fail2:
    free(sfc_impl);
fail1:
    *sfc = NULL;

    return ret;
}

int vigs_drm_surface_open(struct vigs_drm_device *dev,
                          uint32_t name,
                          struct vigs_drm_surface **sfc)
{
    struct vigs_drm_surface_impl *sfc_impl;
    struct drm_gem_open req =
    {
        .name = name,
    };
    struct drm_vigs_surface_info info_req;
    int ret;

    sfc_impl = calloc(sizeof(*sfc_impl), 1);

    if (!sfc_impl) {
        ret = -ENOMEM;
        goto fail1;
    }

    ret = drmIoctl(dev->fd, DRM_IOCTL_GEM_OPEN, &req);

    if (ret != 0) {
        ret = -errno;
        goto fail2;
    }

    info_req.handle = req.handle;

    ret = drmIoctl(dev->fd, DRM_IOCTL_VIGS_SURFACE_INFO, &info_req);

    if (ret != 0) {
        ret = -errno;
        goto fail3;
    }

    vigs_drm_gem_impl_init((struct vigs_drm_gem_impl*)sfc_impl,
                           dev,
                           req.handle,
                           info_req.size,
                           name);

    sfc_impl->base.width = info_req.width;
    sfc_impl->base.height = info_req.height;
    sfc_impl->base.stride = info_req.stride;
    sfc_impl->base.format = info_req.format;
    sfc_impl->base.id = info_req.id;

    *sfc = &sfc_impl->base;

    return 0;

fail3:
    vigs_drm_gem_close(dev, req.handle);
fail2:
    free(sfc_impl);
fail1:
    *sfc = NULL;

    return ret;
}

int vigs_drm_surface_set_gpu_dirty(struct vigs_drm_surface *sfc)
{
    struct drm_vigs_surface_set_gpu_dirty req =
    {
        .handle = sfc->gem.handle
    };
    int ret;

    ret = drmIoctl(sfc->gem.dev->fd, DRM_IOCTL_VIGS_SURFACE_SET_GPU_DIRTY, &req);

    return (ret != 0) ? -errno : 0;
}

int vigs_drm_surface_start_access(struct vigs_drm_surface *sfc,
                                  uint32_t saf)
{
    struct drm_vigs_surface_start_access req =
    {
        .address = (unsigned long)sfc->gem.vaddr,
        .saf = saf
    };
    int ret;

    ret = drmIoctl(sfc->gem.dev->fd, DRM_IOCTL_VIGS_SURFACE_START_ACCESS, &req);

    return (ret != 0) ? -errno : 0;
}

int vigs_drm_surface_end_access(struct vigs_drm_surface *sfc,
                                int sync)
{
    struct drm_vigs_surface_end_access req =
    {
        .address = (unsigned long)sfc->gem.vaddr,
        .sync = sync
    };
    int ret;

    ret = drmIoctl(sfc->gem.dev->fd, DRM_IOCTL_VIGS_SURFACE_END_ACCESS, &req);

    return (ret != 0) ? -errno : 0;
}

int vigs_drm_execbuffer_create(struct vigs_drm_device *dev,
                               uint32_t size,
                               struct vigs_drm_execbuffer **execbuffer)
{
    struct vigs_drm_execbuffer_impl *execbuffer_impl;
    struct drm_vigs_create_execbuffer req =
    {
        .size = size
    };
    int ret;

    execbuffer_impl = calloc(sizeof(*execbuffer_impl), 1);

    if (!execbuffer_impl) {
        ret = -ENOMEM;
        goto fail1;
    }

    ret = drmIoctl(dev->fd, DRM_IOCTL_VIGS_CREATE_EXECBUFFER, &req);

    if (ret != 0) {
        ret = -errno;
        goto fail2;
    }

    vigs_drm_gem_impl_init((struct vigs_drm_gem_impl*)execbuffer_impl,
                           dev,
                           req.handle,
                           req.size,
                           0);

    *execbuffer = &execbuffer_impl->base;

    return 0;

fail2:
    free(execbuffer_impl);
fail1:
    *execbuffer = NULL;

    return ret;
}

int vigs_drm_execbuffer_open(struct vigs_drm_device *dev,
                             uint32_t name,
                             struct vigs_drm_execbuffer **execbuffer)
{
    struct vigs_drm_execbuffer_impl *execbuffer_impl;
    struct drm_gem_open req =
    {
        .name = name,
    };
    int ret;

    execbuffer_impl = calloc(sizeof(*execbuffer_impl), 1);

    if (!execbuffer_impl) {
        ret = -ENOMEM;
        goto fail1;
    }

    ret = drmIoctl(dev->fd, DRM_IOCTL_GEM_OPEN, &req);

    if (ret != 0) {
        ret = -errno;
        goto fail2;
    }

    vigs_drm_gem_impl_init((struct vigs_drm_gem_impl*)execbuffer_impl,
                           dev,
                           req.handle,
                           req.size,
                           name);

    *execbuffer = &execbuffer_impl->base;

    return 0;

fail2:
    free(execbuffer_impl);
fail1:
    *execbuffer = NULL;

    return ret;
}

int vigs_drm_execbuffer_exec(struct vigs_drm_execbuffer *execbuffer)
{
    struct drm_vigs_exec req =
    {
        .handle = execbuffer->gem.handle
    };
    int ret;

    ret = drmIoctl(execbuffer->gem.dev->fd, DRM_IOCTL_VIGS_EXEC, &req);

    return (ret != 0) ? -errno : 0;
}
