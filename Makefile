# Zapada - Makefile
#
# Build system for the Zapada kernel.
#
# Architecture selection:
#   ARCH=x86_64   (default) - builds for x86_64 via GRUB2 ISO
#   ARCH=aarch64            - builds for Raspberry Pi 3/4/5 via kernel8.img
#
# Requirements (x86_64):
#   - x86_64-elf-gcc   (cross-compiler; falls back to system gcc on Ubuntu)
#   - nasm             (assembler)
#   - grub-mkrescue    (GRUB2 ISO creation, requires xorriso)
#
# Requirements (aarch64):
#   - aarch64-linux-gnu-gcc   (install: sudo apt install gcc-aarch64-linux-gnu)
#   - aarch64-linux-gnu-ld
#   - aarch64-linux-gnu-objcopy
#
# Targets (x86_64, default):
#   all        - Build the kernel ELF and create the bootable ISO
#   kernel     - Build only the kernel ELF
#   iso        - Build the kernel ELF and create the ISO
#   qemu-test  - Run Stage 1.2 gate test in QEMU
#   clean      - Remove all build artifacts
#
# Targets (aarch64):
#   make ARCH=aarch64            - build kernel8.img
#   make ARCH=aarch64 clean      - clean AArch64 artifacts
#
# Usage:
#   make                     # x86_64: build everything
#   make ARCH=aarch64        # AArch64: build kernel8.img
#   make clean               # clean
#   make qemu-test           # run x86_64 gate tests
#   make DEBUG=1             # debug build: -O0 -g -DDEBUG (both arches)

# --------------------------------------------------------------------------
# Toolchain
#
# Preferred: x86_64-elf-gcc (proper bare-metal cross-compiler).
# Fallback:  System gcc (x86_64-linux-gnu) with -ffreestanding -nostdlib -nostdinc.
#            This works on Ubuntu WSL because the host is already x86_64.
#
# To install a proper cross-compiler:
#   See https://wiki.osdev.org/GCC_Cross-Compiler
#   Or on Ubuntu: sudo apt install gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu
# --------------------------------------------------------------------------

# --------------------------------------------------------------------------
# Directories (defined early so ARCH conditionals can reference them)
# --------------------------------------------------------------------------

BUILD_DIR   := build
SRC_DIR     := src
LIBRARIES_DIR := $(SRC_DIR)/libraries
CLR_CORE_DIR := $(SRC_DIR)/kernel/clr/core
CLR_FORK_DIR := $(SRC_DIR)/kernel/clr/runtime/fork
CLR_CORE_INCLUDE_DIR := $(CLR_CORE_DIR)/Include
CLR_FORK_INCLUDE_DIR := $(CLR_FORK_DIR)/include
ZACLR_BUILD_DIR := build

ENABLE_ZACLR := 1
ENABLE_ZACLR_TESTS ?= 0

clr_core_or_fork = $(if $(wildcard $(CLR_FORK_DIR)/$(1)),$(CLR_FORK_DIR)/$(1),$(CLR_CORE_DIR)/$(1))

LIBRARY_C_SOURCES := $(shell if [ -d $(LIBRARIES_DIR) ]; then find $(LIBRARIES_DIR) -type f -name '*.c' ! -path '$(LIBRARIES_DIR)/System.Private.CoreLib/nanoprintf/*' | sort; fi)
LIBRARY_CPP_SOURCES_ALL := $(shell if [ -d $(LIBRARIES_DIR) ]; then find $(LIBRARIES_DIR) -type f -name '*.cpp' | sort; fi)
ZACLR_LIBRARY_CPP_SOURCES := $(shell if [ -d $(LIBRARIES_DIR) ]; then find $(LIBRARIES_DIR) -type f -name 'zaclr_native*.cpp' | sort; fi)
LIBRARY_CPP_SOURCES := $(filter-out $(ZACLR_LIBRARY_CPP_SOURCES),$(LIBRARY_CPP_SOURCES_ALL))
LIBRARY_FILES := $(shell if [ -d $(LIBRARIES_DIR) ]; then find $(LIBRARIES_DIR) -type f | sort; fi)
LIBRARY_DIR_NAMES := $(shell if [ -d $(LIBRARIES_DIR) ]; then find $(LIBRARIES_DIR) -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort; fi)
LIBRARY_INCLUDE_FLAGS := $(foreach dir,$(LIBRARY_DIR_NAMES),-I $(LIBRARIES_DIR)/$(dir))

LEGACY_CLR_C_SOURCES := \
	$(SRC_DIR)/kernel/clr/host/runtime_host.c \
	$(SRC_DIR)/kernel/clr/host/runtime_kernel_host.c \
	$(SRC_DIR)/kernel/clr/host/runtime_pal.c \
	$(SRC_DIR)/kernel/clr/runtime/runtime_compat.c

LEGACY_CLR_CPP_SOURCES_X86 := \
	$(SRC_DIR)/kernel/clr/runtime/pe_assembly_helpers.cpp \
	$(call clr_core_or_fork,Core.cpp) \
	$(call clr_core_or_fork,Hardware.cpp) \
	$(call clr_core_or_fork,RuntimeGlobals.cpp) \
	$(SRC_DIR)/kernel/clr/runtime/execution_engine.cpp \
	$(call clr_core_or_fork,Checks.cpp) \
	$(call clr_core_or_fork,CLR_RT_DblLinkedList.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_Array.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_BinaryBlob.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_Delegate.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_Delegate_List.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_Finalizer.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_Lock.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_LockRequest.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_Node.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_String.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_Timer.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_WaitForObject.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapCluster.cpp) \
	$(call clr_core_or_fork,CLR_RT_Memory.cpp) \
	$(call clr_core_or_fork,CLR_RT_ObjectToEvent_Source.cpp) \
	$(call clr_core_or_fork,Thread.cpp) \
	$(call clr_core_or_fork,TypeSystem.cpp) \
	$(call clr_core_or_fork,TypeSystemLookup.cpp) \
	$(call clr_core_or_fork,GarbageCollector.cpp) \
	$(call clr_core_or_fork,GarbageCollector_Compaction.cpp) \
	$(call clr_core_or_fork,GarbageCollector_ComputeReachabilityGraph.cpp) \
	$(call clr_core_or_fork,GarbageCollector_Info.cpp) \
	$(call clr_core_or_fork,CLR_RT_StackFrame.cpp) \
	$(call clr_core_or_fork,Cache.cpp) \
	$(call clr_core_or_fork,Interpreter.cpp) \
	$(call clr_core_or_fork,CLR_RT_RuntimeMemory.cpp) \
	$(call clr_core_or_fork,Various.cpp) \
	$(call clr_core_or_fork,CLR_RT_ObjectToEvent_Destination.cpp) \
	$(call clr_core_or_fork,CLR_RT_UnicodeHelper.cpp) \
	$(call clr_core_or_fork,CLR_RT_SystemAssembliesTable.cpp) \
	$(call clr_core_or_fork,StringTable.cpp) \
	$(call clr_core_or_fork,nanoSupport_CRC32.cpp) \
	$(SRC_DIR)/kernel/clr/runtime/assembly_registry.cpp \
	$(SRC_DIR)/kernel/clr/runtime/image_loader.cpp \
	$(SRC_DIR)/kernel/clr/runtime/runtime_pe.cpp \
	$(SRC_DIR)/kernel/clr/runtime/pe_projection.cpp \
	$(SRC_DIR)/kernel/clr/runtime/method_lookup.cpp \
	$(SRC_DIR)/kernel/clr/runtime/execution.cpp \
	$(SRC_DIR)/kernel/clr/runtime/runtime_api.cpp \
	$(SRC_DIR)/kernel/clr/runtime/runtime_state.cpp \
	$(SRC_DIR)/kernel/clr/runtime/runtime_type_system.cpp \
	$(SRC_DIR)/kernel/clr/runtime/runtime_gc.cpp \
	$(SRC_DIR)/kernel/clr/runtime/Execution_adapters.cpp \
	$(SRC_DIR)/kernel/clr/runtime/runtime_support.cpp \
	$(SRC_DIR)/kernel/clr/runtime/runtime_boot.cpp \
	$(LIBRARY_CPP_SOURCES)

