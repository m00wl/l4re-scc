/* SPDX-License-Identifier: GPL-2.0-only OR License-Ref-kk-custom */
/*
 * Copyright (C) 2021-2023 Stephan Gerhold <stephan@gerhold.net>
 *
 * Qualcomm MDP5 FB driver (panel must be init'ed by boot-loader)
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <pthread.h>

#include <l4/drivers/lcd.h>
#include <l4/io/io.h>
#include <l4/re/c/dma_space.h>
#include <l4/re/c/mem_alloc.h>
#include <l4/re/c/rm.h>
#include <l4/re/c/util/cap_alloc.h>
#include <l4/re/protocols.h>
#include <l4/sys/factory.h>
#include <l4/util/util.h>
#include <l4/vbus/vbus.h>

#define HW_VERSION(major, minor, step) \
  (((major) << 28) | ((minor) << 16) | ((step) << 0))

enum {
  CTL_FLUSH  = 0x18,
  CTL_START  = 0x1c,
};

struct mdp5_pipe {
  const char *name;
  unsigned int base;
  l4_uint32_t flush;
};

struct mdp5_hw {
  const char *name;
  unsigned int ctl0_base;
  struct mdp5_pipe pipes[];
};

static struct mdp5_hw const mdp5_hw_v1_0 = {
  .name = "MDP5 v1.0",
  .ctl0_base = 0x00500,
  .pipes = {
    {
      .name = "VIG_0",
      .base = 0x01100,
      .flush = 1 << 0,
    },
    {
      .name = "RGB_0",
      .base = 0x01d00,
      .flush = 1 << 3,
    },
    {
      .name = "DMA_0",
      .base = 0x02900,
      .flush = 1 << 11,
    },
    { }
  },
};

static struct mdp5_hw const mdp5_hw_v1_6 = {
  .name = "MDP5 v1.6",
  .ctl0_base = 0x01000,
  .pipes = {
    {
      .name = "VIG_0",
      .base = 0x04000,
      .flush = 1 << 0,
    },
    {
      .name = "RGB_0",
      .base = 0x14000,
      .flush = 1 << 3,
    },
    {
      .name = "DMA_0",
      .base = 0x24000,
      .flush = 1 << 11,
    },
    { }
  },
};

enum {
  SSPP_SRC_SIZE            = 0x00,
  SSPP_SRC_IMG_SIZE        = 0x04,
  SSPP_SRC_XY              = 0x08,
  SSPP_OUT_SIZE            = 0x0c,
  SSPP_OUT_XY              = 0x10,
  SSPP_SRC0_ADDR           = 0x14,
  SSPP_SRC_YSTRIDE0        = 0x24,
  SSPP_SRC_FORMAT          = 0x30,
  SSPP_SRC_UNPACK_PATTERN  = 0x34,

  SSPP_SRC_FORMAT_SRC_RGB565  = 0x1 << 9 | 0x1 << 4 | 0x1 << 2 | 0x2 << 0,
  SSPP_SRC_FORMAT_SRC_RGB888  = 0x2 << 9 | 0x3 << 4 | 0x3 << 2 | 0x3 << 0,
  SSPP_SRC_FORMAT_UNPACK_RGB  = 0x1 << 17 | 0x2 << 12,
  SSPP_SRC_UNPACK_PATTERN_RGB = 0x2 << 16 | 0x0 << 8 | 0x1 << 0,
};

static l4_addr_t mdp5_base;
static struct mdp5_hw const *fb_hw;
static struct mdp5_pipe const *fb_pipe;
static l4_uint16_t fb_width, fb_height;

static void *fb_vaddr;
static l4re_dma_space_dma_addr_t fb_paddr;

static bool fb_need_refresh;
static pthread_t fb_refresh_pthread;

static int width(void) { return fb_width; }
static int height(void) { return fb_height; }
static int bytes_per_pixel(void) { return 2; }

static unsigned int fbmem_size(void)
{ return height() * width() * bytes_per_pixel(); }

static void write_ctl(unsigned reg, l4_uint32_t val)
{ *((volatile l4_uint32_t *)(mdp5_base + fb_hw->ctl0_base + reg)) = val; }

static l4_uint32_t read_pipe(unsigned reg)
{ return *((volatile l4_uint32_t *)(mdp5_base + fb_pipe->base + reg)); }

static void write_pipe(unsigned reg, l4_uint32_t val)
{ *((volatile l4_uint32_t *)(mdp5_base + fb_pipe->base + reg)) = val; }

static bool read_size(void)
{
  l4io_device_handle_t dh;
  l4io_resource_handle_t hdl;
  l4_uint32_t val;

  if (mdp5_base)
    return fb_width && fb_height;

  if (l4io_lookup_device("qcom,mdp5", &dh, NULL, &hdl))
    {
      printf("Could not lookup MDP5 device\n");
      return false;
    }

  mdp5_base = l4io_request_resource_iomem(dh, &hdl);
  if (mdp5_base == 0)
    {
      printf("Could not map MDP5 registers\n");
      return false;
    }

  /* Detect hardware version */
  val = *(volatile l4_uint32_t *)mdp5_base;
  if (val >= HW_VERSION(1, 6, 0))
    fb_hw = &mdp5_hw_v1_6;
  else
    fb_hw = &mdp5_hw_v1_0;

  printf("Detected hardware version: %s\n", fb_hw->name);

  for (fb_pipe = fb_hw->pipes; fb_pipe->base; fb_pipe++)
    {
      val = read_pipe(SSPP_OUT_SIZE);
      fb_width = val;
      fb_height = val >> 16;
      if (fb_width && fb_height)
        {
          printf("Found active pipe %s (%dx%d)\n",
                 fb_pipe->name, fb_width, fb_height);
          return true;
        }
    }

  printf("No active pipe found, display not initialized by bootloader?\n");
  return false;
}

