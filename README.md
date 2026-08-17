# vGPU QEMU Fork

A QEMU fork implementing virtual GPU (vGPU) emulation for multiple GPU vendors. Provides a framework for GPU device emulation with support for NVIDIA, AMD, and Intel GPUs, plus a thermal simulation device for testing thermal management drivers.

## Overview

This project extends QEMU with a modular vGPU framework (`hw/display/core/vgpu_core.{c,h}`) and vendor-specific GPU implementations:

| Vendor | Architectures | Device Models |
|--------|---------------|---------------|
| **NVIDIA** | Kepler, Maxwell, Pascal, Turing | GK208 (GT 730), GM107 (GTX 750 Ti), GP104 (GTX 1080), TU102 (RTX 2080) |
| **AMD** | R500, GFX8, GFX10 | R500, GFX8, GFX10 |
| **Intel** | Xe2 | Xe2 |

## Architecture

```
hw/display/
├── core/
│   ├── vgpu_core.c/h    # Common vGPU framework (VGPUState, VGPUOps, MMIO)
│   └── vga.c            # Legacy VGA support
├── nvidia/
│   ├── nv_core.c/h      # NVIDIA common registers, BOOT_0, PMC, PBUS, PFIFO, PGRAPH, PMU
│   ├── gk208/           # Kepler GK208 (GT 730)
│   ├── gm107/           # Maxwell GM107 (GTX 750 Ti)
│   ├── gp104/           # Pascal GP104 (GTX 1080)
│   └── tu102/           # Turing TU102 (RTX 2080)
├── amd/
│   ├── amd_core.c/h     # AMD common infrastructure
│   ├── r500.c           # R500 series
│   ├── gfx8/            # GFX8 (Polaris)
│   └── gfx10/           # GFX10 (RDNA)
└── intel/
    ├── intel_core.c/h   # Intel common infrastructure
    └── xe2/             # Xe2 architecture
```

## Key Components

### vGPU Core Framework (`vgpu_core.{c,h}`)
- **VGPUState**: Common state embedded in each GPU device (PCI device, BARs, VRAM, MMIO backing store)
- **VGPUOps**: Per-chip operations table (chip name, PCI IDs, BAR sizes, register tables, overrides)
- **MMIO handling**: Generic read/write with per-chip register name logging and override hooks
- **Lifecycle hooks**: `chip_realize`, `chip_reset`, `chip_exit` for vendor-specific init
- **Properties**: Standard `vram_size`, `display`, `log_all` properties

### NVIDIA Layer (`nv_core.{c,h}`)
- Common register definitions: PMC, PTIMER, PBUS, PFIFO, PGRAPH, PMU, PFUSE, PFFB, PDISPLAY
- BOOT_0 encoding helpers per architecture
- Shared register name table (`nv_common_reg_table`)
- `nv_common_realize()` - initializes PMC_BOOT_0 and delegates to vGPU core

### Thermal Simulation Device (`hw/misc/vgpu_thermal.c`)
- **PCI device** (10de:13b2) simulating GPU thermal behavior
- **Thermal physics**: `dT/dt = (P - (T - T_amb)/Rth) / Cth` with 100ms timer
- **Trip points**: fan_boost, downclock, critical, shutdown
- **Auto fan curve** + **auto pstate throttling**
- **MMIO interface** compatible with nouveau/amdgpu thermal drivers
- **Live migration** via VMState

## Building

```bash
# Standard QEMU configure
./configure --target-list=x86_64-softmmu --enable-debug

# Or with meson directly
meson setup build
ninja -C build
```

### Enable Thermal Device
```bash
# Via Kconfig
CONFIG_VGPU_THERMAL=y

# Or meson option
meson setup build -Dvgpu_thermal=enabled
```

## Usage

### Run with NVIDIA GK208 (GT 730)
```bash
qemu-system-x86_64 \
  -device nvidia-gt730,romfile=/path/to/vbios.rom \
  -display gtk
```

### Run with Thermal Simulation
```bash
qemu-system-x86_64 \
  -device nvidia-gt730,romfile=/path/to/vbios.rom \
  -device vgpu-thermal \
  -display gtk
```

Inside guest (Linux with nouveau):
```bash
# Check thermal zones
cat /sys/class/thermal/thermal_zone*/temp

# Nouveau hwmon interface
cat /sys/class/hwmon/hwmon*/temp1_input
cat /sys/class/hwmon/hwmon*/pwm1
echo 2 > /sys/class/hwmon/hwmon*/pwm1_enable  # auto mode
```

### Device Properties
```bash
# Set VRAM size
-device nvidia-gt730,vram_size=1G

# Enable display output
-device nvidia-gt730,display=on

# Thermal device parameters (all configurable via MMIO)
-device vgpu-thermal
```

## Supported GPU Models

| Device | PCI ID | Architecture | Status |
|--------|--------|--------------|--------|
| GK208 (GT 730) | 10de:1287 | Kepler | Working |
| GM107 (GTX 750 Ti) | 10de:1381 | Maxwell | Working |
| GP104 (GTX 1080) | 10de:1b80 | Pascal | Working |
| TU102 (RTX 2080) | 10de:1e81 | Turing | Working |
| AMD R500 | 1002:7240 | R500 | Basic |
| AMD GFX8 | 1002:67df | Polaris | Basic |
| AMD GFX10 | 1002:731f | RDNA | Basic |
| Intel Xe2 | 8086:xxxx | Xe2 | Experimental |

## Testing with Nouveau

The GK208 implementation includes VBIOS shadowing at BAR0+0x300000 for nouveau driver compatibility:

```bash
# With real VBIOS
-device nvidia-gt730,romfile=/path/to/gk208.rom

# Key registers implemented:
# - PMC_BOOT_0 (chip identification)
# - NV_PDEV_INFO (device info)
# - PFIFO RAMFC/CHAN (Kepler channel setup)
# - VRAM lo_base/lo_top (1 GiB VRAM reporting)
# - FBP count/pmask (1 partition, enabled)
```

## Development

### Adding a New GPU
1. Create `hw/display/<vendor>/<arch>/<model>.c`
2. Define `VGPUOps` with chip-specific registers/overrides
3. Implement `chip_realize`/`chip_reset` if needed
4. Register device type with `type_init()`
5. Add to `hw/display/<vendor>/meson.build` and Kconfig

### Register Override Pattern
```c
static uint64_t mychip_reg_read_override(void *opaque, hwaddr addr, unsigned size) {
    if (addr == SPECIAL_REG) return SPECIAL_VALUE;
    return UINT64_MAX; // use generic path
}

static const VGPUOps mychip_ops = {
    .reg_read_override = mychip_reg_read_override,
    // ...
};
```

## License

MIT License - see [LICENSE](LICENSE) for details.

This project is a fork of QEMU. QEMU is licensed under GPL-2.0-or-later. The vGPU additions in this repository are licensed under MIT.