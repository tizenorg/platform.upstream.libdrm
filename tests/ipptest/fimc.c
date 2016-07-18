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
#include <sys/time.h>
#include <sys/mman.h>

#include "exynos_drm.h"
#include "fimctest.h"
#include "gem.h"
#include "util.h"

#include "drm_fourcc.h"
#include "drm_mode.h"
#include "libkms.h"
#include "internal.h"

void free_resources(struct resources *res)
{
	if (!res)
		return;

#define free_resource(_res, __res, type, Type)					\
	do {									\
		int i;								\
		if (!(_res)->type##s)						\
			break;							\
		for (i = 0; i < (int)(_res)->__res->count_##type##s; ++i) {	\
			if (!(_res)->type##s[i].type)				\
				break;						\
			drmModeFree##Type((_res)->type##s[i].type);		\
		}								\
		free((_res)->type##s);						\
	} while (0)

#define free_properties(_res, __res, type)					\
	do {									\
		int i;								\
		for (i = 0; i < (int)(_res)->__res->count_##type##s; ++i) {	\
			drmModeFreeObjectProperties(res->type##s[i].props);	\
			free(res->type##s[i].props_info);			\
		}								\
	} while (0)

	if (res->res) {
		free_properties(res, res, crtc);

		free_resource(res, res, crtc, Crtc);
		free_resource(res, res, encoder, Encoder);
		free_resource(res, res, connector, Connector);
		free_resource(res, res, fb, FB);

		drmModeFreeResources(res->res);
	}

	if (res->plane_res) {
		free_properties(res, plane_res, plane);

		free_resource(res, plane_res, plane, Plane);

		drmModeFreePlaneResources(res->plane_res);
	}

	free(res);
}

struct resources *get_resources(struct device *dev)
{
	struct resources *res;
	int i;

	res = malloc(sizeof *res);
	if (res == 0)
		return NULL;

	memset(res, 0, sizeof *res);

	res->res = drmModeGetResources(dev->fd);
	if (!res->res) {
		fprintf(stderr, "drmModeGetResources failed: %s\n",
			strerror(errno));
		goto error;
	}

	res->crtcs = malloc(res->res->count_crtcs * sizeof *res->crtcs);
	res->encoders = malloc(res->res->count_encoders * sizeof *res->encoders);
	res->connectors = malloc(res->res->count_connectors * sizeof *res->connectors);
	res->fbs = malloc(res->res->count_fbs * sizeof *res->fbs);

	if (!res->crtcs || !res->encoders || !res->connectors || !res->fbs)
		goto error;

	memset(res->crtcs , 0, res->res->count_crtcs * sizeof *res->crtcs);
	memset(res->encoders, 0, res->res->count_encoders * sizeof *res->encoders);
	memset(res->connectors, 0, res->res->count_connectors * sizeof *res->connectors);
	memset(res->fbs, 0, res->res->count_fbs * sizeof *res->fbs);

#define get_resource(_res, __res, type, Type)					\
	do {									\
		int i;								\
		for (i = 0; i < (int)(_res)->__res->count_##type##s; ++i) {	\
			(_res)->type##s[i].type =				\
				drmModeGet##Type(dev->fd, (_res)->__res->type##s[i]); \
			if (!(_res)->type##s[i].type)				\
				fprintf(stderr, "could not get %s %i: %s\n",	\
					#type, (_res)->__res->type##s[i],	\
					strerror(errno));			\
		}								\
	} while (0)

	get_resource(res, res, crtc, Crtc);
	get_resource(res, res, encoder, Encoder);
	get_resource(res, res, connector, Connector);
	get_resource(res, res, fb, FB);

#define get_properties(_res, __res, type, Type)					\
	do {									\
		int i;								\
		for (i = 0; i < (int)(_res)->__res->count_##type##s; ++i) {	\
			struct type *obj = &res->type##s[i];			\
			unsigned int j;						\
			obj->props =						\
				drmModeObjectGetProperties(dev->fd, obj->type->type##_id, \
							   DRM_MODE_OBJECT_##Type); \
			if (!obj->props) {					\
				fprintf(stderr,					\
					"could not get %s %i properties: %s\n", \
					#type, obj->type->type##_id,		\
					strerror(errno));			\
				continue;					\
			}							\
			obj->props_info = malloc(obj->props->count_props *	\
						 sizeof *obj->props_info);	\
			if (!obj->props_info)					\
				continue;					\
			for (j = 0; j < obj->props->count_props; ++j)		\
				obj->props_info[j] =				\
					drmModeGetProperty(dev->fd, obj->props->props[j]); \
		}								\
	} while (0)

	get_properties(res, res, crtc, CRTC);

	/**
	 * This code is needed because drm_connector function.
	 * It's replace below code.
	 * get_properties(res, res, drm_connector, CONNECTOR);
	 */
	for (i = 0; i < (int)res->res->count_connectors; ++i) {
		struct drm_connector *obj = &res->connectors[i];
		unsigned int j;
		obj->props = drmModeObjectGetProperties(dev->fd,
				obj->connector->connector_id,
				DRM_MODE_OBJECT_CONNECTOR);

		if (!obj->props) {
			fprintf(stderr, "could not get %s %u properties: %s\n",
				"drm_connector", obj->connector->connector_id,
				strerror(errno));
			continue;
		}
			printf("could not get %s %u properties: %s\n",
				"drm_connector", obj->connector->connector_id,
				strerror(errno));

		obj->props_info = malloc(obj->props->count_props *
					sizeof(*obj->props_info));
		if (!obj->props_info)
			continue;

		for (j = 0; j < obj->props->count_props; ++j)
			obj->props_info[j] =
			drmModeGetProperty(dev->fd, obj->props->props[j]);
	}

	for (i = 0; i < res->res->count_crtcs; ++i)
		res->crtcs[i].mode = &res->crtcs[i].crtc->mode;

	res->plane_res = drmModeGetPlaneResources(dev->fd);
	if (!res->plane_res) {
		fprintf(stderr, "drmModeGetPlaneResources failed: %s\n",
			strerror(errno));
		return res;
	}

	res->planes = malloc(res->plane_res->count_planes * sizeof *res->planes);
	if (!res->planes)
		goto error;

	memset(res->planes, 0, res->plane_res->count_planes * sizeof *res->planes);

	get_resource(res, plane_res, plane, Plane);
	get_properties(res, plane_res, plane, PLANE);

	return res;

error:
	free_resources(res);
	return NULL;
}

static int get_crtc_index(struct device *dev, uint32_t id)
{
	int i;

	for (i = 0; i < dev->resources->res->count_crtcs; ++i) {
		drmModeCrtc *crtc = dev->resources->crtcs[i].crtc;
		if (crtc && crtc->crtc_id == id)
			return i;
	}

	return -1;
}

static drmModeConnector *get_connector_by_id(struct device *dev, uint32_t id)
{
	drmModeConnector *connector;
	int i;

	for (i = 0; i < dev->resources->res->count_connectors; i++) {
		connector = dev->resources->connectors[i].connector;
		if (connector && connector->connector_id == id)
			return connector;
	}

	return NULL;
}

static drmModeEncoder *get_encoder_by_id(struct device *dev, uint32_t id)
{
	drmModeEncoder *encoder;
	int i;

	for (i = 0; i < dev->resources->res->count_encoders; i++) {
		encoder = dev->resources->encoders[i].encoder;
		if (encoder && encoder->encoder_id == id)
			return encoder;
	}

	return NULL;
}

static drmModeModeInfo *
connector_find_mode_output(struct device *dev, uint32_t con_id,
			const char *mode_str, const unsigned int vrefresh)
{
	drmModeConnector *connector;
	drmModeModeInfo *mode;
	int i;

	connector = get_connector_by_id(dev, con_id);
	if (!connector || !connector->count_modes)
		return NULL;

	for (i = 0; i < connector->count_modes; i++) {
		mode = &connector->modes[i];
		if (!strcmp(mode->name, mode_str)) {
			/* If the vertical refresh frequency is not specified
			 * then return the first mode that match with the name.
			 * Else, return the mode that match the name and
			 * the specified vertical refresh frequency.
			 */
			if (vrefresh == 0)
				return mode;
			else if (mode->vrefresh == vrefresh)
				return mode;
		}
	}

	return NULL;
}

struct crtc *pipe_find_crtc(struct device *dev, struct pipe_arg *pipe)
{
	uint32_t possible_crtcs = ~0;
	uint32_t active_crtcs = 0;
	unsigned int crtc_idx;
	unsigned int i;
	int j;

	for (i = 0; i < pipe->num_cons; ++i) {
		uint32_t crtcs_for_connector = 0;
		drmModeConnector *connector;
		drmModeEncoder *encoder;
		int idx;

		connector = get_connector_by_id(dev, pipe->con_ids[i]);
		if (!connector)
			return NULL;

		for (j = 0; j < connector->count_encoders; ++j) {
			encoder = get_encoder_by_id(dev, connector->encoders[j]);
			if (!encoder)
				continue;

			crtcs_for_connector |= encoder->possible_crtcs;

			idx = get_crtc_index(dev, encoder->crtc_id);
			if (idx >= 0)
				active_crtcs |= 1 << idx;
		}

		possible_crtcs &= crtcs_for_connector;
	}

	if (!possible_crtcs)
		return NULL;

	/* Return the first possible and active CRTC if one exists, or the first
	 * possible CRTC otherwise.
	 */
	if (possible_crtcs & active_crtcs)
		crtc_idx = ffs(possible_crtcs & active_crtcs);
	else
		crtc_idx = ffs(possible_crtcs);

	return &dev->resources->crtcs[crtc_idx - 1];
}

static int pipe_find_crtc_and_mode(struct device *dev, struct pipe_arg *pipe,
							struct connector *c)
{
	drmModeModeInfo *mode = NULL;
	int i;

	/* init pipe_arg and parse connector by khg */
	pipe->vrefresh = 0;
	pipe->crtc_id = (uint32_t)-1;
	strcpy(pipe->format_str, "XR24");

	/* Count the number of connectors and allocate them. */
	pipe->num_cons = 1;
	pipe->con_ids = malloc(pipe->num_cons * sizeof(*pipe->con_ids));
	if (!pipe->con_ids)
		return -1;

	pipe->con_ids[0] = c->id;
	pipe->crtc_id = c->crtc;

	strcpy(pipe->mode_str, c->mode_str);
	pipe->fourcc = format_fourcc(pipe->format_str);
	if (!pipe->fourcc) {
		fprintf(stderr, "unknown format %s\n", pipe->format_str);
		return -1;
	}

	pipe->mode = NULL;

	for (i = 0; i < (int)pipe->num_cons; i++) {
		mode = connector_find_mode_output(dev, pipe->con_ids[i],
					   pipe->mode_str, pipe->vrefresh);
		if (mode == NULL) {
			fprintf(stderr,
				"failed to find mode \"%s\" for connector %u\n",
				pipe->mode_str, pipe->con_ids[i]);
			return -EINVAL;
		}
	}

	/* If the CRTC ID was specified, get the corresponding CRTC. Otherwise
	 * locate a CRTC that can be attached to all the connectors.
	 */
	if (pipe->crtc_id != (uint32_t)-1) {
		for (i = 0; i < dev->resources->res->count_crtcs; i++) {
			struct crtc *crtc = &dev->resources->crtcs[i];

			if (pipe->crtc_id == crtc->crtc->crtc_id) {
				pipe->crtc = crtc;
				break;
			}
		}
	} else {
		pipe->crtc = pipe_find_crtc(dev, pipe);
	}

	if (!pipe->crtc) {
		fprintf(stderr, "failed to find CRTC for pipe\n");
		return -EINVAL;
	}

	pipe->mode = mode;
	pipe->crtc->mode = mode;

	return 0;
}

static int exynos_drm_ipp_set_property(int fd,
				struct drm_exynos_ipp_property *property,
				struct drm_exynos_sz *def_sz,
				enum drm_exynos_ipp_cmd cmd,
				enum drm_exynos_ipp_cmd_m2m cmd_m2m,
				enum drm_exynos_degree degree)
{
	struct drm_exynos_pos crop_pos = {0, 0, def_sz->hsize, def_sz->vsize};
	struct drm_exynos_pos scale_pos = {0, 0, def_sz->hsize, def_sz->vsize};
	struct drm_exynos_sz src_sz = {def_sz->hsize, def_sz->vsize};
	struct drm_exynos_sz dst_sz = {def_sz->hsize, def_sz->vsize};
	int ret = 0;

	memset(property, 0x00, sizeof(struct drm_exynos_ipp_property));
	property->cmd = cmd;

	switch(cmd) {
	case IPP_CMD_M2M:
		property->config[EXYNOS_DRM_OPS_SRC].ops_id = EXYNOS_DRM_OPS_SRC;
		property->config[EXYNOS_DRM_OPS_SRC].flip = EXYNOS_DRM_FLIP_NONE;
		property->config[EXYNOS_DRM_OPS_SRC].degree = EXYNOS_DRM_DEGREE_0;
		if (cmd_m2m == IPP_CMD_M2M_FILE)
			property->config[EXYNOS_DRM_OPS_SRC].fmt =
							DRM_FORMAT_XRGB8888;
		else
			property->config[EXYNOS_DRM_OPS_SRC].fmt =
							DRM_FORMAT_YUV422;
		property->config[EXYNOS_DRM_OPS_SRC].pos = crop_pos;
		property->config[EXYNOS_DRM_OPS_SRC].sz = src_sz;

		property->config[EXYNOS_DRM_OPS_DST].ops_id = EXYNOS_DRM_OPS_DST;
		property->config[EXYNOS_DRM_OPS_DST].flip = EXYNOS_DRM_FLIP_NONE;
		property->config[EXYNOS_DRM_OPS_DST].degree = degree;
		property->config[EXYNOS_DRM_OPS_DST].fmt = DRM_FORMAT_XRGB8888;
		if (property->config[EXYNOS_DRM_OPS_DST].degree == EXYNOS_DRM_DEGREE_90) {
			dst_sz.hsize = def_sz->vsize;
			dst_sz.vsize = def_sz->hsize;

			scale_pos.w = def_sz->vsize;
			scale_pos.h = def_sz->hsize;
		}
		property->config[EXYNOS_DRM_OPS_DST].pos = scale_pos;
		property->config[EXYNOS_DRM_OPS_DST].sz = dst_sz;
		property->range = COLOR_RANGE_FULL;	/* Wide default */
		break;
	case IPP_CMD_WB:
		property->config[EXYNOS_DRM_OPS_SRC].ops_id = EXYNOS_DRM_OPS_SRC;
		property->config[EXYNOS_DRM_OPS_SRC].flip = EXYNOS_DRM_FLIP_NONE;
		property->config[EXYNOS_DRM_OPS_SRC].degree = EXYNOS_DRM_DEGREE_0;
		property->config[EXYNOS_DRM_OPS_SRC].fmt = DRM_FORMAT_YUV444;
		if (property->config[EXYNOS_DRM_OPS_SRC].degree == EXYNOS_DRM_DEGREE_90) {
			src_sz.hsize = def_sz->vsize;
			src_sz.vsize = def_sz->hsize;

			crop_pos.w = def_sz->vsize;
			crop_pos.h = def_sz->hsize;
		}
		property->config[EXYNOS_DRM_OPS_SRC].pos = crop_pos;
		property->config[EXYNOS_DRM_OPS_SRC].sz = src_sz;

		property->config[EXYNOS_DRM_OPS_DST].ops_id = EXYNOS_DRM_OPS_DST;
		property->config[EXYNOS_DRM_OPS_DST].flip = EXYNOS_DRM_FLIP_NONE;
		property->config[EXYNOS_DRM_OPS_DST].degree = degree;
		property->config[EXYNOS_DRM_OPS_DST].fmt = DRM_FORMAT_XRGB8888;
		if (property->config[EXYNOS_DRM_OPS_DST].degree == EXYNOS_DRM_DEGREE_90) {
			dst_sz.hsize = def_sz->vsize;
			dst_sz.vsize = def_sz->hsize;

			scale_pos.w = def_sz->vsize;
			scale_pos.h = def_sz->hsize;
		}
		property->config[EXYNOS_DRM_OPS_DST].pos = scale_pos;
		property->config[EXYNOS_DRM_OPS_DST].sz = dst_sz;
		property->range = COLOR_RANGE_FULL;	/* Wide default */
		break;
	case IPP_CMD_OUTPUT:
	default:
		ret = -EINVAL;
		return ret;
	}

	ret = ioctl(fd, DRM_IOCTL_EXYNOS_IPP_SET_PROPERTY, property);
	if (ret)
		fprintf(stderr,
			"failed to DRM_IOCTL_EXYNOS_IPP_SET_PROPERTY : %s\n",
			strerror(errno));
	
	printf("DRM_IOCTL_EXYNOS_IPP_SET_PROPERTY : prop_id[%d]\n",
		property->prop_id);

	return ret;
}

static int exynos_drm_ipp_queue_buf(int fd, struct drm_exynos_ipp_queue_buf *qbuf,
					enum drm_exynos_ops_id ops_id,
					enum drm_exynos_ipp_buf_type buf_type,
					int prop_id,
					int buf_id,
					unsigned int gem_handle_y,
					unsigned int gem_handle_u,
					unsigned int gem_handle_v)
{
	int ret = 0;

	memset(qbuf, 0x00, sizeof(struct drm_exynos_ipp_queue_buf));

	qbuf->ops_id = ops_id;
	qbuf->buf_type = buf_type;
	qbuf->user_data = 0;
	qbuf->prop_id = prop_id;
	qbuf->buf_id = buf_id;
	qbuf->handle[EXYNOS_DRM_PLANAR_Y] = gem_handle_y;
	qbuf->handle[EXYNOS_DRM_PLANAR_CB] = gem_handle_u;
	qbuf->handle[EXYNOS_DRM_PLANAR_CR] = gem_handle_v;

	ret = ioctl(fd, DRM_IOCTL_EXYNOS_IPP_QUEUE_BUF, qbuf);
	if (ret)
		fprintf(stderr,
		"failed to DRM_IOCTL_EXYNOS_IPP_QUEUE_BUF[id:%d][buf_type:%d] : %s\n",
		ops_id, buf_type, strerror(errno));

	return ret;
}

static int exynos_drm_ipp_cmd_ctrl(int fd, struct drm_exynos_ipp_cmd_ctrl *cmd_ctrl,
				int prop_id,
				enum drm_exynos_ipp_ctrl ctrl)
{
	int ret = 0;

	memset(cmd_ctrl, 0x00, sizeof(struct drm_exynos_ipp_cmd_ctrl));

	cmd_ctrl->prop_id = prop_id;
	cmd_ctrl->ctrl = ctrl;

	ret = ioctl(fd, DRM_IOCTL_EXYNOS_IPP_CMD_CTRL, cmd_ctrl);
	if (ret)
		fprintf(stderr,
		"failed to DRM_IOCTL_EXYNOS_IPP_CMD_CTRL[prop_id:%d][ctrl:%d] : %s\n",
		prop_id, ctrl, strerror(errno));

	return ret;
}

int fimc_event_handler(struct drm_exynos_ipp_queue_buf *src_qbuf,
	struct drm_exynos_gem_create *src_gem,
	struct drm_exynos_ipp_queue_buf *dst_qbuf,
	struct drm_exynos_gem_create *dst_gem,
	struct drm_exynos_ipp_property *property, void **usr_addr,
	unsigned int width, unsigned int height, enum drm_exynos_ipp_cmd cmd,
	enum drm_exynos_ipp_cmd_m2m cmd_m2m)
{
	char buffer[1024];
	int len, i;
	struct drm_event *e;
	struct drm_exynos_ipp_event *ipp_event;
	char filename[100];
	int ret = 0;
	int src_buf_id, dst_buf_id;
	static bmp_idx = 0;

	len = read(fd, buffer, sizeof buffer);
	if (len == 0)
		return 0;
	if (len < sizeof *e)
		return -1;

	i = 0;
	while (i < len) {
		e = (struct drm_event *) &buffer[i];
		switch (e->type) {
		case DRM_EXYNOS_IPP_EVENT:
			ipp_event = (struct drm_exynos_ipp_event *) e;
			src_buf_id = ipp_event->buf_id[EXYNOS_DRM_OPS_SRC];
			dst_buf_id = ipp_event->buf_id[EXYNOS_DRM_OPS_DST];

			printf("%s:src_buf_id[%d]dst_buf_id[%d]bmp_idx[%d]\n", __func__, src_buf_id, dst_buf_id, bmp_idx++);
			if (cmd == IPP_CMD_M2M) {
				if (cmd_m2m == IPP_CMD_M2M_DISPLAY) {
					ret = exynos_drm_ipp_queue_buf(
						fd, &src_qbuf[src_buf_id],
						EXYNOS_DRM_OPS_SRC,
						IPP_BUF_ENQUEUE,
						property->prop_id,
						src_buf_id,
						src_gem[src_buf_id].handle,
						src_gem[src_buf_id + 1].handle,
						src_gem[src_buf_id + 2].handle);
				} else {
					sprintf(filename, RESULT_PATH "fimc_m2m_dst%d.bmp", bmp_idx);
					util_write_bmp(filename, usr_addr[dst_buf_id], width, height);

					/* For source buffer queue to IPP */
					ret = exynos_drm_ipp_queue_buf(fd, &src_qbuf[src_buf_id], EXYNOS_DRM_OPS_SRC,
							IPP_BUF_ENQUEUE, property->prop_id,
							src_buf_id, src_gem[src_buf_id].handle, 0, 0);
				}
				if (ret) {
					fprintf(stderr, "failed to ipp buf src queue\n");
					goto err_ipp_ctrl_close;
				}

				/* For destination buffer queue to IPP */
				ret = exynos_drm_ipp_queue_buf(fd, &dst_qbuf[dst_buf_id], EXYNOS_DRM_OPS_DST,
							IPP_BUF_ENQUEUE, property->prop_id,
							dst_buf_id, dst_gem[dst_buf_id].handle, 0, 0);
				if (ret) {
					fprintf(stderr, "failed to ipp buf dst queue\n");
					goto err_ipp_ctrl_close;
				}
			} else if (cmd == IPP_CMD_WB) {
				sprintf(filename, RESULT_PATH "fimc_wb_%d.bmp", bmp_idx);
				util_write_bmp(filename, usr_addr[dst_buf_id], width, height);
			
				/* For destination buffer queue to IPP */
				ret = exynos_drm_ipp_queue_buf(fd, &dst_qbuf[dst_buf_id], EXYNOS_DRM_OPS_DST,
							IPP_BUF_ENQUEUE, property->prop_id,
							dst_buf_id, dst_gem[dst_buf_id].handle, 0, 0);
				if (ret) {
					fprintf(stderr, "failed to ipp buf dst queue\n");
					goto err_ipp_ctrl_close;
				}
			}
			break;
		default:
			break;
		}
		i += e->length;
	}

err_ipp_ctrl_close:
	return ret;
}

void fimc_m2m_set_mode(struct device *dev, struct connector *c, int count,
	enum drm_exynos_degree degree, enum drm_exynos_ipp_cmd_m2m display,
	long int *usec)
{
	struct drm_exynos_ipp_property property;
	struct drm_exynos_ipp_cmd_ctrl cmd_ctrl;
	struct drm_exynos_sz def_sz;
	struct drm_exynos_ipp_queue_buf qbuf1[MAX_BUF], qbuf2[MAX_BUF];
	struct drm_exynos_gem_create gem1[MAX_BUF], gem2[MAX_BUF];
	struct exynos_gem_mmap_data mmap1[MAX_BUF], mmap2[MAX_BUF];
	void *usr_addr1[MAX_BUF], *usr_addr2[MAX_BUF];
	struct drm_gem_close args;
	uint32_t handles[4], pitches[4], offsets[4] = {0};
	unsigned int fb_id_dst;
	struct kms_bo *bo_src[MAX_BUF], *bo_dst;
	struct pipe_arg pipe;
	int ret, i, j;

	struct timeval begin, end;
	char filename[80];

	dev->mode.width = 0;
	dev->mode.height = 0;

	/* For crtc and mode */
	for (i = 0; i < count; i++) {
		ret = pipe_find_crtc_and_mode(dev, &pipe, &c[i]);
		if (ret < 0)
			continue;

		dev->mode.width += pipe.mode->hdisplay;
		if (dev->mode.height < pipe.mode->vdisplay)
			dev->mode.height = pipe.mode->vdisplay;
	}

	/* For source buffer */
	for (i = 0; i < MAX_BUF; i++) {
		bo_src[i] = util_kms_gem_create_mmap(dev->kms, pipe.fourcc,
				dev->mode.width, dev->mode.height,
				handles, pitches, offsets);
		if (!bo_src[i])
			goto err_ipp_src_buff_close;

		mmap1[i].size = gem1[i].size = bo_src[i]->size;
		mmap1[i].offset = gem1[i].flags = 0;
		mmap1[i].handle = gem1[i].handle = bo_src[i]->handle;
		usr_addr1[i] = mmap1[i].addr = bo_src[i]->ptr;
	}

	/* Create test Image
	 * Display is YUV422 format, file is RGB888 format
	 */
	if (display == IPP_CMD_M2M_DISPLAY)
		util_draw_buffer_yuv(usr_addr1,
				dev->mode.width, dev->mode.height);
	else
		util_draw_buffer(usr_addr1[0], 1, dev->mode.width,
				dev->mode.height, dev->mode.width * 4, 0);

	/*For destination buffer */
	bo_dst = util_kms_gem_create_mmap(dev->kms, pipe.fourcc,
			dev->mode.width, dev->mode.height,
			handles, pitches, offsets);
	if (!bo_dst)
		goto err_ipp_src_buff_close;

	mmap2[0].size = gem2[0].size = bo_dst->size;
	mmap2[0].offset = gem2[0].flags = 0;
	mmap2[0].handle = gem2[0].handle = bo_dst->handle;
	usr_addr2[0] = mmap2[0].addr = bo_dst->ptr;

	def_sz.hsize = dev->mode.width;
	def_sz.vsize = dev->mode.height;

	/* For property */
	if (display == IPP_CMD_M2M_DISPLAY)
		ret = exynos_drm_ipp_set_property(dev->fd, &property,
				&def_sz, IPP_CMD_M2M, IPP_CMD_M2M_DISPLAY,
				EXYNOS_DRM_DEGREE_0);
	else
		ret = exynos_drm_ipp_set_property(dev->fd, &property,
				&def_sz, IPP_CMD_M2M, IPP_CMD_M2M_FILE, degree);
	if (ret) {
		fprintf(stderr, "failed to ipp property\n");
		goto err_ipp_dst_buff_close;
	}

	/* For source buffer map to IPP */
	if (display == IPP_CMD_M2M_DISPLAY)
		ret = exynos_drm_ipp_queue_buf(dev->fd, &qbuf1[0],
				EXYNOS_DRM_OPS_SRC, IPP_BUF_ENQUEUE,
				property.prop_id, 0, gem1[0].handle,
				gem1[1].handle, gem1[2].handle);
	else
		ret = exynos_drm_ipp_queue_buf(dev->fd, &qbuf1[0],
				EXYNOS_DRM_OPS_SRC, IPP_BUF_ENQUEUE,
				property.prop_id, 0, gem1[0].handle, 0, 0);
	if (ret) {
		fprintf(stderr, "failed to ipp buf src map\n");
		goto err_ipp_quque_close;
	}

	/* For destination buffer map to IPP */
	ret = exynos_drm_ipp_queue_buf(dev->fd, &qbuf2[0],
			EXYNOS_DRM_OPS_DST, IPP_BUF_ENQUEUE,
			property.prop_id, 0, gem2[0].handle, 0, 0);
	if (ret) {
		fprintf(stderr, "failed to ipp buf dst map\n");
		goto err_ipp_quque_close;
	}

	/* Start */
	ret = exynos_drm_ipp_cmd_ctrl(dev->fd, &cmd_ctrl,
			property.prop_id, IPP_CTRL_PLAY);
	if (ret) {
		fprintf(stderr,
				"failed to ipp ctrl IPP_CMD_M2M start\n");
		goto err_ipp_quque_close;
	}
	gettimeofday(&begin, NULL);

	/* Display OR File */
	switch (display) {
	case IPP_CMD_M2M_FILE:
		/* For src image write file */
		sprintf(filename, RESULT_PATH "fimc_m2m_org_src.bmp");
		util_write_bmp(filename, usr_addr1[0],
				dev->mode.width, dev->mode.height);

		j = 0;
		while (1) {
			struct timeval timeout = {.tv_sec = 3, .tv_usec = 0};
			fd_set fds;

			FD_ZERO(&fds);
			FD_SET(0, &fds);
			FD_SET(dev->fd, &fds);
			ret = select(dev->fd + 1, &fds, NULL, NULL, &timeout);
			if (ret <= 0) {
				fprintf(stderr, "select timed out or error.\n");
				continue;
			} else if (FD_ISSET(0, &fds)) {
				break;
			}

			gettimeofday(&end, NULL);
			usec[j] = (end.tv_sec - begin.tv_sec) * 1000000 +
					(end.tv_usec - begin.tv_usec);

			if (property.config[EXYNOS_DRM_OPS_DST].degree ==
				EXYNOS_DRM_DEGREE_90 ||
				property.config[EXYNOS_DRM_OPS_DST].degree ==
				EXYNOS_DRM_DEGREE_270) {
				if(fimc_event_handler(qbuf1, gem1, qbuf2, gem2,
					&property, usr_addr2, dev->mode.height,
					dev->mode.width, IPP_CMD_M2M,
					IPP_CMD_M2M_FILE) < 0)
					break;
			} else {
				if(fimc_event_handler(qbuf1, gem1, qbuf2, gem2,
					&property, usr_addr2, dev->mode.width,
					dev->mode.height, IPP_CMD_M2M,
					IPP_CMD_M2M_FILE) < 0)
					break;
			}

			if (++j > MAX_LOOP)
				break;

			gettimeofday(&begin, NULL);
		}
		break;
	case IPP_CMD_M2M_DISPLAY:
		/* Add fb2 dst */
		ret = drmModeAddFB2(dev->fd, dev->mode.width, dev->mode.height,
				pipe.fourcc, handles, pitches,
				offsets, &fb_id_dst, 0);
		if (ret) {
			fprintf(stderr, "failed to add fb (%ux%u):%s\n",
					dev->mode.width, dev->mode.height,
					strerror(errno));
			goto err_ipp_quque_close;
		}

		j = 0;
		while (1) {
			struct timeval timeout = {.tv_sec = 3, .tv_usec = 0};
			fd_set fds;

			FD_ZERO(&fds);
			FD_SET(0, &fds);
			FD_SET(dev->fd, &fds);
			ret = select(dev->fd + 1, &fds, NULL, NULL, &timeout);
			if (ret <= 0) {
				fprintf(stderr, "select timed out or error.\n");
				continue;
			} else if (FD_ISSET(0, &fds)) {
				goto err_ipp_quque_close;
			}

			/* Set Flip */
			ret = drmModePageFlip(dev->fd, pipe.crtc->crtc->crtc_id,
					fb_id_dst, DRM_MODE_PAGE_FLIP_EVENT,
					&pipe);
			if (ret) {
				fprintf(stderr, "failed to page flip: %s\n",
						strerror(errno));
				goto err_ipp_quque_close;
			}

			gettimeofday(&end, NULL);
			usec[j] = (end.tv_sec - begin.tv_sec) * 1000000 +
				(end.tv_usec - begin.tv_usec);

			getchar();

			/* For property */
			if (j == 0) {
			ret = exynos_drm_ipp_set_property(dev->fd, &property,
				&def_sz, IPP_CMD_M2M, IPP_CMD_M2M_DISPLAY,
				degree);
			} else {
			ret = exynos_drm_ipp_set_property(dev->fd, &property,
				&def_sz, IPP_CMD_M2M, IPP_CMD_M2M_DISPLAY,
				EXYNOS_DRM_DEGREE_0);
			}
			if (ret) {
				fprintf(stderr, "failed to ipp property\n");
				break;
			}

			if(fimc_event_handler(qbuf1, gem1, qbuf2, gem2,
					&property, usr_addr2, dev->mode.width,
					dev->mode.height, IPP_CMD_M2M,
					IPP_CMD_M2M_DISPLAY) < 0)
				break;

			/* Start */
			ret = exynos_drm_ipp_cmd_ctrl(dev->fd, &cmd_ctrl,
					property.prop_id, IPP_CTRL_PLAY);
			if (ret) {
				fprintf(stderr,
				"failed to ipp ctrl IPP_CMD_M2M start\n");
				break;
			}

			if (++j > 1)
				break;

			gettimeofday(&begin, NULL);
		}
		break;
	}

err_ipp_quque_close:
	for (i = 1; i <= property.prop_id; i++) {
		/* For destination buffer dequeue to IPP */
		ret = exynos_drm_ipp_queue_buf(dev->fd, &qbuf2[0],
				EXYNOS_DRM_OPS_DST, IPP_BUF_DEQUEUE,
				i, 0, gem2[0].handle, 0, 0);
		if (ret < 0)
			fprintf(stderr, "failed to ipp buf dst dequeue\n");

		/* For source buffer dequeue to IPP */
		ret = exynos_drm_ipp_queue_buf(dev->fd, &qbuf1[0],
				EXYNOS_DRM_OPS_SRC, IPP_BUF_DEQUEUE, i, 0,
				gem1[0].handle, gem1[1].handle, gem1[2].handle);
		if (ret < 0)
			fprintf(stderr, "failed to ipp buf dst dequeue\n");

		/* Stop */
		ret = exynos_drm_ipp_cmd_ctrl(dev->fd, &cmd_ctrl,
						i, IPP_CTRL_STOP);
		if (ret)
			fprintf(stderr,
				"failed to ipp ctrl IPP_CMD_M2M stop\n");
	}
err_ipp_dst_buff_close:
	/* Close destination buffer */
	munmap(usr_addr2[0], mmap2[0].size);
	memset(&args, 0x00, sizeof(struct drm_gem_close));
	args.handle = gem2[0].handle;
	exynos_gem_close(dev->fd, &args);
	kms_bo_destroy(&bo_dst);
err_ipp_src_buff_close:
	/* Close source buffer */
	for (i = 0; i < MAX_BUF; i++) {
		munmap(usr_addr1[i], mmap1[i].size);
		memset(&args, 0, sizeof(struct drm_gem_close));
		args.handle = gem1[i].handle;
		exynos_gem_close(dev->fd, &args);
		kms_bo_destroy(&bo_src[i]);
	}
}

void fimc_wb_set_mode(struct connector *c, int count, int page_flip,
								long int *usec)
{
	struct drm_exynos_pos def_pos = {0, 0, 720, 1280};
	struct drm_exynos_sz def_sz = {720, 1280};
	struct drm_exynos_ipp_property property;
	struct drm_exynos_gem_create gem[MAX_BUF];
	struct exynos_gem_mmap_data mmap[MAX_BUF];
	struct drm_exynos_ipp_queue_buf qbuf[MAX_BUF];	
	void *usr_addr[MAX_BUF];
	struct drm_exynos_ipp_cmd_ctrl cmd_ctrl;
	unsigned int width, height, stride;
	int ret, i, j;
	struct timeval begin, end;
	struct drm_gem_close args;

	/* For mode */
	width = height = 0;
	for (i = 0; i < count; i++) {
		connector_find_mode(&c[i]);
		if (c[i].mode == NULL) continue;
		width += c[i].mode->hdisplay;
		if (height < c[i].mode->vdisplay) height = c[i].mode->vdisplay;
	}
	stride = width * 4;

	def_sz.hsize = width;
	def_sz.vsize = height;

	/* For property */
	ret = exynos_drm_ipp_set_property(fd, &property, &def_sz,
				IPP_CMD_WB, IPP_CMD_M2M_NONE, EXYNOS_DRM_DEGREE_0);
	printf("property: %d\n", ret);
	if (ret) {
		fprintf(stderr, "failed to ipp property\n");
		return;
	}

	/* For destination buffer */
	for (i = 0; i < MAX_BUF; i++) {
		ret = util_gem_create_mmap(fd, &gem[i], &mmap[i], stride * height);
		if (ret) {
			fprintf(stderr, "failed to gem create mmap: %s\n",
								strerror(errno));
			if (ret == -1) return;
			else if (ret == -2) goto err_ipp_ctrl_close;
		}
		usr_addr[i] = mmap[i].addr;
		/* For destination buffer map to IPP */
		ret = exynos_drm_ipp_queue_buf(fd, &qbuf[i], EXYNOS_DRM_OPS_DST,
					IPP_BUF_ENQUEUE, property.prop_id, i,
					gem[i].handle, 0, 0);
		if (ret) {
			fprintf(stderr, "failed to ipp buf dst map\n");
			goto err_ipp_ctrl_close;
		}
	}

	/* Start */
	gettimeofday(&begin, NULL);
	ret = exynos_drm_ipp_cmd_ctrl(fd, &cmd_ctrl, property.prop_id, IPP_CTRL_PLAY);
	if (ret) {
		fprintf(stderr,
			"failed to ipp ctrl IPP_CMD_WB start\n");
		goto err_ipp_ctrl_close;
	}

	j = 0;
	while (1) {
		struct timeval timeout = { .tv_sec = 3, .tv_usec = 0 };
		fd_set fds;

		FD_ZERO(&fds);
		FD_SET(0, &fds);
		FD_SET(fd, &fds);
		ret = select(fd + 1, &fds, NULL, NULL, &timeout);
		if (ret <= 0) {
			fprintf(stderr, "select timed out or error.\n");
			continue;
		} else if (FD_ISSET(0, &fds)) {
			fprintf(stderr, "select error.\n");
			break;
		}

		gettimeofday(&end, NULL);
		usec[j] = (end.tv_sec - begin.tv_sec) * 1000000 +
					(end.tv_usec - begin.tv_usec);

		if (property.config[EXYNOS_DRM_OPS_DST].degree == EXYNOS_DRM_DEGREE_90 ||
			property.config[EXYNOS_DRM_OPS_DST].degree == EXYNOS_DRM_DEGREE_270 ||
			property.config[EXYNOS_DRM_OPS_SRC].degree == EXYNOS_DRM_DEGREE_90 ||
			property.config[EXYNOS_DRM_OPS_SRC].degree == EXYNOS_DRM_DEGREE_270) {
			if(fimc_event_handler(NULL, NULL, qbuf, gem, &property, usr_addr, height, width, IPP_CMD_WB, IPP_CMD_M2M_NONE) < 0)
				break;
		} else {
			if(fimc_event_handler(NULL, NULL, qbuf, gem, &property, usr_addr, width, height, IPP_CMD_WB, IPP_CMD_M2M_NONE) < 0)
				break;
		}

		if (++j > MAX_LOOP)
			break;

		if (j == HALF_LOOP) {
			/* Stop */
			ret = exynos_drm_ipp_cmd_ctrl(fd, &cmd_ctrl, property.prop_id, IPP_CTRL_STOP);
			if (ret) {
				fprintf(stderr, "failed to ipp ctrl IPP_CMD_WB stop\n");
				goto err_ipp_ctrl_close;
			}

			/* For property */
			ret = exynos_drm_ipp_set_property(fd, &property,
				&def_sz, IPP_CMD_WB, IPP_CMD_M2M_NONE, EXYNOS_DRM_DEGREE_90);
			if (ret) {
				fprintf(stderr, "failed to ipp property\n");
				goto err_ipp_ctrl_close;
			}

			/* For destination buffer */
			for (i = 0; i < MAX_BUF; i++) {
				/* For destination buffer map to IPP */
				ret = exynos_drm_ipp_queue_buf(fd, &qbuf[i], EXYNOS_DRM_OPS_DST,
							IPP_BUF_ENQUEUE, property.prop_id, i,
							gem[i].handle, 0, 0);
				if (ret) {
					fprintf(stderr, "failed to ipp buf dst map\n");
					goto err_ipp_ctrl_close;
				}
			}

			/* Start */
			ret = exynos_drm_ipp_cmd_ctrl(fd, &cmd_ctrl, property.prop_id, IPP_CTRL_PLAY);
			if (ret) {
				fprintf(stderr,
					"failed to ipp ctrl IPP_CMD_WB start\n");
				goto err_ipp_ctrl_close;
			}
		}

		gettimeofday(&begin, NULL);
	}

err_ipp_ctrl_close:
	/* For destination buffer dequeue to IPP */
	for (i = 0; i < MAX_BUF; i++) {
		ret = exynos_drm_ipp_queue_buf(fd, &qbuf[i], EXYNOS_DRM_OPS_DST,
					IPP_BUF_DEQUEUE, property.prop_id, i,
					gem[i].handle, 0, 0);
		if (ret < 0)
			fprintf(stderr, "failed to ipp buf dst dequeue\n");
	}

	/* Stop */
	ret = exynos_drm_ipp_cmd_ctrl(fd, &cmd_ctrl, property.prop_id, IPP_CTRL_STOP);
	if (ret)
		fprintf(stderr, "failed to ipp ctrl IPP_CMD_WB stop\n");

	for (i = 0; i < MAX_BUF; i++) {
		munmap(usr_addr[i], mmap[i].size);
		memset(&args, 0x00, sizeof(struct drm_gem_close));
		args.handle = gem[i].handle;
		exynos_gem_close(fd, &args);
	}

	return;
}

void fimc_output_set_mode(struct connector *c, int count, int page_flip,
								long int *usec)
{
	fprintf(stderr, "not supported. please wait v2\n");
}
