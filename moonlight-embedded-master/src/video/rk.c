/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015-2018 Iwan Timmer
 * Copyright (C) 2018 Martin Cerveny, Daniel Mehrwald
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "video.h"
#include "../util.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/drm_fourcc.h>
#include <linux/videodev2.h>

#include <rockchip/rk_mpi.h>

#define MAX_FRAMES 3
#define RK_H264 0x7
#define RK_H265 0x1000004
#define RK_AV1  0x1000008
#define ALIGN_UP(value, alignment) (((value) + ((alignment) - 1)) & ~((alignment) - 1))

#define RGBFRAME_MAGIC 0x4647524dU
#define RGBFRAME_VERSION 1
#define RGBFRAME_FORMAT_BGRA8888 1
#define RGBFRAME_FORMAT_RGBA8888 2

// Vendor-defined 10-bit format code used prior to 5.10
#ifndef DRM_FORMAT_NA12
#define DRM_FORMAT_NA12 fourcc_code('N', 'A', '1', '2')
#endif

// Upstreamed 10-bit format code used on 5.10+ kernels
#ifndef DRM_FORMAT_NV15
#define DRM_FORMAT_NV15 fourcc_code('N', 'V', '1', '5')
#endif

// Values for "Colorspace" connector property
#ifndef DRM_MODE_COLORIMETRY_DEFAULT
#define DRM_MODE_COLORIMETRY_DEFAULT     0
#endif
#ifndef DRM_MODE_COLORIMETRY_BT2020_RGB
#define DRM_MODE_COLORIMETRY_BT2020_RGB  9
#endif

#define RK_FORMAT_YCbCr_420_SP (0xa << 8)
#define IM_HAL_TRANSFORM_ROT_90  (1 << 0)
#define IM_HAL_TRANSFORM_ROT_180 (1 << 1)
#define IM_HAL_TRANSFORM_ROT_270 (1 << 2)

typedef uint32_t rga_buffer_handle_t;

typedef struct {
    int max;
    int min;
} im_colorkey_range;

typedef struct {
    int scale_r;
    int scale_g;
    int scale_b;
    int offset_r;
    int offset_g;
    int offset_b;
} im_nn_t;

typedef struct {
    void* vir_addr;
    void* phy_addr;
    int fd;
    int width;
    int height;
    int wstride;
    int hstride;
    int format;
    int color_space_mode;
    union {
        int global_alpha;
        struct {
            uint16_t alpha0;
            uint16_t alpha1;
        } alpha_bit;
    };
    int rd_mode;
    int color;
    im_colorkey_range colorkey_range;
    im_nn_t nn;
    int rop_code;
    rga_buffer_handle_t handle;
} rga_buffer_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t format;
} im_handle_param_t;

typedef rga_buffer_handle_t (*rga_importbuffer_fd_fn)(int fd, im_handle_param_t *param);
typedef rga_buffer_t (*rga_wrapbuffer_handle_t_fn)(rga_buffer_handle_t handle, int width, int height,
                                                   int wstride, int hstride, int format);
typedef int (*rga_releasebuffer_handle_fn)(rga_buffer_handle_t handle);
typedef int (*rga_imrotate_t_fn)(const rga_buffer_t src, rga_buffer_t dst, int rotation, int sync);

// HDR structs copied from linux include/linux/hdmi.h for older libdrm versions
struct rk_hdr_metadata_infoframe
{
    uint8_t eotf;
    uint8_t metadata_type;

    struct
    {
        uint16_t x, y;
    } display_primaries[3];

    struct
    {
        uint16_t x, y;
    } white_point;

    uint16_t max_display_mastering_luminance;
    uint16_t min_display_mastering_luminance;

    uint16_t max_cll;
    uint16_t max_fall;
};

struct rk_hdr_output_metadata
{
    uint32_t metadata_type;

    union {
        struct rk_hdr_metadata_infoframe hdmi_metadata_type1;
    };
};

void *pkt_buf = NULL;
size_t pkt_buf_size = 0;
int fd;
int fb_id;
uint32_t plane_id, crtc_id, conn_id, hdr_metadata_blob_id, pixel_format;
int frm_eos;
int crtc_width;
int crtc_height;
RK_U32 frm_width;
RK_U32 frm_height;
RK_U32 frm_hor_stride;
RK_U32 frm_ver_stride;
RK_U32 drm_frame_width;
RK_U32 drm_frame_height;
RK_U32 rga_output_hor_stride;
RK_U32 rga_output_ver_stride;
MppFrameFormat frm_format;
int fb_x, fb_y, fb_width, fb_height;
bool atomic;
bool drm_stretch;
bool drm_takeover;
bool drm_rotate_swap;
bool rga_rotation;
int rga_rotation_mode;
uint32_t rk_target_width;
uint32_t rk_target_height;
uint64_t drm_zpos;
bool rgbframe_mode;
bool frame_thread_started;
bool display_thread_started;

uint8_t last_colorspace = 0xFF;
bool last_hdr_state = false;

pthread_t tid_frame, tid_display;
pthread_mutex_t mutex;
pthread_cond_t cond;

drmModePlane *ovr = NULL;
drmModeEncoder *encoder = NULL;
drmModeConnector *connector = NULL;
drmModeRes *resources = NULL;
drmModePlaneRes *plane_resources = NULL;
drmModeCrtcPtr crtc = {0};

drmModePropertyPtr hdr_metadata_prop = NULL;

drmModeAtomicReqPtr drm_request = NULL;
drmModePropertyPtr plane_props[32];
drmModePropertyPtr conn_props[32];

MppCtx mpi_ctx;
MppApi *mpi_api;
MppPacket mpi_packet;
MppBufferGroup mpi_frm_grp;

struct drm_frame {
  int prime_fd;
  int fb_id;
  uint32_t handle;
  rga_buffer_handle_t rga_handle;
  rga_buffer_t rga_buffer;
};

struct drm_frame frame_to_drm[MAX_FRAMES];
struct drm_frame rga_to_drm[MAX_FRAMES];

struct {
  void *handle;
  rga_importbuffer_fd_fn importbuffer_fd;
  rga_wrapbuffer_handle_t_fn wrapbuffer_handle_t;
  rga_releasebuffer_handle_fn releasebuffer_handle;
  rga_imrotate_t_fn imrotate_t;
} rga;

struct rgbframe_header {
  uint32_t magic;
  uint32_t version;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t format;
  volatile uint32_t frame_no;
  uint32_t data_size;
};

static int rgbframe_fd = -1;
static uint8_t *rgbframe_base = NULL;
static size_t rgbframe_map_size = 0;
static struct rgbframe_header *rgbframe_hdr = NULL;
static uint8_t *rgbframe_pixels = NULL;
static uint32_t rgbframe_width = 0;
static uint32_t rgbframe_height = 0;
static uint32_t rgbframe_format = RGBFRAME_FORMAT_RGBA8888;
static uint32_t rgbframe_frame_no = 0;

