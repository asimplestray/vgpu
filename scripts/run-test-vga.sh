#!/bin/bash
# Script to run QEMU with an emulated GPU and VGA output
set -e

QEMU_BIN="/home/strayfoda/vgpu/qemu/build/qemu-system-x86_64"
ISO="$1"
RAM="${2:-2G}"
GPU_DEV="${3:-nvidia-gt730}"

if [ -z "$ISO" ]; then
    echo "Usage: $0 <boot-image> [ram] [gpu-device]"
    exit 1
fi

if [ ! -f "$ISO" ]; then
    echo "Error: boot image '$ISO' not found"
    exit 1
fi

echo "Starting QEMU in VGA mode with device: $GPU_DEV"
exec "$QEMU_BIN" \
    -m "$RAM" \
    -enable-kvm \
    -cpu host \
    -smp 4 \
    -machine type=q35,accel=kvm \
    -drive file="$ISO,format=raw,if=virtio" \
    -device "$GPU_DEV,log_all=true,display=true" \
    -vga virtio \
    -display gtk \
    -nodefaults \
    -no-reboot
