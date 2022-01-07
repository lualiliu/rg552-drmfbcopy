// SPDX-License-Identifier: GPL-2.0-only
/*
 * RG552 HDMI output tools 
 * 
 * The framebuffer of DRM is overwritten to /dev/fb0 through rga. (1152*1920 to 1280*720)
 * 
 * Thanks Paul Cercueil <paul@crapouillou.net>
 *
 * Author: Luali Liu <lualiliu@outlook.com>
 *
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

#include <rga/rga.h>
#include <rga/RgaApi.h>

static int rga_prepare_info(int bpp, int width, int height, int pitch, rga_info_t *info) {
    RgaSURF_FORMAT format;

    memset(info, 0, sizeof(rga_info_t));

    info->fd = -1;
    info->mmuFlag = 1;
    info->rotation = HAL_TRANSFORM_ROT_90;

    switch (bpp) {
    case 12:
        format = RK_FORMAT_YCbCr_420_SP;
        break;
    case 16:
        format = RK_FORMAT_RGB_565;
        break;
    case 32:
        format = RK_FORMAT_BGRA_8888;
        break;
    default:
        return -1;
    }

    rga_set_rect(&info->rect, 0, 0, width, height,
                 pitch * 8 / bpp, height, format);
    return 0;
}

static int drm_render_rga(void *buf, void *dst_buf, int bpp, int width, int height, int dst_width, int dst_height, int pitch, int dst_pitch) {
    rga_info_t src_info = {0};
    rga_info_t dst_info = {0};

    static int rga_supported = 1;
    static int rga_inited = 0;

    if (!rga_supported)
        return -1;

    if (!rga_inited) {
        if (c_RkRgaInit() < 0) {
            rga_supported = 0;
            return -1;
        }
        rga_inited = 1;
    }

    if (rga_prepare_info(bpp, width, height, pitch, &src_info) < 0)
        return -1;

    if (rga_prepare_info(bpp, dst_width, dst_height, dst_pitch, &dst_info) < 0)
        return -1;

    src_info.virAddr = buf;
    dst_info.virAddr = dst_buf;

    return c_RkRgaBlit(&src_info, &dst_info, NULL);
}

static void rotate_90_ccw(uint32_t* restrict dst, uint32_t* restrict src, int32_t w, int32_t h)
{
    src += w * h - 1;
    for (int32_t col = w - 1; col >= 0; --col)
    {
        uint32_t *outcol = dst + col;
        for(int32_t row = 0; row < h; ++row, outcol += w)
        {
            *outcol = *src--;
        }
    }
}

/*
static void rotate_90(uint32_t* restrict dst, uint32_t* restrict src, int32_t w, int32_t h)
{

    for (int32_t row = 0; row < h; ++row)
    {
        uint32_t* srcBuf = (uint32_t *)(src) + (row * w);
        uint32_t* dstBuf = (uint32_t *)(dst) + row + ((h - 1) * h);
        for (int32_t col = 0; col < w; ++col)
        {
            *dstBuf = *srcBuf;
            ++srcBuf;
            dstBuf -= w;
        } 
    }
}
*/

static int save_fb(drmModeFB *fb, int prime_fd)
{
	void *buffer, *picture;
	int ret = 0;
	unsigned int len = (fb->bpp >> 3) * fb->width * fb->height;

	unsigned int len_480p = (fb->bpp >> 3) * 720 * 480;

	unsigned int len_720p = (fb->bpp >> 3) * 1280 * 720;

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

	void *membuffer = malloc(len_720p);

	// Rotation and zoom out
	while(1){
	//	rotate_90_ccw(picture, membuffer, 1920,1152);

	//	ret = drm_render_rga(buffer, picture, 32,1152,1920,1280,720,1152*4,1280*4);

		// tearing fixed(720p)
		ret = drm_render_rga(buffer, membuffer, 32,1152,1920,1280,720,1152*4,1280*4);
		memcpy(picture,membuffer,len_720p);
	
		// tearing fixed(480p) the cmdline need set video=HDMI-A-1:720x480@60
	//	ret = drm_render_rga(buffer, membuffer, 32,1152,1920,720,480,1152*4,720*4);
	//	memcpy(picture,membuffer,len_480p);

		usleep(1);	//The RGA actual time-consuming of 1280*720 is 2,000us
	}
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

	//create daemon
	daemon(0,0);

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