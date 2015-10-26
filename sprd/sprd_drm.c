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
#include <video/sprd_fb.h>

#include "xf86drm.h"
#include "xf86drmMode.h"
#include "drm_fourcc.h"
#include "libdrm_lists.h"

#include "sprd_drmif.h"

#define U642VOID(x) ((void *)(unsigned long)(x))
#define U642INTPTR(x) ( (unsigned int*) U642VOID(x) )
#define VOID2U64(x) ((uint64_t)(unsigned long)(x))

#define SPRD_DEBUG_MSG 0

#ifdef SPRD_DEBUG_MSG
	#define SPRD_DRM_DEBUG(fmt, ...) fprintf(stdout, fmt, ##__VA_ARGS__)
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
#define  DEFAULT_ZPOZ	SPRD_LAYERS_OSD

struct sprd_drm_device;


struct sprd_drm_mode_mode {

	bool b;
	struct drm_mode_modeinfo drm_mode;

	uint32_t mode_id; /**< Id */
};

struct sprd_drm_mode_crtc {
	struct drm_mode_crtc drm_crtc;

	drmMMListHead link;
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

/**
 * sprd_drm_mode_set - new values for a CRTC config change
 * @head: list management
 * @fb: framebuffer to use for new config
 * @crtc: CRTC whose configuration we're about to change
 * @mode: mode timings to use
 * @x: position of this CRTC relative to @fb
 * @y: position of this CRTC relative to @fb
 * @connectors: array of connectors to drive with this CRTC if possible
 * @num_connectors: size of @connectors array
 *
 * Represents a single crtc the connectors that it drives with what mode
 * and from which framebuffer it scans out from.
 *
 * This is used to set modes.
 */
struct sprd_drm_mode_set {
	struct sprd_drm_framebuffer *fb;
	struct sprd_drm_mode_crtc *crtc;
//	struct drm_display_mode *mode;

	uint32_t x;
	uint32_t y;

	struct sprd_drm_mode_connector *connectors[MAX_CONNECTOR];
	uint32_t num_connectors;
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

	uint32_t num_connector;
	drmMMListHead connector_list;
	uint32_t num_encoder;
	drmMMListHead encoder_list;
	uint32_t num_plane;
	drmMMListHead plane_list;

	uint32_t num_crtc;
	drmMMListHead crtc_list;

	drmMMListHead property_list;
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

static int sprd_drm_resource_new_id(void * val)
{
	static int next_id = 0;

	if (!resource_storage)
		resource_storage = drmHashCreate();

	drmHashInsert(resource_storage, ++next_id, val);

	return next_id;
}

static void * sprd_drm_resource_get(int id)
{
	static void *val = NULL;

	if (!resource_storage)
		return NULL;

	drmHashLookup(resource_storage, id, &val);

	return val;
}

static void sprd_drm_resource_del(int id)
{
	if (!resource_storage)
		return ;

	drmHashDelete(resource_storage, id);
}


/*
 * from kernel
 * Original addfb only supported RGB formats, so figure out which one
 */
uint32_t sprd_drm_legacy_fb_format(uint32_t bpp, uint32_t depth)
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
void sprd_drm_fb_get_bpp_depth(uint32_t format, unsigned int *depth, int *bpp)
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
int sprd_drm_get_name(uint32_t fd, uint32_t handle)
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

	//TODO: EDID, PDMS
	conn->drm_conn.count_props = 0;
	conn->drm_conn.props_ptr = 0;
	conn->drm_conn.prop_values_ptr = 0;


	//TODO: get size of screen from FB
	conn->drm_conn.subpixel = DRM_MODE_SUBPIXEL_NONE;

	DRMLISTADDTAIL(&conn->link, &dev->connector_list);
	dev->num_connector++;

	conn->fb_fd_name = FB_DEV_LCD;

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

static void sprd_drm_framebuffer_remove(struct sprd_drm_device * dev, struct sprd_drm_framebuffer * fb)
{
	if (--fb->refcount) {
		sprd_drm_resource_new_id(fb->id);
		DRMLISTDEL(&fb->link);
		dev->num_fb--;
		drmFree(fb);
	}
}

static struct sprd_drm_mode_encoder * sprd_drm_encoder_create(struct sprd_drm_device * dev, uint32_t encoder_type)
{
	struct sprd_drm_mode_encoder * enc = NULL;

