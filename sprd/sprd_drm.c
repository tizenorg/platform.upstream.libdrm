/* sprd_drm.c
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <linux/stddef.h>
#include <linux/fb.h>
#include <video/sprdfb.h>

#include "xf86drm.h"
#include "xf86drmMode.h"
#include "drm_fourcc.h"
#include "libdrm_lists.h"

#include "sprd_drmif.h"

#define U642VOID(x) ((void *)(unsigned long)(x))
#define U642INTPTR(x) ( (unsigned int*) U642VOID(x) )
#define VOID2U64(x) ((uint64_t)(unsigned long)(x))

#define ALIGN(_v, _d) (((_v) + ((_d) - 1)) & ~((_d) - 1))

#define SPRD_DEBUG_MSG 1

#ifdef SPRD_DEBUG_MSG
	#define SPRD_DRM_DEBUG(fmt, ...) printf(fmt, ##__VA_ARGS__)
	#define SPRD_DRM_ERROR(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
	#define SPRD_DRM_DEBUG(fmt, ...)
	#define SPRD_DRM_ERROR(fmt, ...)
#endif


#define FB_DEV_LCD  "/dev/fb0"
//TODO::
//#define FB_DEV_HDMI "/dev/fb1"
//#define FB_DEV_WB   "/dev/fb2"


#define  MAX_CRTC		1
#define  MAX_PLANE		2
#define  MAX_CONNECTOR	1
#define  DEFAULT_LAYER	SPRD_LAYER_OSD
#define  DEFAULT_ZPOZ	1

#define  ZPOS_TO_LYR_ID(zpos) ( zpos == 1 ? SPRD_LAYER_OSD : SPRD_LAYER_IMG)

#define DRM_PROP_NAME_LEN	32

#define DRM_MODE_PROP_PENDING	(1<<0)
#define DRM_MODE_PROP_RANGE		(1<<1)
#define DRM_MODE_PROP_IMMUTABLE	(1<<2)
#define DRM_MODE_PROP_ENUM		(1<<3) /* enumerated type with text strings */
#define DRM_MODE_PROP_BLOB		(1<<4)
#define DRM_MODE_PROP_BITMASK	(1<<5) /* bitmask of enumerated types */

struct sprd_drm_device;


struct sprd_drm_mode_mode {

	struct drm_mode_modeinfo drm_mode;

	uint32_t mode_id; /**< Id */
};

struct sprd_drm_mode_crtc {
	struct drm_mode_crtc drm_crtc;

	drmMMListHead link;

	int is_active;
};

#define DRM_CONNECTOR_MAX_ENCODER 3
struct sprd_drm_mode_connector {
	struct drm_mode_get_connector drm_conn;

	drmMMListHead link;

	struct sprd_drm_device * dev;

	/* file descriptor of frame buffer */
	int32_t fb_fd;
	const char * fb_fd_name;

	/* requested DPMS state */
	int32_t dpms;

	/* activated layers */
	uint32_t activated_layers;

	drmMMListHead modes;

	uint32_t encoder_ids[DRM_CONNECTOR_MAX_ENCODER];
};

struct sprd_drm_mode_encoder {
	struct drm_mode_get_encoder drm_encoder;

	drmMMListHead link;
};

struct sprd_drm_mode_plane {
	struct drm_mode_get_plane drm_plane;

	drmMMListHead link;

	/* Signed dest location allows it to be partially off screen */
	int32_t crtc_x, crtc_y;
	uint32_t crtc_w, crtc_h;

	/* Source values are 16.16 fixed point */
	uint32_t src_x, src_y;
	uint32_t src_h, src_w;

	int need_update;

	int zpos;
};

struct sprd_drm_property_blob {
	uint32_t id;
	drmMMListHead head;
	unsigned int length;
	unsigned char data[];
};


struct sprd_drm_prop_enum_list {
	int type;
	const char *name;
};

struct sprd_drm_property {
	drmMMListHead head;
	uint32_t id;
	uint32_t flags;
	char name[DRM_PROP_NAME_LEN];
	uint32_t num_values;

	//DRM_MODE_PROP_RANGE (num_values = 2)
	int range_min, range_max;

	//DRM_MODE_PROP_ENUM | DRM_MODE_PROP_BITMASK
	struct sprd_drm_prop_enum_list *enum_list;

	//DRM_MODE_PROP_BLOB
	drmMMListHead blob_list;

	void 	 (*prop_set)(struct sprd_drm_property *prop, uint32_t obj_id, uint64_t val);
	uint64_t (*prop_get)(struct sprd_drm_property *prop, uint32_t obj_id);
};

struct sprd_drm_event_vblank {
	drmVBlank vbl;
	uint32_t type;
	uint64_t user_data;
	struct drm_mode_crtc set_crtc_info;
};

/**
 * @drm_fd: file descriptor of drm device
 * @num_fb: number of fbs available
 * @fb_list: list of framebuffers available
 * @num_connector: number of connectors on this device
 * @connector_list: list of connector objects
 * @num_encoder: number of encoders on this device
 * @encoder_list: list of encoder objects
 * @num_crtc: number of CRTCs on this device
 * @crtc_list: list of CRTC objects
 *
 * Core mode resource tracking structure.  All CRTC, encoders, and connectors
 * enumerated by the driver are added here, as are global properties.  Some
 * global restrictions are also here, e.g. dimension restrictions.
 */
struct sprd_drm_device {

	drmMMListHead link;

	int drm_fd;

	uint32_t num_fb;
	drmMMListHead fb_list;

	struct sprd_drm_event_vblank * page_flip_event;

	uint32_t num_connector;
	drmMMListHead connector_list;
	uint32_t num_encoder;
	drmMMListHead encoder_list;
	uint32_t num_plane;
	drmMMListHead plane_list;

	uint32_t num_crtc;
	drmMMListHead crtc_list;

	drmMMListHead property_list;

	struct sprd_drm_property *dpms_prop;

};

struct sprd_drm_framebuffer {

	/*
	 * TODO:: this is from kernel
	 *
	 * Note that the fb is refcounted for the benefit of driver internals,
	 * for example some hw, disabling a CRTC/plane is asynchronous, and
	 * scanout does not actually complete until the next vblank.  So some
	 * cleanup (like releasing the reference(s) on the backing GEM bo(s))
	 * should be deferred.  In cases like this, the driver would like to
	 * hold a ref to the fb even though it has already been removed from
	 * userspace perspective.
	 */

	struct sprd_drm_device * dev;

	uint32_t refcount;

	uint32_t id;

	uint32_t pitches[4];
	uint32_t offsets[4];
	uint32_t handles[4];
	uint32_t names[4];
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t bits_per_pixel;
	uint32_t flags;
	uint32_t pixel_format; /* fourcc format */

	drmMMListHead link;

};

/*
* @devices: list of drm device. the system may have more then one video card.
*/
drmMMListHead devices = {&devices, &devices};

/*
* @resource_storage: storage for all recurces
*/
static void * resource_storage = NULL;

static const uint32_t formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YUV420
};

struct sprd_drm_resource {
	void * val;
	int property_count;
	int property_capacity;
	void ** propertys;
};


struct sprd_drm_property * sprd_drm_create_dpms_prpoperty(struct sprd_drm_device * dev);

static int sprd_drm_resource_new_id(void * val)
{
	static int next_id = 0;
	struct sprd_drm_resource * res;
	res = drmMalloc(sizeof(struct sprd_drm_resource));
	res->val = val;
	if (!resource_storage)
		resource_storage = drmHashCreate();

	drmHashInsert(resource_storage, ++next_id, res);

	return next_id;
}

static void * sprd_drm_resource_get(int id)
{
	struct sprd_drm_resource * res = NULL;

	if (!resource_storage)
		return NULL;

	drmHashLookup(resource_storage, id, &res);

	return res ? res->val : NULL;
}

