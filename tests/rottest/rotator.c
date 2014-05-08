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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/mman.h>

#include "exynos_drm.h"
#include "rottest.h"
#include "gem.h"
#include "util.h"

#include "drm_fourcc.h"

static int exynos_drm_ipp_set_property(int fd,
				struct drm_exynos_ipp_property *property,
				struct drm_exynos_pos *src_pos,
				struct drm_exynos_sz *src_sz,
				struct drm_exynos_pos *dst_pos,
				struct drm_exynos_sz *dst_sz)
{
	int ret;

	memset(property, 0x00, sizeof(struct drm_exynos_ipp_property));

	property->cmd = IPP_CMD_M2M;

	property->config[EXYNOS_DRM_OPS_SRC].ops_id = EXYNOS_DRM_OPS_SRC;
	property->config[EXYNOS_DRM_OPS_SRC].flip = EXYNOS_DRM_FLIP_NONE;
	property->config[EXYNOS_DRM_OPS_SRC].degree = EXYNOS_DRM_DEGREE_0;
	property->config[EXYNOS_DRM_OPS_SRC].fmt = DRM_FORMAT_XRGB8888;
	property->config[EXYNOS_DRM_OPS_SRC].pos = *src_pos;
	property->config[EXYNOS_DRM_OPS_SRC].sz = *src_sz;

	property->config[EXYNOS_DRM_OPS_DST].ops_id = EXYNOS_DRM_OPS_DST;
	property->config[EXYNOS_DRM_OPS_DST].flip = EXYNOS_DRM_FLIP_NONE;
	property->config[EXYNOS_DRM_OPS_DST].degree = EXYNOS_DRM_DEGREE_90;
	property->config[EXYNOS_DRM_OPS_DST].fmt = DRM_FORMAT_XRGB8888;
	property->config[EXYNOS_DRM_OPS_DST].pos = *dst_pos;
	property->config[EXYNOS_DRM_OPS_DST].sz = *dst_sz;

	ret = ioctl(fd, DRM_IOCTL_EXYNOS_IPP_SET_PROPERTY, property);
	if (ret)
		fprintf(stderr,
			"failed to DRM_IOCTL_EXYNOS_IPP_SET_PROPERTY : %s\n",
			strerror(errno));

	return ret;
}

static int exynos_drm_ipp_queue_buf(int fd, int prop_id,
					struct drm_exynos_ipp_queue_buf *qbuf,
					enum drm_exynos_ops_id ops_id,
					enum drm_exynos_ipp_buf_type buf_type,
					unsigned int buf_id,
					unsigned int gem_handle)
{
	int ret;

	memset(qbuf, 0x00, sizeof(struct drm_exynos_ipp_queue_buf));

	qbuf->ops_id = ops_id;
	qbuf->buf_type = buf_type;
	qbuf->user_data = 0;
	qbuf->prop_id = prop_id;
	qbuf->buf_id = buf_id;
	qbuf->handle[EXYNOS_DRM_PLANAR_Y] = gem_handle;
	qbuf->handle[EXYNOS_DRM_PLANAR_CB] = 0;
	qbuf->handle[EXYNOS_DRM_PLANAR_CR] = 0;

	ret = ioctl(fd, DRM_IOCTL_EXYNOS_IPP_QUEUE_BUF, qbuf);
	if (ret)
		fprintf(stderr,
			"failed to DRM_IOCTL_EXYNOS_IPP_QUEUE_BUF[prop_id:%d]"\
			"[ops_id:%d][buf_id:%d][buf_type:%d] : %s\n",
			prop_id, ops_id, buf_id, buf_type, strerror(errno));
 
	return ret;
}

static int exynos_drm_ipp_cmd_ctrl(int fd, int prop_id,
				struct drm_exynos_ipp_cmd_ctrl *cmd_ctrl,
				enum drm_exynos_ipp_ctrl ctrl)
{
	int ret;

	memset(cmd_ctrl, 0x00, sizeof(struct drm_exynos_ipp_cmd_ctrl));

	cmd_ctrl->prop_id = prop_id;
	cmd_ctrl->ctrl = ctrl;

	ret = ioctl(fd, DRM_IOCTL_EXYNOS_IPP_CMD_CTRL, cmd_ctrl);
	if (ret)
		fprintf(stderr,
			"failed to DRM_IOCTL_EXYNOS_IPP_CMD_CTRL[prop_id:%d]"\
			"[ctrl:%d] : %s\n", prop_id, ctrl, strerror(errno));

