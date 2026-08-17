/*
 * NVIDIA TU102 (GeForce RTX 2080) — PCI Device Emulation
 *
 * Implements the NVIDIA TU102 Turing GPU as a QEMU PCI device.
 * Targets the nvidia-open kernel module for Turing-era driver development.
 *
 * PCI IDs: Vendor 0x10de (NVIDIA), Device 0x1e04 (RTX 2080 TU102)
 *
 * Key Turing differences from Pascal:
 *  - BOOT_0: 0x162000A1
 *  - RT Cores (ray tracing hardware) — NVRTCORE registers
 *  - Tensor Cores
 *  - NVLINK 2.0 (multi-GPU)
 *  - MIG (Multi-Instance GPU) precursor registers
 *  - New display engine: NVDISPLAY (replaces older PDISPLAY)
 *  - GSP (GPU System Processor) — nvidia-open uses this heavily
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "migration/vmstate.h"
#include "hw/pci/pci_device.h"
#include "hw/core/qdev-properties.h"
#include "../../nvidia/nv_core.h"

/* -------------------------------------------------------------------------
 * Device type
 * ---------------------------------------------------------------------- */
#define TYPE_TU102  "nvidia-rtx2080"
OBJECT_DECLARE_SIMPLE_TYPE(TU102State, TU102)

#define TU102_DEVICE_ID  0x1e04
#define TU102_REVISION   0xa1

/* -------------------------------------------------------------------------
 * TU102-specific register name table
 * ---------------------------------------------------------------------- */
static const VGPURegEntry tu102_reg_table[] = {
    /* PMC */
    NV_REG(NV_PMC_BOOT_0,           "PMC_BOOT_0"),
    NV_REG(NV_PMC_BOOT_1,           "PMC_BOOT_1"),
    NV_REG(NV_PMC_INTR_0,           "PMC_INTR_0"),
    NV_REG(NV_PMC_INTR_EN_0,        "PMC_INTR_EN_0"),
    NV_REG(NV_PMC_ENABLE,           "PMC_ENABLE"),

    /* PTIMER */
    NV_REG(NV_PTIMER_TIME_0,         "PTIMER_TIME_0"),
    NV_REG(NV_PTIMER_TIME_1,         "PTIMER_TIME_1"),

    /* PBUS */
    NV_REG(NV_PDEV_INFO,             "PDEV_INFO"),
    NV_REG(NV_PBUS_INTR_0,          "PBUS_INTR_0"),

    /* PFIFO */
    NV_REG(NV_GK208_PFIFO_INTR_0,   "PFIFO_INTR_0"),
    NV_REG(NV_GK208_PFIFO_INTR_EN,  "PFIFO_INTR_EN_0"),
    NV_REG(NV_GK208_PFIFO_RAMFC,    "PFIFO_RAMFC"),
    NV_REG(NV_GK208_PFIFO_CHAN,     "PFIFO_CHAN"),

    /* PMU */
    NV_REG(NV_PMU_INTR_0,           "PMU_INTR_0"),
    NV_REG(NV_PMU_INTR_EN_0,        "PMU_INTR_EN_0"),

    /* PFUSE */
    NV_REG(NV_PFUSE_STATUS_OPTIONB,  "PFUSE_STATUS_OPTIONB"),

    /* PGRAPH */
    NV_REG(NV_PGRAPH_CTXCTL,        "PGRAPH_CTXCTL"),
    NV_REG(NV_PGRAPH_INTR,          "PGRAPH_INTR"),
    NV_REG(NV_PGRAPH_STATUS,        "PGRAPH_STATUS"),

    /* Turing-specific: GSP (GPU System Processor) */
    NV_REG(0x110000,                 "GSP_BASE"),
    NV_REG(0x110004,                 "GSP_INTR_0"),

    /* Turing-specific: NVRTCORE (RT cores) */
    NV_REG(0x5B4000,                 "NVRTCORE_BASE"),

    /* NVDEC / NVENC */
    NV_REG(0x084000,                 "NVDEC_BASE"),
    NV_REG(0x082000,                 "NVENC_BASE"),

    /* NVDISPLAY (Turing uses new display engine at same base) */
    NV_REG(NV_PDISPLAY_BEGIN,       "NVDISPLAY_BEGIN"),

    VGPU_REG_TABLE_END,
};