static void setup_memory(void)
{
  if (fb_vaddr || !read_size())
    return;

  // get some frame buffer
  l4re_ds_t mem = l4re_util_cap_alloc();
  if (l4_is_invalid_cap(mem))
    return;

  if (l4re_ma_alloc(fbmem_size(), mem,
                    L4RE_MA_CONTINUOUS | L4RE_MA_PINNED | L4RE_MA_SUPER_PAGES))
    {
      printf("Error allocating memory\n");
      return;
    }

  fb_vaddr = 0;
  if (l4re_rm_attach(&fb_vaddr, fbmem_size(),
                     L4RE_RM_F_SEARCH_ADDR | L4RE_RM_F_EAGER_MAP | L4RE_RM_F_RW,
                     mem, 0, L4_SUPERPAGESHIFT))
    {
      printf("Error getting memory\n");
      return;
    }

  printf("Video memory is at virtual %p (size: 0x%x Bytes)\n",
         fb_vaddr, fbmem_size());

  l4re_dma_space_t dma = l4re_util_cap_alloc();
  if (l4_is_invalid_cap(dma))
    {
      printf("error: failed to allocate DMA space capability.\n");
      return;
    }

  if (l4_error(l4_factory_create(l4re_global_env->mem_alloc,
                                 L4RE_PROTO_DMA_SPACE, dma)))
    {
      printf("error: failed to create DMA space\n");
      return;
    }

  l4_cap_idx_t vbus = l4re_env_get_cap("vbus");
  if (l4_is_invalid_cap(vbus))
    {
      printf("Failed to get 'vbus' capability\n");
      return;
    }

  int ret = l4vbus_assign_dma_domain(vbus, ~0U, L4VBUS_DMAD_BIND |
                                     L4VBUS_DMAD_L4RE_DMA_SPACE, dma);
  if (ret)
    {
      printf("Failed to assign DMA domain to vbus\n");
      return;
    }

  printf("DMA domain assigned successfully\n");

  l4_size_t phys_size = fbmem_size();

  // get physical address
  if (l4re_dma_space_map(dma, mem | L4_CAP_FPAGE_RW, 0, &phys_size, 0,
                         L4RE_DMA_SPACE_TO_DEVICE, &fb_paddr)
      || phys_size != fbmem_size())
    {
      printf("Getting the physical address failed or not contiguous\n");
      return;
    }
  printf("Physical video memory is at %llx\n", fb_paddr);
}

