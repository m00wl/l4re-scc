PKGDIR          ?= ../..
L4DIR           ?= $(PKGDIR)/../../..

TARGET          = moe
DEFAULT_RELOC   = 0x00240000
RELOC_PHYS      = y
SRC_CC          = main.cc page_alloc.cc dataspace.cc dataspace_cont.cc \
                  dataspace_anon.cc globals.cc \
                  alloc.cc boot_fs.cc dataspace_util.cc \
                  region.cc debug.cc malloc.cc quota.cc \
                  loader.cc loader_elf.cc \
                  app_task.cc dataspace_noncont.cc pages.cc \
                  name_space.cc mem.cc log.cc sched_proxy.cc \
                  delete.cc vesa_fb.cc server_obj.cc \
                  dma_space.cc
SRC_S          := ARCH-$(ARCH)/crt0.S
SRC_S_arm64-l4f = exception-el1.S
SRC_CC_arm64-l4f = exception-el1-handler.cc
MODE            = sigma0

REQUIRES_LIBS  := libkproxy libloader l4re-util libsigma0 \
                  cxx_io cxx_libc_io libc_be_minimal_log_io libsupc++_minimal
EXTRA_LIBS     := -ll4sys-direct
DEFINES        += -DL4_CXX_NO_EXCEPTION_BACKTRACE -DL4_MINIMAL_LIBC \
                  -DEXCEPTION_EL1_DETECT_X1
LDFLAGS        += --entry=_real_start

vpath exception-el1-handler.cc $(PKGDIR)/../uclibc/lib/uclibc/ARCH-arm64
vpath exception-el1.S $(PKGDIR)/../uclibc/lib/uclibc/ARCH-arm64

include $(L4DIR)/mk/prog.mk

DEFINES_arm64-l4f += $(if $(CONFIG_KERNEL_CPU_VIRT),-DCONFIG_KERNEL_CPU_VIRT=1)
