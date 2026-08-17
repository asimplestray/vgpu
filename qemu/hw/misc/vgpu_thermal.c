/*
 * vGPU Thermal Simulation Device
 *
 * Simulates GPU thermal behavior including temperature, fan speed,
 * and automatic throttling based on trip points. Designed for testing
 * thermal management drivers (nouveau, amdgpu, etc.) in virtualized environments.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "hw/core/qdev-properties.h"
#include "qemu/timer.h"
#include "qemu/module.h"
#include "trace.h"
#include "qom/object.h"
#include "system/runstate.h"
#include "migration/vmstate.h"

#define TYPE_VGPU_THERMAL "vgpu-thermal"
OBJECT_DECLARE_SIMPLE_TYPE(VGPUThermalState, VGPU_THERMAL)

#define VGPU_THERMAL_MMIO_SIZE 0x1000

enum {
    REG_TEMP_CURRENT     = 0x00,  /* RO, °C * 100 */
    REG_FAN_PWM          = 0x04,  /* RW, 0-100% */
    REG_FAN_RPM          = 0x08,  /* RO */
    REG_PSTATE           = 0x0C,  /* RW, pstate value */
    REG_TRIP_FAN_BOOST   = 0x10,  /* RW, °C */
    REG_TRIP_DOWNCLK     = 0x14,  /* RW, °C */
    REG_TRIP_CRITICAL    = 0x18,  /* RW, °C */
    REG_TRIP_SHUTDOWN    = 0x1C,  /* RW, °C */
    REG_THERMAL_RTH      = 0x20,  /* RW, Q16.16 °C/W */
    REG_THERMAL_CTH      = 0x24,  /* RW, Q16.16 J/°C */
    REG_AMBIENT_TEMP     = 0x28,  /* RW, °C * 100 */
    REG_MAX_POWER        = 0x2C,  /* RW, W * 100 */
    REG_MIN_POWER        = 0x30,  /* RW, W * 100 */
    REG_FAN_PWM_MIN      = 0x34,  /* RW, 0-100% */
    REG_FAN_PWM_MAX      = 0x38,  /* RW, 0-100% */
    REG_FAN_TEMP_TARGET  = 0x3C,  /* RW, °C */
    REG_FAN_TEMP_MAX     = 0x40,  /* RW, °C */
    REG_STATUS           = 0x44,  /* RO, status flags */
    REG_CTRL             = 0x48,  /* RW, control flags */
};

#define STATUS_THROTTLING       (1 << 0)
#define STATUS_FAN_BOOST        (1 << 1)
#define STATUS_CRITICAL         (1 << 2)
#define STATUS_SHUTDOWN_PENDING (1 << 3)

#define CTRL_ENABLE             (1 << 0)
#define CTRL_AUTO_FAN           (1 << 1)
#define CTRL_AUTO_THROTTLE      (1 << 2)

struct VGPUThermalState {
    PCIDevice parent_obj;

    MemoryRegion mmio;

    double ambient_temp;        /* °C */
    double thermal_resistance;  /* °C/W (Rth) */
    double thermal_capacitance; /* J/°C (Cth) */
    double max_power;           /* W */
    double min_power;           /* W */

    struct {
        uint8_t pwm_min;
        uint8_t pwm_max;
        uint8_t temp_target;
        uint8_t temp_max;
    } fan_curve;

    uint8_t trip_fan_boost;
    uint8_t trip_downclock;
    uint8_t trip_critical;
    uint8_t trip_shutdown;

    double current_temp;
    double current_power;
    uint8_t fan_pwm;
    uint32_t fan_rpm;
    uint8_t pstate;
    uint32_t status;
    uint32_t ctrl;

    QEMUTimer *thermal_timer;
    bool timer_active;
};