static drmModePropertyPtr find_property(drmModePropertyPtr *props, const char *name) {
  while (*props) {
    if (!strcasecmp(name, (*props)->name)) {
      return *props;
    }
    props++;
  }

  return NULL;
}

static bool property_supports_bitmask_value(drmModePropertyPtr prop, uint64_t value) {
  if (!(prop->flags & DRM_MODE_PROP_BITMASK))
    return true;

  for (int i = 0; i < prop->count_enums; i++) {
    if (prop->enums[i].value == value)
      return true;
  }

  return false;
}

static uint64_t property_range_max(drmModePropertyPtr prop, uint64_t fallback) {
  if (prop && prop->count_values >= 2)
    return prop->values[1];

  return fallback;
}

static bool env_enabled(const char *name, bool fallback) {
  const char *value = getenv(name);
  if (!value || value[0] == 0)
    return fallback;

  return strcmp(value, "0") != 0 &&
         strcasecmp(value, "false") != 0 &&
         strcasecmp(value, "no") != 0;
}

static uint32_t env_u32(const char *name, uint32_t fallback) {
  const char *value = getenv(name);
  if (!value || value[0] == 0)
    return fallback;

  char *end = NULL;
  unsigned long parsed = strtoul(value, &end, 0);
  if (end == value || parsed == 0 || parsed > UINT32_MAX)
    return fallback;

  return (uint32_t)parsed;
}

static bool rga_load(void) {
  if (rga.handle)
    return true;

  rga.handle = dlopen("librga.so", RTLD_NOW | RTLD_LOCAL);
  if (!rga.handle) {
    fprintf(stderr, "RK RGA: unable to load librga.so: %s\n", dlerror());
    return false;
  }

  rga.importbuffer_fd = (rga_importbuffer_fd_fn)dlsym(rga.handle, "importbuffer_fd");
  rga.wrapbuffer_handle_t = (rga_wrapbuffer_handle_t_fn)dlsym(rga.handle, "wrapbuffer_handle_t");
  rga.releasebuffer_handle = (rga_releasebuffer_handle_fn)dlsym(rga.handle, "releasebuffer_handle");
  rga.imrotate_t = (rga_imrotate_t_fn)dlsym(rga.handle, "imrotate_t");
  if (!rga.importbuffer_fd || !rga.wrapbuffer_handle_t || !rga.releasebuffer_handle || !rga.imrotate_t) {
    fprintf(stderr, "RK RGA: librga.so is missing required IM2D symbols\n");
    dlclose(rga.handle);
    memset(&rga, 0, sizeof(rga));
    return false;
  }

  return true;
}

static void rga_release_frame(struct drm_frame *frame) {
  if (frame->rga_handle && rga.releasebuffer_handle) {
    rga.releasebuffer_handle(frame->rga_handle);
    frame->rga_handle = 0;
    memset(&frame->rga_buffer, 0, sizeof(frame->rga_buffer));
  }
}

static bool rga_import_frame(struct drm_frame *frame, uint32_t width, uint32_t height,
                             uint32_t hor_stride, uint32_t ver_stride) {
  im_handle_param_t param = {
    .width = hor_stride,
    .height = ver_stride,
    .format = RK_FORMAT_YCbCr_420_SP,
  };

  frame->rga_handle = rga.importbuffer_fd(frame->prime_fd, &param);
  if (!frame->rga_handle) {
    fprintf(stderr, "RK RGA: importbuffer_fd failed for fd %d (%ux%u stride %ux%u)\n",
            frame->prime_fd, width, height, hor_stride, ver_stride);
    return false;
  }

  frame->rga_buffer = rga.wrapbuffer_handle_t(frame->rga_handle, width, height,
                                             hor_stride, ver_stride, RK_FORMAT_YCbCr_420_SP);
  return true;
}

static bool rga_rotate_frame(int index) {
  int ret = rga.imrotate_t(frame_to_drm[index].rga_buffer, rga_to_drm[index].rga_buffer,
                           rga_rotation_mode, 1);
  if (ret <= 0) {
    fprintf(stderr, "RK RGA: imrotate_t failed for buffer %d: %d\n", index, ret);
    return false;
  }

  return true;
}

static void rgbframe_close_shared(void) {
  if (rgbframe_base && rgbframe_base != MAP_FAILED) {
    munmap(rgbframe_base, rgbframe_map_size);
  }
  if (rgbframe_fd >= 0) {
    close(rgbframe_fd);
  }

  rgbframe_fd = -1;
  rgbframe_base = NULL;
  rgbframe_map_size = 0;
  rgbframe_hdr = NULL;
  rgbframe_pixels = NULL;
  rgbframe_width = 0;
  rgbframe_height = 0;
  rgbframe_frame_no = 0;
}

static int rgbframe_open_shared(uint32_t source_width, uint32_t source_height) {
  const char *path = getenv("MOONLIGHT_RGBFRAME_SHM");
  const char *format = getenv("MOONLIGHT_RGBFRAME_FORMAT");
  if (!path || path[0] == 0)
    path = "/tmp/moonlight-rgbframe.shm";

  rgbframe_close_shared();

  rgbframe_width = env_u32("MOONLIGHT_RGBFRAME_WIDTH", source_width);
  rgbframe_height = env_u32("MOONLIGHT_RGBFRAME_HEIGHT", source_height);
  rgbframe_format = RGBFRAME_FORMAT_RGBA8888;
  if (format && !strcasecmp(format, "bgra"))
    rgbframe_format = RGBFRAME_FORMAT_BGRA8888;

  const uint32_t stride = rgbframe_width * 4;
  const uint32_t data_size = stride * rgbframe_height;
  rgbframe_map_size = sizeof(struct rgbframe_header) + data_size;

  rgbframe_fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
  if (rgbframe_fd < 0) {
    fprintf(stderr, "RK RGBFrame: unable to open %s: %d\n", path, errno);
    return -1;
  }

  if (ftruncate(rgbframe_fd, (off_t)rgbframe_map_size) != 0) {
    fprintf(stderr, "RK RGBFrame: ftruncate(%s) failed: %d\n", path, errno);
    rgbframe_close_shared();
    return -1;
  }

  rgbframe_base = mmap(NULL, rgbframe_map_size, PROT_READ | PROT_WRITE, MAP_SHARED, rgbframe_fd, 0);
  if (rgbframe_base == MAP_FAILED) {
    fprintf(stderr, "RK RGBFrame: mmap(%s) failed: %d\n", path, errno);
    rgbframe_base = NULL;
    rgbframe_close_shared();
    return -1;
  }

  rgbframe_hdr = (struct rgbframe_header *)rgbframe_base;
  rgbframe_pixels = rgbframe_base + sizeof(struct rgbframe_header);
  memset(rgbframe_base, 0, rgbframe_map_size);

  rgbframe_hdr->magic = RGBFRAME_MAGIC;
  rgbframe_hdr->version = RGBFRAME_VERSION;
  rgbframe_hdr->width = rgbframe_width;
  rgbframe_hdr->height = rgbframe_height;
  rgbframe_hdr->stride = stride;
  rgbframe_hdr->format = rgbframe_format;
  rgbframe_hdr->data_size = data_size;

  printf("RK RGBFrame: %s source %ux%u target %ux%u stride %u format %s\n",
         path, source_width, source_height, rgbframe_width, rgbframe_height, stride,
         rgbframe_format == RGBFRAME_FORMAT_BGRA8888 ? "BGRA" : "RGBA");
  return 0;
}