LEGACY_CLR_CPP_SOURCES_AA64 := \
	$(SRC_DIR)/kernel/clr/runtime/pe_assembly_helpers.cpp \
	$(call clr_core_or_fork,Core.cpp) \
	$(call clr_core_or_fork,Hardware_stub.cpp) \
	$(call clr_core_or_fork,RuntimeGlobals.cpp) \
	$(SRC_DIR)/kernel/clr/runtime/execution_engine.cpp \
	$(call clr_core_or_fork,Checks.cpp) \
	$(call clr_core_or_fork,CLR_RT_DblLinkedList.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_Array.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_BinaryBlob.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_Delegate.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_Delegate_List.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_Finalizer.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_Lock.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_LockRequest.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_Node.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_String.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_Timer.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapBlock_WaitForObject.cpp) \
	$(call clr_core_or_fork,CLR_RT_HeapCluster.cpp) \
	$(call clr_core_or_fork,CLR_RT_Memory.cpp) \
	$(call clr_core_or_fork,CLR_RT_ObjectToEvent_Source.cpp) \
	$(call clr_core_or_fork,Thread.cpp) \
	$(call clr_core_or_fork,TypeSystem.cpp) \
	$(call clr_core_or_fork,TypeSystemLookup.cpp) \
	$(call clr_core_or_fork,GarbageCollector.cpp) \
	$(call clr_core_or_fork,GarbageCollector_Compaction.cpp) \
	$(call clr_core_or_fork,GarbageCollector_ComputeReachabilityGraph.cpp) \
	$(call clr_core_or_fork,GarbageCollector_Info.cpp) \
	$(call clr_core_or_fork,CLR_RT_StackFrame.cpp) \
	$(call clr_core_or_fork,Cache.cpp) \
	$(call clr_core_or_fork,Interpreter.cpp) \
	$(call clr_core_or_fork,CLR_RT_RuntimeMemory.cpp) \
	$(call clr_core_or_fork,Various.cpp) \
	$(call clr_core_or_fork,CLR_RT_ObjectToEvent_Destination.cpp) \
	$(call clr_core_or_fork,CLR_RT_UnicodeHelper.cpp) \
	$(call clr_core_or_fork,CLR_RT_SystemAssembliesTable.cpp) \
	$(call clr_core_or_fork,StringTable.cpp) \
	$(call clr_core_or_fork,nanoSupport_CRC32.cpp) \
	$(SRC_DIR)/kernel/clr/runtime/assembly_registry.cpp \
	$(SRC_DIR)/kernel/clr/runtime/image_loader.cpp \
	$(SRC_DIR)/kernel/clr/runtime/runtime_pe.cpp \
	$(SRC_DIR)/kernel/clr/runtime/pe_projection.cpp \
	$(SRC_DIR)/kernel/clr/runtime/method_lookup.cpp \
	$(SRC_DIR)/kernel/clr/runtime/execution.cpp \
	$(SRC_DIR)/kernel/clr/runtime/runtime_api.cpp \
	$(SRC_DIR)/kernel/clr/runtime/runtime_state.cpp \
	$(SRC_DIR)/kernel/clr/runtime/runtime_type_system.cpp \
	$(SRC_DIR)/kernel/clr/runtime/runtime_gc.cpp \
	$(SRC_DIR)/kernel/clr/runtime/Execution_adapters.cpp \
	$(SRC_DIR)/kernel/clr/runtime/runtime_support.cpp \
	$(SRC_DIR)/kernel/clr/runtime/runtime_boot.cpp \
	$(LIBRARY_CPP_SOURCES)

# Architecture selection (default: x86_64)
ARCH ?= x86_64

# Debug build: DEBUG=1 enables -g -O0 -DDEBUG; default is optimised release.
DEBUG ?= 0
ifeq ($(DEBUG),1)
    OPT_FLAGS := -O0 -g -DDEBUG
else
    OPT_FLAGS := -O2
endif

ifeq ($(ARCH),aarch64)

# --------------------------------------------------------------------------
# AArch64 toolchain
# --------------------------------------------------------------------------

CC_AA64 := aarch64-linux-gnu-gcc
CXX_AA64 := aarch64-linux-gnu-g++
LD_AA64 := aarch64-linux-gnu-ld
OC_AA64 := aarch64-linux-gnu-objcopy

BUILD_DIR_AA64    := $(BUILD_DIR)/aarch64
KERNEL_ELF_AA64   := $(BUILD_DIR_AA64)/Zapada.elf
KERNEL_IMG_AA64   := $(BUILD_DIR_AA64)/kernel8.img

# --------------------------------------------------------------------------
# Board selection (AArch64 only)
#
# Controls the PL011 UART0 base address and the FDT fallback memory layout.
# Supported values:
#   BOARD=virt  (default) - QEMU virt, PL011 UART @ 0x09000000
#   BOARD=rpi3            - BCM2837   (RPi 3, 3+, 3A+), UART0 @ 0x3F201000
#   BOARD=rpi4            - BCM2711   (RPi 4B, CM4),     UART0 @ 0xFE201000
#
# Note: Raspberry Pi 5 uses the RP1 south-bridge chip for I/O. Its UART
# controller is not PL011-compatible at the same register level. RPi 5
# support is deferred to a future driver stage.
#
# On real hardware, fdt_get_memory() reads the /memory node from the DTB
# passed by the firmware so the memory layout is always correct regardless
# of BOARD. The fallback constants in fdt.h are only used when no valid
# FDT is present (e.g. QEMU raspi3b, which does not forward a DTB).
#
# Usage:
#   make ARCH=aarch64                  (default: QEMU virt)
#   make ARCH=aarch64 BOARD=virt      (QEMU virt machine)
#   make ARCH=aarch64 BOARD=rpi4      (RPi 4B / CM4)
# --------------------------------------------------------------------------

