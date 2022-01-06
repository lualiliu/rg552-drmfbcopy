// SPDX-License-Identifier: GPL-2.0-only
/*
 * KMS/DRM screenshot tool
 *
 * Copyright (c) 2021 Paul Cercueil <paul@crapouillou.net>
 */

#include <drm.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

int src_ipr = 1152;
int dst_ipr = 1920;

void rotate(uint16_t *src_p,uint16_t *dst_p,unsigned int len){
    int row, col;

    int src_h = 1920;
    int src_w = 1152;
    int dst_h = 1080;

    uint16_t* srcBuf;
    uint16_t* dstBuf;

    printf("%d %d",src_ipr,dst_ipr);
    for (row = 0; row < src_h; ++row)
    {
        srcBuf = (uint16_t*)(src_p) + (row * src_ipr);
        dstBuf = (uint16_t*)(dst_p) + row + ((dst_h - 1) * dst_ipr);
        for (col = 0; col < src_w; ++col)
        {
            *dstBuf = *srcBuf;
            ++srcBuf;
            dstBuf -= dst_ipr;
        } 
    }
}

static int save_fb(drmModeFB *fb, int prime_fd)
{
	void *buffer, *picture;
	int ret = 0;
	unsigned int len = (fb->bpp >> 3) * fb->width * fb->height;

	/* open screen file. */
	int fb0_fd = open("/dev/fb0",O_RDWR);

	picture = mmap(NULL,len,PROT_READ|PROT_WRITE,MAP_SHARED,fb0_fd, 0);                     //fb0
	if (picture == MAP_FAILED) {
		ret = -errno;
		printf(stderr, "Unable to mmap picture\n");
	}

	buffer = mmap(NULL, len,										//drm
		      PROT_READ, MAP_PRIVATE, prime_fd, 0);

	if (buffer == MAP_FAILED) {
		ret = -errno;
		printf(stderr, "Unable to mmap prime buffer\n");
	}

	/* Drop privileges, to write PNG with user rights */
	seteuid(getuid());

	// Not implemented
	//rotate(picture, buffer, len);

	memcpy(picture,buffer,len);

	return ret;
}

int main(int argc, char **argv)
{
	int err, drm_fd, prime_fd, retval = EXIT_FAILURE;
	unsigned int i, card;
	uint32_t fb_id, crtc_id;
	drmModePlaneRes *plane_res;
	drmModePlane *plane;
	drmModeFB *fb;
	char buf[256];
	uint64_t has_dumb;

	for (card = 0; ; card++) {
		snprintf(buf, sizeof(buf), "/dev/dri/card%u", card);

		drm_fd = open(buf, O_RDWR | O_CLOEXEC);
		if (drm_fd < 0) {
			fprintf(stderr, "Could not open KMS/DRM device.\n");
			goto out_return;
		}

		if (drmGetCap(drm_fd, DRM_CAP_DUMB_BUFFER, &has_dumb) >= 0 &&
		    has_dumb)
			break;

		close(drm_fd);
	}

	drm_fd = open(buf, O_RDWR | O_CLOEXEC);
	if (drm_fd < 0) {
		fprintf(stderr, "Could not open KMS/DRM device.\n");
		goto out_return;
	}

	if (drmSetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
		fprintf(stderr, "Unable to set atomic cap.\n");
		goto out_close_fd;
	}

	if (drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
		fprintf(stderr, "Unable to set universal planes cap.\n");
		goto out_close_fd;
	}

	plane_res = drmModeGetPlaneResources(drm_fd);
	if (!plane_res) {
		fprintf(stderr, "Unable to get plane resources.\n");
		goto out_close_fd;
	}

	for (i = 0; i < plane_res->count_planes; i++) {
		plane = drmModeGetPlane(drm_fd, plane_res->planes[i]);
		fb_id = plane->fb_id;
		crtc_id = plane->crtc_id;
		drmModeFreePlane(plane);

		if (fb_id != 0 && crtc_id != 0)
			break;
	}

	if (i == plane_res->count_planes) {
		fprintf(stderr, "No planes found\n");
		goto out_free_resources;
	}

	fb = drmModeGetFB(drm_fd, fb_id);
	if (!fb) {
		fprintf(stderr, "Failed to get framebuffer %"PRIu32": %s\n",
			fb_id, strerror(errno));
		goto out_free_resources;
	}

	err = drmPrimeHandleToFD(drm_fd, fb->handle, O_RDONLY, &prime_fd);
	if (err < 0) {
		fprintf(stderr, "Failed to retrieve prime handler: %s\n",
			strerror(-err));
		goto out_free_fb;
	}

	err = save_fb(fb, prime_fd);
	if (err < 0) {
		fprintf(stderr, "Failed to take screenshot: %s\n",
			strerror(-err));
		goto out_close_prime_fd;
	}

	retval = EXIT_SUCCESS;

out_close_prime_fd:
	close(prime_fd);
out_free_fb:
	drmModeFreeFB(fb);
out_free_resources:
	drmModeFreePlaneResources(plane_res);
out_close_fd:
	close(drm_fd);
out_return:
	return retval;
}