static uint8_t rgbframe_clip_byte(int value) {
  if (value < 0)
    return 0;
  if (value > 255)
    return 255;
  return (uint8_t)value;
}

static void rgbframe_write_nv12(uint8_t *src, uint32_t source_width, uint32_t source_height,
                                uint32_t hor_stride, uint32_t ver_stride) {
  if (!rgbframe_hdr || !rgbframe_pixels || !src || rgbframe_width == 0 || rgbframe_height == 0)
    return;

  const uint8_t *y_base = src;
  const uint8_t *uv_base = src + (size_t)hor_stride * ver_stride;

  for (uint32_t y = 0; y < rgbframe_height; y++) {
    uint32_t sy = ((uint64_t)y * source_height) / rgbframe_height;
    const uint8_t *y_row = y_base + (size_t)sy * hor_stride;
    const uint8_t *uv_row = uv_base + (size_t)(sy / 2) * hor_stride;
    uint8_t *dst_row = rgbframe_pixels + (size_t)y * rgbframe_hdr->stride;

    for (uint32_t x = 0; x < rgbframe_width; x++) {
      uint32_t sx = ((uint64_t)x * source_width) / rgbframe_width;
      uint32_t uv_x = sx & ~1U;
      int y_value = y_row[sx];
      int u_value = uv_row[uv_x];
      int v_value = uv_row[uv_x + 1];

      int c = y_value - 16;
      int d = u_value - 128;
      int e = v_value - 128;
      if (c < 0)
        c = 0;

      uint8_t r = rgbframe_clip_byte((298 * c + 409 * e + 128) >> 8);
      uint8_t g = rgbframe_clip_byte((298 * c - 100 * d - 208 * e + 128) >> 8);
      uint8_t b = rgbframe_clip_byte((298 * c + 516 * d + 128) >> 8);
      uint8_t *px = dst_row + x * 4;

      if (rgbframe_format == RGBFRAME_FORMAT_BGRA8888) {
        px[0] = b;
        px[1] = g;
        px[2] = r;
      } else {
        px[0] = r;
        px[1] = g;
        px[2] = b;
      }
      px[3] = 0xff;
    }
  }

  __sync_synchronize();
  rgbframe_hdr->frame_no = ++rgbframe_frame_no;
}

int set_property(uint32_t id, uint32_t type, drmModePropertyPtr *props, const char *name, uint64_t value) {
  drmModePropertyPtr prop = find_property(props, name);
  if (prop) {
    if (atomic)
      return drmModeAtomicAddProperty(drm_request, id, prop->prop_id, value);
    else
      return drmModeObjectSetProperty(fd, id, type, prop->prop_id, value);
  }

  fprintf(stderr, "Property '%s' not found\n", name);
  return -EINVAL;
}

static int set_property_optional(uint32_t id, uint32_t type, drmModePropertyPtr *props, const char *name, uint64_t value) {
  drmModePropertyPtr prop = find_property(props, name);
  if (!prop)
    return 0;

  if (atomic)
    return drmModeAtomicAddProperty(drm_request, id, prop->prop_id, value);
  else
    return drmModeObjectSetProperty(fd, id, type, prop->prop_id, value);
}

static int set_object_property_optional(uint32_t id, uint32_t type, const char *name, uint64_t value) {
  drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(fd, id, type);
  if (!props)
    return 0;

  int ret = 0;
  for (uint32_t i = 0; i < props->count_props; i++) {
    drmModePropertyPtr prop = drmModeGetProperty(fd, props->props[i]);
    if (!prop)
      continue;

    if (!strcasecmp(prop->name, name)) {
      if (atomic)
        ret = drmModeAtomicAddProperty(drm_request, id, prop->prop_id, value);
      else
        ret = drmModeObjectSetProperty(fd, id, type, prop->prop_id, value);

      drmModeFreeProperty(prop);
      break;
    }

    drmModeFreeProperty(prop);
  }

  drmModeFreeObjectProperties(props);
  return ret;
}