BOARD ?= virt

ifeq ($(BOARD),virt)
    BOARD_UART_DEFINE := -DBOARD_UART_BASE=0x09000000UL
    LINKER_SCRIPT_AA64 := linker_aarch64_virt.ld
else ifeq ($(BOARD),rpi4)
    BOARD_UART_DEFINE := -DBOARD_UART_BASE=0xFE201000UL
    LINKER_SCRIPT_AA64 := linker_aarch64.ld
else
    BOARD_UART_DEFINE := -DBOARD_UART_BASE=0x3F201000UL
    LINKER_SCRIPT_AA64 := linker_aarch64.ld
endif

CFLAGS_AA64 := \
    -std=c99             \
    -ffreestanding       \
    -fno-builtin         \
    -fno-stack-protector \
    -fno-pic             \
    -nostdinc            \
    -march=armv8-a       \
    -mgeneral-regs-only  \
    -mstrict-align       \
    $(OPT_FLAGS)         \
    -Wall                \
    -Wextra              \
    -Wno-sign-compare    \
    -Werror              \
    -DARCH_AARCH64       \
    -DPLATFORM_EMULATED_FLOATINGPOINT \
    -DVERSION_MAJOR=1    \
    -DVERSION_MINOR=0    \
    -DVERSION_BUILD=0    \
    -DVERSION_REVISION=0 \
    -DPLATFORM_DEPENDENT_HEAP_SIZE_THRESHOLD=1u/2u \
    -DPLATFORM_DEPENDENT_HEAP_SIZE_THRESHOLD_UPPER=3u/4u \
    -I $(SRC_DIR) \
    -I $(SRC_DIR)/kernel/clr/include \
    -I $(CLR_FORK_DIR) \
    -I $(CLR_FORK_INCLUDE_DIR) \
    -I $(SRC_DIR)/kernel/clr/core \
    -I $(SRC_DIR)/kernel/clr/core/Include \
    $(LIBRARY_INCLUDE_FLAGS) \
    $(BOARD_UART_DEFINE)

ASFLAGS_AA64 := $(CFLAGS_AA64)
CXXFLAGS_AA64 := \
    -ffreestanding       \
    -fno-builtin         \
    -fno-exceptions      \
    -fno-rtti            \
    -fno-stack-protector \
    -fno-pic             \
    -march=armv8-a       \
    -mstrict-align       \
    $(OPT_FLAGS)         \
    -Wall                \
    -Wextra              \
    -Wno-sign-compare    \
    -Werror              \
    -DARCH_AARCH64       \
    -DPLATFORM_EMULATED_FLOATINGPOINT \
    -DVERSION_MAJOR=1    \
    -DVERSION_MINOR=0    \
    -DVERSION_BUILD=0    \
    -DVERSION_REVISION=0 \
    -DPLATFORM_DEPENDENT_HEAP_SIZE_THRESHOLD=1u/2u \
    -DPLATFORM_DEPENDENT_HEAP_SIZE_THRESHOLD_UPPER=3u/4u \
    -I $(SRC_DIR)        \
    -I $(SRC_DIR)/kernel/clr/include \
    -I $(CLR_FORK_DIR) \
    -I $(CLR_FORK_INCLUDE_DIR) \
    -I $(SRC_DIR)/kernel/clr/core \
    -I $(SRC_DIR)/kernel/clr/core/Include \
    $(LIBRARY_INCLUDE_FLAGS) \
    $(BOARD_UART_DEFINE)

ifeq ($(ENABLE_ZACLR),1)
CFLAGS_AA64 := $(CFLAGS_AA64) -DZACLR_ENABLED=1
CXXFLAGS_AA64 := $(CXXFLAGS_AA64) -DZACLR_ENABLED=1
endif
LDFLAGS_AA64 := -T $(LINKER_SCRIPT_AA64) -nostdlib

ASM_SOURCES_AA64 := \
    $(SRC_DIR)/boot/aarch64/boot.S                      \
    $(SRC_DIR)/kernel/arch/aarch64/exception_vectors.S  \
    $(SRC_DIR)/kernel/arch/aarch64/context_switch.S

C_SOURCES_AA64 := \
    $(SRC_DIR)/kernel/main_aarch64.c                    \
    $(SRC_DIR)/kernel/fb_console.c                      \
    $(SRC_DIR)/kernel/text_console.c                    \
    $(SRC_DIR)/kernel/panic.c                           \
    $(SRC_DIR)/kernel/support/kernel_memory.c           \
    $(SRC_DIR)/kernel/initramfs/bootstrap.c             \
    $(SRC_DIR)/kernel/initramfs/tinflate.c              \
    $(SRC_DIR)/kernel/initramfs/cpio.c                  \
    $(SRC_DIR)/kernel/initramfs/ramdisk.c               \
    $(SRC_DIR)/kernel/initramfs/loader.c                \
    $(SRC_DIR)/kernel/arch/aarch64/uart.c               \
    $(SRC_DIR)/kernel/arch/aarch64/exception.c          \
    $(SRC_DIR)/kernel/arch/aarch64/fdt.c                \
    $(SRC_DIR)/kernel/arch/aarch64/pci.c                \
    $(SRC_DIR)/kernel/arch/aarch64/console.c            \
    $(SRC_DIR)/kernel/arch/aarch64/gentimer.c           \
    $(SRC_DIR)/kernel/mm/pmm.c                          \
    $(SRC_DIR)/kernel/mm/heap.c                         \
    $(SRC_DIR)/kernel/process/process.c                 \
    $(SRC_DIR)/kernel/sched/sched.c                     \
    $(SRC_DIR)/kernel/sched/timer.c                     \
    $(SRC_DIR)/kernel/sched/kstack.c                    \
    $(SRC_DIR)/kernel/syscall/syscall.c                 \
    $(SRC_DIR)/kernel/ipc/ipc.c                         \
    $(SRC_DIR)/kernel/arch/aarch64/virtio_mmio.c        \
    $(SRC_DIR)/kernel/drivers/virtio.c                  \
    $(SRC_DIR)/kernel/drivers/virtio_blk.c              \
    $(SRC_DIR)/kernel/gates/phase_gates.c               \
    $(SRC_DIR)/kernel/phase2b.c                         \
    $(SRC_DIR)/kernel/phase2c.c                         \
    $(LIBRARY_C_SOURCES)