static void sprd_drm_resource_del(int id)
{
	struct sprd_drm_resource * res = NULL;

	if (!resource_storage)
		return ;

	drmHashLookup(resource_storage, id, &res);
	drmHashDelete(resource_storage, id);
	drmFree(res->propertys);
	drmFree(res);
}

static void sprd_drm_resource_prop_add(int id, void * prop)
{
	struct sprd_drm_resource * res = NULL;

	if (!resource_storage)
		return;

	if (!prop)
		return;

	drmHashLookup(resource_storage, id, &res);

	if (!res)
		return;

	if (res->property_capacity == 0) {
		res->property_capacity = 8;
		res->propertys = drmMalloc( sizeof(void *) * res->property_capacity);
	}
	else if (res->property_count >= res->property_capacity) {
		//TODO: relocate data
		return;
	}
	res->propertys[res->property_count++] = prop;
}


static int sprd_drm_resource_prop_list_get(int id,  void ** propertys)
{
	struct sprd_drm_resource * res = NULL;

	if (!resource_storage)
		return 0;

	drmHashLookup(resource_storage, id, &res);

	if (!res)
		return 0;

	*propertys = res->propertys;

	return res->property_count;
}

/*
 * from kernel
 * Original addfb only supported RGB formats, so figure out which one
 */
static uint32_t sprd_drm_legacy_fb_format(uint32_t bpp, uint32_t depth)
{
	uint32_t fmt;

	switch (bpp) {
	case 8:
		fmt = DRM_FORMAT_C8;
		break;
	case 16:
		if (depth == 15)
			fmt = DRM_FORMAT_XRGB1555;
		else
			fmt = DRM_FORMAT_RGB565;
		break;
	case 24:
		fmt = DRM_FORMAT_RGB888;
		break;
	case 32:
		if (depth == 24)
			fmt = DRM_FORMAT_XRGB8888;
		else if (depth == 30)
			fmt = DRM_FORMAT_XRGB2101010;
		else
			fmt = DRM_FORMAT_ARGB8888;
		break;
	default:
		SPRD_DRM_DEBUG("bad bpp, assuming x8r8g8b8 pixel format\n");
		fmt = DRM_FORMAT_XRGB8888;
		break;
	}

	return fmt;
}

/*
 * from kernel
 *
 * Just need to support RGB formats here for compat with code that doesn't
 * use pixel formats directly yet.
 */
static void sprd_drm_fb_get_bpp_depth(uint32_t format, unsigned int *depth, unsigned int *bpp)
{
	switch (format) {
	case DRM_FORMAT_C8:
	case DRM_FORMAT_RGB332:
	case DRM_FORMAT_BGR233:
		*depth = 8;
		*bpp = 8;
		break;
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_XBGR1555:
	case DRM_FORMAT_RGBX5551:
	case DRM_FORMAT_BGRX5551:
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_BGRA5551:
		*depth = 15;
		*bpp = 16;
		break;
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		*depth = 16;
		*bpp = 16;
		break;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		*depth = 24;
		*bpp = 24;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_BGRX8888:
		*depth = 24;
		*bpp = 32;
		break;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_RGBX1010102:
	case DRM_FORMAT_BGRX1010102:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_BGRA1010102:
		*depth = 30;
		*bpp = 32;
		break;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_BGRA8888:
		*depth = 32;
		*bpp = 32;
		break;
	default:
		SPRD_DRM_DEBUG("unsupported pixel format\n");
		*depth = 0;
		*bpp = 0;
		break;
	}
}

static inline uint32_t _get_refresh(struct fb_var_screeninfo * timing)
{
	uint32_t pixclock, hfreq, htotal, vtotal;

	pixclock = PICOS2KHZ(timing->pixclock) * 1000;

	htotal = timing->xres + timing->right_margin + timing->hsync_len +
			timing->left_margin;
	vtotal = timing->yres + timing->lower_margin + timing->vsync_len +
			timing->upper_margin;

	if (timing->vmode & FB_VMODE_INTERLACED)
		vtotal /= 2;
	if (timing->vmode & FB_VMODE_DOUBLE)
		vtotal *= 2;

	hfreq = pixclock/htotal;
	return hfreq/vtotal;
}

/**
 * drm_mode_set_name - set the name on a mode
 * @mode: name will be set in this mode
 *
 * Set the name of @mode to a standard format.
 */
static void drm_mode_set_name(struct drm_mode_modeinfo *mode)
{
	int interlaced = !!(mode->flags & DRM_MODE_FLAG_INTERLACE);

	snprintf(mode->name, DRM_DISPLAY_MODE_LEN, "%dx%d%s",
		 mode->hdisplay, mode->vdisplay,
		 interlaced ? "i" : "");
}

/*
 * Convert fb_var_screeninfo to drm_mode_modeinfo
 */
static inline void _fb_var_to_drm_mode(struct drm_mode_modeinfo *mode,
			struct fb_var_screeninfo * timing)
{

	if (!timing->pixclock)
		return;

	mode->clock = timing->pixclock / 1000;
	mode->vrefresh = _get_refresh(timing);
	mode->hdisplay = timing->xres;
	mode->hsync_start = mode->hdisplay + timing->right_margin;
	mode->hsync_end = mode->hsync_start + timing->hsync_len;
	mode->htotal = mode->hsync_end + timing->left_margin;

	mode->vdisplay = timing->yres;
	mode->vsync_start = mode->vdisplay + timing->lower_margin;
	mode->vsync_end = mode->vsync_start + timing->vsync_len;
	mode->vtotal = mode->vsync_end + timing->upper_margin;

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	if (timing->vmode & FB_VMODE_INTERLACED)
		mode->flags |= DRM_MODE_FLAG_INTERLACE;

	if (timing->vmode & FB_VMODE_DOUBLE)
		mode->flags |= DRM_MODE_FLAG_DBLSCAN;

	drm_mode_set_name(mode);
}

static struct sprd_drm_mode_mode * sprd_drm_mode_create(void)
{
	struct sprd_drm_mode_mode * mode = NULL;

	mode = drmMalloc(sizeof (struct sprd_drm_mode_mode));
	mode->mode_id = sprd_drm_resource_new_id(mode);
	return mode;
}

static void sprd_drm_mode_connector_update_from_frame_buffer(struct sprd_drm_mode_connector * conn)
{
	struct sprd_drm_mode_mode * mode;
	conn->drm_conn.connection = 0;

	if (!conn->fb_fd_name) return;

	if (!conn->fb_fd)
		conn->fb_fd = open (conn->fb_fd_name, O_RDWR, 0);

	if (conn->fb_fd > 0)
	{
	    /* Get the current screen setting */
	    struct fb_var_screeninfo mi;
	    if (!ioctl (conn->fb_fd, FBIOGET_VSCREENINFO, &mi))
	    {
	    	conn->drm_conn.connection = 1;
			conn->drm_conn.mm_height = mi.height;
			conn->drm_conn.mm_width = mi.width;

			//get modes at first
			if (conn->drm_conn.count_modes < 1)
			{
				conn->drm_conn.count_modes = 1;
				mode = sprd_drm_mode_create();
				conn->drm_conn.modes_ptr = VOID2U64(&mode->drm_mode);
			}
			else
			{
				mode = (struct sprd_drm_mode_mode*)(U642VOID(conn->drm_conn.modes_ptr));
			}

			_fb_var_to_drm_mode(&mode->drm_mode, &mi);

			//TODO: check real state
			conn->dpms = DRM_MODE_DPMS_OFF;
	    }
	    else
	    {
			close(conn->fb_fd);
	    }
	}
}

/*
 * Get a gem global object name from a gem object handle
 *
 * @fd: file descriptor of drm device
 * @handle: a gem object handle
 *
 * This interface is used to get a gem global object name from a gem object
 * handle to a buffer that wants to share it with another process
 *
 * If true, return gem global object name(positive) else 0
 */