static uint64_t vgpu_thermal_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    VGPUThermalState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case REG_TEMP_CURRENT:
        val = (uint32_t)(s->current_temp * 100);
        break;
    case REG_FAN_PWM:
        val = s->fan_pwm;
        break;
    case REG_FAN_RPM:
        val = s->fan_rpm;
        break;
    case REG_PSTATE:
        val = s->pstate;
        break;
    case REG_TRIP_FAN_BOOST:
        val = s->trip_fan_boost;
        break;
    case REG_TRIP_DOWNCLK:
        val = s->trip_downclock;
        break;
    case REG_TRIP_CRITICAL:
        val = s->trip_critical;
        break;
    case REG_TRIP_SHUTDOWN:
        val = s->trip_shutdown;
        break;
    case REG_THERMAL_RTH:
        val = (uint32_t)(s->thermal_resistance * 65536.0);
        break;
    case REG_THERMAL_CTH:
        val = (uint32_t)(s->thermal_capacitance * 65536.0);
        break;
    case REG_AMBIENT_TEMP:
        val = (uint32_t)(s->ambient_temp * 100);
        break;
    case REG_MAX_POWER:
        val = (uint32_t)(s->max_power * 100);
        break;
    case REG_MIN_POWER:
        val = (uint32_t)(s->min_power * 100);
        break;
    case REG_FAN_PWM_MIN:
        val = s->fan_curve.pwm_min;
        break;
    case REG_FAN_PWM_MAX:
        val = s->fan_curve.pwm_max;
        break;
    case REG_FAN_TEMP_TARGET:
        val = s->fan_curve.temp_target;
        break;
    case REG_FAN_TEMP_MAX:
        val = s->fan_curve.temp_max;
        break;
    case REG_STATUS:
        val = s->status;
        break;
    case REG_CTRL:
        val = s->ctrl;
        break;
    default:
        trace_vgpu_thermal_mmio_read_unknown(addr);
        break;
    }

    return val;
}

