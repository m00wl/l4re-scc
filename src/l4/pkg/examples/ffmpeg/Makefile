PKGDIR	?= ..
L4DIR		?= $(PKGDIR)/../..

#TARGET	= $(if $(wildcard $(SRC_DIR)/do-build),ffmpeg-bench-mem)
TARGET	= $(if $(wildcard $(SRC_DIR)/do-build),video-pipeline)

$(if $(SYSTEM),$(if $(TARGET),,$(info ffmpeg-bench-mem not built because 'do-build' not there in $(SRC_DIR).)))

FFMPEG_SRC 	= $(SRC_DIR)/ffmpeg-src
FFMPEG_OBJ 	= $(OBJ_DIR)/ffmpeg-obj
FFMPEG_INST	= $(OBJ_DIR)/ffmpeg-inst

FFMPEG_MULTITHREADED	= yesplease
FILE_MODE	= rom
BUILD_MODE	= decodeonly

DEFINES_fb      = -DUSE_FB
DEFINES_decodeonly = -DUSE_DECODEONLY

LIBS		= -lavutil -lavformat -lavcodec -lswscale -lavutil -lswresample \
					-lavfilter
REQUIRES_LIBS   = rtc libgfxbitmap libc_be_fs_noop zlib libc_be_math \
		  libc_support_misc l4re_c-util libpthread
DEPENDS_PKGS    = $(REQUIRES_LIBS)
CFLAGS         += $(DEFINES_$(BUILD_MODE))
PRIVATE_INCDIR  = $(FFMPEG_INST)/include
PRIVATE_LIBDIR  = $(FFMPEG_INST)/lib
#SRC_C		= bench-mem.c
SRC_C		= video-pipeline.c

include $(L4DIR)/mk/prog.mk

FFMPEG_OPTS_x86 =

LDFLAGS += --no-warn-mismatch

#$(TARGET) $(SRC_DIR)/bench-mem.c: $(FFMPEG_INST)/lib/libavcodec.a
$(TARGET) $(SRC_DIR)/video-pipeline: $(FFMPEG_INST)/lib/libavcodec.a

ARCH_MAP_$(ARCH)   = $(ARCH)
ARCH_MAP_x86       = x86
ARCH_MAP_amd64     = x86
ARCH_MAP_ppc32     = ppc

FFMPEG_GENERIC_OPTIONS = --target-os=none --disable-network \
                         --disable-ffplay --disable-ffmpeg --disable-vulkan \
                         --disable-devices --disable-ffprobe --enable-gpl \
                         $(if $(FFMPEG_MULTITHREADED),--enable-pthreads,--disable-pthreads)

FFMPEG_L4_OPTIONS += --arch=$(ARCH_MAP_$(ARCH)) \
                     --cc="$(CC) $(CARCHFLAGS) $(CCXX_FLAGS) -DARCH_$(ARCH)" \
                     --cxx="$(CXX) $(CARCHFLAGS) $(CCXX_FLAGS) -DARCH_$(ARCH)" \
                     --cross-prefix=$(CROSS_COMPILE) \
                     --enable-cross-compile

get_ffmpeg:
	if test ! -d $(FFMPEG_SRC); then \
	  git clone git://git.videolan.org/ffmpeg.git $(FFMPEG_SRC); \
	fi

update_ffmpeg:
	cd $(FFMPEG_SRC) && git pull

DOMFLAGS = MAKEFLAGS="$(strip $(foreach a,$(MAKEFLAGS),$(filter --job%,$(a)) $(filter -j,$(a))))"

$(FFMPEG_INST)/lib/libavcodec.a: $(SRC_DIR)/Makefile build_ffmpeg
	mkdir -p $(FFMPEG_OBJ)
	cd $(FFMPEG_OBJ) && \
	  $(FFMPEG_SRC)/configure --prefix=$(FFMPEG_INST) \
	         --enable-cross-compile \
	         $(FFMPEG_GENERIC_OPTIONS) $(FFMPEG_L4_OPTIONS) \
	         --extra-cflags='-nostdinc $(FFMPEG_OPTS_CFLAGS_$(BUILD_ARCH)) $(CPPFLAGS) -fno-stack-protector' \
	         --extra-ldflags='$(FFMPEG_OPTS_LDFLAGS_$(BUILD_ARCH))' $(FFMPEG_OPTS_$(BUILD_ARCH))
	sed -e '/HAVE_SYSCTL/d' -i $(FFMPEG_OBJ)/config.h
	sed -e '/getenv/d' -i $(FFMPEG_OBJ)/config.h
	$(DOMFLAGS) $(MAKE) -C $(FFMPEG_OBJ)
	$(DOMFLAGS) $(MAKE) -C $(FFMPEG_OBJ) install

build_ffmpeg: get_ffmpeg