/* -------------------------------------------------------------------------
 * TU102 state
 * ---------------------------------------------------------------------- */
struct TU102State {
    VGPUState state;   /* MUST be first */
};

/* -------------------------------------------------------------------------
 * Register read override
 * ---------------------------------------------------------------------- */
static uint64_t tu102_reg_read_override(void *opaque, hwaddr addr,
                                        unsigned size)
{
    switch (addr & ~3) {
    case NV_PMC_BOOT_0:
        return NV_TU102_BOOT_0;
    /*
     * GSP_BASE: return a non-zero value so nvidia-open doesn't spin
     * waiting for the GSP to become ready.  A real stub would need to
     * emulate the GSP boot handshake, but for identification purposes
     * returning 0x1 is sufficient.
     */
    case 0x110000:
        return 0x00000001;
    default:
        break;
    }
    return UINT64_MAX;
}

/* -------------------------------------------------------------------------
 * Chip reset
 * ---------------------------------------------------------------------- */
static void tu102_chip_reset(void *opaque)
{
    VGPUState *s = opaque;
    if (s->mmio_data && s->mmio_nr > 0) {
        s->mmio_data[NV_PMC_BOOT_0 >> 2] = NV_TU102_BOOT_0;
    }
}

/* -------------------------------------------------------------------------
 * VGPUOps for TU102
 * ---------------------------------------------------------------------- */
static const VGPUOps tu102_ops = {
    .chip_name          = "TU102",
    .vendor_id          = NV_VENDOR_ID,
    .device_id          = TU102_DEVICE_ID,
    .revision           = TU102_REVISION,
    .bar0_size          = NV_BAR0_SIZE,
    .bar1_min_size      = NV_BAR1_MIN_SIZE,
    .reg_table          = tu102_reg_table,
    .reg_read_override  = tu102_reg_read_override,
    .reg_write_override = NULL,
    .chip_realize       = NULL,
    .chip_reset         = tu102_chip_reset,
    .chip_exit          = NULL,
};

/* -------------------------------------------------------------------------
 * QEMU device lifecycle
 * ---------------------------------------------------------------------- */
static void tu102_realize(PCIDevice *pdev, Error **errp)
{
    TU102State *g = TU102(pdev);
    g->state.ops = &tu102_ops;
    nv_common_realize(pdev, NV_TU102_BOOT_0, errp);
}

static void tu102_exit(PCIDevice *pdev)
{
    vgpu_common_exit(pdev);
}

static void tu102_reset(DeviceState *dev)
{
    vgpu_common_reset(dev);
}

/* -------------------------------------------------------------------------
 * VMState
 * ---------------------------------------------------------------------- */
static const VMStateDescription vmstate_tu102 = {
    .name = TYPE_TU102,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(state.pdev, TU102State),
        VMSTATE_END_OF_LIST()
    },
};

/* -------------------------------------------------------------------------
 * Properties
 * ---------------------------------------------------------------------- */
static const Property tu102_properties[] = {
    VGPU_COMMON_PROPS(TU102State, state),
};

/* -------------------------------------------------------------------------
 * Class and type registration
 * ---------------------------------------------------------------------- */
static void tu102_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass    *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k  = PCI_DEVICE_CLASS(klass);

    k->realize   = tu102_realize;
    k->exit      = tu102_exit;
    k->vendor_id = NV_VENDOR_ID;
    k->device_id = TU102_DEVICE_ID;
    k->revision  = TU102_REVISION;
    k->class_id  = PCI_CLASS_DISPLAY_VGA;

    dc->desc  = "NVIDIA GeForce RTX 2080 (TU102 Turing)";
    dc->vmsd  = &vmstate_tu102;
    device_class_set_props_n(dc, tu102_properties, ARRAY_SIZE(tu102_properties));
    device_class_set_legacy_reset(dc, tu102_reset);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static void tu102_instance_init(Object *obj)
{
    PCI_DEVICE(obj)->cap_present |= QEMU_PCI_CAP_EXPRESS;
}

static const TypeInfo tu102_info = {
    .name          = TYPE_TU102,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(TU102State),
    .instance_init = tu102_instance_init,
    .class_init    = tu102_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void tu102_register_types(void)
{
    type_register_static(&tu102_info);
}

type_init(tu102_register_types)