static void vgpu_thermal_mmio_write(void *opaque, hwaddr addr, uint64_t val64, unsigned size)
{
    VGPUThermalState *s = opaque;
    uint32_t val = (uint32_t)val64;

    switch (addr) {
    case REG_FAN_PWM:
        if (s->ctrl & CTRL_AUTO_FAN) {
            break; /* Ignore manual writes in auto mode */
        }
        s->fan_pwm = MIN(val, 100);
        s->fan_rpm = 1000 + (s->fan_pwm * 40);
        break;
    case REG_PSTATE:
        s->pstate = (uint8_t)val;
        break;
    case REG_TRIP_FAN_BOOST:
        s->trip_fan_boost = (uint8_t)MIN(val, 255);
        break;
    case REG_TRIP_DOWNCLK:
        s->trip_downclock = (uint8_t)MIN(val, 255);
        break;
    case REG_TRIP_CRITICAL:
        s->trip_critical = (uint8_t)MIN(val, 255);
        break;
    case REG_TRIP_SHUTDOWN:
        s->trip_shutdown = (uint8_t)MIN(val, 255);
        break;
    case REG_THERMAL_RTH:
        s->thermal_resistance = val / 65536.0;
        break;
    case REG_THERMAL_CTH:
        s->thermal_capacitance = val / 65536.0;
        break;
    case REG_AMBIENT_TEMP:
        s->ambient_temp = val / 100.0;
        break;
    case REG_MAX_POWER:
        s->max_power = val / 100.0;
        break;
    case REG_MIN_POWER:
        s->min_power = val / 100.0;
        break;
    case REG_FAN_PWM_MIN:
        s->fan_curve.pwm_min = (uint8_t)MIN(val, 100);
        break;
    case REG_FAN_PWM_MAX:
        s->fan_curve.pwm_max = (uint8_t)MIN(val, 100);
        break;
    case REG_FAN_TEMP_TARGET:
        s->fan_curve.temp_target = (uint8_t)MIN(val, 255);
        break;
    case REG_FAN_TEMP_MAX:
        s->fan_curve.temp_max = (uint8_t)MIN(val, 255);
        break;
    case REG_CTRL:
        s->ctrl = val;
        if (val & CTRL_ENABLE) {
            if (!s->timer_active) {
                timer_mod(s->thermal_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
                s->timer_active = true;
            }
        } else {
            if (s->timer_active) {
                timer_del(s->thermal_timer);
                s->timer_active = false;
            }
        }
        break;
    default:
        trace_vgpu_thermal_mmio_write_unknown(addr, val);
        break;
    }
}

static const MemoryRegionOps vgpu_thermal_mmio_ops = {
    .read = vgpu_thermal_mmio_read,
    .write = vgpu_thermal_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void vgpu_thermal_update_fan(VGPUThermalState *s)
{
    if (!(s->ctrl & CTRL_AUTO_FAN)) {
        return;
    }

    if (s->current_temp > s->fan_curve.temp_target) {
        double range = s->fan_curve.temp_max - s->fan_curve.temp_target;
        if (range > 0) {
            double ratio = (s->current_temp - s->fan_curve.temp_target) / range;
            ratio = MIN(ratio, 1.0);
            s->fan_pwm = s->fan_curve.pwm_min +
                (uint8_t)((s->fan_curve.pwm_max - s->fan_curve.pwm_min) * ratio);
        } else {
            s->fan_pwm = s->fan_curve.pwm_max;
        }
    } else {
        s->fan_pwm = s->fan_curve.pwm_min;
    }
    s->fan_rpm = 1000 + (s->fan_pwm * 40);
}

static void vgpu_thermal_check_trips(VGPUThermalState *s)
{
    s->status &= ~(STATUS_THROTTLING | STATUS_FAN_BOOST | STATUS_CRITICAL | STATUS_SHUTDOWN_PENDING);

    if (s->current_temp >= s->trip_shutdown) {
        s->status |= STATUS_SHUTDOWN_PENDING;
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_ERROR);
        return;
    }

    if (s->current_temp >= s->trip_critical) {
        s->status |= STATUS_CRITICAL;
        if (s->ctrl & CTRL_AUTO_THROTTLE) {
            s->pstate = 0x07; /* Force powersave */
            s->status |= STATUS_THROTTLING;
        }
        return;
    }

    if (s->current_temp >= s->trip_downclock) {
        s->status |= STATUS_THROTTLING;
        if (s->ctrl & CTRL_AUTO_THROTTLE && s->pstate > 0x07) {
            s->pstate--;
        }
        return;
    }

    if (s->current_temp >= s->trip_fan_boost) {
        s->status |= STATUS_FAN_BOOST;
        s->fan_pwm = 100;
        s->fan_rpm = 5000;
    }
}

static void vgpu_thermal_tick(void *opaque)
{
    VGPUThermalState *s = opaque;
    const double dt = 0.1; /* 100ms */

    /* Power based on pstate (linear interpolation between min/max) */
    double pstate_ratio = (s->pstate > 0x07) ? (s->pstate - 0x07) / (double)(0xFF - 0x07) : 0.0;
    pstate_ratio = CLAMP(pstate_ratio, 0.0, 1.0);
    s->current_power = s->min_power + (s->max_power - s->min_power) * pstate_ratio;

    /* Thermal equation: dT/dt = (P - (T - T_amb)/Rth) / Cth */
    double heat_flow = (s->current_temp - s->ambient_temp) / s->thermal_resistance;
    double net_power = s->current_power - heat_flow;
    s->current_temp += (net_power / s->thermal_capacitance) * dt;

    /* Clamp to reasonable bounds */
    s->current_temp = MAX(s->current_temp, s->ambient_temp);
    s->current_temp = MIN(s->current_temp, 200.0);

    vgpu_thermal_update_fan(s);
    vgpu_thermal_check_trips(s);

    /* Reschedule timer */
    if (s->timer_active) {
        timer_mod(s->thermal_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
    }
}

static void vgpu_thermal_reset(DeviceState *dev)
{
    VGPUThermalState *s = VGPU_THERMAL(dev);

    s->current_temp = s->ambient_temp;
    s->fan_pwm = s->fan_curve.pwm_min;
    s->fan_rpm = 1000 + (s->fan_pwm * 40);
    s->pstate = 0x07;
    s->status = 0;
    s->ctrl = CTRL_ENABLE | CTRL_AUTO_FAN | CTRL_AUTO_THROTTLE;

    if (!s->timer_active) {
        timer_mod(s->thermal_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
        s->timer_active = true;
    }
}

static const VMStateDescription vmstate_vgpu_thermal = {
    .name = "vgpu-thermal",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(fan_curve.pwm_min, VGPUThermalState),
        VMSTATE_UINT8(fan_curve.pwm_max, VGPUThermalState),
        VMSTATE_UINT8(fan_curve.temp_target, VGPUThermalState),
        VMSTATE_UINT8(fan_curve.temp_max, VGPUThermalState),
        VMSTATE_UINT8(trip_fan_boost, VGPUThermalState),
        VMSTATE_UINT8(trip_downclock, VGPUThermalState),
        VMSTATE_UINT8(trip_critical, VGPUThermalState),
        VMSTATE_UINT8(trip_shutdown, VGPUThermalState),
        VMSTATE_UINT8(fan_pwm, VGPUThermalState),
        VMSTATE_UINT32(fan_rpm, VGPUThermalState),
        VMSTATE_UINT8(pstate, VGPUThermalState),
        VMSTATE_UINT32(status, VGPUThermalState),
        VMSTATE_UINT32(ctrl, VGPUThermalState),
        VMSTATE_END_OF_LIST()
    }
};

static void vgpu_thermal_realize(PCIDevice *pci_dev, Error **errp)
{
    VGPUThermalState *s = VGPU_THERMAL(pci_dev);

    /* Defaults for a typical desktop GPU (e.g., GTX 780 ~250W) */
    s->ambient_temp = 25.0;
    s->thermal_resistance = 0.5;    /* °C/W */
    s->thermal_capacitance = 10.0;  /* J/°C */
    s->max_power = 250.0;           /* W */
    s->min_power = 30.0;            /* W */

    s->fan_curve.pwm_min = 20;
    s->fan_curve.pwm_max = 100;
    s->fan_curve.temp_target = 70;
    s->fan_curve.temp_max = 90;

    s->trip_fan_boost = 85;
    s->trip_downclock = 90;
    s->trip_critical = 95;
    s->trip_shutdown = 100;

    s->current_temp = s->ambient_temp;
    s->fan_pwm = s->fan_curve.pwm_min;
    s->fan_rpm = 1000 + (s->fan_pwm * 40);
    s->pstate = 0x07;
    s->ctrl = CTRL_ENABLE | CTRL_AUTO_FAN | CTRL_AUTO_THROTTLE;

    s->thermal_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, vgpu_thermal_tick, s);
    s->timer_active = false;

    memory_region_init_io(&s->mmio, OBJECT(s), &vgpu_thermal_mmio_ops, s,
                          "vgpu-thermal", VGPU_THERMAL_MMIO_SIZE);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void vgpu_thermal_exit(PCIDevice *pci_dev)
{
    VGPUThermalState *s = VGPU_THERMAL(pci_dev);

    if (s->timer_active) {
        timer_del(s->thermal_timer);
        s->timer_active = false;
    }
    timer_free(s->thermal_timer);
}

static void vgpu_thermal_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = vgpu_thermal_realize;
    k->exit = vgpu_thermal_exit;
    k->vendor_id = 0x10DE; /* NVIDIA */
    k->device_id = 0x13B2; /* Virtual thermal */
    k->revision = 0x01;
    k->class_id = PCI_CLASS_OTHERS;

    device_class_set_legacy_reset(dc, vgpu_thermal_reset);
    dc->vmsd = &vmstate_vgpu_thermal;
    dc->desc = "Virtual GPU Thermal Simulation Device";
}

static const TypeInfo vgpu_thermal_info = {
    .name = TYPE_VGPU_THERMAL,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VGPUThermalState),
    .class_init = vgpu_thermal_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void vgpu_thermal_register_types(void)
{
    type_register_static(&vgpu_thermal_info);
}

type_init(vgpu_thermal_register_types)