static int sprd_drm_get_name(uint32_t fd, uint32_t handle)
{
	struct drm_gem_flink arg;
	int ret;

	if (!handle) return -EINVAL;

	char strErroBuf[256] = {0,};
	memset(&arg, 0, sizeof(arg));
	arg.handle = handle;

	ret = drmIoctl(fd, DRM_IOCTL_GEM_FLINK, &arg);
	if (ret) {
		SPRD_DRM_ERROR("failed to get gem global name[%s]\n", strerror_r(errno, strErroBuf, 256));
		return ret;
	}

	return arg.name;
}

static int sprd_drm_connector_LCD_create(struct sprd_drm_device * dev, struct sprd_drm_mode_encoder * enc)
{
	struct sprd_drm_mode_connector * conn;

	conn = drmMalloc(sizeof (struct sprd_drm_mode_connector));
	conn->drm_conn.connector_id = sprd_drm_resource_new_id(conn);
	conn->drm_conn.connector_type = DRM_MODE_CONNECTOR_LVDS;
	//TODO:: unknown value;
	conn->drm_conn.connector_type_id = 1;

	conn->dev = dev;

	//attach encoder
	conn->encoder_ids[0] = enc->drm_encoder.encoder_id;

	conn->drm_conn.count_encoders = 1;
	conn->drm_conn.encoder_id = conn->encoder_ids[0];  /**< Current Encoder */
	conn->drm_conn.encoders_ptr = VOID2U64(conn->encoder_ids);

	conn->drm_conn.subpixel = DRM_MODE_SUBPIXEL_NONE;

	DRMLISTADDTAIL(&conn->link, &dev->connector_list);
	dev->num_connector++;

	conn->fb_fd_name = FB_DEV_LCD;

	if (!dev->dpms_prop)
		dev->dpms_prop = sprd_drm_create_dpms_prpoperty(dev);

	sprd_drm_resource_prop_add(conn->drm_conn.connector_id, dev->dpms_prop);

	sprd_drm_mode_connector_update_from_frame_buffer(conn);

	return 0;
}

static int32_t sprd_drm_framebuffer_create(struct sprd_drm_device * dev, struct drm_mode_fb_cmd2 * fb_cmd)
{
	struct sprd_drm_framebuffer * fb = NULL;
	int i;
	int names[4];


	for (i = 0; i < 4; i++) {
		names[i] = sprd_drm_get_name(dev->drm_fd, fb_cmd->handles[i]);
		if (names[i] == 0)
			return -EACCES;
	}

	fb = drmMalloc(sizeof (struct sprd_drm_framebuffer));
	fb->id = sprd_drm_resource_new_id(fb);
	fb->refcount = 1;
	fb->dev = dev;
	DRMLISTADDTAIL(&fb->link, &dev->crtc_list);
	dev->num_fb++;


	fb->width  = fb_cmd->width;
	fb->height = fb_cmd->height;
	fb->pixel_format = fb_cmd->pixel_format;
	fb->flags = fb_cmd->flags;

	sprd_drm_fb_get_bpp_depth(fb_cmd->pixel_format, &fb->depth, &fb->bits_per_pixel);

	memcpy(fb->handles, fb_cmd->handles, 4 * sizeof(fb->handles[0]));
	memcpy(fb->pitches, fb_cmd->pitches, 4 * sizeof(fb->pitches[0]));
	memcpy(fb->offsets, fb_cmd->offsets, 4 * sizeof(fb->offsets[0]));
	memcpy(fb->names,   names,			 4 * sizeof(fb->names[0]));

	//returned value
	fb_cmd->fb_id = fb->id;


	return 0;
}

static int  sprd_drm_framebuffer_remove(struct sprd_drm_framebuffer * fb)
{
	if (!fb) return -EINVAL;

//	if (--fb->refcount) {
//		sprd_drm_resource_del(fb->id);
//		DRMLISTDEL(&fb->link);
//		fb->dev->num_fb--;
//		drmFree(fb);
//	}
	return 0;
}

static struct sprd_drm_mode_encoder * sprd_drm_encoder_create(struct sprd_drm_device * dev, uint32_t encoder_type)
{
	struct sprd_drm_mode_encoder * enc = NULL;

	enc = drmMalloc(sizeof (struct sprd_drm_mode_encoder));
	enc->drm_encoder.encoder_id = sprd_drm_resource_new_id(enc);
	enc->drm_encoder.crtc_id = 1;
	enc->drm_encoder.encoder_type = encoder_type;
	enc->drm_encoder.possible_crtcs = (1 << MAX_CRTC) - 1;  //can be connected with any crct
	enc->drm_encoder.possible_clones = 0x00; //no clones

	DRMLISTADDTAIL(&enc->link, &dev->encoder_list);
	dev->num_encoder++;

	return enc;
}

static struct sprd_drm_mode_crtc * sprd_drm_crtc_create(struct sprd_drm_device * dev)
{
	struct sprd_drm_mode_crtc * crtc;

	crtc = drmMalloc(sizeof (struct sprd_drm_mode_crtc));
	crtc->drm_crtc.crtc_id = sprd_drm_resource_new_id(crtc);
	crtc->drm_crtc.fb_id = 0;

	crtc->drm_crtc.count_connectors = 0;
	crtc->drm_crtc.set_connectors_ptr  = VOID2U64(drmMalloc(MAX_CONNECTOR * sizeof(struct sprd_drm_mode_connector)));

	crtc->drm_crtc.x = 0;
	crtc->drm_crtc.y = 0;

	crtc->drm_crtc.gamma_size = 0;
	crtc->drm_crtc.mode_valid = 0;
	DRMLISTADDTAIL(&crtc->link, &dev->crtc_list);
	dev->num_crtc++;

	return crtc;
}

static void sprd_drm_plane_set_property(struct sprd_drm_property *prop, uint32_t obj_id, uint64_t val)
{
	struct sprd_drm_mode_plane * plane = sprd_drm_resource_get(obj_id);
	if (plane) {
		if (plane->zpos != (uint32_t)val) {
			plane->need_update = 1;
			plane->zpos = (uint32_t)val;
		}
	}
}

static uint64_t sprd_drm_plane_get_property(struct sprd_drm_property *prop, uint32_t obj_id)
{
	struct sprd_drm_mode_plane * plane = sprd_drm_resource_get(obj_id);
	if (plane) {
		return (uint64_t)plane->zpos;
	}
	return DEFAULT_ZPOZ;
}


static struct sprd_drm_property * sprd_drm_create_zpos_prpoperty(struct sprd_drm_device * dev)
{

	struct sprd_drm_property * prop;

	prop = drmMalloc(sizeof (struct sprd_drm_property));
	prop->id = sprd_drm_resource_new_id(prop);
	strcpy(prop->name, "zpos");
	prop->flags = DRM_MODE_PROP_IMMUTABLE | DRM_MODE_PROP_RANGE;
	prop->prop_set = sprd_drm_plane_set_property;
	prop->prop_get = sprd_drm_plane_get_property;

	prop->num_values = 2;
	prop->range_min = 0;
	prop->range_max = MAX_PLANE - 1;

	DRMLISTADDTAIL(&prop->head, &dev->property_list);

	return prop;
}

static struct sprd_drm_mode_plane *  sprd_drm_plane_create(struct sprd_drm_device * dev, unsigned int possible_crtcs)
{
	struct sprd_drm_mode_plane * plane;

	plane = drmMalloc(sizeof (struct sprd_drm_mode_plane));
	plane->drm_plane.plane_id = sprd_drm_resource_new_id(plane);

	plane->drm_plane.crtc_id = 0;
	plane->drm_plane.fb_id = 0;

	plane->drm_plane.possible_crtcs = possible_crtcs;