	enc = drmMalloc(sizeof (struct sprd_drm_mode_encoder));
	enc->drm_encoder.encoder_id = sprd_drm_resource_new_id(enc);
	enc->drm_encoder.crtc_id = 0;
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

	return plane;
}

static int sprd_drm_connector_modeset(struct sprd_drm_mode_connector * conn,
		struct sprd_drm_mode_set * mode_set, int zpos) {

    struct overlay_setting ovs = {0,};
    struct overlay_display_setting ds = {0,};

    ovs.layer_index = zpos;

	if (mode_set->fb) {
		//TODO:: support different formats
		ovs.data_type = SPRD_DATA_FORMAT_RGB888;
		ovs.y_endian = SPRD_DATA_ENDIAN_B0B1B2B3;
		ovs.uv_endian = SPRD_DATA_ENDIAN_B0B1B2B3;
		ovs.v_endian = SPRD_DATA_ENDIAN_B0B1B2B3;
		ovs.rb_switch = 0;

		//TODO:: I am not sure
		ovs.rect.x = mode_set->x;
		ovs.rect.y = mode_set->y;
		ovs.rect.w = mode_set->fb->width;
		ovs.rect.h = mode_set->fb->height;

		ds.display_mode = SPRD_DISPLAY_OVERLAY_ASYNC;
		ds.layer_index |= ovs.layer_index;
		if (ovs.layer_index == SPRD_LAYERS_OSD) {
			ds.osd_handle = mode_set->fb->names[0];
		} else {
			ds.img_handle = mode_set->fb->names[0];
		}

		if (conn->dpms != DRM_MODE_DPMS_ON) {
			/* lcd on */
			if (ioctl(conn->fb_fd, FBIOBLANK, FB_BLANK_UNBLANK) < 0) {
				SPRD_DRM_ERROR("FB_BLANK_UNBLANK is failed.: %s\n",
						strerror (errno));
				return -EACCES;
			}
		}

		conn->dpms = DRM_MODE_DPMS_ON;

		if (ioctl(conn->fb_fd, SPRD_FB_SET_OVERLAY, &ovs) == -1) {
			SPRD_DRM_ERROR( "error:%s Unable to set overlay: SPRD_FB_SET_OVERLAY\n",
					strerror (errno));
			return -EACCES;
		}

		if (ioctl(conn->fb_fd, SPRD_FB_DISPLAY_OVERLAY, &ds) == -1) {
			SPRD_DRM_ERROR( "error:%s Unable to SPRD_FB_DISPLAY_OVERLAY layer %d\n",
					strerror (errno), ds.layer_index);
			return -EACCES;
		}

		conn->activated_layers |= zpos;
		mode_set->fb->refcount++;
	}
	else {
		conn->activated_layers &= ~zpos;

		//TODO::disable one layer

		if (!conn->activated_layers) {
			conn->dpms = DRM_MODE_DPMS_OFF;
			/* lcd off */
			if (ioctl(conn->fb_fd, FBIOBLANK, FB_BLANK_POWERDOWN) < 0) {
				SPRD_DRM_ERROR("FB_BLANK_UNBLANK is failed.: %s\n", strerror (errno));
				return -EACCES;
			}
		}

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

	if(out_conn->props_ptr && out_conn->count_props == conn->drm_conn.count_props) {
		len = sizeof(uint32_t) * out_conn->count_props;
		memcpy(U642VOID(out_conn->props_ptr), U642VOID(conn->drm_conn.props_ptr), len);
	}

	if(out_conn->prop_values_ptr && out_conn->count_props == conn->drm_conn.count_props) {
		len = sizeof(__u64) * out_conn->count_props;
		memcpy(U642VOID(out_conn->prop_values_ptr), U642VOID(conn->drm_conn.prop_values_ptr), len);

	}

	out_conn->count_props = conn->drm_conn.count_props;

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

static int sprd_drm_mode_get_property(int fd, void *arg) {
	return 0;
}

static int sprd_drm_mode_set_property(int fd, void *arg)
{
	return -1;
}
static int sprd_drm_mode_get_obj_properties(int fd, void *arg)
{
	return -1;
}

static int sprd_drm_mode_set_obj_property(int fd, void *arg)
{
	return -1;
}

static int sprd_drm_mode_set_plane(int fd, void *arg)
{
	return -1;
}

/*
 * We should do following:
	IOCTL(fd,FBIOBLANK)
	IOCTL(fd,SPRD_FB_SET_OVERLAY)
	IOCTL(fd,SPRD_FB_DISPLAY_OVERLAY)
*/
static int sprd_drm_mode_set_crtc(int fd, void *arg)
{
	struct drm_mode_crtc * crtc_cmd = (struct drm_mode_crtc *) arg;
	struct sprd_drm_mode_crtc * crtc;
	struct sprd_drm_framebuffer * fb = NULL;
	struct sprd_drm_mode_set mode_set;
	int i,j;

	memset(&mode_set, 0, sizeof(struct sprd_drm_mode_set));

	crtc = sprd_drm_resource_get(crtc_cmd->crtc_id);
	if (!crtc)
		return -EINVAL;
	mode_set.crtc = crtc;


	if (crtc_cmd->fb_id) {
		mode_set.fb = sprd_drm_resource_get(crtc_cmd->fb_id);
		if (!mode_set.fb)
			return -EINVAL;
	}


	//disable all connectors(and layers) witch are connected with this crtc
	if (crtc_cmd->fb_id == 0 && crtc_cmd->count_connectors == 0) {
		if (crtc->drm_crtc.set_connectors_ptr) {

			int * ids = U642INTPTR(crtc->drm_crtc.set_connectors_ptr);

			for (i = 0; i < crtc->drm_crtc.count_connectors; i++) {
				mode_set.connectors[i] = (struct sprd_drm_mode_connector *)sprd_drm_resource_get(ids[i]);
				if (mode_set.connectors[i])
					mode_set.num_connectors++;
			}
		}
	}
	else if (crtc_cmd->count_connectors > 0) {

		if (crtc_cmd->set_connectors_ptr == 0)
			return -EINVAL;

		int * ids = U642INTPTR(crtc_cmd->set_connectors_ptr);

		for (i = 0; i < crtc_cmd->count_connectors; i++) {
			mode_set.connectors[i] = (struct sprd_drm_mode_connector *)sprd_drm_resource_get(ids[i]);
			if (mode_set.connectors[i] == NULL)
				return -EINVAL;
			mode_set.num_connectors++;
		}
	}

	for (i = 0; i < mode_set.num_connectors; i++) {
		if(sprd_drm_connector_modeset(mode_set.connectors[i], &mode_set, DEFAULT_ZPOZ) == 0) {
			if (mode_set.fb) {
				crtc->drm_crtc.fb_id = mode_set.fb->id;

				//attach connector to crtc
				int * ids = U642INTPTR(crtc->drm_crtc.set_connectors_ptr);
				int new_connector = 1;
				for (j = 0; j < crtc->drm_crtc.count_connectors; j++) {
					if (ids[j] == mode_set.connectors[i]->drm_conn.connector_id)
						new_connector = 0;
				}

				if (new_connector)
					ids[crtc->drm_crtc.count_connectors++] = mode_set.connectors[i]->drm_conn.connector_id;
			}
			else
			{
				//detach fb from crtc
				crtc->drm_crtc.fb_id = 0;

				//detach connector from crtc
				//TODO:: may be more then one connector, so we should use "for"
				crtc->drm_crtc.count_connectors = 0;
			}
		}
	}

	return 0;
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
	struct drm_mode_fb_cmd2 *r = (struct drm_mode_fb_cmd *)arg;
	int res;

	res = sprd_drm_framebuffer_create(get_sprd_device(fd), &r);

	return res;
}

static int sprd_drm_mode_rem_fb(int fd, void *arg)
{
	struct sprd_drm_framebuffer *fb;
	uint32_t id = *((uint32_t *)arg);

	fb = (struct sprd_drm_framebuffer *)sprd_drm_resource_get(id);

	if (!fb) return -EINVAL;

	sprd_drm_framebuffer_remove(get_sprd_device(fd), fb);

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
	fb_cmd->pitch 	= fb->pitches;
	fb_cmd->bpp 	= fb->bits_per_pixel;
	fb_cmd->depth 	= fb->depth;
	fb_cmd->handle 	= fb->handles[0];

	return 0;
}

static int sprd_drm_mode_page_flip(int fd, void *arg)
{
	return -1;
}

static int sprd_drm_wait_vblank(int fd, void *arg)
{
	return -1;
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
		DRM_IOCTL_MODE_GETFB,				sprd_drm_mode_get_fb/*,
		DRM_IOCTL_MODE_PAGE_FLIP,			sprd_drm_mode_page_flip,
		DRM_IOCTL_WAIT_VBLANK,				sprd_drm_wait_vblank
*/
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
	//TODO::

	free(dev);
}