	return ret;
}

static int rotator_set_mode_property(struct connector *c, int count,
				unsigned int *width, unsigned int *height,
				unsigned int *stride,
				struct drm_exynos_ipp_property *property)
{
	int ret, i;
	struct drm_exynos_pos src_pos = {0, 0, 720, 1280};
	struct drm_exynos_sz src_sz = {720, 1280};
	struct drm_exynos_pos dst_pos = {0, 0, 1280, 720};
	struct drm_exynos_sz dst_sz = {1280, 720};

	*width = 0;
	*height = 0;

	/* For mode */
	for (i = 0; i < count; i++) {
		connector_find_mode(&c[i]);
		if (c[i].mode == NULL)
			continue;

		*width += c[i].mode->hdisplay;
		if (*height < c[i].mode->vdisplay)
			*height = c[i].mode->vdisplay;
	}

	*stride = *width * 4;

	/* For IPP setting property */
	ret = exynos_drm_ipp_set_property(fd, property, &src_pos, &src_sz,
							&dst_pos, &dst_sz);
	if (ret)
		fprintf(stderr, "failed to ipp set property: %s\n",
							strerror(errno));

	return ret;
}

static int rotator_event_handler(int fd, int idx, int prop_id,
				struct drm_exynos_ipp_queue_buf *src_qbuf,
				struct drm_exynos_gem_create *src_gem,
				struct drm_exynos_ipp_queue_buf *dst_qbuf,
				struct drm_exynos_gem_create *dst_gem,
				void **usr_addr, unsigned int width,
				unsigned int height, struct timeval *begin)
{
	char buffer[1024];
	int ret = 0, len, i;
	struct drm_event *e;
	struct drm_exynos_ipp_event *ipp_event;
	int src_buf_id, dst_buf_id;
	char filename[100];

	len = read(fd, buffer, sizeof(buffer));
	if (!len)
		return 0;
	if (len < sizeof (*e))
		return -1;

	i = 0;
	while (i < len) {
		e = (struct drm_event *)&buffer[i];
		switch (e->type) {
		case DRM_EXYNOS_IPP_EVENT:
			ipp_event = (struct drm_exynos_ipp_event *)e;
			src_buf_id = ipp_event->buf_id[EXYNOS_DRM_OPS_SRC];
			dst_buf_id = ipp_event->buf_id[EXYNOS_DRM_OPS_DST];

			sprintf(filename, "/opt/media/rot_%d_%d.bmp", idx,
								dst_buf_id);
			util_write_bmp(filename, usr_addr[dst_buf_id], width,
									height);

			gettimeofday(begin, NULL);
			ret = exynos_drm_ipp_queue_buf(fd, prop_id, src_qbuf,
							EXYNOS_DRM_OPS_SRC,
							IPP_BUF_ENQUEUE,
							src_buf_id,
							src_gem->handle);
			if (ret)
				fprintf(stderr, "failed to queue src_buf\n");

			ret = exynos_drm_ipp_queue_buf(fd, prop_id,
						&dst_qbuf[dst_buf_id],
						EXYNOS_DRM_OPS_DST,
						IPP_BUF_ENQUEUE, dst_buf_id,
						dst_gem[dst_buf_id].handle);
			if (ret)
				fprintf(stderr, "failed to queue dst_buf\n");
			break;
		default:
			break;
		}
		i += e->length;
	}

	return ret;
}

