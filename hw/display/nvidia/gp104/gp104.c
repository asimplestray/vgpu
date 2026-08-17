/*
 * NVIDIA GP104 (GeForce GTX 1080) — PCI Device Emulation
 *
 * Implements the NVIDIA GP104 Pascal GPU as a QEMU PCI device.
 * Targets both Nouveau and the nvidia-open kernel module.
 *
 * PCI IDs: Vendor 0x10de (NVIDIA), Device 0x1b80 (GTX 1080 GP104)
 *
 * Key Pascal differences from Maxwell:
 *  - BOOT_0: 0x1B040000
 *  - NVLink support registers (not emulated here yet)
 *  - Unified Virtual Memory (UVM) — Pascal introduced HW page faulting
 *  - PFIFO: MIG-style channel grouping begins appearing
 *  - New PGRAPH sm60 ISA
 *  - NVDEC / NVENC video engine registers
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
#define TYPE_GP104  "nvidia-gtx1080"
OBJECT_DECLARE_SIMPLE_TYPE(GP104State, GP104)

#define GP104_DEVICE_ID  0x1b80
#define GP104_REVISION   0xa1

/* -------------------------------------------------------------------------
 * GP104-specific register name table
 * ---------------------------------------------------------------------- */
static const VGPURegEntry gp104_reg_table[] = {
    /* PMC */
    NV_REG(NV_PMC_BOOT_0,           "PMC_BOOT_0"),
    NV_REG(NV_PMC_BOOT_1,           "PMC_BOOT_1"),
    NV_REG(NV_PMC_INTR_0,           "PMC_INTR_0"),
    NV_REG(NV_PMC_INTR_EN_0,        "PMC_INTR_EN_0"),
    NV_REG(NV_PMC_ENABLE,           "PMC_ENABLE"),

    /* PTIMER */
    NV_REG(NV_PTIMER_TIME_0,         "PTIMER_TIME_0"),
    NV_REG(NV_PTIMER_TIME_1,         "PTIMER_TIME_1"),
    NV_REG(NV_PTIMER_INTR_0,         "PTIMER_INTR_0"),
    NV_REG(NV_PTIMER_INTR_EN_0,      "PTIMER_INTR_EN_0"),

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

    /* Pascal-specific: NVDEC / NVENC (placeholder offsets) */
    NV_REG(0x084000,                 "NVDEC_BASE"),
    NV_REG(0x082000,                 "NVENC_BASE"),

    /* PDISPLAY */
    NV_REG(NV_PDISPLAY_BEGIN,       "PDISPLAY_BEGIN"),

    VGPU_REG_TABLE_END,
};

/* -------------------------------------------------------------------------
 * GP104 state
 * ---------------------------------------------------------------------- */
struct GP104State {
    VGPUState state;   /* MUST be first */
};

/* -------------------------------------------------------------------------
 * Register read override
 * ---------------------------------------------------------------------- */
static uint64_t gp104_reg_read_override(void *opaque, hwaddr addr,
                                        unsigned size)
{
    if ((addr & ~3) == NV_PMC_BOOT_0) {
        return NV_GP104_BOOT_0;
    }
    return UINT64_MAX;
}

/* -------------------------------------------------------------------------
 * Chip reset
 * ---------------------------------------------------------------------- */
static void gp104_chip_reset(void *opaque)
{
    VGPUState *s = opaque;
    if (s->mmio_data && s->mmio_nr > 0) {
        s->mmio_data[NV_PMC_BOOT_0 >> 2] = NV_GP104_BOOT_0;
    }
}

/* -------------------------------------------------------------------------
 * VGPUOps for GP104
 * ---------------------------------------------------------------------- */
static const VGPUOps gp104_ops = {
    .chip_name          = "GP104",
    .vendor_id          = NV_VENDOR_ID,
    .device_id          = GP104_DEVICE_ID,
    .revision           = GP104_REVISION,
    .bar0_size          = NV_BAR0_SIZE,
    .bar1_min_size      = NV_BAR1_MIN_SIZE,
    .reg_table          = gp104_reg_table,
    .reg_read_override  = gp104_reg_read_override,
    .reg_write_override = NULL,
    .chip_realize       = NULL,
    .chip_reset         = gp104_chip_reset,
    .chip_exit          = NULL,
};

/* -------------------------------------------------------------------------
 * QEMU device lifecycle
 * ---------------------------------------------------------------------- */
static void gp104_realize(PCIDevice *pdev, Error **errp)
{
    GP104State *g = GP104(pdev);
    g->state.ops = &gp104_ops;
    nv_common_realize(pdev, NV_GP104_BOOT_0, errp);
}

static void gp104_exit(PCIDevice *pdev)
{
    vgpu_common_exit(pdev);
}

static void gp104_reset(DeviceState *dev)
{
    vgpu_common_reset(dev);
}

/* -------------------------------------------------------------------------
 * VMState
 * ---------------------------------------------------------------------- */
static const VMStateDescription vmstate_gp104 = {
    .name = TYPE_GP104,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(state.pdev, GP104State),
        VMSTATE_END_OF_LIST()
    },
};

/* -------------------------------------------------------------------------
 * Properties
 * ---------------------------------------------------------------------- */
static const Property gp104_properties[] = {
    VGPU_COMMON_PROPS(GP104State, state),
};

/* -------------------------------------------------------------------------
 * Class and type registration
 * ---------------------------------------------------------------------- */
static void gp104_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass    *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k  = PCI_DEVICE_CLASS(klass);

    k->realize   = gp104_realize;
    k->exit      = gp104_exit;
    k->vendor_id = NV_VENDOR_ID;
    k->device_id = GP104_DEVICE_ID;
    k->revision  = GP104_REVISION;
    k->class_id  = PCI_CLASS_DISPLAY_VGA;

    dc->desc  = "NVIDIA GeForce GTX 1080 (GP104 Pascal)";
    dc->vmsd  = &vmstate_gp104;
    device_class_set_props_n(dc, gp104_properties, ARRAY_SIZE(gp104_properties));
    device_class_set_legacy_reset(dc, gp104_reset);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static void gp104_instance_init(Object *obj)
{
    PCI_DEVICE(obj)->cap_present |= QEMU_PCI_CAP_EXPRESS;
}

static const TypeInfo gp104_info = {
    .name          = TYPE_GP104,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(GP104State),
    .instance_init = gp104_instance_init,
    .class_init    = gp104_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void gp104_register_types(void)
{
    type_register_static(&gp104_info);
}

type_init(gp104_register_types)
