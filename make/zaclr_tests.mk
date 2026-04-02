# ZACLR test-shell build wiring independent of full boot execution.

ZACLR_ROOT := $(SRC_DIR)/kernel/zaclr

ZACLR_TEST_CPP_SOURCES := \
	$(ZACLR_ROOT)/tests/zaclr_boot_trace_tests.cpp \
	$(ZACLR_ROOT)/tests/zaclr_dispatch_tests.cpp \
	$(ZACLR_ROOT)/tests/zaclr_metadata_tests.cpp \
	$(ZACLR_ROOT)/tests/zaclr_smoke_tests.cpp