CPP_SOURCES_AA64 := \
    $(SRC_DIR)/kernel/gates/gate_pe_helpers.cpp

LIBRARY_REGISTRY_CPP_AA64 :=
LIBRARY_REGISTRY_OBJ_AA64 :=
CPP_SOURCES_AA64 :=

# All AArch64 intermediate objects are rooted in $(BUILD_DIR_AA64) so they
# never collide with x86_64 objects in $(BUILD_DIR)/kernel/. Switching between
# architectures without a 'make clean' is therefore safe.
ASM_OBJECTS_AA64 := $(patsubst $(SRC_DIR)/%.S,$(BUILD_DIR_AA64)/%.o,$(ASM_SOURCES_AA64))
C_OBJECTS_AA64   := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR_AA64)/%.o,$(C_SOURCES_AA64))
CPP_OBJECTS_AA64 := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR_AA64)/%.o,$(CPP_SOURCES_AA64))
ALL_OBJECTS_AA64_BASE := $(ASM_OBJECTS_AA64) $(C_OBJECTS_AA64) $(CPP_OBJECTS_AA64) $(LIBRARY_REGISTRY_OBJ_AA64)

else

# --------------------------------------------------------------------------
# x86_64 toolchain (default)
# --------------------------------------------------------------------------

# Use the cross-compiler if available, otherwise fall back to the system gcc.
CC_CROSS := x86_64-elf-gcc
CC_SYS   := gcc

ifneq ($(shell which $(CC_CROSS) 2>/dev/null),)
    CC := $(CC_CROSS)
    CXX := x86_64-elf-g++
    LD := x86_64-elf-ld
else
    CC := $(CC_SYS)
    CXX := g++
    LD := ld
endif

AS       := nasm
MKRESCUE := grub-mkrescue
QEMU     := qemu-system-x86_64

endif

# --------------------------------------------------------------------------
# Directories
# --------------------------------------------------------------------------

BUILD_DIR   := build
SRC_DIR     := src
ISOROOT_DIR := isoroot
BOOT_GRUB   := $(ISOROOT_DIR)/boot/grub

# --------------------------------------------------------------------------
# Output artifacts
# --------------------------------------------------------------------------

KERNEL_ELF := $(BUILD_DIR)/Zapada.elf
KERNEL_ISO := $(BUILD_DIR)/Zapada.iso
INITRAMFS_IMG := $(BUILD_DIR)/initramfs.cpio.gz

# --------------------------------------------------------------------------
# Source files
# --------------------------------------------------------------------------

# Assembly sources (NASM)
ASM_SOURCES := \
    $(SRC_DIR)/boot/boot.asm                        \
    $(SRC_DIR)/kernel/arch/x86_64/isr.asm          \
    $(SRC_DIR)/kernel/arch/x86_64/context_switch.asm

# C sources
C_SOURCES := \
    $(SRC_DIR)/kernel/main.c                        \
    $(SRC_DIR)/kernel/fb_console.c                  \
    $(SRC_DIR)/kernel/text_console.c                \
    $(SRC_DIR)/kernel/serial.c                      \
    $(SRC_DIR)/kernel/panic.c                       \
    $(SRC_DIR)/kernel/support/kernel_memory.c       \
    $(SRC_DIR)/kernel/initramfs/bootstrap.c         \
    $(SRC_DIR)/kernel/initramfs/tinflate.c          \
    $(SRC_DIR)/kernel/initramfs/cpio.c              \
    $(SRC_DIR)/kernel/initramfs/ramdisk.c           \
    $(SRC_DIR)/kernel/initramfs/loader.c            \
    $(SRC_DIR)/kernel/arch/x86_64/gdt.c             \
    $(SRC_DIR)/kernel/arch/x86_64/idt.c             \
    $(SRC_DIR)/kernel/arch/x86_64/console.c         \
    $(SRC_DIR)/kernel/arch/x86_64/pic.c             \
    $(SRC_DIR)/kernel/arch/x86_64/pit.c             \
    $(SRC_DIR)/kernel/mm/pmm.c                      \
    $(SRC_DIR)/kernel/mm/heap.c                     \
    $(SRC_DIR)/kernel/process/process.c             \
    $(SRC_DIR)/kernel/sched/sched.c                 \
    $(SRC_DIR)/kernel/sched/timer.c                 \
    $(SRC_DIR)/kernel/sched/kstack.c                \
    $(SRC_DIR)/kernel/syscall/syscall.c             \
    $(SRC_DIR)/kernel/ipc/ipc.c                     \
    $(SRC_DIR)/kernel/arch/x86_64/pci.c             \
    $(SRC_DIR)/kernel/drivers/virtio.c               \
    $(SRC_DIR)/kernel/drivers/virtio_blk.c           \
    $(SRC_DIR)/kernel/gates/phase_gates.c           \
    $(SRC_DIR)/kernel/phase2b.c                     \
    $(SRC_DIR)/kernel/phase2c.c                     \
    $(LIBRARY_C_SOURCES)

CPP_SOURCES := \
    $(SRC_DIR)/kernel/gates/gate_pe_helpers.cpp

CPP_SOURCES :=

ZACLR_CPP_SOURCES :=
ZACLR_TEST_CPP_SOURCES :=
ZACLR_GENERATED_ARTIFACTS :=
ZACLR_GENERATED_CPP_SOURCES :=
ZACLR_GENERATED_OBJECTS :=
ZACLR_GENERATED_OBJECTS_AA64 :=
ZACLR_CPP_OBJECTS :=
ZACLR_CPP_OBJECTS_AA64 :=
ZACLR_TEST_OBJECTS :=
ZACLR_TEST_OBJECTS_AA64 :=
ZACLR_EXTRA_OBJECTS :=
ZACLR_EXTRA_OBJECTS_AA64 :=

include make/zaclr_host.mk
include make/zaclr_process.mk
include make/zaclr_native_registry.mk
include make/zaclr_tests.mk
include make/zaclr.mk

