# ZACLR core build wiring for M1 scaffold integration.

ZACLR_ROOT := $(SRC_DIR)/kernel/zaclr

ZACLR_CORE_CPP_SOURCES := \
	$(ZACLR_ROOT)/boot/zaclr_boot.cpp \
	$(ZACLR_ROOT)/boot/zaclr_boot_path.cpp \
	$(ZACLR_ROOT)/diag/zaclr_assert.cpp \
	$(ZACLR_ROOT)/diag/zaclr_dump_state.cpp \
	$(ZACLR_ROOT)/diag/zaclr_trace.cpp \
	$(ZACLR_ROOT)/diag/zaclr_trace_format.cpp \
	$(ZACLR_ROOT)/exec/zaclr_call_resolution.cpp \
	$(ZACLR_ROOT)/exec/zaclr_dispatch.cpp \
	$(ZACLR_ROOT)/exec/zaclr_engine.cpp \
	$(ZACLR_ROOT)/exec/zaclr_interop_dispatch.cpp \
	$(ZACLR_ROOT)/exec/zaclr_intrinsics.cpp \
	$(ZACLR_ROOT)/exec/zaclr_type_init.cpp \
	$(ZACLR_ROOT)/exec/zaclr_eval_stack.cpp \
	$(ZACLR_ROOT)/exec/zaclr_exceptions.cpp \
	$(ZACLR_ROOT)/exec/zaclr_frame.cpp \
	$(ZACLR_ROOT)/exec/zaclr_opcode_table.cpp \
	$(ZACLR_ROOT)/exec/zaclr_thread.cpp \
	$(ZACLR_ROOT)/heap/zaclr_array.cpp \
	$(ZACLR_ROOT)/heap/zaclr_gc.cpp \
	$(ZACLR_ROOT)/heap/zaclr_gc_roots.cpp \
	$(ZACLR_ROOT)/heap/zaclr_heap.cpp \
	$(ZACLR_ROOT)/heap/zaclr_object.cpp \
	$(ZACLR_ROOT)/heap/zaclr_string.cpp \
	$(ZACLR_ROOT)/interop/zaclr_internal_call_registry.cpp \
	$(ZACLR_ROOT)/interop/zaclr_marshalling.cpp \
	$(ZACLR_ROOT)/interop/zaclr_native_assembly.cpp \
	$(ZACLR_ROOT)/interop/zaclr_pinvoke_resolver.cpp \
	$(ZACLR_ROOT)/interop/zaclr_qcall_table.cpp \
	$(ZACLR_ROOT)/loader/zaclr_assembly_registry.cpp \
	$(ZACLR_ROOT)/loader/zaclr_assembly_source_initramfs.cpp \
	$(ZACLR_ROOT)/loader/zaclr_binder.cpp \
	$(ZACLR_ROOT)/loader/zaclr_loader.cpp \
	$(ZACLR_ROOT)/loader/zaclr_pe_image.cpp \
	$(ZACLR_ROOT)/metadata/zaclr_metadata_reader.cpp \
	$(ZACLR_ROOT)/metadata/zaclr_method_map.cpp \
	$(ZACLR_ROOT)/metadata/zaclr_signature.cpp \
	$(ZACLR_ROOT)/metadata/zaclr_token.cpp \
	$(ZACLR_ROOT)/metadata/zaclr_type_map.cpp \
	$(ZACLR_ROOT)/typesystem/zaclr_field_layout.cpp \
	$(ZACLR_ROOT)/typesystem/zaclr_delegate_runtime.cpp \
	$(ZACLR_ROOT)/typesystem/zaclr_call_target.cpp \
	$(ZACLR_ROOT)/typesystem/zaclr_generic_context.cpp \
	$(ZACLR_ROOT)/typesystem/zaclr_method_handle.cpp \
	$(ZACLR_ROOT)/typesystem/zaclr_member_resolution.cpp \
	$(ZACLR_ROOT)/typesystem/zaclr_method_table.cpp \
	$(ZACLR_ROOT)/typesystem/zaclr_type_identity.cpp \
	$(ZACLR_ROOT)/typesystem/zaclr_type_prepare.cpp \
	$(ZACLR_ROOT)/typesystem/zaclr_type_system.cpp \
	$(ZACLR_ROOT)/runtime/zaclr_boot_shared.cpp \
	$(ZACLR_ROOT)/runtime/zaclr_runtime.cpp \
	$(ZACLR_ROOT)/runtime/zaclr_runtime_init.cpp \
	$(ZACLR_ROOT)/runtime/zaclr_runtime_state.cpp

ZACLR_CPP_SOURCES := $(ZACLR_CORE_CPP_SOURCES) $(ZACLR_HOST_CPP_SOURCES) $(ZACLR_PROCESS_CPP_SOURCES)