	//TODO::
	plane->drm_plane.gamma_size = 0;

	plane->drm_plane.count_format_types = sizeof(formats)/sizeof(formats[0]) ;
	plane->drm_plane.format_type_ptr = VOID2U64(formats);

	DRMLISTADDTAIL(&plane->link, &dev->plane_list);

	dev->num_plane++;

	static struct sprd_drm_property *zpos_prop = NULL;
	if (!zpos_prop)
		zpos_prop = sprd_drm_create_zpos_prpoperty(dev);

	sprd_drm_resource_prop_add(plane->drm_plane.plane_id, zpos_prop);

	return plane;
}

static int sprd_drm_connector_disable(struct sprd_drm_mode_connector * conn)
{
	conn->dpms = DRM_MODE_DPMS_OFF;
	if (ioctl(conn->fb_fd, FBIOBLANK, FB_BLANK_POWERDOWN) < 0) {
		SPRD_DRM_ERROR("FB_BLANK_UNBLANK is failed.: %s\n", strerror (errno));
		return -EACCES;
	}
	return 0;
}

static int sprd_drm_connector_enable(struct sprd_drm_mode_connector * conn)
{
	if (conn->dpms != DRM_MODE_DPMS_ON) {
		if (ioctl(conn->fb_fd, FBIOBLANK, FB_BLANK_UNBLANK) < 0) {
			SPRD_DRM_ERROR("FB_BLANK_UNBLANK is failed.: %s\n",
					strerror (errno));
			return -EACCES;
		}
	}

	conn->dpms = DRM_MODE_DPMS_ON;
	return 0;
}

static void sprd_drm_connector_set_property(struct sprd_drm_property *prop, uint32_t obj_id, uint64_t val)
{
	struct sprd_drm_mode_connector * conn = sprd_drm_resource_get(obj_id);
	if (conn) {
		if (strcmp(prop->name, "DPMS") == 0) {
			if (val == DRM_MODE_DPMS_OFF)
				sprd_drm_connector_disable(conn);
			else
				sprd_drm_connector_enable(conn);
		}
	}
}

static uint64_t sprd_drm_connector_get_property(struct sprd_drm_property *prop, uint32_t obj_id)
{
	struct sprd_drm_mode_connector * conn = sprd_drm_resource_get(obj_id);
	if (conn) {
		if (strcmp(prop->name, "DPMS") == 0) {
			return conn->dpms;
		}
	}
	return 0;
}

static struct sprd_drm_prop_enum_list drm_dpms_enum_list[] =
{	{ DRM_MODE_DPMS_ON, "On" },
	{ DRM_MODE_DPMS_STANDBY, "Standby" },
	{ DRM_MODE_DPMS_SUSPEND, "Suspend" },
	{ DRM_MODE_DPMS_OFF, "Off" }
};

struct sprd_drm_property * sprd_drm_create_dpms_prpoperty(struct sprd_drm_device * dev)
{

	struct sprd_drm_property * prop;

	prop = drmMalloc(sizeof (struct sprd_drm_property));
	prop->id = sprd_drm_resource_new_id(prop);
	strcpy(prop->name, "DPMS");
	prop->flags = DRM_MODE_PROP_ENUM | DRM_MODE_PROP_IMMUTABLE;
	prop->prop_set = sprd_drm_connector_set_property;
	prop->prop_get = sprd_drm_connector_get_property;

	prop->num_values = sizeof(drm_dpms_enum_list)/sizeof(struct sprd_drm_prop_enum_list);
	prop->enum_list = drm_dpms_enum_list;

	DRMLISTADDTAIL(&prop->head, &dev->property_list);

	return prop;
}


static int sprd_drm_connector_overlay_set(struct sprd_drm_mode_connector * conn,
		int x, int y, int w, int h, struct sprd_drm_framebuffer *fb, int zpos) {

    overlay_info ovi;
    overlay_display ov_disp;

    ovi.layer_index = ZPOS_TO_LYR_ID(zpos);

	if (fb) {

		sprd_drm_connector_enable(conn);

		//TODO:: support different formats
		ovi.data_type = SPRD_DATA_FORMAT_RGB888;
		ovi.endian.y = SPRD_DATA_ENDIAN_B0B1B2B3;
		ovi.endian.u = SPRD_DATA_ENDIAN_B0B1B2B3;
		ovi.endian.v = SPRD_DATA_ENDIAN_B0B1B2B3;
		ovi.rb_switch = 0;

		ovi.size.hsize = ALIGN(fb->pitches[0],128) / 4;
		ovi.size.vsize = fb->height;

		ovi.rect.x = x;
		ovi.rect.y = y;
		ovi.rect.w = w;
		ovi.rect.h = h;

		ov_disp.display_mode = SPRD_DISPLAY_OVERLAY_ASYNC;
		ov_disp.layer_index = ovi.layer_index;
		if (ovi.layer_index == SPRD_LAYER_OSD) {
			ov_disp.osd_handle = fb->names[0];
		} else {
			ov_disp.img_handle = fb->names[0];
		}

		SPRD_DRM_DEBUG("SPRD_FB_SET_OVERLAY(%d) rect:%dx%d+%d+%d size:%dx%d\n", ovi.layer_index, w, h, x, y, ovi.size.hsize, ovi.size.vsize);
		if (ioctl(conn->fb_fd, SPRD_FB_SET_OVERLAY, &ovi) == -1) {
			SPRD_DRM_ERROR( "error:%s Unable to set overlay: SPRD_FB_SET_OVERLAY\n",
					strerror (errno));
			return -EACCES;
		}

		//commit last setting immediately
		SPRD_DRM_DEBUG("SPRD_FB_DISPLAY_OVERLAY(%d) osd_handle:%d img_handle:%d\n", ov_disp.layer_index, ov_disp.osd_handle, ov_disp.img_handle);
		if (ioctl(conn->fb_fd, SPRD_FB_DISPLAY_OVERLAY, &ov_disp) == -1) {
			SPRD_DRM_ERROR( "error:%s Unable to SPRD_FB_DISPLAY_OVERLAY layer %d\n",
					strerror (errno), ov_disp.layer_index);
			return -EACCES;
		}

		conn->activated_layers |= zpos;
	}
	else {
		return -EINVAL;
	}

	return 0;
}

static int sprd_drm_connector_overlay_unset(struct sprd_drm_mode_connector * conn, int zpos) {

	uint32_t layer = ZPOS_TO_LYR_ID(zpos);
	conn->activated_layers &= ~layer;

	if (ioctl(conn->fb_fd, SPRD_FB_UNSET_OVERLAY, &zpos) == -1) {
		SPRD_DRM_ERROR( "error:%s Unable to SPRD_FB_UNSET_OVERLAY layer %d\n", strerror (errno), zpos);
		return -EACCES;
	}

	return 0;
}


static struct sprd_drm_device * get_sprd_device(int fd)
{
	struct sprd_drm_device * dev;

	DRMLISTFOREACHENTRY(dev, &devices, link) {
		if (dev->drm_fd == fd)
			return dev;
	}

	return NULL;
}