ZACLR_CPP_OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(ZACLR_CPP_SOURCES))
ZACLR_CPP_OBJECTS_AA64 := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR_AA64)/%.o,$(ZACLR_CPP_SOURCES))
ZACLR_GENERATED_OBJECTS := $(patsubst $(BUILD_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(ZACLR_GENERATED_CPP_SOURCES))
ZACLR_GENERATED_OBJECTS_AA64 := $(patsubst $(BUILD_DIR)/generated/%.cpp,$(BUILD_DIR_AA64)/generated/%.o,$(ZACLR_GENERATED_CPP_SOURCES))
ZACLR_EXTRA_OBJECTS := $(ZACLR_CPP_OBJECTS) $(ZACLR_GENERATED_OBJECTS) $(ZACLR_NATIVE_LIBRARY_OBJECTS)
ZACLR_EXTRA_OBJECTS_AA64 := $(ZACLR_CPP_OBJECTS_AA64) $(ZACLR_GENERATED_OBJECTS_AA64) $(ZACLR_NATIVE_LIBRARY_OBJECTS_AA64)

ifeq ($(ENABLE_ZACLR_TESTS),1)
ZACLR_TEST_OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(ZACLR_TEST_CPP_SOURCES))
ZACLR_TEST_OBJECTS_AA64 := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR_AA64)/%.o,$(ZACLR_TEST_CPP_SOURCES))
ZACLR_EXTRA_OBJECTS := $(ZACLR_EXTRA_OBJECTS) $(ZACLR_TEST_OBJECTS)
ZACLR_EXTRA_OBJECTS_AA64 := $(ZACLR_EXTRA_OBJECTS_AA64) $(ZACLR_TEST_OBJECTS_AA64)
endif

LIBRARY_REGISTRY_CPP :=
LIBRARY_REGISTRY_OBJ :=

# --------------------------------------------------------------------------
# Object files (placed in build/ mirroring src/ layout)
# --------------------------------------------------------------------------

