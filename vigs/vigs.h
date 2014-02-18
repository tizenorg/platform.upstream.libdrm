/* vigs.h
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

#ifndef __VIGS_H__
#define __VIGS_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Surface formats.
 */
typedef enum
{
    vigs_drm_surface_bgrx8888 = 0x0,
    vigs_drm_surface_bgra8888 = 0x1,
} vigs_drm_surface_format;

/*
 * Surface access flags.
 */
#define VIGS_DRM_SAF_READ 1
#define VIGS_DRM_SAF_WRITE 2

struct vigs_drm_device
{
    /* DRM fd. */
    int fd;
};

struct vigs_drm_gem
{
    /* VIGS device object. */
    struct vigs_drm_device *dev;

    /* size of the buffer created. */
    uint32_t size;

    /* a gem handle to gem object created. */
    uint32_t handle;

    /* a gem global handle from flink request. initially 0. */
    uint32_t name;

    /* user space address to a gem buffer mmaped. initially NULL. */
    void *vaddr;
};

struct vigs_drm_surface
{
    struct vigs_drm_gem gem;

    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    int scanout;
    uint32_t id;
};

struct vigs_drm_execbuffer
{
    struct vigs_drm_gem gem;
};

struct vigs_drm_fence
{
    /* VIGS device object. */
    struct vigs_drm_device *dev;

    /* a handle to fence object. */
    uint32_t handle;

    /* fence sequence number. */
    uint32_t seq;

    /* is fence signaled ? updated on 'vigs_drm_fence_check'. */
    int signaled;
};

/*
 * All functions return 0 on success and < 0 on error, i.e. kernel style:
 * return -ENOMEM;
 */

/*
 * Device functions.
 * @{
 */

/*
 * Returns -EINVAL on driver version mismatch.
 */
int vigs_drm_device_create(int fd, struct vigs_drm_device **dev);

void vigs_drm_device_destroy(struct vigs_drm_device *dev);

int vigs_drm_device_get_protocol_version(struct vigs_drm_device *dev,
                                         uint32_t *protocol_version);

/*
 * @}
 */

/*
 * GEM functions.
 * @{
 */

/*
 * Passing NULL won't hurt, this is for convenience.
 */
void vigs_drm_gem_ref(struct vigs_drm_gem *gem);

/*
 * Passing NULL won't hurt, this is for convenience.
 */
void vigs_drm_gem_unref(struct vigs_drm_gem *gem);

int vigs_drm_gem_get_name(struct vigs_drm_gem *gem);

int vigs_drm_gem_map(struct vigs_drm_gem *gem, int track_access);

void vigs_drm_gem_unmap(struct vigs_drm_gem *gem);

int vigs_drm_gem_wait(struct vigs_drm_gem *gem);

/*
 * @}
 */

/*
 * Surface functions.
 * @{
 */

int vigs_drm_surface_create(struct vigs_drm_device *dev,
                            uint32_t width,
                            uint32_t height,
                            uint32_t stride,
                            uint32_t format,
                            int scanout,
                            struct vigs_drm_surface **sfc);

int vigs_drm_surface_open(struct vigs_drm_device *dev,
                          uint32_t name,
                          struct vigs_drm_surface **sfc);

int vigs_drm_surface_set_gpu_dirty(struct vigs_drm_surface *sfc);

int vigs_drm_surface_start_access(struct vigs_drm_surface *sfc,
                                  uint32_t saf);

int vigs_drm_surface_end_access(struct vigs_drm_surface *sfc,
                                int sync);

/*
 * @}
 */

/*
 * Execbuffer functions.
 * @{
 */

int vigs_drm_execbuffer_create(struct vigs_drm_device *dev,
                               uint32_t size,
                               struct vigs_drm_execbuffer **execbuffer);

int vigs_drm_execbuffer_open(struct vigs_drm_device *dev,
                             uint32_t name,
                             struct vigs_drm_execbuffer **execbuffer);

int vigs_drm_execbuffer_exec(struct vigs_drm_execbuffer *execbuffer);

/*
 * @}
 */

/*
 * Fence functions.
 * @{
 */

int vigs_drm_fence_create(struct vigs_drm_device *dev,
                          int send,
                          struct vigs_drm_fence **fence);

/*
 * Passing NULL won't hurt, this is for convenience.
 */
void vigs_drm_fence_ref(struct vigs_drm_fence *fence);

/*
 * Passing NULL won't hurt, this is for convenience.
 */
void vigs_drm_fence_unref(struct vigs_drm_fence *fence);

int vigs_drm_fence_wait(struct vigs_drm_fence *fence);

int vigs_drm_fence_check(struct vigs_drm_fence *fence);

/*
 * @}
 */

/*
 * Plane functions.
 * @{
 */

int vigs_drm_plane_set_zpos(struct vigs_drm_device *dev,
                            uint32_t plane_id,
                            int zpos);

/*
 * @}
 */

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif
