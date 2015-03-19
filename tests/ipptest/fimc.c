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

static int exynos_drm_ipp_set_property(int fd,
				struct drm_exynos_ipp_property *property,
				struct drm_exynos_sz *def_sz,
				enum drm_exynos_ipp_cmd cmd,
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
		property->config[EXYNOS_DRM_OPS_SRC].fmt = DRM_FORMAT_XRGB8888;
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
					unsigned int gem_handle)
{
	int ret = 0;

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

int fimc_event_handler(struct drm_exynos_ipp_queue_buf *src_qbuf, struct drm_exynos_gem_create *src_gem,
	struct drm_exynos_ipp_queue_buf *dst_qbuf, struct drm_exynos_gem_create *dst_gem,
	struct drm_exynos_ipp_property *property, void **usr_addr,
	unsigned int width, unsigned int height, enum drm_exynos_ipp_cmd cmd)
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
				sprintf(filename, RESULT_PATH "fimc_m2m_dst%d.bmp", bmp_idx);

				util_write_bmp(filename, usr_addr[dst_buf_id], width, height);

				/* For source buffer queue to IPP */
				ret = exynos_drm_ipp_queue_buf(fd, &src_qbuf[src_buf_id], EXYNOS_DRM_OPS_SRC,
							IPP_BUF_ENQUEUE, property->prop_id,
							src_buf_id, src_gem[src_buf_id].handle);
				if (ret) {
					fprintf(stderr, "failed to ipp buf src queue\n");
					goto err_ipp_ctrl_close;
				}

				/* For destination buffer queue to IPP */
				ret = exynos_drm_ipp_queue_buf(fd, &dst_qbuf[dst_buf_id], EXYNOS_DRM_OPS_DST,
							IPP_BUF_ENQUEUE, property->prop_id,
							dst_buf_id, dst_gem[dst_buf_id].handle);
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
							dst_buf_id, dst_gem[dst_buf_id].handle);
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

void fimc_m2m_set_mode(struct connector *c, int count, int page_flip,
								long int *usec)
{
	struct drm_exynos_ipp_property property;
	struct drm_exynos_ipp_cmd_ctrl cmd_ctrl;
	struct drm_exynos_sz def_sz = {720, 1280};
	struct drm_exynos_ipp_queue_buf qbuf1[MAX_BUF], qbuf2[MAX_BUF];
	unsigned int width=720, height=1280, stride;
	int ret, i, j, x;
	struct drm_exynos_gem_create gem1[MAX_BUF], gem2[MAX_BUF];
	struct exynos_gem_mmap_data mmap1[MAX_BUF], mmap2[MAX_BUF];
	void *usr_addr1[MAX_BUF], *usr_addr2[MAX_BUF];
	struct timeval begin, end;
	struct drm_gem_close args;
	char filename[100];

	/* For mode */
	width = height = 0;
	for (i = 0; i < count; i++) {
		connector_find_mode(&c[i]);
		if (c[i].mode == NULL) continue;
		width += c[i].mode->hdisplay;
		if (height < c[i].mode->vdisplay) height = c[i].mode->vdisplay;
	}
	stride = width * 4;    
	i =0;

	def_sz.hsize = width;
	def_sz.vsize = height;

	/* For property */
	ret = exynos_drm_ipp_set_property(fd, &property, &def_sz, IPP_CMD_M2M, EXYNOS_DRM_DEGREE_90);
	if (ret) {
		fprintf(stderr, "failed to ipp property\n");
		return;
	}

	for (i = 0; i < MAX_BUF; i++) {
		/* For source buffer */
		ret = util_gem_create_mmap(fd, &gem1[i], &mmap1[i], stride * height);
		if (ret) {
			fprintf(stderr, "failed to gem create mmap: %s\n",
			strerror(errno));

			if (ret == -1)
				return;
			else if (ret == -2)
				goto err_ipp_ctrl_close;
		}
		usr_addr1[i] = mmap1[i].addr;

		/* For source buffer map to IPP */
		ret = exynos_drm_ipp_queue_buf(fd, &qbuf1[i], EXYNOS_DRM_OPS_SRC,
				IPP_BUF_ENQUEUE, property.prop_id, i, gem1[i].handle);
		if (ret) {
			fprintf(stderr, "failed to ipp buf src map\n");
			goto err_ipp_ctrl_close;
		}

		util_draw_buffer(usr_addr1[i], 1, width, height, stride, 0);
		sprintf(filename, RESULT_PATH "fimc_m2m_org_src%d.bmp", j);
		util_write_bmp(filename, usr_addr1[i], width, height);
	}

	for (i = 0; i < MAX_BUF; i++) {
		/* For destination buffer */
		ret = util_gem_create_mmap(fd, &gem2[i], &mmap2[i], stride * height);
		if (ret) {
			fprintf(stderr, "failed to gem create mmap: %s\n",
						strerror(errno));
			if (ret == -1)
				goto err_ipp_ctrl_close;
			else if (ret == -2)
				goto err_ipp_ctrl_close;
		}
		usr_addr2[i] = mmap2[i].addr;

		/* For destination buffer map to IPP */
		ret = exynos_drm_ipp_queue_buf(fd, &qbuf2[i], EXYNOS_DRM_OPS_DST,
			IPP_BUF_ENQUEUE, property.prop_id, i, gem2[i].handle);
		if (ret) {
			fprintf(stderr, "failed to ipp buf dst map\n");
			goto err_ipp_ctrl_close;
		}

		util_draw_buffer(usr_addr2[i], 0, 0, 0, 0, mmap2[i].size);
		sprintf(filename, RESULT_PATH "fimc_m2m_org_dst%d.bmp", j);
		util_write_bmp(filename, usr_addr2[i], height, width);
	}

	/* Start */
	gettimeofday(&begin, NULL);
	ret = exynos_drm_ipp_cmd_ctrl(fd, &cmd_ctrl, property.prop_id, IPP_CTRL_PLAY);
	if (ret) {
		fprintf(stderr,
		"failed to ipp ctrl IPP_CMD_M2M start\n");
		goto err_ipp_ctrl_close;
	}
        
	j=0;
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
			break;
		}

		gettimeofday(&end, NULL);
		usec[j] = (end.tv_sec - begin.tv_sec) * 1000000 +
		(end.tv_usec - begin.tv_usec);

		if (property.config[EXYNOS_DRM_OPS_DST].degree == EXYNOS_DRM_DEGREE_90 ||
			property.config[EXYNOS_DRM_OPS_DST].degree == EXYNOS_DRM_DEGREE_270) {
			if(fimc_event_handler(qbuf1, gem1, qbuf2, gem2, &property, usr_addr2, height, width, IPP_CMD_M2M) < 0)
				break;
		} else {
			if(fimc_event_handler(qbuf1, gem1, qbuf2, gem2, &property, usr_addr2, width, height, IPP_CMD_M2M) < 0)
				break;
		}

		if (++j > MAX_LOOP)
			break;

		gettimeofday(&begin, NULL);
	}

