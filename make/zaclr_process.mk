# ZACLR process-first ownership build wiring.

ZACLR_ROOT := $(SRC_DIR)/kernel/zaclr

ZACLR_PROCESS_CPP_SOURCES := \
	$(ZACLR_ROOT)/process/zaclr_app_domain.cpp \
	$(ZACLR_ROOT)/process/zaclr_handle_table.cpp \
	$(ZACLR_ROOT)/process/zaclr_process.cpp \
	$(ZACLR_ROOT)/process/zaclr_process_launch.cpp \
	$(ZACLR_ROOT)/process/zaclr_process_manager.cpp \
	$(ZACLR_ROOT)/process/zaclr_security_context.cpp