ASM_OBJECTS := $(ASM_SOURCES:$(SRC_DIR)/%.asm=$(BUILD_DIR)/%.o)
C_OBJECTS   := $(C_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
CPP_OBJECTS := $(CPP_SOURCES:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
ALL_OBJECTS_BASE := $(ASM_OBJECTS) $(C_OBJECTS) $(CPP_OBJECTS) $(LIBRARY_REGISTRY_OBJ)
ALL_OBJECTS := $(ALL_OBJECTS_BASE) $(ZACLR_EXTRA_OBJECTS)
ALL_OBJECTS_AA64 := $(ALL_OBJECTS_AA64_BASE) $(ZACLR_EXTRA_OBJECTS_AA64)

# --------------------------------------------------------------------------
# Compiler and assembler flags
# --------------------------------------------------------------------------

# Include path: project root of src/ so that <kernel/types.h> etc. resolve.
INCLUDES := \
	-I $(SRC_DIR) \
	-I $(BUILD_DIR)/generated/zaclr \
	-I $(SRC_DIR)/kernel/clr/include \
	-I $(CLR_FORK_DIR) \
	-I $(CLR_FORK_INCLUDE_DIR) \
	-I $(CLR_CORE_DIR) \
	-I $(CLR_CORE_INCLUDE_DIR) \
	$(LIBRARY_INCLUDE_FLAGS)

# C compiler flags
#   -std=c99              - C99 standard (required for _Bool, inline, etc.)
#   -ffreestanding        - no standard library, no runtime startup files
#   -fno-builtin          - do not use built-in function substitutions
#   -fno-stack-protector  - no __stack_chk_fail (not available bare metal)
#   -fno-pic -no-pie      - disable position-independent code (fixed link addresses)
#   -mno-red-zone         - disable 128-byte red zone (unsafe with interrupts)
#   -mno-mmx              - disable MMX (kernel does not save/restore MMX state)
#   -mno-sse -mno-sse2    - disable SSE (no save/restore in context switch yet)
#   -m64                  - explicit 64-bit output
#   -nostdinc             - do not use system include paths (use only ours)
#   $(OPT_FLAGS)          - -O2 by default; -O0 -g -DDEBUG when DEBUG=1
#   -Wall -Wextra         - enable warnings
#   -Werror               - treat warnings as errors (clean code discipline)
CFLAGS := \
    -std=c99            \
    -ffreestanding      \
    -fno-builtin        \
    -fno-stack-protector \
    -fno-pic            \
    -no-pie             \
    -mno-red-zone       \
    -mno-mmx            \
    -mno-sse            \
    -mno-sse2           \
    -m64                \
    -nostdinc           \
    $(OPT_FLAGS)        \
    -Wall               \
    -Wextra             \
    -Wno-sign-compare   \
    -Werror             \
    -DARCH_X86_64       \
    -DPLATFORM_EMULATED_FLOATINGPOINT \
    -DVERSION_MAJOR=1   \
    -DVERSION_MINOR=0   \
    -DVERSION_BUILD=0   \
    -DVERSION_REVISION=0 \
    $(INCLUDES)

CFLAGS := $(CFLAGS) -DZACLR_ENABLED=1

CXXFLAGS := \
    -ffreestanding      \
    -fno-builtin        \
    -fno-exceptions     \
    -fno-rtti           \
    -fno-stack-protector \
    -fno-pic            \
    -no-pie             \
    -mno-red-zone       \
    -mno-mmx            \
    -mno-sse            \
    -mno-sse2           \
    -m64                \
    $(OPT_FLAGS)        \
    -Wall               \
    -Wextra             \
    -Wno-sign-compare   \
    -Werror             \
    -DARCH_X86_64       \
    -DPLATFORM_EMULATED_FLOATINGPOINT \
    -DVERSION_MAJOR=1   \
    -DVERSION_MINOR=0   \
    -DVERSION_BUILD=0   \
    -DVERSION_REVISION=0 \
    $(INCLUDES)

CXXFLAGS := $(CXXFLAGS) -DZACLR_ENABLED=1

# NASM flags
#   -f elf64     - 64-bit ELF object output
#   -g           - include debug information
ASFLAGS := -f elf64 -g

# Linker flags
#   -T linker.ld  - use our custom linker script
#   -nostdlib     - no standard library
LDFLAGS := -T linker.ld -nostdlib

# --------------------------------------------------------------------------
# Default target (ARCH-conditional)
# --------------------------------------------------------------------------

ifeq ($(ARCH),aarch64)

.PHONY: all clean

all: $(KERNEL_IMG_AA64)
	@echo "AArch64 initramfs : $(INITRAMFS_IMG)"
	@echo "AArch64 build complete."

else

.PHONY: all kernel iso clean zaclr-generated zaclr-test-shell

all: iso

kernel: $(KERNEL_ELF)

iso: $(KERNEL_ISO)

endif

# --------------------------------------------------------------------------
# Link the kernel ELF
# --------------------------------------------------------------------------

$(KERNEL_ELF): $(ZACLR_GENERATED_ARTIFACTS) $(ALL_OBJECTS) linker.ld | $(BUILD_DIR)
	$(LD) $(LDFLAGS) -o $@ $(ALL_OBJECTS)
	@echo "Kernel ELF: $@"

# --------------------------------------------------------------------------
# Compile C sources
# --------------------------------------------------------------------------

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/generated/%.o: $(BUILD_DIR)/generated/%.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(LIBRARY_REGISTRY_CPP): Makefile $(LIBRARY_FILES) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@echo '/* Auto-generated native library registry. */' > $@
	@echo '#include <kernel/clr/core/Core.h>' >> $@
	@echo '#include <kernel/clr/runtime/runtime_pe.h>' >> $@
	@echo '#include <kernel/clr/core/include/nanoCLR_Runtime.h>' >> $@
	@for lib in $(LIBRARY_DIR_NAMES); do \
		sym=`echo $$lib | sed 's/[^A-Za-z0-9]/_/g'`; \
		lookup=`echo $$lib | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9]/_/g'`; \
		echo "extern \"C\" const CLR_RT_NativeAssemblyData g_$${sym}NativeAssemblyData;" >> $@; \
		echo "extern \"C\" void $${lookup}_build_native_lookup(void);" >> $@; \
	done
	@echo 'const CLR_RT_NativeAssemblyData *g_CLR_InteropAssembliesNativeData[] = {' >> $@
	@for lib in $(LIBRARY_DIR_NAMES); do \
		sym=`echo $$lib | sed 's/[^A-Za-z0-9]/_/g'`; \
		echo "    &g_$${sym}NativeAssemblyData," >> $@; \
	done
	@echo '    NULL' >> $@
	@echo '};' >> $@
	@count=`printf '%s\n' $(LIBRARY_DIR_NAMES) | sed '/^$$/d' | wc -l`; \
		echo "const uint16_t g_CLR_InteropAssembliesCount = $${count}u;" >> $@
	@echo 'extern "C" const CLR_RT_NativeAssemblyData *zapada_clr_get_native_assembly_data(const zapada_clr_loaded_assembly_t *loadedAssembly)' >> $@
	@echo '{' >> $@
	@echo '    const CLR_RT_NativeAssemblyData *nativeAssembly;' >> $@
	@echo '    const char *assemblyName;' >> $@
	@echo '    if (loadedAssembly == NULL) { return NULL; }' >> $@
	@echo '    assemblyName = loadedAssembly->name != NULL ? loadedAssembly->name : pe_get_assembly_name(&loadedAssembly->pe);' >> $@
	@echo '    nativeAssembly = GetAssemblyNativeData(assemblyName);' >> $@
	@for lib in $(LIBRARY_DIR_NAMES); do \
		sym=`echo $$lib | sed 's/[^A-Za-z0-9]/_/g'`; \
		lookup=`echo $$lib | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9]/_/g'`; \
		echo "    if (nativeAssembly == &g_$${sym}NativeAssemblyData) { $${lookup}_build_native_lookup(); return nativeAssembly; }" >> $@; \
	done
	@echo '    return nativeAssembly;' >> $@
	@echo '}' >> $@

$(LIBRARY_REGISTRY_OBJ): $(LIBRARY_REGISTRY_CPP) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --------------------------------------------------------------------------
# Assemble NASM sources
# --------------------------------------------------------------------------

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.asm | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# --------------------------------------------------------------------------
# Create the bootable ISO with GRUB2
# --------------------------------------------------------------------------


$(INITRAMFS_IMG): | $(BUILD_DIR)
	@rm -rf $(BUILD_DIR)/initramfs_root
	@mkdir -p $(BUILD_DIR)/initramfs_root
	@if [ -f $(BUILD_DIR)/boot.dll ]; then cp $(BUILD_DIR)/boot.dll $(BUILD_DIR)/initramfs_root/Zapada.Boot.dll; \
	elif [ -f $(SRC_DIR)/managed/Zapada.Boot/bin/Release/net10.0/Zapada.Boot.dll ]; then cp $(SRC_DIR)/managed/Zapada.Boot/bin/Release/net10.0/Zapada.Boot.dll $(BUILD_DIR)/initramfs_root/Zapada.Boot.dll; fi
	@if [ -f $(BUILD_DIR)/hello.dll ]; then cp $(BUILD_DIR)/hello.dll $(BUILD_DIR)/initramfs_root/Zapada.Test.Hello.dll; \
	elif [ -f $(SRC_DIR)/managed/Zapada.Test.Hello/bin/Release/net10.0/Zapada.Test.Hello.dll ]; then cp $(SRC_DIR)/managed/Zapada.Test.Hello/bin/Release/net10.0/Zapada.Test.Hello.dll $(BUILD_DIR)/initramfs_root/Zapada.Test.Hello.dll; fi
	@if [ -f $(BUILD_DIR)/vblk.dll ]; then cp $(BUILD_DIR)/vblk.dll $(BUILD_DIR)/initramfs_root/Zapada.Drivers.VirtioBlock.dll; \
	elif [ -f $(SRC_DIR)/managed/Zapada.Drivers.VirtioBlock/bin/Release/net10.0/Zapada.Drivers.VirtioBlock.dll ]; then cp $(SRC_DIR)/managed/Zapada.Drivers.VirtioBlock/bin/Release/net10.0/Zapada.Drivers.VirtioBlock.dll $(BUILD_DIR)/initramfs_root/Zapada.Drivers.VirtioBlock.dll; fi
	@if [ -f $(BUILD_DIR)/gpt.dll ]; then cp $(BUILD_DIR)/gpt.dll $(BUILD_DIR)/initramfs_root/Zapada.Fs.Gpt.dll; \
	elif [ -f $(SRC_DIR)/managed/Zapada.Fs.Gpt/bin/Release/net10.0/Zapada.Fs.Gpt.dll ]; then cp $(SRC_DIR)/managed/Zapada.Fs.Gpt/bin/Release/net10.0/Zapada.Fs.Gpt.dll $(BUILD_DIR)/initramfs_root/Zapada.Fs.Gpt.dll; fi
	@if [ -f $(BUILD_DIR)/storage.dll ]; then cp $(BUILD_DIR)/storage.dll $(BUILD_DIR)/initramfs_root/Zapada.Storage.dll; \
	elif [ -f $(SRC_DIR)/managed/Zapada.Storage/bin/Release/net10.0/Zapada.Storage.dll ]; then cp $(SRC_DIR)/managed/Zapada.Storage/bin/Release/net10.0/Zapada.Storage.dll $(BUILD_DIR)/initramfs_root/Zapada.Storage.dll; fi
	@if [ -f $(BUILD_DIR)/fat32.dll ]; then cp $(BUILD_DIR)/fat32.dll $(BUILD_DIR)/initramfs_root/Zapada.Fs.Fat32.dll; \
	elif [ -f $(SRC_DIR)/managed/Zapada.Fs.Fat32/bin/Release/net10.0/Zapada.Fs.Fat32.dll ]; then cp $(SRC_DIR)/managed/Zapada.Fs.Fat32/bin/Release/net10.0/Zapada.Fs.Fat32.dll $(BUILD_DIR)/initramfs_root/Zapada.Fs.Fat32.dll; fi
	@if [ -f $(BUILD_DIR)/vfs.dll ]; then cp $(BUILD_DIR)/vfs.dll $(BUILD_DIR)/initramfs_root/Zapada.Fs.Vfs.dll; \
	elif [ -f $(SRC_DIR)/managed/Zapada.Fs.Vfs/bin/Release/net10.0/Zapada.Fs.Vfs.dll ]; then cp $(SRC_DIR)/managed/Zapada.Fs.Vfs/bin/Release/net10.0/Zapada.Fs.Vfs.dll $(BUILD_DIR)/initramfs_root/Zapada.Fs.Vfs.dll; fi
	@if [ -f $(BUILD_DIR)/conf-crossasm.dll ]; then cp $(BUILD_DIR)/conf-crossasm.dll $(BUILD_DIR)/initramfs_root/Zapada.Conformance.CrossAsm.dll; \
	elif [ -f $(SRC_DIR)/managed/Zapada.Conformance.CrossAsm/bin/Release/net10.0/Zapada.Conformance.CrossAsm.dll ]; then cp $(SRC_DIR)/managed/Zapada.Conformance.CrossAsm/bin/Release/net10.0/Zapada.Conformance.CrossAsm.dll $(BUILD_DIR)/initramfs_root/Zapada.Conformance.CrossAsm.dll; fi
	@if [ -f $(BUILD_DIR)/conf.dll ]; then cp $(BUILD_DIR)/conf.dll $(BUILD_DIR)/initramfs_root/Zapada.Conformance.dll; \
	elif [ -f $(SRC_DIR)/managed/Zapada.Conformance/bin/Release/net10.0/Zapada.Conformance.dll ]; then cp $(SRC_DIR)/managed/Zapada.Conformance/bin/Release/net10.0/Zapada.Conformance.dll $(BUILD_DIR)/initramfs_root/Zapada.Conformance.dll; fi
	@if [ -f $(BUILD_DIR)/System.Console.dll ]; then cp $(BUILD_DIR)/System.Console.dll $(BUILD_DIR)/initramfs_root/System.Console.dll; \
	elif [ -f $(SRC_DIR)/managed/System.Console/bin/Release/net10.0/System.Console.dll ]; then cp $(SRC_DIR)/managed/System.Console/bin/Release/net10.0/System.Console.dll $(BUILD_DIR)/initramfs_root/System.Console.dll; fi
	@if [ -d dotnet ]; then \
		find dotnet -maxdepth 1 -type f -exec cp {} $(BUILD_DIR)/initramfs_root/ \; ; \
	fi
	@if [ ! -f $(BUILD_DIR)/initramfs_root/System.Private.CoreLib.dll ]; then \
		if [ -f $(BUILD_DIR)/System.Private.CoreLib.dll ]; then cp $(BUILD_DIR)/System.Private.CoreLib.dll $(BUILD_DIR)/initramfs_root/System.Private.CoreLib.dll; \
		elif [ -f $(SRC_DIR)/managed/System.Private.CoreLib/bin/Release/net10.0/System.Private.CoreLib.dll ]; then cp $(SRC_DIR)/managed/System.Private.CoreLib/bin/Release/net10.0/System.Private.CoreLib.dll $(BUILD_DIR)/initramfs_root/System.Private.CoreLib.dll; fi; \
	fi
	@(cd $(BUILD_DIR)/initramfs_root && find . -print | cpio -o -H newc 2>/dev/null | gzip -n > ../initramfs.cpio.gz)
	@echo "Initramfs image: $@"

$(KERNEL_ISO): $(KERNEL_ELF) $(INITRAMFS_IMG) $(BOOT_GRUB)/grub.cfg | $(BUILD_DIR)
	@cp $(KERNEL_ELF) $(ISOROOT_DIR)/boot/Zapada.elf
	@cp $(INITRAMFS_IMG) $(ISOROOT_DIR)/boot/initramfs.cpio.gz
	$(MKRESCUE) -o $@ $(ISOROOT_DIR)
	@echo "Bootable ISO: $@"

# --------------------------------------------------------------------------
# Create build directory
# --------------------------------------------------------------------------

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# --------------------------------------------------------------------------
# Clean
# --------------------------------------------------------------------------

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(ISOROOT_DIR)/boot/Zapada.elf
	rm -f $(ISOROOT_DIR)/boot/initramfs.cpio.gz
	@echo "Clean complete."

zaclr-generated: $(ZACLR_GENERATED_ARTIFACTS)
	@echo "ZACLR generated artifacts ready."

zaclr-test-shell: $(ZACLR_GENERATED_ARTIFACTS) $(ZACLR_TEST_OBJECTS)
	@echo "ZACLR test-shell objects ready."

# --------------------------------------------------------------------------
# AArch64 build rules (active when ARCH=aarch64)
# --------------------------------------------------------------------------

ifeq ($(ARCH),aarch64)

$(KERNEL_IMG_AA64): $(KERNEL_ELF_AA64)
	@test -f $(INITRAMFS_IMG) || $(MAKE) $(INITRAMFS_IMG)
	$(OC_AA64) -O binary $< $@
	@echo "Kernel image: $@"

$(KERNEL_ELF_AA64): $(ZACLR_GENERATED_ARTIFACTS) $(ALL_OBJECTS_AA64) | $(BUILD_DIR_AA64)
	$(LD_AA64) $(LDFLAGS_AA64) -o $@ $(ALL_OBJECTS_AA64)
	@echo "Kernel ELF: $@"

$(BUILD_DIR_AA64):
	mkdir -p $(BUILD_DIR_AA64)

# Compile boot.S (AArch64 startup assembly via GCC assembler)
$(BUILD_DIR_AA64)/boot/aarch64/%.o: $(SRC_DIR)/boot/aarch64/%.S | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CC_AA64) -c $(ASFLAGS_AA64) $< -o $@

# Compile kernel/main_aarch64.c
$(BUILD_DIR_AA64)/kernel/main_aarch64.o: $(SRC_DIR)/kernel/main_aarch64.c | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CC_AA64) $(CFLAGS_AA64) -c $< -o $@