void rotator_1_N_set_mode(struct connector *c, int count, int page_flip,
								long int *usec)
{
	struct drm_exynos_ipp_property property;
	struct drm_exynos_ipp_queue_buf qbuf1, qbuf2[MAX_BUF];
	struct drm_exynos_ipp_cmd_ctrl cmd_ctrl;
	unsigned int width, height, stride;
	int ret, i, counter;
	struct drm_exynos_gem_create gem1, gem2[MAX_BUF];
	struct drm_exynos_gem_mmap mmap1, mmap2[MAX_BUF];
	void *usr_addr1, *usr_addr2[MAX_BUF];
	struct timeval begin, end;
	struct drm_gem_close args;
	char filename[100];

	/* For setting mode / IPP setting property */
	ret = rotator_set_mode_property(c, count, &width, &height, &stride,
								&property);
	if (ret) {
		fprintf(stderr, "failed to set mode property : %s\n",
							strerror(errno));
		return;
	}

	/* For GEM create / mmap / draw buffer */
	/* For source buffer */
	ret = util_gem_create_mmap(fd, &gem1, &mmap1, &usr_addr1,
							stride * height);
	if (ret) {
		fprintf(stderr, "failed to gem create mmap: %s\n",
							strerror(errno));
		return;
	}
	util_draw_buffer(usr_addr1, 1, width, height, stride, 0);
	sprintf(filename, "/opt/media/rot_src.bmp");
	util_write_bmp(filename, usr_addr1, width, height);

	/* For destination buffer */
	for (i = 0; i < MAX_BUF; i++) {
		ret = util_gem_create_mmap(fd, &gem2[i], &mmap2[i],
						&usr_addr2[i], stride * height);
		if (ret) {
			fprintf(stderr, "failed to gem create mmap: %d : %s\n",
							i, strerror(errno));
			goto err_gem_create_mmap;
		}
		util_draw_buffer(usr_addr2[i], 0, 0, 0, 0, stride * height);
		sprintf(filename, "/opt/media/rot_dst%d.bmp", i);
		util_write_bmp(filename, usr_addr2[i], height, width);
	}

	/* For IPP queueing */
	/* For source buffer */
	ret = exynos_drm_ipp_queue_buf(fd, property.prop_id, &qbuf1,
					EXYNOS_DRM_OPS_SRC, IPP_BUF_ENQUEUE,
					0, gem1.handle);
	if (ret) {
		fprintf(stderr, "failed to ipp queue buf src queue\n");
		goto err_ipp_queue_buf;
	}

	/* For destination buffer */
	for (i = 0; i < MAX_BUF; i++) {
		ret = exynos_drm_ipp_queue_buf(fd, property.prop_id, &qbuf2[i],
					EXYNOS_DRM_OPS_DST, IPP_BUF_ENQUEUE,
					i, gem2[i].handle);
		if (ret) {
			fprintf(stderr,
				"failed to ipp queue buf dst queue : %d\n", i);
			goto err_ipp_queue_buf;
		}
	}

	/* For IPP starting */
	gettimeofday(&begin, NULL);
	ret = exynos_drm_ipp_cmd_ctrl(fd, property.prop_id, &cmd_ctrl,
								IPP_CTRL_PLAY);
	if (ret) {
		fprintf(stderr, "failed to ipp cmd ctrl IPP_CMD_M2M start\n");
		goto err_ipp_queue_buf;
	}

	/* For IPP handling event */
	for (i = 0; i < MAX_LOOP; i++) {
		counter = 0;

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
			} else if (FD_ISSET(0, &fds))
				break;

			gettimeofday(&end, NULL);
			usec[MAX_BUF * i + counter] =
					(end.tv_sec - begin.tv_sec) * 1000000 +
					(end.tv_usec - begin.tv_usec);

			ret = rotator_event_handler(fd, i, property.prop_id,
							&qbuf1, &gem1, qbuf2,
							gem2, usr_addr2, height,
							width, &begin);
			if (ret) {
				fprintf(stderr,
					"failed to rotator_event_handler()\n");
				goto err_rotator_event_handler;
			}
			if (++counter > MAX_BUF)
				break;
		}
	}

	/* For IPP dequeing */
	/* For source buffer */
	ret = exynos_drm_ipp_queue_buf(fd, property.prop_id, &qbuf1,
				EXYNOS_DRM_OPS_SRC, IPP_BUF_DEQUEUE, 0,
				gem1.handle);
	if (ret) {
		fprintf(stderr, "failed to ipp queue buf src dequeue\n");
		goto err_rotator_event_handler;
	}

	/* For destination buffer */
	for (i = 0; i < MAX_BUF; i++) {
		ret = exynos_drm_ipp_queue_buf(fd, property.prop_id, &qbuf2[i],
				EXYNOS_DRM_OPS_DST, IPP_BUF_DEQUEUE, i,
				gem2[i].handle);
		if (ret) {
			fprintf(stderr,
				"failed to ipp queue buf dst queue : %d\n", i);
			goto err_rotator_event_handler;
		}
	}

	/* For IPP stopping */
	ret = exynos_drm_ipp_cmd_ctrl(fd, property.prop_id, &cmd_ctrl,
								IPP_CTRL_STOP);
	if (ret) {
		fprintf(stderr, "failed to ipp cmd ctrl IPP_CMD_M2M stop\n");
	}

	/* For munmap / GEM close */
	/* For destination buffer */
	for (i = 0; i < MAX_BUF; i++) {
		munmap(usr_addr2[i], mmap2[i].size);
		memset(&args, 0x00, sizeof(struct drm_gem_close));
		args.handle = gem2[i].handle;
		exynos_gem_close(fd, &args);
	}

	/* For source buffer */
	munmap(usr_addr1, mmap1.size);
	memset(&args, 0x00, sizeof(struct drm_gem_close));
	args.handle = gem1.handle;
	exynos_gem_close(fd, &args);

	return;

