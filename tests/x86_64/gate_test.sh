#!/usr/bin/env bash
# Zapada - tests/x86_64/gate_test.sh
#
# Stage 1.2 gate test for Linux / CI.
#
# Launches the kernel in QEMU, captures serial output, and checks that all
# Stage 1.2 gate conditions are present in the output.
#
# Usage:
#   bash tests/x86_64/gate_test.sh
#   bash tests/x86_64/gate_test.sh --build
#   QEMU=qemu-system-x86_64 bash tests/x86_64/gate_test.sh
#
# Exit code:
#   0 - all gate checks passed
#   1 - one or more gate checks failed or QEMU did not produce output
#
# Environment variables:
#   QEMU         - QEMU binary (default: qemu-system-x86_64)
#   ISO          - Kernel ISO path (default: build/Zapada.iso)
#   SERIAL_LOG   - Serial output capture path (default: build/serial.log)
#   TIMEOUT_SECS - Seconds to wait for kernel (default: 10)

set -euo pipefail

QEMU="${QEMU:-qemu-system-x86_64}"
ISO="${ISO:-build/Zapada.iso}"
SERIAL_LOG="${SERIAL_LOG:-build/serial.log}"
TIMEOUT_SECS="${TIMEOUT_SECS:-10}"

BUILD=0
for arg in "$@"; do
    case "$arg" in
        --build) BUILD=1 ;;
    esac
done

# --------------------------------------------------------------------------
# Gate conditions
#
# GATE_CHECKS  : substrings that MUST appear somewhere in the serial log.
# FORBID_CHECKS: substrings that must NOT appear in the serial log.
#
# Extend these arrays as new stages are implemented.
# --------------------------------------------------------------------------

GATE_CHECKS=(
    # Stage 1.1 conditions (always required)
    "Multiboot2 magic   : OK"
    "Memory map         :"

    # Stage 1.2 conditions
    "GDT                : loaded"
    "IDT                : loaded (vectors 0-255 installed)"
    "PMM free frames    :"
    "Heap probe         : 0x"
    "Stage 1.2 check    : early scaffolding initialized"
    "System halted after successful Stage 1.2 bring-up."
)

FORBID_CHECKS=(
    "KERNEL PANIC"
)

# --------------------------------------------------------------------------
# Optional build step
# --------------------------------------------------------------------------

if [ "$BUILD" -eq 1 ]; then
    echo "Building kernel..."
    make all
fi

# --------------------------------------------------------------------------
# Check prerequisites
# --------------------------------------------------------------------------

if ! command -v "$QEMU" > /dev/null 2>&1; then
    echo "FAIL: QEMU binary not found: $QEMU"
    exit 1
fi

if [ ! -f "$ISO" ]; then
    echo "FAIL: ISO not found: $ISO"
    echo "      Run 'make all' or pass --build to build first."
    exit 1
fi

# --------------------------------------------------------------------------
# Run QEMU
# --------------------------------------------------------------------------

echo ""
echo "Zapada Gate Test - x86_64 Stage 1.2"
echo "------------------------------------"
echo "ISO     : $ISO"
echo "Timeout : ${TIMEOUT_SECS}s"
echo ""
echo "Launching QEMU..."

rm -f "$SERIAL_LOG"

"$QEMU" \
    -cdrom "$ISO" \
    -serial "file:$SERIAL_LOG" \
    -display none \
    -no-reboot &

QEMU_PID=$!
sleep "$TIMEOUT_SECS"

if kill -0 "$QEMU_PID" 2>/dev/null; then
    kill "$QEMU_PID" 2>/dev/null || true
fi

sleep 0.5

# --------------------------------------------------------------------------
# Check output
# --------------------------------------------------------------------------

if [ ! -f "$SERIAL_LOG" ]; then
    echo "FAIL: serial.log was not created. QEMU may not have started correctly."
    exit 1
fi

if [ ! -s "$SERIAL_LOG" ]; then
    echo "FAIL: serial.log is empty. Kernel produced no output."
    exit 1
fi

PASSED=0
FAILED=0

echo "Gate check results:"
echo ""

for check in "${GATE_CHECKS[@]}"; do
    if grep -qF "$check" "$SERIAL_LOG"; then
        echo "  PASS  $check"
        PASSED=$((PASSED + 1))
    else
        echo "  FAIL  $check"
        FAILED=$((FAILED + 1))
    fi
done

for check in "${FORBID_CHECKS[@]}"; do
    if grep -qF "$check" "$SERIAL_LOG"; then
        echo "  FAIL  (forbidden) $check"
        FAILED=$((FAILED + 1))
    else
        echo "  PASS  (not present) $check"
        PASSED=$((PASSED + 1))
    fi
done

echo ""
echo "Results: $PASSED passed, $FAILED failed"
echo ""

if [ "$FAILED" -gt 0 ]; then
    echo "GATE TEST FAILED"
    echo ""
    echo "Full serial output:"
    echo "------------------------------------"
    cat "$SERIAL_LOG"
    exit 1
else
    echo "GATE TEST PASSED"
    exit 0
fi

