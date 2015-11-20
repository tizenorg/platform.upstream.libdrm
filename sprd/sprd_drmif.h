/* sprd_drmif.h
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Authors:
 *	Roman Marchenko <r.marchenko@samsung.com>
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

#ifndef _SPRD_DRMIF_H_
#define _SPRD_DRMIF_H_

struct sprd_drm_device;
struct sprd_drm_vendor_event_data;
typedef int (*sprd_drm_vendor_event_handler)(struct drm_event *event);

/*
 * device related functions:
 */
struct sprd_drm_device * sprd_device_create(int fd);
void sprd_device_destroy(struct sprd_drm_device *dev);
struct sprd_drm_device * sprd_device_get(void);

int sprd_device_add_vendor_event_handler(struct sprd_drm_device *dev,
                                         sprd_drm_vendor_event_handler handler);

#endif /* _SPRD_DRMIF_H_ */
