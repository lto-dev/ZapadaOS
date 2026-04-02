# ZACLR generated-artifact contracts for M1 scaffold integration.

ZACLR_GENERATED_DIR := $(BUILD_DIR)/generated/zaclr
ZACLR_NATIVE_REGISTRY_CPP := $(ZACLR_GENERATED_DIR)/native_registry.cpp
ZACLR_NATIVE_LIBRARY_DIRS := $(shell if [ -d $(LIBRARIES_DIR) ]; then find $(LIBRARIES_DIR) -type f -name 'zaclr_native_*.cpp' ! -name 'zaclr_native_registry.cpp' -printf '%h\n' | xargs -r -n1 basename | sort -u; fi)
ZACLR_NATIVE_REGISTRY_HEADERS := $(foreach dir,$(ZACLR_NATIVE_LIBRARY_DIRS),$(ZACLR_GENERATED_DIR)/$(dir)/zaclr_native_registry.h)
ZACLR_NATIVE_REGISTRY_PER_LIBRARY_CPP := $(foreach dir,$(ZACLR_NATIVE_LIBRARY_DIRS),$(ZACLR_GENERATED_DIR)/$(dir)/zaclr_native_registry.cpp)
ZACLR_OPCODE_TABLE_INC := $(ZACLR_GENERATED_DIR)/opcode_table.inc
ZACLR_OPCODE_DESC_INC := $(ZACLR_GENERATED_DIR)/opcode_desc_table.inc
ZACLR_TRACE_EVENTS_INC := $(ZACLR_GENERATED_DIR)/trace_events.inc
ZACLR_OPCODE_VIEW_GENERATOR := $(CURDIR)/scripts/generate_zaclr_opcode_views.py

ZACLR_NATIVE_LIBRARY_CPP_SOURCES := $(shell if [ -d $(LIBRARIES_DIR) ]; then find $(LIBRARIES_DIR) -type f -name 'zaclr_native*.cpp' | sort; fi)
ZACLR_NATIVE_LIBRARY_REGISTRY_CPP_SOURCES := $(shell if [ -d $(LIBRARIES_DIR) ]; then find $(LIBRARIES_DIR) -type f -name 'zaclr_native_registry.cpp' | sort; fi)
ZACLR_NATIVE_LIBRARY_OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(ZACLR_NATIVE_LIBRARY_CPP_SOURCES))
ZACLR_NATIVE_LIBRARY_OBJECTS_AA64 := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR_AA64)/%.o,$(ZACLR_NATIVE_LIBRARY_CPP_SOURCES))

ZACLR_GENERATED_ARTIFACTS := $(ZACLR_NATIVE_REGISTRY_CPP) $(ZACLR_NATIVE_REGISTRY_HEADERS) $(ZACLR_NATIVE_REGISTRY_PER_LIBRARY_CPP) $(ZACLR_OPCODE_TABLE_INC) $(ZACLR_OPCODE_DESC_INC) $(ZACLR_TRACE_EVENTS_INC)
ZACLR_GENERATED_CPP_SOURCES := $(ZACLR_NATIVE_REGISTRY_CPP) $(ZACLR_NATIVE_REGISTRY_PER_LIBRARY_CPP)