void *display_thread(void *param) {
  int ret;
  while (!frm_eos) {
    int _fb_id;

    pthread_mutex_lock(&mutex);
    while (fb_id == 0) {
      pthread_cond_wait(&cond, &mutex);
      if (fb_id == 0 && frm_eos) {
        pthread_mutex_unlock(&mutex);
        return NULL;
      }
    }
    _fb_id = fb_id;

    fb_id = 0;
    pthread_mutex_unlock(&mutex);

    if (atomic) {
      // We may need to modeset to apply colorspace changes when toggling HDR
      set_property(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "FB_ID", _fb_id);
      ret = drmModeAtomicCommit(fd, drm_request, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
      if (ret) {
        perror("drmModeAtomicCommit");
      }
    } else {
      ret = drmModeSetPlane(fd, plane_id, crtc_id, _fb_id, 0,
                            fb_x, fb_y, fb_width, fb_height,
                            0, 0, drm_frame_width << 16, drm_frame_height << 16);
      if (ret) {
        perror("drmModeSetPlane");
      }
    }
  }

  return NULL;
}

void *frame_thread(void *param) {

  int count = 0;
  int ret;
  int i;
  MppFrame frame = NULL;

  while (!frm_eos) {

    ret = mpi_api->decode_get_frame(mpi_ctx, &frame);
    if (ret != MPP_OK && ret != MPP_ERR_TIMEOUT) {
      if (count < 3) {
         fprintf(stderr, "Waiting for Frame (return code = %d, retry count = %d)\n", ret, count);
         usleep(10000);
         count++;
         continue;
      }
    }
    if (frame) {
      if (mpp_frame_get_info_change(frame)) {
        // new resolution
        assert(!mpi_frm_grp);

        frm_width = mpp_frame_get_width(frame);
        frm_height = mpp_frame_get_height(frame);
        RK_U32 hor_stride = mpp_frame_get_hor_stride(frame);
        RK_U32 ver_stride = mpp_frame_get_ver_stride(frame);
        MppFrameFormat fmt = mpp_frame_get_fmt(frame);
        frm_hor_stride = hor_stride;
        frm_ver_stride = ver_stride;
        frm_format = fmt;
        if (fmt != MPP_FMT_YUV420SP && fmt != MPP_FMT_YUV420SP_10BIT) {
          fprintf(stderr, "Unsupported MPP frame format: %d\n", fmt);
          mpp_frame_deinit(&frame);
          frame = NULL;
          continue;
        }

        if (rgbframe_mode) {
          if (fmt != MPP_FMT_YUV420SP) {
            fprintf(stderr, "RK RGBFrame: unsupported MPP frame format for canvas: %d\n", fmt);
            frm_eos = 1;
            mpp_frame_deinit(&frame);
            frame = NULL;
            continue;
          }

          assert(!mpi_frm_grp);
          ret = mpp_buffer_group_get_internal(&mpi_frm_grp, MPP_BUFFER_TYPE_ION);
          assert(!ret);

          size_t buffer_size = (size_t)hor_stride * ver_stride * 2;
          ret = mpp_buffer_group_limit_config(mpi_frm_grp, buffer_size, MAX_FRAMES);
          if (ret) {
            fprintf(stderr, "RK RGBFrame: mpp_buffer_group_limit_config failed: %d\n", ret);
          }

          ret = mpi_api->control(mpi_ctx, MPP_DEC_SET_EXT_BUF_GROUP, mpi_frm_grp);
          assert(!ret);
          ret = mpi_api->control(mpi_ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
          assert(!ret);

          if (rgbframe_open_shared(frm_width, frm_height) != 0)
            frm_eos = 1;

          mpp_frame_deinit(&frame);
          frame = NULL;
          continue;
        }

        if (rga_rotation && fmt != MPP_FMT_YUV420SP) {
          fprintf(stderr, "RK RGA: rotation only supports 8-bit NV12 frames\n");
          frm_eos = 1;
          mpp_frame_deinit(&frame);
          frame = NULL;
          continue;
        }

        drm_frame_width = rga_rotation ? rk_target_width : frm_width;
        drm_frame_height = rga_rotation ? rk_target_height : frm_height;
        rga_output_hor_stride = ALIGN_UP(drm_frame_width, 16);
        rga_output_ver_stride = ALIGN_UP(drm_frame_height, 16);

        if (drm_stretch) {
          fb_width = crtc_width;
          fb_height = crtc_height;
          fb_x = 0;
          fb_y = 0;
        } else {
          // position overlay, scale to ratio
          float crt_ratio = (float)crtc_width/crtc_height;
          float frame_ratio = rga_rotation ? (float)rk_target_width/rk_target_height :
                              (drm_rotate_swap ? (float)frm_height/frm_width : (float)frm_width/frm_height);

          if (crt_ratio>frame_ratio) {
            fb_width = frame_ratio/crt_ratio*crtc_width;
            fb_height = crtc_height;
            fb_x = (crtc_width-fb_width)/2;
            fb_y = 0;
          } else {
            fb_width = crtc_width;
            fb_height = crt_ratio/frame_ratio*crtc_height;
            fb_x = 0;
            fb_y = (crtc_height-fb_height)/2;
          }
        }

        // create new external frame group and allocate (commit flow) new DRM buffers and DRM FB
        assert(!mpi_frm_grp);
        ret = mpp_buffer_group_get_external(&mpi_frm_grp, MPP_BUFFER_TYPE_DRM);
        assert(!ret);
        for (i = 0; i < MAX_FRAMES; i++) {

          // new DRM buffer
          struct drm_mode_create_dumb dmcd = {0};
          dmcd.bpp = 8; // hor_stride is already adjusted for 10 vs 8 bit
          dmcd.width = hor_stride;
          dmcd.height = ver_stride * 2; // documentation say not v*2/3 but v*2 (additional info included)
          ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &dmcd);
          if (ret) {
            perror("drmIoctl(DRM_IOCTL_MODE_CREATE_DUMB)");
            exit(EXIT_FAILURE);
          }
          assert(dmcd.pitch == dmcd.width);
          assert(dmcd.size == dmcd.pitch * dmcd.height);
          frame_to_drm[i].handle = dmcd.handle;

          // commit DRM buffer to frame group
          struct drm_prime_handle dph = {0};
          dph.handle = dmcd.handle;
          dph.fd = -1;
          ret = drmIoctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &dph);
          if (ret) {
            perror("drmIoctl(DRM_IOCTL_PRIME_HANDLE_TO_FD)");
            exit(EXIT_FAILURE);
          }
          MppBufferInfo info = {0};
          info.type = MPP_BUFFER_TYPE_DRM;
          info.size = dmcd.width * dmcd.height;
          info.fd = dph.fd;
          ret = mpp_buffer_commit(mpi_frm_grp, &info);
          assert(!ret);
          frame_to_drm[i].prime_fd = info.fd; // dups fd

          if (rga_rotation && !rga_import_frame(&frame_to_drm[i], frm_width, frm_height,
                                                frm_hor_stride, frm_ver_stride)) {
            exit(EXIT_FAILURE);
          }

          // allocate DRM FB from DRM buffer
          uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
          handles[0] = frame_to_drm[i].handle;
          offsets[0] = 0;
          pitches[0] = dmcd.pitch;
          handles[1] = frame_to_drm[i].handle;
          offsets[1] = pitches[0] * ver_stride;
          pitches[1] = dmcd.pitch;
          ret = drmModeAddFB2(fd, frm_width, frm_height, pixel_format, handles, pitches, offsets, &frame_to_drm[i].fb_id, 0);
          if (ret) {
            perror("drmModeAddFB2");
            exit(EXIT_FAILURE);
          }
        }
        if (rga_rotation) {
          for (i = 0; i < MAX_FRAMES; i++) {
            struct drm_mode_create_dumb dmcd = {0};
            dmcd.bpp = 8;
            dmcd.width = rga_output_hor_stride;
            dmcd.height = rga_output_ver_stride * 2;
            ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &dmcd);
            if (ret) {
              perror("drmIoctl(DRM_IOCTL_MODE_CREATE_DUMB)");
              exit(EXIT_FAILURE);
            }
            if (dmcd.pitch < rga_output_hor_stride) {
              fprintf(stderr, "RK RGA: output pitch %u smaller than stride %u\n",
                      dmcd.pitch, rga_output_hor_stride);
              exit(EXIT_FAILURE);
            }
            rga_to_drm[i].handle = dmcd.handle;

            struct drm_prime_handle dph = {0};
            dph.handle = dmcd.handle;
            dph.fd = -1;
            ret = drmIoctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &dph);
            if (ret) {
              perror("drmIoctl(DRM_IOCTL_PRIME_HANDLE_TO_FD)");
              exit(EXIT_FAILURE);
            }
            rga_to_drm[i].prime_fd = dph.fd;

            if (!rga_import_frame(&rga_to_drm[i], drm_frame_width, drm_frame_height,
                                  dmcd.pitch, rga_output_ver_stride)) {
              exit(EXIT_FAILURE);
            }

            uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
            handles[0] = rga_to_drm[i].handle;
            offsets[0] = 0;
            pitches[0] = dmcd.pitch;
            handles[1] = rga_to_drm[i].handle;
            offsets[1] = pitches[0] * rga_output_ver_stride;
            pitches[1] = dmcd.pitch;
            ret = drmModeAddFB2(fd, drm_frame_width, drm_frame_height, pixel_format,
                                handles, pitches, offsets, &rga_to_drm[i].fb_id, 0);
            if (ret) {
              perror("drmModeAddFB2");
              exit(EXIT_FAILURE);
            }
          }

          fprintf(stderr, "RK RGA: rotating source %ux%u stride %ux%u to %ux%u stride %ux%u\n",
                  frm_width, frm_height, frm_hor_stride, frm_ver_stride,
                  drm_frame_width, drm_frame_height, rga_to_drm[0].rga_buffer.wstride,
                  rga_output_ver_stride);
        }
        // register external frame group
        ret = mpi_api->control(mpi_ctx, MPP_DEC_SET_EXT_BUF_GROUP, mpi_frm_grp);
        ret = mpi_api->control(mpi_ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);

        // Set atomic properties for the plane prior to the first commit
        if (atomic) {
          set_property(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "CRTC_ID", crtc_id);
          set_property(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "SRC_X", 0 << 16);
          set_property(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "SRC_Y", 0 << 16);
          set_property(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "SRC_W", drm_frame_width << 16);
          set_property(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "SRC_H", drm_frame_height << 16);
          set_property(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "CRTC_X", fb_x);
          set_property(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "CRTC_Y", fb_y);
          set_property(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "CRTC_W", fb_width);
          set_property(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "CRTC_H", fb_height);
          set_property_optional(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "zpos", drm_zpos);
          set_property_optional(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "alpha", 65535);
          set_property_optional(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "pixel blend mode", 2);
        }

        // Set properties on the connector
        set_property_optional(conn_id, DRM_MODE_OBJECT_CONNECTOR, conn_props, "allm_enable", 1); // HDMI ALLM (Game Mode)
        set_property_optional(conn_id, DRM_MODE_OBJECT_CONNECTOR, conn_props, "Colorspace", last_hdr_state ? DRM_MODE_COLORIMETRY_BT2020_RGB : DRM_MODE_COLORIMETRY_DEFAULT);
      } else {
        // regular frame received

        MppBuffer buffer = mpp_frame_get_buffer(frame);
        if (rgbframe_mode) {
          if (buffer) {
            uint8_t *src = NULL;
            src = (uint8_t *)mpp_buffer_get_ptr(buffer);
            if (src) {
              RK_U32 hor_stride = mpp_frame_get_hor_stride(frame);
              RK_U32 ver_stride = mpp_frame_get_ver_stride(frame);
              if (!hor_stride)
                hor_stride = frm_hor_stride;
              if (!ver_stride)
                ver_stride = frm_ver_stride;

              rgbframe_write_nv12(src, frm_width, frm_height, hor_stride, ver_stride);
            } else {
              fprintf(stderr, "RK RGBFrame: frame buffer has no CPU pointer\n");
            }
          } else {
            fprintf(stderr, "RK RGBFrame: frame no buff\n");
          }
        } else if (buffer) {
          // find fb_id by frame prime_fd
          MppBufferInfo info;
          ret = mpp_buffer_info_get(buffer, &info);
          assert(!ret);
          for (i = 0; i < MAX_FRAMES; i++) {
            if (frame_to_drm[i].prime_fd == info.fd) break;
          }
          assert(i != MAX_FRAMES);
          int display_fb_id = frame_to_drm[i].fb_id;
          if (rga_rotation) {
            if (!rga_rotate_frame(i)) {
              frm_eos = 1;
              mpp_frame_deinit(&frame);
              frame = NULL;
              continue;
            }
            display_fb_id = rga_to_drm[i].fb_id;
          }
          // send DRM FB to display thread
          pthread_mutex_lock(&mutex);
          fb_id = display_fb_id;
          pthread_cond_signal(&cond);
          pthread_mutex_unlock(&mutex);
        } else {
          fprintf(stderr, "Frame no buff\n");
        }
      }

      frm_eos = mpp_frame_get_eos(frame);
      mpp_frame_deinit(&frame);
      frame = NULL;
    } else {
      if (!frm_eos) {
        fprintf(stderr, "Didn't get frame from MPP (return code = %d)\n", ret);
      }
      break;
    }
  }

  return NULL;
}