static int sprd_drm_mode_get_resources(int fd, void *arg) {

	struct drm_mode_card_res * res = (struct drm_mode_card_res *)arg;
	struct sprd_drm_framebuffer *fb;
	struct sprd_drm_mode_crtc *crtc;
	struct sprd_drm_mode_connector *conn;
	struct sprd_drm_mode_encoder *enc;

	uint32_t * id_ptr;

	struct sprd_drm_device * dev = get_sprd_device(fd);

	res->count_fbs = dev->num_fb;
	id_ptr = U642INTPTR(res->fb_id_ptr);
	if (res->fb_id_ptr && res->count_fbs >= dev->num_fb) {
		DRMLISTFOREACHENTRY(fb, &dev->fb_list, link) {
			*id_ptr++ = fb->id;
		}
	}

	res->count_crtcs = dev->num_crtc;
	id_ptr = U642INTPTR(res->crtc_id_ptr);
	if (res->crtc_id_ptr) {
		DRMLISTFOREACHENTRY(crtc, &dev->crtc_list, link) {
			*id_ptr++ = crtc->drm_crtc.crtc_id;
		}
	}

	res->count_connectors = dev->num_connector;
	id_ptr = U642INTPTR(res->connector_id_ptr);
	if (res->connector_id_ptr) {
		DRMLISTFOREACHENTRY(conn, &dev->connector_list, link) {
			*id_ptr++  = conn->drm_conn.connector_id;
		}
	}

	res->count_encoders = dev->num_encoder;
	id_ptr = U642INTPTR(res->encoder_id_ptr);
	if (res->encoder_id_ptr) {
		DRMLISTFOREACHENTRY(enc, &dev->encoder_list, link) {
			*id_ptr++ = enc->drm_encoder.encoder_id;
		}
	}

	return 0;
}

static int sprd_drm_mode_get_crtc(int fd, void *arg) {

	struct drm_mode_crtc * out_crtc = (struct drm_mode_crtc *) arg;
	struct sprd_drm_mode_crtc * crtc;

	crtc = (struct sprd_drm_mode_crtc *)sprd_drm_resource_get(out_crtc->crtc_id);

	if (!crtc) return -1;

	memcpy(out_crtc, &crtc->drm_crtc, sizeof(struct drm_mode_crtc));

	return 0;
}

static int sprd_drm_mode_get_obj_properties(int fd, void *arg)
{
	struct drm_mode_obj_get_properties * get_prop = (struct drm_mode_obj_get_properties *)arg;
	struct sprd_drm_property **props = NULL;
	uint32_t count, i;
	uint32_t * out_props_id;
	uint64_t * out_values;

	count = sprd_drm_resource_prop_list_get(get_prop->obj_id, &props);

	if (get_prop->count_props == count) {
		out_props_id = U642INTPTR(get_prop->props_ptr);
		out_values = (uint64_t*) U642VOID(get_prop->prop_values_ptr);
		for (i = 0; i < count; i++) {
			out_props_id[i] = props[i]->id;
			out_values[i] = props[i]->prop_get(props[i], get_prop->obj_id);
		}
	}
	get_prop->count_props = count;

	return 0;
}

static int sprd_drm_mode_get_connector(int fd, void *arg) {

	struct sprd_drm_mode_connector *conn;
	struct drm_mode_get_connector * out_conn = (struct drm_mode_get_connector *) arg;
	uint32_t len;

	conn = (struct sprd_drm_mode_connector *)sprd_drm_resource_get(out_conn->connector_id);

	if (!conn) return -1;


	if(out_conn->encoders_ptr && out_conn->count_encoders == conn->drm_conn.count_encoders) {
		len = sizeof(uint32_t) * out_conn->count_encoders;
		memcpy(U642VOID(out_conn->encoders_ptr), U642VOID(conn->drm_conn.encoders_ptr), len);
	}
	out_conn->count_encoders = conn->drm_conn.count_encoders;

	//return return list of the whole structures not only list of ids
	if(out_conn->modes_ptr && out_conn->count_modes == conn->drm_conn.count_modes) {
		len = sizeof(struct drm_mode_modeinfo) * out_conn->count_modes;
		memcpy(U642VOID(out_conn->modes_ptr), U642VOID(conn->drm_conn.modes_ptr), len);
	}
	out_conn->count_modes = conn->drm_conn.count_modes;

	//get property
	struct drm_mode_obj_get_properties get_prop = {
			.obj_id 		= conn->drm_conn.connector_id,
			.count_props 	= out_conn->count_props,
			.props_ptr 		= out_conn->props_ptr,
			.prop_values_ptr = out_conn->prop_values_ptr,
	};
	sprd_drm_mode_get_obj_properties(fd, &get_prop);
	out_conn->count_props = get_prop.count_props;

	out_conn->encoder_id 		= conn->drm_conn.encoder_id;
	out_conn->connector_id 		= conn->drm_conn.connector_id;
	out_conn->connector_type 	= conn->drm_conn.connector_type;
	out_conn->connector_type_id = conn->drm_conn.connector_type_id;

	out_conn->connection 		= conn->drm_conn.connection;
	out_conn->mm_width 			= conn->drm_conn.mm_width;
	out_conn->mm_height 		= conn->drm_conn.mm_height;
	out_conn->subpixel 			= conn->drm_conn.subpixel;

	return 0;
}

static int sprd_drm_mode_get_plane(int fd, void *arg) {

	struct drm_mode_get_plane * out_plane = (struct drm_mode_get_plane *)arg;
	struct sprd_drm_mode_plane * plane;
	int len;

	plane = (struct sprd_drm_mode_plane *)sprd_drm_resource_get(out_plane->plane_id);

	if (!plane) return -1;

	if(out_plane->format_type_ptr && out_plane->count_format_types == plane->drm_plane.count_format_types) {
		len = sizeof(uint32_t) * out_plane->count_format_types;
		memcpy(U642VOID(out_plane->format_type_ptr), U642VOID(plane->drm_plane.format_type_ptr), len);
	}
	out_plane->count_format_types = plane->drm_plane.count_format_types;

	out_plane->plane_id 		= plane->drm_plane.plane_id;

	out_plane->crtc_id 			= plane->drm_plane.crtc_id;
	out_plane->fb_id 			= plane->drm_plane.fb_id;

	out_plane->possible_crtcs 	= plane->drm_plane.possible_crtcs;
	out_plane->gamma_size 		= plane->drm_plane.gamma_size;

	return 0;
}

static int sprd_drm_mode_get_plane_resources(int fd, void *arg) {

	struct drm_mode_get_plane_res * out_res = (struct drm_mode_get_plane_res *)arg;
	struct sprd_drm_device * dev = get_sprd_device(fd);
	struct sprd_drm_mode_plane *plane;
	uint32_t * id_ptr;

	out_res->count_planes = dev->num_plane;
	id_ptr = U642INTPTR(out_res->plane_id_ptr);
	if (out_res->plane_id_ptr) {
		DRMLISTFOREACHENTRY(plane, &(dev->plane_list), link) {
			*id_ptr++ = plane->drm_plane.plane_id;
		}
	}

	return 0;
}

static int sprd_drm_mode_get_encoder(int fd, void *arg) {

	struct drm_mode_get_encoder * out_enc = (struct drm_mode_get_encoder *)arg;
	struct sprd_drm_mode_encoder * enc;

	enc = (struct sprd_drm_mode_encoder *)sprd_drm_resource_get(out_enc->encoder_id);

	if (!enc) return -1;

	memcpy(out_enc, &enc->drm_encoder, sizeof(struct drm_mode_get_encoder));

	return 0;
}

static int sprd_drm_mode_get_property(int fd, void *arg)
{
	struct drm_mode_get_property * out_resp = arg;
	struct sprd_drm_property *property;
	uint32_t count_values = 0;
	struct drm_mode_property_enum *out_enum_blops;
	uint64_t *out_values;
	uint32_t i;

	property = sprd_drm_resource_get(out_resp->prop_id);

	if (!property)
		return -EINVAL;

	count_values = property->num_values;

	strncpy(out_resp->name, property->name, DRM_PROP_NAME_LEN);
	out_resp->name[DRM_PROP_NAME_LEN-1] = 0;
	out_resp->flags = property->flags;

	out_values = 		(uint64_t *) 						U642INTPTR(out_resp->values_ptr);
	out_enum_blops = 	(struct drm_mode_property_enum *)	U642INTPTR(out_resp->enum_blob_ptr);

	if (property->flags & DRM_MODE_PROP_RANGE){
		if (count_values && out_resp->count_values >= count_values) {
			out_values[0] = property->range_min;
			out_values[1] = property->range_max;
		}
	}
	else if (property->flags & (DRM_MODE_PROP_ENUM | DRM_MODE_PROP_BITMASK)) {
		if (count_values && out_resp->count_values >= count_values
						&& out_resp->count_enum_blobs >= count_values) {
			for (i = 0; i < count_values; i++) {
				out_values[i] 			= property->enum_list[i].type;
				out_enum_blops[i].value = property->enum_list[i].type;
				strcpy(out_enum_blops[i].name, property->enum_list[i].name);
			}
		}
		out_resp->count_enum_blobs = count_values;
	}
	else 	if (property->flags & DRM_MODE_PROP_BLOB) {
		//TODO::
		out_resp->count_enum_blobs = count_values;
	}

	out_resp->count_values = count_values;

	return 0;
}

