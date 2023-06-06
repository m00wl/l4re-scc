PKGDIR		?= ../..
L4DIR		?= $(PKGDIR)/../..

include $(L4DIR)/mk/Makeconf

# sparc: undefined reference to `__atomic_fetch_or_4'
SYSTEMS         = $(filter-out sparc-l4f,$(SYSTEMS_ABI))
TARGET          = lib4shmc.a lib4shmc.so
SRC_C           = shmc.c
REQUIRES_LIBS   = l4re_c l4re_c-util

include $(L4DIR)/mk/lib.mk