int rk_setup(int videoFormat, int width, int height, int redrawRate, void* context, int drFlags) {

  int ret;
  int i;
  int j;
  int format;
  const char *drm_card = getenv("MOONLIGHT_RK_DRM_CARD");
  const char *connector_env = getenv("MOONLIGHT_RK_DRM_CONNECTOR");
  const char *plane_env = getenv("MOONLIGHT_RK_DRM_PLANE");
  const char *zpos_env = getenv("MOONLIGHT_RK_DRM_ZPOS");
  uint32_t requested_connector = connector_env ? strtoul(connector_env, NULL, 0) : 0;
  uint32_t requested_plane = plane_env ? strtoul(plane_env, NULL, 0) : 0;
  uint64_t requested_zpos = zpos_env ? strtoull(zpos_env, NULL, 0) : (uint64_t)-1;

  if (drm_card == NULL || drm_card[0] == 0)
    drm_card = "/dev/dri/card0";

  fb_id = 0;
  fd = -1;
  frm_eos = 0;
  mpi_frm_grp = NULL;
  hdr_metadata_blob_id = 0;
  plane_id = 0;
  frm_hor_stride = 0;
  frm_ver_stride = 0;
  drm_frame_width = 0;
  drm_frame_height = 0;
  rga_output_hor_stride = 0;
  rga_output_ver_stride = 0;
  frame_thread_started = false;
  display_thread_started = false;
  memset(frame_to_drm, 0, sizeof(frame_to_drm));
  memset(rga_to_drm, 0, sizeof(rga_to_drm));
  rgbframe_mode = env_enabled("MOONLIGHT_RK_RGBFRAME", false);
  drm_stretch = env_enabled("MOONLIGHT_RK_DRM_STRETCH", false);
  drm_takeover = env_enabled("MOONLIGHT_RK_DRM_TAKEOVER", true);
  drm_rotate_swap = false;
  rga_rotation = false;
  rga_rotation_mode = 0;
  rk_target_width = 0;
  rk_target_height = 0;
  drm_zpos = requested_zpos;
  last_colorspace = 0xFF;
  last_hdr_state = false;

  if (videoFormat & VIDEO_FORMAT_MASK_H264) {
    format = RK_H264;
  } else if (videoFormat & VIDEO_FORMAT_MASK_H265) {
    format = RK_H265;
  } else if (videoFormat & VIDEO_FORMAT_MASK_AV1) {
    format = RK_AV1;
  } else {
    fprintf(stderr, "Video format not supported\n");
    return -1;
  }

  // We need atomic plane properties for HDR, but atomic seems to perform quite bad
  // on RK3588 for some reason. The performance of commits seems to go down the longer
  // the stream runs. We'll use the legacy API for non-HDR streams as a workaround.
  atomic = !!(videoFormat & VIDEO_FORMAT_MASK_10BIT);

  MppCodingType mpp_type = (MppCodingType)format;
  ret = mpp_check_support_format(MPP_CTX_DEC, mpp_type);
  if (ret) {
    fprintf(stderr, "Selected video format is not supported\n");
    return -1;
  }

  if (!rgbframe_mode) {
  fd = open(drm_card, O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    fprintf(stderr, "Unable to open %s: %d\n", drm_card, errno);
    return -1;
  }

  resources = drmModeGetResources(fd);
  if (!resources) {
    perror("drmModeGetResources");
    return -1;
  }

  // find active monitor
  for (i = 0; i < resources->count_connectors; ++i) {
    connector = drmModeGetConnector(fd, resources->connectors[i]);
    if (!connector) {
      continue;
    }
    if ((!requested_connector || connector->connector_id == requested_connector) &&
        connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) {
      break;
    }
    drmModeFreeConnector(connector);
  }
  assert(i < resources->count_connectors);

  conn_id = connector->connector_id;

  {
    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(fd, conn_id, DRM_MODE_OBJECT_CONNECTOR);
    assert(props->count_props < sizeof(conn_props) / sizeof(conn_props[0]));
    for (j = 0; j < props->count_props; j++) {
      drmModePropertyPtr prop = drmModeGetProperty(fd, props->props[j]);
      if (!prop) {
        continue;
      }
      if (!strcmp(prop->name, "HDR_OUTPUT_METADATA")) {
        hdr_metadata_prop = prop;
      }
      conn_props[j] = prop;
    }
    drmModeFreeObjectProperties(props);
  }

  for (i = 0; i < resources->count_encoders; ++i) {
    encoder = drmModeGetEncoder(fd, resources->encoders[i]);
    if (!encoder) {
      continue;
    }
    if (encoder->encoder_id == connector->encoder_id) {
      break;
    }
    drmModeFreeEncoder(encoder);
  }
  assert(i < resources->count_encoders);

  for (i = 0; i < resources->count_crtcs; ++i) {
    if (resources->crtcs[i] == encoder->crtc_id) {
      crtc = drmModeGetCrtc(fd, resources->crtcs[i]);
      if (!crtc) {
        perror("drmModeGetCrtc");
        continue;
      }
      break;
    }
  }
  assert(i < resources->count_crtcs);
  crtc_id = crtc->crtc_id;
  crtc_width = crtc->width;
  crtc_height = crtc->height;
  uint32_t crtc_bit = (1 << i);

  ret = drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
  if (ret) {
    perror("drmSetClientCap(DRM_CLIENT_CAP_UNIVERSAL_PLANES)");
  }
  if (atomic) {
    ret = drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
    if (ret) {
      perror("drmSetClientCap(DRM_CLIENT_CAP_ATOMIC)");
      atomic = false;
    }
    else {
      drm_request = drmModeAtomicAlloc();
      assert(drm_request);
    }
  }
  plane_resources = drmModeGetPlaneResources(fd);
  if (!plane_resources) {
    perror("drmModeGetPlaneResources");
    return -1;
  }

  // search for OVERLAY (for active connector, unused, NV12 support)
  for (i = 0; i < plane_resources->count_planes; i++) {
    ovr = drmModeGetPlane(fd, plane_resources->planes[i]);
    if (!ovr) {
      continue;
    }
    if (requested_plane && ovr->plane_id != requested_plane) {
      drmModeFreePlane(ovr);
      continue;
    }
    for (j = 0; j < ovr->count_formats; j++) {
      if (videoFormat & VIDEO_FORMAT_MASK_10BIT) {
        // 10-bit formats use NA12 (vendor-defined) or NV15 (upstreamed in 5.10+)
        if (ovr->formats[j] == DRM_FORMAT_NA12 || ovr->formats[j] == DRM_FORMAT_NV15) {
          break;
        }
      } else if (ovr->formats[j] == DRM_FORMAT_NV12) {
        // 8-bit formats always use NV12
        break;
      }
    }
    if (j < ovr->count_formats) {
      pixel_format = ovr->formats[j];
    } else {
      continue;
    }
    if ((ovr->possible_crtcs & crtc_bit) && !ovr->crtc_id) {
      drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(fd, plane_resources->planes[i], DRM_MODE_OBJECT_PLANE);
      if (!props) {
        continue;
      }

      assert(props->count_props < sizeof(plane_props) / sizeof(plane_props[0]));
      for (j = 0; j < props->count_props; j++) {
        drmModePropertyPtr prop = drmModeGetProperty(fd, props->props[j]);
        if (!prop) {
          continue;
        }
        plane_props[j] = prop;
        if (!strcmp(prop->name, "type") && (props->prop_values[j] == DRM_PLANE_TYPE_OVERLAY ||
                                            props->prop_values[j] == DRM_PLANE_TYPE_CURSOR ||
                                            props->prop_values[j] == DRM_PLANE_TYPE_PRIMARY)) {
          plane_id = ovr->plane_id;
        }
      }
      if (plane_id) {
        drmModeFreeObjectProperties(props);
        break;
      } else {
        for (j = 0; j < props->count_props; j++) {
          drmModeFreeProperty(plane_props[j]);
          plane_props[j] = NULL;
        }
        drmModeFreeObjectProperties(props);
      }
    }
    drmModeFreePlane(ovr);
  }

  if (!plane_id) {
    fprintf(stderr, "Unable to find suitable plane\n");
    return -1;
  }

  drmModePropertyPtr zpos_prop = find_property(plane_props, "zpos");
  if (drm_zpos == (uint64_t)-1)
    drm_zpos = property_range_max(zpos_prop, 3);

  char fourcc[5] = {
    pixel_format & 0xFF,
    (pixel_format >> 8) & 0xFF,
    (pixel_format >> 16) & 0xFF,
    (pixel_format >> 24) & 0xFF,
    0,
  };
  printf("RK DRM: %s connector %u crtc %u %dx%d plane %u format %s zpos %llu%s%s\n",
         drm_card, conn_id, crtc_id, crtc_width, crtc_height, plane_id, fourcc,
         (unsigned long long)drm_zpos,
         drm_takeover ? " takeover" : "",
         drm_stretch ? " stretch" : "");

  // DRM defines rotation in degrees counter-clockwise while we define
  // rotation in degrees clockwise, so we swap the 90 and 270 cases
  int displayRotation = drFlags & DISPLAY_ROTATE_MASK;
  drm_rotate_swap = displayRotation == DISPLAY_ROTATE_90 || displayRotation == DISPLAY_ROTATE_270;
  uint32_t fallback_target_width = drm_rotate_swap ? height : width;
  uint32_t fallback_target_height = drm_rotate_swap ? width : height;
  rk_target_width = env_u32("MOONLIGHT_RK_TARGET_WIDTH", fallback_target_width);
  rk_target_height = env_u32("MOONLIGHT_RK_TARGET_HEIGHT", fallback_target_height);
  uint64_t drmRotation = DRM_MODE_ROTATE_0;
  switch (displayRotation) {
  case DISPLAY_ROTATE_90:
    drmRotation = DRM_MODE_ROTATE_270;
    rga_rotation_mode = IM_HAL_TRANSFORM_ROT_90;
    break;
  case DISPLAY_ROTATE_180:
    drmRotation = DRM_MODE_ROTATE_180;
    rga_rotation_mode = IM_HAL_TRANSFORM_ROT_180;
    break;
  case DISPLAY_ROTATE_270:
    drmRotation = DRM_MODE_ROTATE_90;
    rga_rotation_mode = IM_HAL_TRANSFORM_ROT_270;
    break;
  default:
    drmRotation = DRM_MODE_ROTATE_0;
    rga_rotation_mode = 0;
    break;
  }

  drmModePropertyPtr rotation_prop = find_property(plane_props, "rotation");
  if (rotation_prop && property_supports_bitmask_value(rotation_prop, drmRotation)) {
    set_property(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "rotation", drmRotation);
  } else if (drmRotation != DRM_MODE_ROTATE_0) {
    if (videoFormat & VIDEO_FORMAT_MASK_10BIT) {
      fprintf(stderr, "RK RGA: software-managed rotation does not support 10-bit formats\n");
      return -1;
    }
    if (!rga_load()) {
      fprintf(stderr, "RK RGA: unable to enable rotation fallback\n");
      return -1;
    }
    rga_rotation = true;
    fprintf(stderr, "RK DRM plane %u does not support requested rotation; using RGA rotation to %ux%u\n",
            plane_id, rk_target_width, rk_target_height);
  }

  if (drm_takeover) {
    for (i = 0; i < plane_resources->count_planes; i++) {
      drmModePlanePtr plane = drmModeGetPlane(fd, plane_resources->planes[i]);
      if (!plane)
        continue;

      if (plane->plane_id != plane_id && plane->crtc_id == crtc_id)
        set_object_property_optional(plane->plane_id, DRM_MODE_OBJECT_PLANE, "zpos", 0);

      drmModeFreePlane(plane);
    }

    set_property_optional(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "zpos", drm_zpos);
    set_property_optional(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "alpha", 65535);
    set_property_optional(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "pixel blend mode", 2);
  }

  // hide cursor by move in left lower corner
  drmModeMoveCursor(fd, crtc_id, 0, crtc_height);
  } else {
    printf("RK RGBFrame: using shared canvas output, DRM plane disabled\n");
  }

  // MPI SETUP

  ensure_buf_size(&pkt_buf, &pkt_buf_size, INITIAL_DECODER_BUFFER_SIZE);
  ret = mpp_packet_init(&mpi_packet, pkt_buf, pkt_buf_size);
  assert(!ret);

  ret = mpp_create(&mpi_ctx, &mpi_api);
  if (ret) {
    fprintf(stderr, "mpp_create() failed: %d\n", ret);
    return -1;
  }

  // decoder split mode (multi-data-input) need to be set before init
  int param = 1;
  ret = mpi_api->control(mpi_ctx, MPP_DEC_SET_PARSER_SPLIT_MODE, &param);
  assert(!ret);

  ret = mpp_init(mpi_ctx, MPP_CTX_DEC, mpp_type);
  if (ret) {
    fprintf(stderr, "mpp_init() failed: %d\n", ret);
    return -1;
  }

  // set blocked read on Frame Thread
  param = MPP_POLL_BLOCK;
  ret = mpi_api->control(mpi_ctx, MPP_SET_OUTPUT_BLOCK, &param);
  assert(!ret);

  pthread_mutex_init(&mutex, NULL);
  pthread_cond_init(&cond, NULL);

  pthread_create(&tid_frame, NULL, frame_thread, NULL);
  frame_thread_started = true;
  if (!rgbframe_mode) {
    pthread_create(&tid_display, NULL, display_thread, NULL);
    display_thread_started = true;
  }

  return 0;
}

