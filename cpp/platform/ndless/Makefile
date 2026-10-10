GCC := nspire-gcc
GXX := nspire-g++
LD := nspire-ld
GENZEHN := genzehn

ROOT := ../../..
OBJDIR := $(ROOT)/build-ndless/obj
DISTDIR := $(ROOT)/dist/ndless
EXE := ddlc
LAUNCHERS := ddlc

CPP_SOURCES := \
  cpp/src/pack.cpp cpp/src/navigation.cpp cpp/src/info_pages.cpp \
  cpp/src/player.cpp cpp/src/raster.cpp cpp/src/session.cpp \
  cpp/src/ui.cpp cpp/src/ui_common.cpp cpp/src/nvp.cpp cpp/src/video.cpp \
  cpp/platform/ndless/monotonic_clock.cpp

C_SOURCES := \
  cpp/third_party/nvp/mpeg4_xvid.c \
  cpp/third_party/nvp/xvid/xvid.c cpp/third_party/nvp/xvid/decoder.c \
  cpp/third_party/nvp/xvid/bitstream/bitstream.c cpp/third_party/nvp/xvid/bitstream/cbp.c cpp/third_party/nvp/xvid/bitstream/mbcoding.c \
  cpp/third_party/nvp/xvid/dct/fdct.c cpp/third_party/nvp/xvid/dct/idct.c cpp/third_party/nvp/xvid/dct/simple_idct.c \
  cpp/third_party/nvp/xvid/image/arm/yv12_to_rgb565.c cpp/third_party/nvp/xvid/image/colorspace.c cpp/third_party/nvp/xvid/image/font.c \
  cpp/third_party/nvp/xvid/image/image.c cpp/third_party/nvp/xvid/image/interpolate8x8.c cpp/third_party/nvp/xvid/image/postprocessing.c \
  cpp/third_party/nvp/xvid/image/qpel.c cpp/third_party/nvp/xvid/image/reduced.c \
  cpp/third_party/nvp/xvid/motion/estimation_common.c cpp/third_party/nvp/xvid/motion/gmc.c cpp/third_party/nvp/xvid/motion/motion_comp.c cpp/third_party/nvp/xvid/motion/sad.c \
  cpp/third_party/nvp/xvid/prediction/mbprediction.c \
  cpp/third_party/nvp/xvid/quant/quant_h263.c cpp/third_party/nvp/xvid/quant/quant_matrix.c cpp/third_party/nvp/xvid/quant/quant_mpeg.c \
  cpp/third_party/nvp/xvid/utils/emms.c cpp/third_party/nvp/xvid/utils/mbtransquant.c cpp/third_party/nvp/xvid/utils/mem_align.c \
  cpp/third_party/nvp/xvid/utils/mem_transfer.c cpp/third_party/nvp/xvid/utils/sram_tables.c cpp/third_party/nvp/xvid/utils/timer.c

CPP_OBJECTS := $(addprefix $(OBJDIR)/,$(CPP_SOURCES:.cpp=.o))
C_OBJECTS := $(addprefix $(OBJDIR)/,$(C_SOURCES:.c=.o))
OBJECTS := $(CPP_OBJECTS) $(C_OBJECTS)
LAUNCHER_OBJECTS := $(addprefix $(OBJDIR)/launcher/,$(addsuffix .o,$(LAUNCHERS)))
DEPENDENCIES := $(OBJECTS:.o=.d) $(LAUNCHER_OBJECTS:.o=.d)

INCLUDES := -I$(ROOT)/cpp/include -I$(ROOT)/cpp/third_party/nvp -I$(ROOT)/cpp/third_party/nvp/xvid
DEFINES := -DXVID_DECODER_ONLY -DARCH_IS_32BIT -DARCH_IS_ARM
COMMON_FLAGS := -marm -mcpu=arm926ej-s -mtune=arm926ej-s -fomit-frame-pointer -MMD -MP \
  -fno-strict-aliasing -ffunction-sections -fdata-sections -Wall -Wextra $(INCLUDES) $(DEFINES)
CXXFLAGS := $(COMMON_FLAGS) -O3 -DNDEBUG -falign-functions=32 -falign-loops=32 \
  -funroll-loops -std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics
# Xvid's upstream Ndless player deliberately builds its decoder hot path with
# these speed flags. The previous blanket -O2 left a substantial amount of
# ARM926 performance unused on high-motion P-frames.
CFLAGS := $(COMMON_FLAGS) -O3 -DNDEBUG -falign-functions=32 -falign-loops=32 \
  -funroll-loops -std=c11
LDFLAGS := -Wl,--gc-sections
ZEHNFLAGS := --name "DDLC TI-Nspire (unofficial)" --author "Santiagobbboff; nZATO by zyf722" --version 2 --ndless-min 36 --ndless-rev-min 2022 \
  --color-support 1 --clickpad-support 0 --touchpad-support 1 \
  --32MB-support 1 --240x320-support 1 --uses-lcd-blit 1 --compress

.PHONY: all clean
all: $(addprefix $(DISTDIR)/,$(addsuffix .tns,$(LAUNCHERS)))

-include $(wildcard $(DEPENDENCIES))
# Included dependency files are outputs, not link targets. Without an explicit
# empty rule GNU Make may apply its built-in .o-to-executable chain while it
# tries to remake an included .d file, creating spurious *.d.o files.
$(DEPENDENCIES): ;

$(OBJECTS): Makefile

$(OBJDIR)/%.o: $(ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(GXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(GCC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/launcher/%.o: $(ROOT)/cpp/platform/ndless/main.cpp
	@mkdir -p $(dir $@)
	$(GXX) $(CXXFLAGS) \
	  $(if $(findstring -zh,$*),-DNZATO_LAUNCHER_CHINESE=1,-DNZATO_LAUNCHER_CHINESE=0) \
	  $(if $(findstring -debug,$*),-DNZATO_LAUNCHER_DEBUG=1,-DNZATO_LAUNCHER_DEBUG=0) \
	  -c $< -o $@

$(DISTDIR)/%.elf: $(OBJECTS) $(OBJDIR)/launcher/%.o
	@mkdir -p $(DISTDIR)
	$(LD) $(OBJECTS) $(OBJDIR)/launcher/$*.o -o $@ $(LDFLAGS)

$(DISTDIR)/%.tns: $(DISTDIR)/%.elf
	$(GENZEHN) --input $< --output $@ $(ZEHNFLAGS)

clean:
	rm -rf $(ROOT)/build-ndless $(addprefix $(DISTDIR)/,$(addsuffix .elf,$(LAUNCHERS))) \
	  $(addprefix $(DISTDIR)/,$(addsuffix .tns,$(LAUNCHERS)))