static int sprd_drm_mode_set_obj_property(int fd, void *arg)
{
	struct drm_mode_obj_set_property *set_prop = (struct drm_mode_obj_set_property *)arg;
	struct sprd_drm_property *prop = NULL;

	prop = sprd_drm_resource_get(set_prop->prop_id);
	if(prop)
		prop->prop_set(prop, set_prop->obj_id, set_prop->value);

	return 0;
}

static int sprd_drm_mode_set_plane(int fd, void *arg)
{
	struct drm_mode_set_plane * plane_cmd = (struct drm_mode_set_plane *) arg;
	struct sprd_drm_mode_plane * plane;
	struct sprd_drm_mode_crtc * crtc;
	struct sprd_drm_framebuffer * fb = NULL, * old_fb = NULL;
	struct sprd_drm_mode_connector * conns[MAX_CONNECTOR] = {NULL,};
	uint32_t i;
	uint32_t * ids;

	plane = sprd_drm_resource_get(plane_cmd->plane_id);
	if (!plane)
		return -EINVAL;

	crtc = sprd_drm_resource_get(plane_cmd->crtc_id);
	if (!crtc)
		return -EINVAL;

	ids = U642INTPTR(crtc->drm_crtc.set_connectors_ptr);
	for (i = 0; i < crtc->drm_crtc.count_connectors; i++) {
		conns[i] = (struct sprd_drm_mode_connector *)sprd_drm_resource_get(ids[i]);
		if (conns[i] == NULL)
			return -EPERM;
	}

	/* No fb means shut it down */
	if (plane_cmd->fb_id == 0 && plane->drm_plane.fb_id) {

		for (i = 0; i < crtc->drm_crtc.count_connectors; i++) {
			sprd_drm_connector_overlay_unset(conns[i], plane->zpos);
		}
		fb = sprd_drm_resource_get(plane->drm_plane.fb_id);
		sprd_drm_framebuffer_remove(fb);
		plane->drm_plane.fb_id = 0;
		plane->drm_plane.crtc_id = 0;
		plane->need_update = 0;
		return 0;
	}

	fb = sprd_drm_resource_get(plane_cmd->fb_id);
	if (!fb)
		return -EINVAL;


	if (plane->drm_plane.fb_id != fb->id ||
			plane->crtc_x != plane_cmd->crtc_x ||
			plane->crtc_y != plane_cmd->crtc_y ||
			plane->src_w  != plane_cmd->src_w ||
			plane->src_h  != plane_cmd->src_h ||
			plane->need_update)
	{
		for (i = 0; i < crtc->drm_crtc.count_connectors; i++) {
			if(sprd_drm_connector_overlay_set(conns[i], plane_cmd->crtc_x, plane_cmd->crtc_y,
					plane_cmd->src_w >> 16, plane_cmd->src_h >> 16, fb, plane->zpos)) {
				goto err;
			}
		}
		plane->need_update = 0;
		if (plane->drm_plane.fb_id != fb->id) {
			old_fb = sprd_drm_resource_get(plane->drm_plane.fb_id);
			sprd_drm_framebuffer_remove(old_fb);
			plane->drm_plane.fb_id = fb->id;
			fb->refcount++;
		}
		plane->crtc_x = plane_cmd->crtc_x;
		plane->crtc_y = plane_cmd->crtc_y;
		plane->src_w  = plane_cmd->src_w;
		plane->src_h  = plane_cmd->src_h;
	}


	if(plane->zpos == DEFAULT_ZPOZ) {
		crtc->is_active = 0;
	}

	plane->drm_plane.crtc_id = plane_cmd->crtc_id;

	return 0;

err:

	if(plane->drm_plane.fb_id) {
		fb = sprd_drm_resource_get(plane->drm_plane.fb_id);
		if (fb) {
			for (i = 0; i < crtc->drm_crtc.count_connectors; i++) {
				sprd_drm_connector_overlay_set(conns[i], plane->crtc_x, plane->crtc_y,
						plane->src_x >> 16, plane->src_y >> 16, fb, plane->zpos);
				plane->need_update = 0;
			}
		}
	}

	return -EINVAL;

}

static int sprd_drm_mode_set_property(int fd, void *arg)
{
	struct drm_mode_connector_set_property *conn_set_prop = (struct drm_mode_connector_set_property *)arg;
	struct drm_mode_obj_set_property obj_set_prop = {
		.value = conn_set_prop->value,
		.prop_id = conn_set_prop->prop_id,
		.obj_id = conn_set_prop->connector_id,
	};

	/* It does all the locking and checking we need */
	return sprd_drm_mode_set_obj_property(fd, &obj_set_prop);

	return -1;
}