void rk_cleanup() {

  int i;
  int ret;

  frm_eos = 1;
  pthread_mutex_lock(&mutex);
  pthread_cond_signal(&cond);
  pthread_mutex_unlock(&mutex);

  if (display_thread_started)
    pthread_join(tid_display, NULL);

  pthread_cond_destroy(&cond);
  pthread_mutex_destroy(&mutex);

  ret = mpi_api->reset(mpi_ctx);
  assert(!ret);

  if (frame_thread_started)
    pthread_join(tid_frame, NULL);

  if (mpi_frm_grp) {
    ret = mpp_buffer_group_put(mpi_frm_grp);
    assert(!ret);
    mpi_frm_grp = NULL;
    if (!rgbframe_mode) {
      for (i = 0; i < MAX_FRAMES; i++) {
        rga_release_frame(&frame_to_drm[i]);
        rga_release_frame(&rga_to_drm[i]);

        if (frame_to_drm[i].fb_id) {
          ret = drmModeRmFB(fd, frame_to_drm[i].fb_id);
          if (ret) {
            perror("drmModeRmFB");
          }
          frame_to_drm[i].fb_id = 0;
        }
        if (rga_to_drm[i].fb_id) {
          ret = drmModeRmFB(fd, rga_to_drm[i].fb_id);
          if (ret) {
            perror("drmModeRmFB");
          }
          rga_to_drm[i].fb_id = 0;
        }

        if (frame_to_drm[i].handle) {
          struct drm_mode_destroy_dumb dmdd = {0};
          dmdd.handle = frame_to_drm[i].handle;
          ret = drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dmdd);
          if (ret) {
            perror("drmIoctl(DRM_IOCTL_MODE_DESTROY_DUMB)");
          }
          frame_to_drm[i].handle = 0;
        }
        if (rga_to_drm[i].handle) {
          struct drm_mode_destroy_dumb dmdd = {0};
          dmdd.handle = rga_to_drm[i].handle;
          ret = drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dmdd);
          if (ret) {
            perror("drmIoctl(DRM_IOCTL_MODE_DESTROY_DUMB)");
          }
          rga_to_drm[i].handle = 0;
        }

        if (frame_to_drm[i].prime_fd > 0) {
          close(frame_to_drm[i].prime_fd);
          frame_to_drm[i].prime_fd = 0;
        }
        if (rga_to_drm[i].prime_fd > 0) {
          close(rga_to_drm[i].prime_fd);
          rga_to_drm[i].prime_fd = 0;
        }
      }
    }
  }

  mpp_packet_deinit(&mpi_packet);
  mpp_destroy(mpi_ctx);
  free(pkt_buf);
  pkt_buf = NULL;
  pkt_buf_size = 0;
  rgbframe_close_shared();

  if (rgbframe_mode)
    return;

  // Undo the connector-wide changes we performed
  if (atomic)
    drmModeAtomicSetCursor(drm_request, 0);
  set_property_optional(conn_id, DRM_MODE_OBJECT_CONNECTOR, conn_props, "HDR_OUTPUT_METADATA", 0);
  set_property_optional(conn_id, DRM_MODE_OBJECT_CONNECTOR, conn_props, "allm_enable", 0);
  set_property_optional(conn_id, DRM_MODE_OBJECT_CONNECTOR, conn_props, "Colorspace", DRM_MODE_COLORIMETRY_DEFAULT);
  if (atomic)
    drmModeAtomicCommit(fd, drm_request, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);

  if (hdr_metadata_blob_id) {
    drmModeDestroyPropertyBlob(fd, hdr_metadata_blob_id);
    hdr_metadata_blob_id = 0;
  }

  if (atomic)
    drmModeAtomicFree(drm_request);
  drmModeFreePlane(ovr);
  drmModeFreePlaneResources(plane_resources);
  drmModeFreeEncoder(encoder);
  drmModeFreeConnector(connector);
  drmModeFreeCrtc(crtc);
  drmModeFreeResources(resources);

  close(fd);

  if (rga.handle) {
    dlclose(rga.handle);
    memset(&rga, 0, sizeof(rga));
  }
}