err_ipp_ctrl_close:
	/* For source buffer dequeue to IPP */
	for (i = 0; i < MAX_BUF; i++) {
		ret = exynos_drm_ipp_queue_buf(fd, &qbuf1[i], EXYNOS_DRM_OPS_SRC,
						IPP_BUF_DEQUEUE, property.prop_id, i, gem1[i].handle);
		if (ret < 0)
			fprintf(stderr, "failed to ipp buf dst dequeue\n");
	}

	/* For destination buffer dequeue to IPP */
	for (i = 0; i < MAX_BUF; i++) {
		ret = exynos_drm_ipp_queue_buf(fd, &qbuf2[i], EXYNOS_DRM_OPS_DST,
						IPP_BUF_DEQUEUE, property.prop_id, i, gem2[i].handle);
		if (ret < 0)
			fprintf(stderr, "failed to ipp buf dst dequeue\n");
	}

	/* Stop */
	ret = exynos_drm_ipp_cmd_ctrl(fd, &cmd_ctrl, property.prop_id, IPP_CTRL_STOP);
	if (ret)
		fprintf(stderr, "failed to ipp ctrl IPP_CMD_WB stop\n");

	/* Close source buffer */
	for (i = 0; i < MAX_BUF; i++) {
		munmap(usr_addr1[i], mmap1[i].size);
		memset(&args, 0x00, sizeof(struct drm_gem_close));
		args.handle = gem1[i].handle;
		exynos_gem_close(fd, &args);
	}

	/* Close destination buffer */
	for (i = 0; i < MAX_BUF; i++) {
		munmap(usr_addr2[i], mmap2[i].size);
		memset(&args, 0x00, sizeof(struct drm_gem_close));
		args.handle = gem2[i].handle;
		exynos_gem_close(fd, &args);
	}

	return;
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
	ret = exynos_drm_ipp_set_property(fd, &property, &def_sz, IPP_CMD_WB, EXYNOS_DRM_DEGREE_0);
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
						IPP_BUF_ENQUEUE, property.prop_id, i, gem[i].handle);
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
			if(fimc_event_handler(NULL, NULL, qbuf, gem, &property, usr_addr, height, width, IPP_CMD_WB) < 0)
				break;
		} else {
			if(fimc_event_handler(NULL, NULL, qbuf, gem, &property, usr_addr, width, height, IPP_CMD_WB) < 0)
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
			ret = exynos_drm_ipp_set_property(fd, &property, &def_sz, IPP_CMD_WB, EXYNOS_DRM_DEGREE_90);
			if (ret) {
				fprintf(stderr, "failed to ipp property\n");
				goto err_ipp_ctrl_close;
			}

			/* For destination buffer */
			for (i = 0; i < MAX_BUF; i++) {
				/* For destination buffer map to IPP */
				ret = exynos_drm_ipp_queue_buf(fd, &qbuf[i], EXYNOS_DRM_OPS_DST,
								IPP_BUF_ENQUEUE, property.prop_id, i, gem[i].handle);
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
						IPP_BUF_DEQUEUE, property.prop_id, i, gem[i].handle);
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