static int sprd_drm_mode_set_crtc(int fd, void *arg)
{
	struct drm_mode_crtc * crtc_cmd = (struct drm_mode_crtc *) arg;
	struct sprd_drm_mode_crtc * crtc;
	struct sprd_drm_framebuffer * fb = NULL, * old_fb = NULL;
	struct sprd_drm_device * dev;
	struct sprd_drm_mode_connector * conn;
	struct sprd_drm_mode_connector * conns[MAX_CONNECTOR];
	struct sprd_drm_mode_plane * plane;
	uint32_t i;
	uint32_t * ids;

    if (fd = -1)
		return -EINVAL;

	memset(&conns, 0, sizeof(conns[0]) * MAX_CONNECTOR);

	dev = get_sprd_device(fd);

	crtc = sprd_drm_resource_get(crtc_cmd->crtc_id);
	if (!crtc)
		return -EINVAL;

	if (crtc_cmd->fb_id) {
		fb = sprd_drm_resource_get(crtc_cmd->fb_id);
		if (!fb)
			return -EINVAL;
	}

	if (crtc_cmd->count_connectors > 0 && !crtc_cmd->fb_id) {
		SPRD_DRM_DEBUG("Count connectors is %d but not fb set\n", crtc_cmd->count_connectors);
		return -EINVAL;
	}

	//disable all connectors witch are connected with this crtc
	if (crtc_cmd->fb_id == 0) {
		//get list connector from crtc
		uint32_t * ids = U642INTPTR(crtc->drm_crtc.set_connectors_ptr);

		for (i = 0; i < crtc->drm_crtc.count_connectors; i++ ) {
			conn = (struct sprd_drm_mode_connector *)sprd_drm_resource_get(ids[i]);
			//disable planes
			DRMLISTFOREACHENTRY(plane, &dev->plane_list, link) {
				if (plane->drm_plane.crtc_id == crtc->drm_crtc.crtc_id) {
					struct drm_mode_set_plane plane_cmd = {0,};
					plane_cmd.fb_id = 0;
					plane_cmd.crtc_id = plane->drm_plane.crtc_id;
					plane_cmd.plane_id = plane->drm_plane.plane_id;
					sprd_drm_mode_set_plane(fd, &plane_cmd);
				}
			}

			//disable crtc
			if(crtc->is_active) {
				sprd_drm_connector_overlay_unset(conn, DEFAULT_ZPOZ);
			}

			//disable connector
			sprd_drm_connector_disable(conn);
		}

		//clear crtc
		if(crtc->drm_crtc.fb_id) {
			fb = sprd_drm_resource_get(crtc->drm_crtc.fb_id);
			sprd_drm_framebuffer_remove(fb);
			crtc->drm_crtc.fb_id = 0;
		}
		crtc->drm_crtc.count_connectors = 0;
		memset(U642VOID(crtc->drm_crtc.set_connectors_ptr), 0, MAX_CONNECTOR);
		crtc->drm_crtc.x = 0;
		crtc->drm_crtc.y = 0;
		crtc->drm_crtc.fb_id = 0;
		crtc->is_active = 0;

		return 0;
	}


	if (crtc_cmd->count_connectors > 0 && crtc_cmd->set_connectors_ptr == 0) {
		return -EINVAL;
	}

	if (crtc_cmd->count_connectors > 0) {
		ids = U642INTPTR(crtc_cmd->set_connectors_ptr);
		for (i = 0; i < crtc_cmd->count_connectors; i++) {
			conns[i] = (struct sprd_drm_mode_connector *)sprd_drm_resource_get(ids[i]);
			if (conns[i] == NULL)
				return -EINVAL;
			//TODO:: check new mode for connector
		}

		/**
		 * TODO:: reconfigure mode setting:
		 * 	1) check new mode with all connectors
		 * 	2) check previous pipes (connections crtc and connector)
		 *
		 * 	NOTE: now we jast set new pipas
		 */
		crtc->drm_crtc.count_connectors = crtc_cmd->count_connectors;
		memcpy(U642VOID(crtc->drm_crtc.set_connectors_ptr), ids, crtc_cmd->count_connectors);
	}
	else {
		ids = U642INTPTR(crtc->drm_crtc.set_connectors_ptr);
		for (i = 0; i < crtc->drm_crtc.count_connectors; i++) {
			conns[i] = (struct sprd_drm_mode_connector *)sprd_drm_resource_get(ids[i]);
			if (conns[i] == NULL)
				return -EINVAL;
			//TODO:: check new mode for connector
		}
	}


	//update configure
	for (i = 0; i < crtc->drm_crtc.count_connectors; i++) {

		//TODO:: set new mode for connector

		sprd_drm_connector_enable(conns[i]);

		//reset the plane what uses DEFAULT_ZPOZ
		DRMLISTFOREACHENTRY(plane, &dev->plane_list, link) {
			if (plane->drm_plane.crtc_id == crtc->drm_crtc.crtc_id && plane->zpos == DEFAULT_ZPOZ) {
				old_fb = sprd_drm_resource_get(plane->drm_plane.fb_id);
				sprd_drm_framebuffer_remove(old_fb);
				plane->drm_plane.fb_id = 0;
				plane->drm_plane.crtc_id = 0;
				plane->need_update = 0;
			}
		}

		if (crtc->drm_crtc.fb_id != crtc_cmd->fb_id || !crtc->is_active) {
			if (sprd_drm_connector_overlay_set(conns[i], crtc_cmd->x, crtc_cmd->y, fb->width, fb->height, fb, DEFAULT_ZPOZ) == 0) {
				if(crtc->drm_crtc.fb_id != crtc_cmd->fb_id) {
					old_fb = sprd_drm_resource_get(crtc->drm_crtc.fb_id);
					sprd_drm_framebuffer_remove(old_fb);
					fb->refcount++;
				}
			}
			else {
				goto err;
			}
		}

	}

	//save new settings
	crtc->drm_crtc.x = crtc_cmd->x;
	crtc->drm_crtc.y = crtc_cmd->y;
	crtc->drm_crtc.fb_id = crtc_cmd->fb_id;

	crtc->drm_crtc.mode = crtc_cmd->mode;
	crtc->drm_crtc.mode_valid = 1;
	crtc->is_active = 1;

	return 0;

err:
	//TODO:: restore previous modesetting
	return -EACCES;
}

static int sprd_drm_mode_add_fb(int fd, void *arg)
{
	struct drm_mode_fb_cmd *or = (struct drm_mode_fb_cmd *)arg;
	struct drm_mode_fb_cmd2 r;
	int res;

	memset(&r, 0, sizeof(struct drm_mode_fb_cmd2));

	/* Use new struct with format internally */
	r.width = or->width;
	r.height = or->height;
	r.pitches[0] = or->pitch;
	r.pixel_format = sprd_drm_legacy_fb_format(or->bpp, or->depth);
	r.handles[0] = or->handle;

	res = sprd_drm_framebuffer_create(get_sprd_device(fd), &r);

	//returned value
	or->fb_id = r.fb_id;

	return res;
}

static int sprd_drm_mode_add_fb2(int fd, void *arg)
{
	struct drm_mode_fb_cmd2 *r = (struct drm_mode_fb_cmd2 *)arg;
	int res;

	res = sprd_drm_framebuffer_create(get_sprd_device(fd), r);

	return res;
}

static int sprd_drm_mode_rem_fb(int fd, void *arg)
{
	struct sprd_drm_framebuffer *fb;
	uint32_t id = *((uint32_t *)arg);

	fb = (struct sprd_drm_framebuffer *)sprd_drm_resource_get(id);

	if (!fb) return -EINVAL;

	sprd_drm_framebuffer_remove(fb);

	return 0;
}

static int sprd_drm_mode_get_fb(int fd, void *arg)
{
	struct drm_mode_fb_cmd *fb_cmd = (struct drm_mode_fb_cmd *)arg;
	struct sprd_drm_framebuffer *fb = NULL;

	fb = (struct sprd_drm_framebuffer *)sprd_drm_resource_get(fb_cmd->fb_id);

	if (!fb) return -EINVAL;

	fb_cmd->width 	= fb->width;
	fb_cmd->height 	= fb->height;
	fb_cmd->pitch 	= fb->pitches[0];
	fb_cmd->bpp 	= fb->bits_per_pixel;
	fb_cmd->depth 	= fb->depth;
	fb_cmd->handle 	= fb->handles[0];

	return 0;
}

static int sprd_drm_mode_page_flip(int fd, void *arg)
{
	struct sprd_drm_event_vblank * sprd_event;
	struct sprd_drm_mode_crtc * crtc;
	struct sprd_drm_device * dev;
	drmVBlankPtr vbl;
	struct drm_mode_crtc_page_flip * flip;
	int res;

	flip = (struct drm_mode_crtc_page_flip *)arg;

	dev = get_sprd_device(fd);
	if (!dev)
		return -EINVAL;

	crtc = sprd_drm_resource_get(flip->crtc_id);
	if (!crtc)
		return -EINVAL;

	// TODO:
	// Several page flips simultaneously prohibited.
	if (dev->page_flip_event != NULL)
		return -EBUSY;

	sprd_event = drmMalloc(sizeof (struct sprd_drm_event_vblank));
	sprd_event->type = DRM_EVENT_FLIP_COMPLETE;
	sprd_event->user_data = flip->user_data;

	sprd_event->set_crtc_info.fb_id = flip->fb_id;
	sprd_event->set_crtc_info.crtc_id = flip->crtc_id;
	sprd_event->set_crtc_info.mode_valid = 0;
	sprd_event->set_crtc_info.gamma_size = crtc->drm_crtc.gamma_size;
	sprd_event->set_crtc_info.x = crtc->drm_crtc.x;
	sprd_event->set_crtc_info.y = crtc->drm_crtc.y;
	sprd_event->set_crtc_info.count_connectors = 0;

	sprd_event->vbl.request.signal = VOID2U64(sprd_event);
	sprd_event->vbl.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT;
	sprd_event->vbl.request.sequence = 1;
	sprd_event->vbl.request.type |= DRM_VBLANK_NEXTONMISS;

    //TODO:: request vblank for each connectors(LCD, HDMI, WB) separately
//	sprd_event->vbl.request.type |= DRM_SPRD_CRTC_PRIMARY << DRM_VBLANK_HIGH_CRTC_SHIFT;

	res = ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &sprd_event->vbl);
	if (res != 0) {
		drmFree(sprd_event);
	}
	else {
		dev->page_flip_event = sprd_event;
	}

	return res;
}

