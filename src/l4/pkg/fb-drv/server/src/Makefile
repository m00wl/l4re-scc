PKGDIR          ?= ../..
L4DIR           ?= $(PKGDIR)/../..

TARGET                  = fb-drv
SRC_CC_x86-l4f          = vesa.cc
SRC_CC_amd64-l4f        = vesa.cc
SRC_CC_arm-l4f          = lcddrv.cc
SRC_CC_arm64-l4f        = lcddrv.cc
SRC_CC                  = main.cc dummy.cc splash.cc
REQUIRES_LIBS_x86-l4f   = x86emu_int10
REQUIRES_LIBS_amd64-l4f = x86emu_int10
REQUIRES_LIBS_arm-l4f   = libdrivers-lcd
REQUIRES_LIBS_arm64-l4f = libdrivers-lcd
REQUIRES_LIBS           = libc_support_misc libio-vbus

DEFINES         = -DSPLASHNAME=gimp_image \
                  -DSPLASHNAME_RUN_LENGTH_DECODE=GIMP_IMAGE_RUN_LENGTH_DECODE

CPPFLAGS_splash.cc = -include $(PKGDIR)/server/data/splash1.c

include $(L4DIR)/mk/prog.mk
