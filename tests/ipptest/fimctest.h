#ifndef __FIMCTEST_H__
#define __FIMCTEST_H__

#include <stdbool.h>
#include "xf86drm.h"
#include "xf86drmMode.h"
#include "libkms.h"

#define MAX_LOOP 20
#define HALF_LOOP 10
#define MAX_BUF 3

#define RESULT_PATH "/tmp/"

struct connector {
	uint32_t id;
	char mode_str[64];
	drmModeModeInfo *mode;
	drmModeEncoder *encoder;
	int crtc;
	unsigned int fb_id[2], current_fb_id;
	struct timeval start;

	int swap_count;
};

struct crtc {
	drmModeCrtc *crtc;
	drmModeObjectProperties *props;
	drmModePropertyRes **props_info;
	drmModeModeInfo *mode;
};

struct encoder {
	drmModeEncoder *encoder;
};

struct drm_connector {
	drmModeConnector *connector;
	drmModeObjectProperties *props;
	drmModePropertyRes **props_info;
};

struct fb {
	drmModeFB *fb;
};

struct plane {
	drmModePlane *plane;
	drmModeObjectProperties *props;
	drmModePropertyRes **props_info;
};

struct resources {
	drmModeRes *res;
	drmModePlaneRes *plane_res;

	struct crtc *crtcs;
	struct encoder *encoders;
	struct drm_connector *connectors;
	struct fb *fbs;
	struct plane *planes;
};

struct device {
	int fd;

	struct resources *resources;
	struct kms_driver *kms;

	struct {
		unsigned int width;
		unsigned int height;

		unsigned int fb_id;
		struct kms_bo *bo;
	} mode;
};

struct pipe_arg {
	uint32_t *con_ids;
	unsigned int num_cons;
	uint32_t crtc_id;
	char mode_str[64];
	char format_str[5];
	unsigned int vrefresh;
	unsigned int fourcc;
	drmModeModeInfo *mode;
	struct crtc *crtc;
	struct timeval start;
};

struct plane_arg {
	uint32_t crtc_id;  /* the id of CRTC to bind to */
	bool has_position;
	int32_t x, y;
	uint32_t w, h;
	double scale;
	unsigned int fb_id;
	char format_str[5]; /* need to leave room for terminating \0 */
	unsigned int fourcc;
};

extern int fd;

extern void connector_find_mode(struct connector *c);

#endif