static int lcd_probe(const char *configstr)
{
  fb_need_refresh = configstr && strcasestr(configstr, "refresh") != NULL;
  return !l4io_lookup_device("qcom,mdp5", NULL, 0, 0);
}

static void *lcd_get_fb(void)
{
  setup_memory();
  return fb_vaddr;
}

static unsigned int lcd_fbmem_size(void)
{ return fbmem_size(); }

static const char *lcd_get_info(void)
{ return "MDP5 FASTBOOT init'ed FB"; }

static int lcd_get_fbinfo(l4re_video_view_info_t *vinfo)
{
  if (!read_size())
    return 1;

  vinfo->width               = width();
  vinfo->height              = height();
  vinfo->bytes_per_line      = bytes_per_pixel() * vinfo->width;

  vinfo->pixel_info.bytes_per_pixel = bytes_per_pixel();
  vinfo->pixel_info.r.shift         = 11;
  vinfo->pixel_info.r.size          = 5;
  vinfo->pixel_info.g.shift         = 5;
  vinfo->pixel_info.g.size          = 6;
  vinfo->pixel_info.b.shift         = 0;
  vinfo->pixel_info.b.size          = 5;
  vinfo->pixel_info.a.shift         = 0;
  vinfo->pixel_info.a.size          = 0;
  return 0;
}

static void *lcd_refresh_func(void *data)
{
  while (true)
    {
      write_ctl(CTL_START, 1);
      l4_sleep(20); // ~50 Hz
    }
  return data;
}

static void lcd_start_refresh_thread(void)
{
  pthread_attr_t thread_attr;

  int err;
  if ((err = pthread_attr_init(&thread_attr)) != 0) {
    printf("Error: Initializing pthread attr: %d\n", err);
    return;
  }

  struct sched_param sp;
  sp.sched_priority = 0xff;
  pthread_attr_setschedpolicy(&thread_attr, SCHED_L4);
  pthread_attr_setschedparam(&thread_attr, &sp);
  pthread_attr_setinheritsched(&thread_attr, PTHREAD_EXPLICIT_SCHED);

  err = pthread_create(&fb_refresh_pthread, &thread_attr, lcd_refresh_func, NULL);
  if (err != 0)
    printf("Error: Creating thread: %d\n", err);
}

static void lcd_enable(void)
{
  setup_memory();
  if (!fb_paddr)
    return;

  // write pipe configuration
  write_pipe(SSPP_SRC_SIZE, width() | height() << 16);
  write_pipe(SSPP_SRC_IMG_SIZE, width() | height() << 16);
  write_pipe(SSPP_SRC_XY, 0);
  write_pipe(SSPP_SRC0_ADDR, fb_paddr);
  write_pipe(SSPP_SRC_YSTRIDE0, bytes_per_pixel() * width());
  write_pipe(SSPP_SRC_FORMAT,
             SSPP_SRC_FORMAT_SRC_RGB565 | SSPP_SRC_FORMAT_UNPACK_RGB);
  write_pipe(SSPP_SRC_UNPACK_PATTERN, SSPP_SRC_UNPACK_PATTERN_RGB);

  // flush pipe configuration
  write_ctl(CTL_FLUSH, fb_pipe->flush);

  if (fb_need_refresh)
    lcd_start_refresh_thread();
}

static void lcd_disable(void)
{
  printf("%s unimplemented.\n", __func__);
}

static struct arm_lcd_ops arm_lcd_ops_mdp5 = {
  .probe              = lcd_probe,
  .get_fb             = lcd_get_fb,
  .get_fbinfo         = lcd_get_fbinfo,
  .get_video_mem_size = lcd_fbmem_size,
  .get_info           = lcd_get_info,
  .enable             = lcd_enable,
  .disable            = lcd_disable,
};

arm_lcd_register(&arm_lcd_ops_mdp5);
