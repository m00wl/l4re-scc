PKGDIR		?= ../..
L4DIR		?= $(PKGDIR)/../..

include $(L4DIR)/mk/Makeconf

# sparc: undefined reference to `__atomic_fetch_or_4'
SYSTEMS          = $(filter-out sparc-l4f,$(SYSTEMS_ABI))
TARGET           = lib4shmc_ringbuf.a lib4shmc_ringbuf.so
PC_FILENAME      = shmc_ringbuf
SRC_C            = ringbuf.c
REQUIRES_LIBS    = shmc

include $(L4DIR)/mk/lib.mk