$(ZACLR_GENERATED_DIR)/%/zaclr_native_registry.h: Makefile make/zaclr_native_registry.mk | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@library_name='$*'; \
		source_dir='$(LIBRARIES_DIR)/'$${library_name}; \
		echo '/* Auto-generated ZACLR native declaration header. */' > $@; \
		echo '/* Generated from assembly-local ZACLR native declarations. */' >> $@; \
		echo '' >> $@; \
		echo '#ifndef ZACLR_GENERATED_NATIVE_REGISTRY_H' >> $@; \
		echo '#define ZACLR_GENERATED_NATIVE_REGISTRY_H' >> $@; \
		echo '' >> $@; \
		echo '#include <kernel/zaclr/interop/zaclr_native_assembly.h>' >> $@; \
		echo '' >> $@; \
		echo '#ifdef __cplusplus' >> $@; \
		echo 'extern "C" {' >> $@; \
		echo '#endif' >> $@; \
		echo '' >> $@; \
		symbol=`echo $$library_name | sed 's#[./-]#_#g'`; \
		echo "const struct zaclr_native_assembly_descriptor* zaclr_native_$${symbol}_descriptor(void);" >> $@; \
		echo '' >> $@; \
		find $$source_dir -maxdepth 1 -type f -name 'zaclr_native_*.h' | sort | while read file; do \
			awk 'BEGIN { copy = 0 } \
			    /^struct zaclr_native_/ { copy = 1 } \
			    copy == 1 { print } \
			    /^};/ && copy == 1 { copy = 0 }' $$file >> $@; \
			echo '' >> $@; \
		done; \
		echo '' >> $@; \
		echo '#ifdef __cplusplus' >> $@; \
		echo '}' >> $@; \
		echo '#endif' >> $@; \
		echo '' >> $@; \
		echo '#endif /* ZACLR_GENERATED_NATIVE_REGISTRY_H */' >> $@

$(ZACLR_GENERATED_DIR)/%/zaclr_native_registry.cpp: Makefile make/zaclr_native_registry.mk $(ZACLR_GENERATED_DIR)/%/zaclr_native_registry.h scripts/generate_zaclr_native_registry.py | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@python3 $(CURDIR)/scripts/generate_zaclr_native_registry.py '$*' '$(LIBRARIES_DIR)/$*' '$@'

$(ZACLR_NATIVE_REGISTRY_CPP): Makefile make/zaclr_native_registry.mk | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@echo '/* Auto-generated ZACLR native registry contract. */' > $@
	@echo '/* Generated from ZACLR-native wrapper declarations only. */' >> $@
	@echo '' >> $@
	@echo '#include <kernel/zaclr/interop/zaclr_internal_call_registry.h>' >> $@
	@for library in $(ZACLR_NATIVE_LIBRARY_DIRS); do \
		parent=$$library; \
		sym=`echo $$parent | sed 's/[^A-Za-z0-9]/_/g'`; \
		echo "extern \"C\" const struct zaclr_native_assembly_descriptor* zaclr_native_$${sym}_descriptor(void);" >> $@; \
	done
	@echo '' >> $@
	@echo 'extern "C" struct zaclr_result zaclr_register_generated_native_assemblies(struct zaclr_internal_call_registry* registry)' >> $@
	@echo '{' >> $@
	@echo '    struct zaclr_result result;' >> $@
	@echo '    if (registry == NULL) {' >> $@
	@echo '        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_INTEROP);' >> $@
	@echo '    }' >> $@
	@for library in $(ZACLR_NATIVE_LIBRARY_DIRS); do \
		parent=$$library; \
		sym=`echo $$parent | sed 's/[^A-Za-z0-9]/_/g'`; \
		echo "    result = zaclr_internal_call_registry_register_assembly(registry, zaclr_native_$${sym}_descriptor());" >> $@; \
		echo '    if (result.status != ZACLR_STATUS_OK) {' >> $@; \
		echo '        return result;' >> $@; \
		echo '    }' >> $@; \
	done
	@echo '    return zaclr_result_ok();' >> $@
	@echo '}' >> $@

$(ZACLR_OPCODE_TABLE_INC): Makefile make/zaclr_native_registry.mk | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@cp $(CURDIR)/CLONES/runtime/src/coreclr/inc/opcode.def $@

$(ZACLR_OPCODE_DESC_INC): $(ZACLR_OPCODE_TABLE_INC) $(ZACLR_OPCODE_VIEW_GENERATOR) Makefile make/zaclr_native_registry.mk | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@python3 $(ZACLR_OPCODE_VIEW_GENERATOR) $(ZACLR_OPCODE_TABLE_INC) /dev/null $(ZACLR_OPCODE_DESC_INC)

$(ZACLR_TRACE_EVENTS_INC): Makefile make/zaclr_native_registry.mk | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@echo '/* Auto-generated ZACLR trace-event contract. */' > $@
	@echo '/* Placeholder only; replace with generated trace-event expansion data. */' >> $@