# Compile any C file under arch/aarch64/
$(BUILD_DIR_AA64)/kernel/arch/aarch64/%.o: $(SRC_DIR)/kernel/arch/aarch64/%.c | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CC_AA64) $(CFLAGS_AA64) -c $< -o $@

# Assemble any .S file under arch/aarch64/
$(BUILD_DIR_AA64)/kernel/arch/aarch64/%.o: $(SRC_DIR)/kernel/arch/aarch64/%.S | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CC_AA64) -c $(ASFLAGS_AA64) $< -o $@

# Compile shared kernel/mm/ sources (pmm.c, heap.c) for AArch64
$(BUILD_DIR_AA64)/kernel/mm/%.o: $(SRC_DIR)/kernel/mm/%.c | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CC_AA64) $(CFLAGS_AA64) -c $< -o $@

# Compile shared kernel/clr_legacy/ sources (CLR subsystem) for AArch64
$(BUILD_DIR_AA64)/kernel/clr_legacy/%.o: $(SRC_DIR)/kernel/clr_legacy/%.c | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CC_AA64) $(CFLAGS_AA64) -c $< -o $@

$(BUILD_DIR_AA64)/kernel/clr/%.o: $(SRC_DIR)/kernel/clr/%.cpp | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CXX_AA64) $(CXXFLAGS_AA64) -c $< -o $@

