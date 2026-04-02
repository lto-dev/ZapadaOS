# ZACLR host build wiring. Architecture-specific host selection stays here.

ZACLR_ROOT := $(SRC_DIR)/kernel/zaclr

ZACLR_HOST_CPP_SOURCES_COMMON := \
	$(ZACLR_ROOT)/host/zaclr_host.cpp \
	$(ZACLR_ROOT)/host/zaclr_trace_sink_serial.cpp

ifeq ($(ARCH),aarch64)
ZACLR_HOST_CPP_SOURCES_ARCH := $(ZACLR_ROOT)/host/zaclr_host_kernel.cpp
else
ZACLR_HOST_CPP_SOURCES_ARCH := $(ZACLR_ROOT)/host/zaclr_host_kernel.cpp
endif

ZACLR_HOST_CPP_SOURCES := $(ZACLR_HOST_CPP_SOURCES_COMMON) $(ZACLR_HOST_CPP_SOURCES_ARCH)
