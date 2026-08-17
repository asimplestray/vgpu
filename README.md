# vGPU Thermal Simulation Device

A QEMU PCI device that simulates GPU thermal behavior for testing thermal management drivers (nouveau, amdgpu, etc.) in virtualized environments.

## Features

- **Thermal physics simulation**: `dT/dt = (P - (T - T_amb)/Rth) / Cth` with 100ms timer
- **Trip points**: fan_boost (85°C), downclock (90°C), critical (95°C), shutdown (100°C)
- **Auto fan curve**: PWM scales from `pwm_min`@`temp_target` to `pwm_max`@`temp_max`
- **Auto throttling**: Decrements pstate on downclock trip, forces powersave on critical
- **MMIO interface** (BAR 0): temperature, fan PWM/RPM, pstate, trip points, thermal parameters
- **Live migration support** via VMState
- **Compatible with** nouveau, amdgpu, and other GPU thermal drivers

## Device Usage

```bash
qemu-system-x86_64 ... -device vgpu-thermal
```

PCI IDs: Vendor `0x10de` (NVIDIA), Device `0x13b2` (Virtual Thermal)

## MMIO Register Map (BAR 0)

| Offset | Register | Access | Description |
|--------|----------|--------|-------------|
| 0x00 | TEMP_CURRENT | RO | Current temperature (°C × 100) |
| 0x04 | FAN_PWM | RW | Fan PWM duty cycle (0-100%) |
| 0x08 | FAN_RPM | RO | Fan speed (RPM) |
| 0x0C | PSTATE | RW | Current pstate value |
| 0x10 | TRIP_FAN_BOOST | RW | Fan boost trip point (°C) |
| 0x14 | TRIP_DOWNCLK | RW | Downclock trip point (°C) |
| 0x18 | TRIP_CRITICAL | RW | Critical trip point (°C) |
| 0x1C | TRIP_SHUTDOWN | RW | Shutdown trip point (°C) |
| 0x20 | THERMAL_RTH | RW | Thermal resistance (Q16.16 °C/W) |
| 0x24 | THERMAL_CTH | RW | Thermal capacitance (Q16.16 J/°C) |
| 0x28 | AMBIENT_TEMP | RW | Ambient temperature (°C × 100) |
| 0x2C | MAX_POWER | RW | Max power at max pstate (W × 100) |
| 0x30 | MIN_POWER | RW | Min power at min pstate (W × 100) |
| 0x34 | FAN_PWM_MIN | RW | Minimum fan PWM (0-100%) |
| 0x38 | FAN_PWM_MAX | RW | Maximum fan PWM (0-100%) |
| 0x3C | FAN_TEMP_TARGET | RW | Fan curve target temp (°C) |
| 0x40 | FAN_TEMP_MAX | RW | Fan curve max temp (°C) |
| 0x44 | STATUS | RO | Status flags |
| 0x48 | CTRL | RW | Control flags |

### Status Flags
- `0x1`: THROTTLING - GPU is being throttled
- `0x2`: FAN_BOOST - Fan at 100% (fan boost trip hit)
- `0x4`: CRITICAL - Critical temperature reached
- `0x8`: SHUTDOWN_PENDING - Shutdown trip hit, system will power off

### Control Flags
- `0x1`: ENABLE - Enable thermal simulation timer
- `0x2`: AUTO_FAN - Automatic fan curve control
- `0x4`: AUTO_THROTTLE - Automatic pstate throttling

## Default Configuration (GTX 780-like)

- Ambient temp: 25°C
- Thermal resistance: 0.5 °C/W
- Thermal capacitance: 10 J/°C
- Max power: 250W
- Min power: 30W
- Fan curve: 20% @ 70°C → 100% @ 90°C
- Trip points: 85°C / 90°C / 95°C / 100°C

## Building

Part of QEMU build system. Enable with:

```bash
./configure --enable-vgpu-thermal
```

Or set `CONFIG_VGPU_THERMAL=y` in Kconfig.

## Testing Thermal Driver

Inside guest (Linux with nouveau):

```bash
# Read current temperature
cat /sys/class/hwmon/hwmonX/temp1_input

# Read trip points
cat /sys/class/hwmon/hwmonX/temp1_max
cat /sys/class/hwmon/hwmonX/temp1_crit

# Manual fan control
echo 1 > /sys/class/hwmon/hwmonX/pwm1_enable
echo 128 > /sys/class/hwmon/hwmonX/pwm1  # 50%

# Auto fan mode
echo 2 > /sys/class/hwmon/hwmonX/pwm1_enable
```

## License

MIT License - see [LICENSE](LICENSE) for details.