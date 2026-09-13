#!/bin/bash
# Script to run QEMU with an emulated GPU for testing
set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
QEMU_BIN="$PROJECT_DIR/qemu/build/qemu-system-x86_64"
INITRD="$PROJECT_DIR/initrd"
ISO="$1"
RAM="${2:-2G}"
GPU_DEV="${3:-nvidia-gt730}"

if [ -z "$ISO" ]; then
    echo "Usage: $0 <boot-image> [ram] [gpu-device]"
    echo ""
    echo "Options:"
    echo "  boot-image: ISO or disk image to boot"
    echo "  ram: RAM size (default: 2G)"
    echo "  gpu-device: QEMU GPU device name (default: nvidia-gt730)"
    echo "              e.g. nvidia-gtx750ti, nvidia-gtx1080, nvidia-rtx2080,"
    echo "                   amd-rx480, amd-rx6700xt, amd-r500, intel-arc-a770"
    exit 1
fi

# Check if image exists
if [ ! -f "$ISO" ]; then
    echo "Error: boot image '$ISO' not found"
    exit 1
fi

echo "Starting QEMU with device: $GPU_DEV"
exec "$QEMU_BIN" \
    -L /usr/share/qemu \
    -m "$RAM" \
    -enable-kvm \
    -cpu host \
    -smp 4 \
    -machine type=q35,accel=kvm \
    -kernel "$ISO" \
    -initrd "$INITRD" \
    -append "console=ttyS0 console=tty0 nouveau.modeset=1 nouveau.debug=debug rd.live.image root=LABEL=ARGENT rootfstype=auto rd.luks=0 rd.lvm=0 rd.md=0 rd.dm=0" \
    -device "$GPU_DEV,log_all=true,display=false" \
    -serial stdio \
    -vga none \
    -nographic \
    -nodefaults \
    -no-reboot