err_rotator_event_handler:
	exynos_drm_ipp_cmd_ctrl(fd, property.prop_id, &cmd_ctrl, IPP_CTRL_STOP);
err_ipp_queue_buf:
	for (i = 0; i < MAX_BUF; i++) {
		exynos_drm_ipp_queue_buf(fd, property.prop_id, &qbuf2[i],
				EXYNOS_DRM_OPS_DST, IPP_BUF_DEQUEUE, i,
				gem2[i].handle);
	}
	exynos_drm_ipp_queue_buf(fd, property.prop_id, &qbuf1,
			EXYNOS_DRM_OPS_SRC, IPP_BUF_DEQUEUE, 0, gem1.handle);
err_gem_create_mmap:
	for (i = 0; i < MAX_BUF; i++) {
		munmap(usr_addr2[i], mmap2[i].size);
		memset(&args, 0x00, sizeof(struct drm_gem_close));
		args.handle = gem2[i].handle;
		exynos_gem_close(fd, &args);
	}

	munmap(usr_addr1, mmap1.size);
	memset(&args, 0x00, sizeof(struct drm_gem_close));
	args.handle = gem1.handle;
	exynos_gem_close(fd, &args);
}

void rotator_N_N_set_mode(struct connector *c, int count, int page_flip,
								long int *usec)
{
	struct drm_exynos_ipp_property property;
	struct drm_exynos_ipp_queue_buf qbuf1[MAX_BUF], qbuf2[MAX_BUF];
	struct drm_exynos_ipp_cmd_ctrl cmd_ctrl;
	unsigned int width, height, stride;
	int ret, i, counter;
	struct drm_exynos_gem_create gem1[MAX_BUF], gem2[MAX_BUF];
	struct drm_exynos_gem_mmap mmap1[MAX_BUF], mmap2[MAX_BUF];
	void *usr_addr1[MAX_BUF], *usr_addr2[MAX_BUF];
	struct timeval begin, end;
	struct drm_gem_close args;
	char filename[100];

	/* For setting mode / IPP setting property */
	ret = rotator_set_mode_property(c, count, &width, &height, &stride,
								&property);
	if (ret) {
		fprintf(stderr, "failed to set mode property : %s\n",
							strerror(errno));
		return;
	}

	/* For GEM create / mmap / draw buffer */
	for (i = 0; i < MAX_BUF; i++) {
		/* For source buffer */
		ret = util_gem_create_mmap(fd, &gem1[i], &mmap1[i],
						&usr_addr1[i], stride * height);
		if (ret) {
			fprintf(stderr, "failed to gem create mmap: %d : %s\n",
							i, strerror(errno));
			goto err_gem_create_mmap;
		}
		util_draw_buffer(usr_addr1[i], 1, width, height, stride, 0);
		sprintf(filename, "/opt/media/rot_src%d.bmp", i);
		util_write_bmp(filename, usr_addr1[i], width, height);

		/* For destination buffer */
		ret = util_gem_create_mmap(fd, &gem2[i], &mmap2[i],
						&usr_addr2[i], stride * height);
		if (ret) {
			fprintf(stderr, "failed to gem create mmap: %d : %s\n",
							i, strerror(errno));
			goto err_gem_create_mmap;
		}
		util_draw_buffer(usr_addr2[i], 0, 0, 0, 0, stride * height);
		sprintf(filename, "/opt/media/rot_dst%d.bmp", i);
		util_write_bmp(filename, usr_addr2[i], height, width);
	}

	/* For IPP queueing */
	for (i = 0; i < MAX_BUF; i++) {
		/* For source buffer */
		ret = exynos_drm_ipp_queue_buf(fd, property.prop_id, &qbuf1[i],
					EXYNOS_DRM_OPS_SRC, IPP_BUF_ENQUEUE,
					i, gem1[i].handle);
		if (ret) {
			fprintf(stderr,
				"failed to ipp queue buf src queue : %d\n", i);
			goto err_ipp_queue_buf;
		}

		/* For destination buffer */
		ret = exynos_drm_ipp_queue_buf(fd, property.prop_id, &qbuf2[i],
					EXYNOS_DRM_OPS_DST, IPP_BUF_ENQUEUE,
					i, gem2[i].handle);
		if (ret) {
			fprintf(stderr,
				"failed to ipp queue buf dst queue : %d\n", i);
			goto err_ipp_queue_buf;
		}
	}

	/* For IPP starting */
	gettimeofday(&begin, NULL);
	ret = exynos_drm_ipp_cmd_ctrl(fd, property.prop_id, &cmd_ctrl,
								IPP_CTRL_PLAY);
	if (ret) {
		fprintf(stderr, "failed to ipp cmd ctrl IPP_CMD_M2M start\n");
		goto err_ipp_queue_buf;
	}

	/* For IPP handling event */
	for (i = 0; i < MAX_LOOP; i++) {
		counter = 0;

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
			} else if (FD_ISSET(0, &fds))
				break;

			gettimeofday(&end, NULL);
			usec[MAX_BUF * i + counter] =
					(end.tv_sec - begin.tv_sec) * 1000000 +
					(end.tv_usec - begin.tv_usec);

			ret = rotator_event_handler(fd, i, property.prop_id,
							qbuf1, gem1, qbuf2,
							gem2, usr_addr2, height,
							width, &begin);
			if (ret) {
				fprintf(stderr,
					"failed to rotator_event_handler()\n");
				goto err_rotator_event_handler;
			}
			if (++counter > MAX_BUF)
				break;
		}
	}

	/* For IPP dequeing */
	for (i = 0; i < MAX_BUF; i++) {
		/* For destination buffer */
		ret = exynos_drm_ipp_queue_buf(fd, property.prop_id, &qbuf2[i],
				EXYNOS_DRM_OPS_DST, IPP_BUF_DEQUEUE, i,
				gem2[i].handle);
		if (ret) {
			fprintf(stderr,
				"failed to ipp queue buf dst dequeue: %d\n", i);
			goto err_rotator_event_handler;
		}

		/* For source buffer */
		ret = exynos_drm_ipp_queue_buf(fd, property.prop_id, &qbuf1[i],
				EXYNOS_DRM_OPS_SRC, IPP_BUF_DEQUEUE, i,
				gem1[i].handle);
		if (ret) {
			fprintf(stderr,
				"failed to ipp queue buf src dequeue: %d\n", i);
			goto err_rotator_event_handler;
		}
	}

	/* For IPP stopping */
	ret = exynos_drm_ipp_cmd_ctrl(fd, property.prop_id, &cmd_ctrl,
								IPP_CTRL_STOP);
	if (ret) {
		fprintf(stderr, "failed to ipp cmd ctrl IPP_CMD_M2M stop\n");
	}

	/* For munmap / GEM close */
	for (i = 0; i < MAX_BUF; i++) {
		/* For destination buffer */
		munmap(usr_addr2[i], mmap2[i].size);
		memset(&args, 0x00, sizeof(struct drm_gem_close));
		args.handle = gem2[i].handle;
		exynos_gem_close(fd, &args);

		/* For source buffer */
		munmap(usr_addr1[i], mmap1[i].size);
		memset(&args, 0x00, sizeof(struct drm_gem_close));
		args.handle = gem1[i].handle;
		exynos_gem_close(fd, &args);
	}

	return;

err_rotator_event_handler:
	exynos_drm_ipp_cmd_ctrl(fd, property.prop_id, &cmd_ctrl, IPP_CTRL_STOP);
err_ipp_queue_buf:
	for (i = 0; i < MAX_BUF; i++) {
		exynos_drm_ipp_queue_buf(fd, property.prop_id, &qbuf2[i],
				EXYNOS_DRM_OPS_DST, IPP_BUF_DEQUEUE, i,
				gem2[i].handle);

		exynos_drm_ipp_queue_buf(fd, property.prop_id, &qbuf1[i],
				EXYNOS_DRM_OPS_SRC, IPP_BUF_DEQUEUE, i,
				gem1[i].handle);
	}
err_gem_create_mmap:
	for (i = 0; i < MAX_BUF; i++) {
		munmap(usr_addr2[i], mmap2[i].size);
		memset(&args, 0x00, sizeof(struct drm_gem_close));
		args.handle = gem2[i].handle;
		exynos_gem_close(fd, &args);

		munmap(usr_addr1, mmap1[i].size);
		memset(&args, 0x00, sizeof(struct drm_gem_close));
		args.handle = gem1[i].handle;
		exynos_gem_close(fd, &args);
	}
}
