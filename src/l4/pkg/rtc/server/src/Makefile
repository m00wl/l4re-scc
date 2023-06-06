PKGDIR		?= ../..
L4DIR		?= $(PKGDIR)/../..

TARGET		= rtc
SRC_CC_x86-l4f   = x86.cc
SRC_CC_amd64-l4f = x86.cc
SRC_CC_arm-l4f   = pl031.cc
SRC_CC_arm64-l4f = pl031.cc
SRC_CC           = main.cc
REQUIRES_LIBS    = libio cxx_libc_io cxx_io libstdc++

include $(L4DIR)/mk/prog.mk