$(BUILD_DIR_AA64)/kernel/zaclr/%.o: $(SRC_DIR)/kernel/zaclr/%.cpp | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CXX_AA64) $(CXXFLAGS_AA64) -c $< -o $@

$(BUILD_DIR_AA64)/kernel/gates/%.o: $(SRC_DIR)/kernel/gates/%.cpp | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CXX_AA64) $(CXXFLAGS_AA64) -c $< -o $@

$(BUILD_DIR_AA64)/libraries/%.o: $(SRC_DIR)/libraries/%.cpp | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CXX_AA64) $(CXXFLAGS_AA64) -c $< -o $@

$(BUILD_DIR_AA64)/libraries/%.o: $(SRC_DIR)/libraries/%.c | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CC_AA64) $(CFLAGS_AA64) -c $< -o $@

$(BUILD_DIR_AA64)/generated/%.o: $(BUILD_DIR)/generated/%.cpp | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CXX_AA64) $(CXXFLAGS_AA64) -c $< -o $@

$(LIBRARY_REGISTRY_CPP_AA64): Makefile $(LIBRARY_FILES) | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	@echo '/* Auto-generated native library registry. */' > $@
	@echo '#include <kernel/clr/core/Core.h>' >> $@
	@echo '#include <kernel/clr/runtime/runtime_pe.h>' >> $@
	@echo '#include <kernel/clr/core/include/nanoCLR_Runtime.h>' >> $@
	@for lib in $(LIBRARY_DIR_NAMES); do \
		sym=`echo $$lib | sed 's/[^A-Za-z0-9]/_/g'`; \
		lookup=`echo $$lib | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9]/_/g'`; \
		echo "extern \"C\" const CLR_RT_NativeAssemblyData g_$${sym}NativeAssemblyData;" >> $@; \
		echo "extern \"C\" void $${lookup}_build_native_lookup(void);" >> $@; \
	done
	@echo 'const CLR_RT_NativeAssemblyData *g_CLR_InteropAssembliesNativeData[] = {' >> $@
	@for lib in $(LIBRARY_DIR_NAMES); do \
		sym=`echo $$lib | sed 's/[^A-Za-z0-9]/_/g'`; \
		echo "    &g_$${sym}NativeAssemblyData," >> $@; \
	done
	@echo '    NULL' >> $@
	@echo '};' >> $@
	@count=`printf '%s\n' $(LIBRARY_DIR_NAMES) | sed '/^$$/d' | wc -l`; \
		echo "const uint16_t g_CLR_InteropAssembliesCount = $${count}u;" >> $@
	@echo 'extern "C" const CLR_RT_NativeAssemblyData *zapada_clr_get_native_assembly_data(const zapada_clr_loaded_assembly_t *loadedAssembly)' >> $@
	@echo '{' >> $@
	@echo '    const CLR_RT_NativeAssemblyData *nativeAssembly;' >> $@
	@echo '    const char *assemblyName;' >> $@
	@echo '    if (loadedAssembly == NULL) { return NULL; }' >> $@
	@echo '    assemblyName = loadedAssembly->name != NULL ? loadedAssembly->name : pe_get_assembly_name(&loadedAssembly->pe);' >> $@
	@echo '    nativeAssembly = GetAssemblyNativeData(assemblyName);' >> $@
	@for lib in $(LIBRARY_DIR_NAMES); do \
		sym=`echo $$lib | sed 's/[^A-Za-z0-9]/_/g'`; \
		lookup=`echo $$lib | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9]/_/g'`; \
		echo "    if (nativeAssembly == &g_$${sym}NativeAssemblyData) { $${lookup}_build_native_lookup(); return nativeAssembly; }" >> $@; \
	done
	@echo '    return nativeAssembly;' >> $@
	@echo '}' >> $@

$(LIBRARY_REGISTRY_OBJ_AA64): $(LIBRARY_REGISTRY_CPP_AA64) | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CXX_AA64) $(CXXFLAGS_AA64) -c $< -o $@

# Compile Phase 2B kernel/process/ sources for AArch64
$(BUILD_DIR_AA64)/kernel/process/%.o: $(SRC_DIR)/kernel/process/%.c | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CC_AA64) $(CFLAGS_AA64) -c $< -o $@

# Compile Phase 2B kernel/sched/ sources for AArch64
$(BUILD_DIR_AA64)/kernel/sched/%.o: $(SRC_DIR)/kernel/sched/%.c | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CC_AA64) $(CFLAGS_AA64) -c $< -o $@

# Compile Phase 2B kernel/syscall/ sources for AArch64
$(BUILD_DIR_AA64)/kernel/syscall/%.o: $(SRC_DIR)/kernel/syscall/%.c | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CC_AA64) $(CFLAGS_AA64) -c $< -o $@

# Compile Phase 2B kernel/ipc/ sources for AArch64
$(BUILD_DIR_AA64)/kernel/ipc/%.o: $(SRC_DIR)/kernel/ipc/%.c | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CC_AA64) $(CFLAGS_AA64) -c $< -o $@

# Compile top-level kernel/ C sources for AArch64 (e.g. phase2b.c)
$(BUILD_DIR_AA64)/kernel/%.o: $(SRC_DIR)/kernel/%.c | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CC_AA64) $(CFLAGS_AA64) -c $< -o $@

# Compile initramfs/ sources for AArch64
$(BUILD_DIR_AA64)/kernel/initramfs/%.o: $(SRC_DIR)/kernel/initramfs/%.c | $(BUILD_DIR_AA64)
	@mkdir -p $(dir $@)
	$(CC_AA64) $(CFLAGS_AA64) -c $< -o $@

endif

# --------------------------------------------------------------------------
# Gate test - runs the kernel in QEMU and checks serial output patterns
#
# Requires QEMU to be available in the environment PATH.
# On Windows: run from WSL, or use tests/x86_64/gate_test.ps1 from PowerShell.
# On Linux/CI: QEMU must be installed (apt install qemu-system-x86).
#
# Usage:
#   make qemu-test
#   QEMU=/path/to/qemu-system-x86_64 make qemu-test
# --------------------------------------------------------------------------

qemu-test: $(KERNEL_ISO)
	@echo "Running Stage 1.2 gate test..."
	@bash tests/x86_64/gate_test.sh

.PHONY: all kernel iso qemu-test clean