static int sprd_drm_wait_vblank(int fd, void *arg)
{
	struct sprd_drm_event_vblank * sprd_event;
	int res;

	drmVBlankPtr vbl = (drmVBlankPtr)arg;

    if (!(vbl->request.type & DRM_VBLANK_EVENT)) {
    	return ioctl(fd, DRM_IOCTL_WAIT_VBLANK, vbl);
    }

	sprd_event = drmMalloc(sizeof (struct sprd_drm_event_vblank));
	sprd_event->type = DRM_EVENT_VBLANK;
	sprd_event->user_data = vbl->request.signal;
	vbl->request.signal = VOID2U64(sprd_event);

	res = ioctl(fd, DRM_IOCTL_WAIT_VBLANK, vbl);

	if (res != 0) {
		drmFree(sprd_event);
	}

	return res;
}

static int sprd_drm_handle_event(int fd, drmEventContextPtr evctx)
{
	char buffer[1024];
	int len, i;
	struct drm_event *e;
	struct drm_event_vblank *vblank;
	struct sprd_drm_event_vblank *sprd_event;
	struct sprd_drm_device * dev;


	dev = get_sprd_device(fd);

	len = read(fd, buffer, sizeof buffer);
	if (len == 0)
		return 0;
	if (len < sizeof *e)
		return -1;

	i = 0;
	while (i < len) {
		e = (struct drm_event *) &buffer[i];
		if (e->type == DRM_EVENT_VBLANK) {
			vblank = (struct drm_event_vblank *) e;
			sprd_event = U642VOID (vblank->user_data);
			if (sprd_event->type == DRM_EVENT_VBLANK) {
				if (evctx->vblank_handler != NULL)
					evctx->vblank_handler(fd,
							vblank->sequence,
							vblank->tv_sec,
							vblank->tv_usec,
							U642VOID (sprd_event->user_data));
			}
			else if (sprd_event->type  == DRM_EVENT_FLIP_COMPLETE) {
				if (dev->page_flip_event == sprd_event) {
					sprd_drm_mode_set_crtc(fd, &sprd_event->set_crtc_info);
					dev->page_flip_event = NULL;
				}
				if (evctx->version >= 2 &&
						evctx->page_flip_handler != NULL)
						evctx->page_flip_handler(fd,
								vblank->sequence,
								vblank->tv_sec,
								vblank->tv_usec,
								U642VOID (sprd_event->user_data));
			}
			drmFree(sprd_event);
		}
		i += e->length;
	}

	return 0;
}

struct _ioctl_hook {
	unsigned long request;
	int (*hook)(int fd, void *arg);
};


struct _ioctl_hook _ioctl_hooks[] = {
		DRM_IOCTL_MODE_GETRESOURCES,		sprd_drm_mode_get_resources,
		DRM_IOCTL_MODE_GETCRTC,				sprd_drm_mode_get_crtc,
		DRM_IOCTL_MODE_GETPLANE,			sprd_drm_mode_get_plane,
		DRM_IOCTL_MODE_GETPLANERESOURCES, 	sprd_drm_mode_get_plane_resources,
		DRM_IOCTL_MODE_GETENCODER,			sprd_drm_mode_get_encoder,
		DRM_IOCTL_MODE_GETCONNECTOR,		sprd_drm_mode_get_connector,
		DRM_IOCTL_MODE_GETPROPERTY,			sprd_drm_mode_get_property,
		DRM_IOCTL_MODE_SETPROPERTY,			sprd_drm_mode_set_property,
		DRM_IOCTL_MODE_OBJ_GETPROPERTIES,	sprd_drm_mode_get_obj_properties,
		DRM_IOCTL_MODE_OBJ_SETPROPERTY,		sprd_drm_mode_set_obj_property,
		DRM_IOCTL_MODE_SETPLANE,			sprd_drm_mode_set_plane,
		DRM_IOCTL_MODE_SETCRTC,				sprd_drm_mode_set_crtc,
		DRM_IOCTL_MODE_ADDFB,				sprd_drm_mode_add_fb,
		DRM_IOCTL_MODE_ADDFB2,				sprd_drm_mode_add_fb2,
		DRM_IOCTL_MODE_RMFB,				sprd_drm_mode_rem_fb,
		DRM_IOCTL_MODE_GETFB,				sprd_drm_mode_get_fb,
		DRM_IOCTL_MODE_PAGE_FLIP,			sprd_drm_mode_page_flip,
		DRM_IOCTL_WAIT_VBLANK,				sprd_drm_wait_vblank
};

static int sprd_ioctl_hook(int fd, unsigned long request, void *arg)
{
    uint32_t i;
    int32_t ret;
	for (i = 0; i < sizeof(_ioctl_hooks)/sizeof (struct _ioctl_hook); i++) {
		if (_ioctl_hooks[i].request == request)
			return _ioctl_hooks[i].hook(fd,arg);
	}

    do {
    	ret = ioctl(fd, request, arg);
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));
	return ret;
}

/*
 * Create sprd drm device object.
 *
 * @fd: file descriptor to sprd drm driver opened.
 *
 * if true, return the device object else NULL.
 */
struct sprd_drm_device * sprd_device_create(int fd)
{
	struct sprd_drm_device *dev;
	struct sprd_drm_mode_encoder *enc;
	uint32_t possible_crtcs;
	int i;

	dev = drmMalloc(sizeof(struct sprd_drm_device));
	if (!dev) {
		fprintf(stderr, "failed to create device[%s].\n",
				strerror(errno));
		return NULL;
	}

	dev->drm_fd = fd;

	drmIoctlSetHook(sprd_ioctl_hook);
	drmHandleEventSetHook(sprd_drm_handle_event);


	DRMINITLISTHEAD(&dev->crtc_list);
	DRMINITLISTHEAD(&dev->encoder_list);
	DRMINITLISTHEAD(&dev->connector_list);
	DRMINITLISTHEAD(&dev->fb_list);
	DRMINITLISTHEAD(&dev->plane_list);
	DRMINITLISTHEAD(&dev->property_list);
	/*
	 * SPRD7730 is enough to have two CRTCs and each crtc would be used
	 * without dependency of hardware.
	 */
	for (i = 0; i < MAX_CRTC; i++) {
		if (!sprd_drm_crtc_create(dev))
			goto err;
	}

	possible_crtcs = (1 << MAX_CRTC) - 1;
	for (i = 0; i < MAX_PLANE; i++) {
		if (!sprd_drm_plane_create(dev, possible_crtcs))
			goto err;
	}


	//LCD encoder + connector
	enc = sprd_drm_encoder_create(dev, DRM_MODE_ENCODER_LVDS);
	sprd_drm_connector_LCD_create(dev, enc);

	//TODO: HDMI encoder + connector

	//TODO: WB encoder + connector

	DRMLISTADDTAIL(&dev->link, &devices);

	return dev;

err:

	return 0;
}


/*
 * Destroy sprd drm device object
 *
 * @dev: sprd drm device object.
 */
void sprd_device_destroy(struct sprd_drm_device *dev)
{

	//clear hooks
	drmIoctlSetHook(NULL);
	drmHandleEventSetHook(NULL);

	//TODO::
	//removing all resource,
	//disabling overlay and connectors

	DRMLISTDEL(&dev->link);

	DRMINITLISTHEAD(&(devices));
	free(dev);
}