int rk_submit_decode_unit(PDECODE_UNIT decodeUnit) {

  int result = DR_OK;
  PLENTRY entry = decodeUnit->bufferList;
  int length = 0;

  if (ensure_buf_size(&pkt_buf, &pkt_buf_size, decodeUnit->fullLength)) {
    // Buffer was reallocated, so update the mpp_packet accordingly
    mpp_packet_set_data(mpi_packet, pkt_buf);
    mpp_packet_set_size(mpi_packet, pkt_buf_size);
  }

  while (entry != NULL) {
    memcpy(pkt_buf+length, entry->data, entry->length);
    length += entry->length;
    entry = entry->next;
  }

  mpp_packet_set_pos(mpi_packet, pkt_buf);
  mpp_packet_set_length(mpi_packet, length);

  if (!rgbframe_mode && last_hdr_state != decodeUnit->hdrActive) {
    if (hdr_metadata_prop != NULL) {
      int err;

      if (hdr_metadata_blob_id) {
        drmModeDestroyPropertyBlob(fd, hdr_metadata_blob_id);
        hdr_metadata_blob_id = 0;
      }

      if (decodeUnit->hdrActive) {
        struct rk_hdr_output_metadata outputMetadata;
        SS_HDR_METADATA sunshineHdrMetadata;

        // Sunshine will have HDR metadata but GFE will not
        if (!LiGetHdrMetadata(&sunshineHdrMetadata)) {
          memset(&sunshineHdrMetadata, 0, sizeof(sunshineHdrMetadata));
        }

        outputMetadata.metadata_type = 0; // HDMI_STATIC_METADATA_TYPE1
        outputMetadata.hdmi_metadata_type1.eotf = 2; // SMPTE ST 2084
        outputMetadata.hdmi_metadata_type1.metadata_type = 0; // Static Metadata Type 1
        for (int i = 0; i < 3; i++) {
          outputMetadata.hdmi_metadata_type1.display_primaries[i].x = sunshineHdrMetadata.displayPrimaries[i].x;
          outputMetadata.hdmi_metadata_type1.display_primaries[i].y = sunshineHdrMetadata.displayPrimaries[i].y;
        }
        outputMetadata.hdmi_metadata_type1.white_point.x = sunshineHdrMetadata.whitePoint.x;
        outputMetadata.hdmi_metadata_type1.white_point.y = sunshineHdrMetadata.whitePoint.y;
        outputMetadata.hdmi_metadata_type1.max_display_mastering_luminance = sunshineHdrMetadata.maxDisplayLuminance;
        outputMetadata.hdmi_metadata_type1.min_display_mastering_luminance = sunshineHdrMetadata.minDisplayLuminance;
        outputMetadata.hdmi_metadata_type1.max_cll = sunshineHdrMetadata.maxContentLightLevel;
        outputMetadata.hdmi_metadata_type1.max_fall = sunshineHdrMetadata.maxFrameAverageLightLevel;

        err = drmModeCreatePropertyBlob(fd, &outputMetadata, sizeof(outputMetadata), &hdr_metadata_blob_id);
        if (err < 0) {
          hdr_metadata_blob_id = 0;
          fprintf(stderr, "Failed to create HDR metadata blob: %d\n", errno);
        }
      }

      err = drmModeObjectSetProperty(fd, conn_id, DRM_MODE_OBJECT_CONNECTOR, hdr_metadata_prop->prop_id, hdr_metadata_blob_id);
      if (err < 0) {
        fprintf(stderr, "Failed to set HDR metadata: %d\n", errno);
      } else {
        printf("Set display HDR mode: %s\n", decodeUnit->hdrActive ? "active" : "inactive");
      }
    } else {
      fprintf(stderr, "HDR_OUTPUT_METADATA property is not supported by your display/kernel. Do you have an HDR display connected?\n");
    }

    // Adjust plane EOTF property
    set_property_optional(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "EOTF", decodeUnit->hdrActive ? 2 : 0); // PQ or SDR
  }

  if (!rgbframe_mode && last_colorspace != decodeUnit->colorspace) {
    uint32_t v4l2_colorspace;
    switch (decodeUnit->colorspace) {
    default:
      fprintf(stderr, "Unknown frame colorspace: %d\n", decodeUnit->colorspace);
      /* fall-through */
    case COLORSPACE_REC_601:
      v4l2_colorspace = V4L2_COLORSPACE_SMPTE170M;
      break;
    case COLORSPACE_REC_709:
      v4l2_colorspace = V4L2_COLORSPACE_REC709;
      break;
    case COLORSPACE_REC_2020:
      v4l2_colorspace = V4L2_COLORSPACE_BT2020;
      break;
    }
    set_property_optional(plane_id, DRM_MODE_OBJECT_PLANE, plane_props, "COLOR_SPACE", v4l2_colorspace);
  }

  last_colorspace = decodeUnit->colorspace;
  last_hdr_state = decodeUnit->hdrActive;

  while (MPP_OK != mpi_api->decode_put_packet(mpi_ctx, mpi_packet));

  return result;
}

DECODER_RENDERER_CALLBACKS decoder_callbacks_rk = {
  .setup = rk_setup,
  .cleanup = rk_cleanup,
  .submitDecodeUnit = rk_submit_decode_unit,
  .capabilities = CAPABILITY_DIRECT_SUBMIT,
};
