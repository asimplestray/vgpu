#!/bin/bash
# Build QEMU with vGPU multi-GPU emulation support
set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
QEMU_DIR="$PROJECT_DIR/qemu"
BUILD_DIR="$QEMU_DIR/build"

CPU_CORES=$(nproc)

echo "=== vGPU Project - Build QEMU with Multi-GPU Emulation Framework ==="
echo ""

# Step 1: Copy device sources to QEMU tree
echo "[1/4] Copying vGPU framework components to QEMU tree..."

# Remove any old copies if they exist, to ensure clean sync
rm -rf "$QEMU_DIR/hw/display/core"
rm -rf "$QEMU_DIR/hw/display/nvidia"
rm -rf "$QEMU_DIR/hw/display/amd"
rm -rf "$QEMU_DIR/hw/display/intel"

# Copy directories
cp -r "$PROJECT_DIR/hw/display/core" "$QEMU_DIR/hw/display/"
cp -r "$PROJECT_DIR/hw/display/nvidia" "$QEMU_DIR/hw/display/"
cp -r "$PROJECT_DIR/hw/display/amd" "$QEMU_DIR/hw/display/"
cp -r "$PROJECT_DIR/hw/display/intel" "$QEMU_DIR/hw/display/"

# Step 2: Configure QEMU
echo "[2/4] Configuring QEMU..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
../configure \
    --target-list=x86_64-softmmu \
    --enable-pixman \
    --disable-opengl \
    --disable-spice \
    --disable-docs \
    --disable-vhost-net \
    --disable-vhost-user \
    --python=$(which python3) \
    2>&1 | tail -5

# Step 3: Build QEMU
echo "[3/4] Building QEMU (this may take a while)..."
ninja -j"$CPU_CORES" qemu-system-x86_64 2>&1 | tail -5

# Step 4: Verify
echo "[4/4] Verifying vGPU devices..."
FAILED_DEVICES=""
for dev in nvidia-gt730 nvidia-gtx750ti nvidia-gtx1080 nvidia-rtx2080 amd-rx480 amd-rx6700xt amd-r500 intel-arc-a770; do
    if "$BUILD_DIR/qemu-system-x86_64" -device help 2>&1 | grep -q "$dev"; then
        echo "  [OK] $dev is available"
    else
        echo "  [FAIL] $dev is missing!"
        FAILED_DEVICES="$FAILED_DEVICES $dev"
    fi
done

if [ -n "$FAILED_DEVICES" ]; then
    echo ""
    echo "=== FAILED: Some vGPU devices are missing in the build: $FAILED_DEVICES ==="
    exit 1
else
    echo ""
    echo "=== SUCCESS: All vGPU devices built and available! ==="
    exit 0
fi
