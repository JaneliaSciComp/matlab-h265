# Makefile for compiling MEX functions for h.265 video reading and writing
#
# Usage:
#   make          - Build all MEX files
#   make clean    - Delete all MEX files
#   make rebuild  - Clean and rebuild all
#
# Requires:
#   - MATLAB mex command in PATH (or set MEX variable)
#   - FFmpeg development libraries: libavformat, libavcodec, libavutil, libswscale

MEX ?= mex

# Detect MEX extension based on platform
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    MEXEXT := mexa64
endif
ifeq ($(UNAME_S),Darwin)
    MEXEXT := mexmaci64
endif
# Windows (MSYS/MinGW/Cygwin)
ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
    MEXEXT := mexw64
endif
ifeq ($(findstring CYGWIN,$(UNAME_S)),CYGWIN)
    MEXEXT := mexw64
endif

# Default extension if not detected
MEXEXT ?= mexa64

# Library flags
LIBS_BASE := -lavformat -lavcodec -lavutil
LIBS_SCALE := $(LIBS_BASE) -lswscale

# Header files
CACHE_HDR := h265_frame_cache.h
DECODE_HDR := h265_decode_common.h
DECODE_SRC := h265_decode_common.c

# MEX targets
TARGETS := \
    open_h265_video.$(MEXEXT) \
    read_h265_frame.$(MEXEXT) \
    read_h265_frames.$(MEXEXT) \
    close_h265_video.$(MEXEXT) \
    open_h265_write.$(MEXEXT) \
    write_h265_frames.$(MEXEXT) \
    close_h265_write.$(MEXEXT)

.PHONY: all clean rebuild

all: $(TARGETS)

clean:
	rm -f $(TARGETS)

rebuild: clean all

# Video reading functions
open_h265_video.$(MEXEXT): open_h265_video.c $(CACHE_HDR)
	$(MEX) $< $(LIBS_BASE)

read_h265_frame.$(MEXEXT): read_h265_frame.c $(CACHE_HDR) $(DECODE_HDR) $(DECODE_SRC)
	$(MEX) $< $(DECODE_SRC) $(LIBS_SCALE)

read_h265_frames.$(MEXEXT): read_h265_frames.c $(DECODE_HDR) $(DECODE_SRC)
	$(MEX) $< $(DECODE_SRC) $(LIBS_SCALE)

close_h265_video.$(MEXEXT): close_h265_video.c $(CACHE_HDR)
	$(MEX) $< $(LIBS_BASE)

# h.265 writing functions
open_h265_write.$(MEXEXT): open_h265_write.c
	$(MEX) $< $(LIBS_SCALE)

write_h265_frames.$(MEXEXT): write_h265_frames.c
	$(MEX) $< $(LIBS_SCALE)

close_h265_write.$(MEXEXT): close_h265_write.c
	$(MEX) $< $(LIBS_SCALE)
