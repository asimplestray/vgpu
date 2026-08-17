/*
 * NVIDIA GM107 (GeForce GTX 750 Ti) — PCI Device Emulation
 *
 * Implements the NVIDIA GM107 Maxwell GPU as a QEMU PCI device.
 * Designed for Nouveau driver development targeting the Maxwell
 * microarchitecture.
 *
 * PCI IDs: Vendor 0x10de (NVIDIA), Device 0x1380 (GTX 750 Ti GM107)
 *
 * Key Maxwell differences from Kepler (GK208):
 *  - New BOOT_0 value:  0x11700000
 *  - Unified memory architecture; no separate PFFB partition registers
 *  - PFIFO: new Kepler-style runlist engine (same base offsets as GK208)
 *  - PGRAPH: Maxwell shader ISA (SM 5.0)
 *  - PMU: revised power management registers
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
#define TYPE_GM107  "nvidia-gtx750ti"
OBJECT_DECLARE_SIMPLE_TYPE(GM107State, GM107)

#define GM107_DEVICE_ID  0x1380
#define GM107_REVISION   0xa2

/* -------------------------------------------------------------------------
 * GM107-specific register name table
 * ---------------------------------------------------------------------- */
static const VGPURegEntry gm107_reg_table[] = {
    /* PMC */
    NV_REG(NV_PMC_BOOT_0,          "PMC_BOOT_0"),
    NV_REG(NV_PMC_BOOT_1,          "PMC_BOOT_1"),
    NV_REG(NV_PMC_INTR_0,          "PMC_INTR_0"),
    NV_REG(NV_PMC_INTR_EN_0,       "PMC_INTR_EN_0"),
    NV_REG(NV_PMC_ENABLE,          "PMC_ENABLE"),

    /* PTIMER */
    NV_REG(NV_PTIMER_TIME_0,        "PTIMER_TIME_0"),
    NV_REG(NV_PTIMER_TIME_1,        "PTIMER_TIME_1"),
    NV_REG(NV_PTIMER_INTR_0,        "PTIMER_INTR_0"),
    NV_REG(NV_PTIMER_INTR_EN_0,     "PTIMER_INTR_EN_0"),

    /* PBUS */
    NV_REG(NV_PDEV_INFO,            "PDEV_INFO"),
    NV_REG(NV_PBUS_INTR_0,         "PBUS_INTR_0"),

    /* PFIFO (same Kepler-style offsets on Maxwell) */
    NV_REG(NV_GK208_PFIFO_INTR_0,  "PFIFO_INTR_0"),
    NV_REG(NV_GK208_PFIFO_INTR_EN, "PFIFO_INTR_EN_0"),
    NV_REG(NV_GK208_PFIFO_RAMFC,   "PFIFO_RAMFC"),
    NV_REG(NV_GK208_PFIFO_CHAN,    "PFIFO_CHAN"),

    /* PMU */
    NV_REG(NV_PMU_INTR_0,          "PMU_INTR_0"),
    NV_REG(NV_PMU_INTR_EN_0,       "PMU_INTR_EN_0"),

    /* PFUSE */
    NV_REG(NV_PFUSE_STATUS_OPTIONB, "PFUSE_STATUS_OPTIONB"),

    /* PGRAPH */
    NV_REG(NV_PGRAPH_CTXCTL,       "PGRAPH_CTXCTL"),
    NV_REG(NV_PGRAPH_INTR,         "PGRAPH_INTR"),
    NV_REG(NV_PGRAPH_STATUS,       "PGRAPH_STATUS"),

    /* PDISPLAY */
    NV_REG(NV_PDISPLAY_BEGIN,      "PDISPLAY_BEGIN"),

    VGPU_REG_TABLE_END,
};

/* -------------------------------------------------------------------------
 * GM107 state
 * ---------------------------------------------------------------------- */
struct GM107State {
    VGPUState state;   /* MUST be first */
};

/* -------------------------------------------------------------------------
 * Register read override
 * ---------------------------------------------------------------------- */
static uint64_t gm107_reg_read_override(void *opaque, hwaddr addr,
                                        unsigned size)
{
    if ((addr & ~3) == NV_PMC_BOOT_0) {
        return NV_GM107_BOOT_0;
    }
    return UINT64_MAX;
}

/* -------------------------------------------------------------------------
 * Chip reset — re-seed BOOT_0 after generic memset
 * ---------------------------------------------------------------------- */
static void gm107_chip_reset(void *opaque)
{
    VGPUState *s = opaque;
    if (s->mmio_data && s->mmio_nr > 0) {
        s->mmio_data[NV_PMC_BOOT_0 >> 2] = NV_GM107_BOOT_0;
    }
}

/* -------------------------------------------------------------------------
 * VGPUOps for GM107
 * ---------------------------------------------------------------------- */
static const VGPUOps gm107_ops = {
    .chip_name          = "GM107",
    .vendor_id          = NV_VENDOR_ID,
    .device_id          = GM107_DEVICE_ID,
    .revision           = GM107_REVISION,
    .bar0_size          = NV_BAR0_SIZE,
    .bar1_min_size      = NV_BAR1_MIN_SIZE,
    .reg_table          = gm107_reg_table,
    .reg_read_override  = gm107_reg_read_override,
    .reg_write_override = NULL,
    .chip_realize       = NULL,
    .chip_reset         = gm107_chip_reset,
    .chip_exit          = NULL,
};

/* -------------------------------------------------------------------------
 * QEMU device lifecycle
 * ---------------------------------------------------------------------- */
static void gm107_realize(PCIDevice *pdev, Error **errp)
{
    GM107State *g = GM107(pdev);
    g->state.ops = &gm107_ops;
    nv_common_realize(pdev, NV_GM107_BOOT_0, errp);
}

static void gm107_exit(PCIDevice *pdev)
{
    vgpu_common_exit(pdev);
}

static void gm107_reset(DeviceState *dev)
{
    vgpu_common_reset(dev);
}

/* -------------------------------------------------------------------------
 * VMState
 * ---------------------------------------------------------------------- */
static const VMStateDescription vmstate_gm107 = {
    .name = TYPE_GM107,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(state.pdev, GM107State),
        VMSTATE_END_OF_LIST()
    },
};

/* -------------------------------------------------------------------------
 * Properties
 * ---------------------------------------------------------------------- */
static const Property gm107_properties[] = {
    VGPU_COMMON_PROPS(GM107State, state),
};

/* -------------------------------------------------------------------------
 * Class and type registration
 * ---------------------------------------------------------------------- */
static void gm107_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass    *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k  = PCI_DEVICE_CLASS(klass);

    k->realize   = gm107_realize;
    k->exit      = gm107_exit;
    k->vendor_id = NV_VENDOR_ID;
    k->device_id = GM107_DEVICE_ID;
    k->revision  = GM107_REVISION;
    k->class_id  = PCI_CLASS_DISPLAY_VGA;

    dc->desc  = "NVIDIA GeForce GTX 750 Ti (GM107 Maxwell)";
    dc->vmsd  = &vmstate_gm107;
    device_class_set_props_n(dc, gm107_properties, ARRAY_SIZE(gm107_properties));
    device_class_set_legacy_reset(dc, gm107_reset);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static void gm107_instance_init(Object *obj)
{
    PCI_DEVICE(obj)->cap_present |= QEMU_PCI_CAP_EXPRESS;
}

static const TypeInfo gm107_info = {
    .name          = TYPE_GM107,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(GM107State),
    .instance_init = gm107_instance_init,
    .class_init    = gm107_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void gm107_register_types(void)
{
    type_register_static(&gm107_info);
}

type_init(gm107_register_types)
