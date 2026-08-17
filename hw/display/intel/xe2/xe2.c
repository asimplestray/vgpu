/*
 * Intel Arc A770 (Xe-HPG ACM-G10) — PCI Device Emulation
 *
 * Implements the Intel Arc A770 Alchemist GPU as a QEMU PCI device.
 * Targets the xe (or i915-with-Xe-extension) kernel driver.
 *
 * PCI IDs: Vendor 0x8086 (Intel), Device 0x56A0 (Arc A770 ACM-G10)
 *
 * Xe-HPG (Alchemist) characteristics:
 *  - 32 Xe-Cores (512 EU)
 *  - 16 GB GDDR6 (256-bit)
 *  - PCIe 4.0 x16
 *  - Hardware ray tracing
 *  - XMX AI acceleration
 *  - Targets both i915 (legacy) and the new xe driver
 *  - GuC / HuC firmware for command submission and video decode
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
#include "../../intel/intel_core.h"

/* -------------------------------------------------------------------------
 * Device type
 * ---------------------------------------------------------------------- */
#define TYPE_XE2_ARC_A770  "intel-arc-a770"
OBJECT_DECLARE_SIMPLE_TYPE(Xe2State, XE2_ARC_A770)

#define XE2_DEVICE_ID  0x56A0
#define XE2_REVISION   0x08

/* -------------------------------------------------------------------------
 * Arc A770-specific register name table
 * ---------------------------------------------------------------------- */
static const VGPURegEntry xe2_reg_table[] = {
    INTEL_REG(INTEL_PIPEACONF,            "PIPEACONF"),
    INTEL_REG(INTEL_GEN_RPNSWREQ,         "GT_RPNSWREQ"),
    INTEL_REG(INTEL_GEN_RC6_CONTROL,      "GT_RC6_CONTROL"),
    INTEL_REG(INTEL_GEN_RC6_STATE,        "GT_RC6_STATE"),
    INTEL_REG(INTEL_GEN_MASTER_IRQ,       "GEN_MASTER_IRQ"),
    INTEL_REG(INTEL_GEN_GT_IRQ_ISR,       "GT_IRQ_ISR"),
    INTEL_REG(INTEL_GEN_GT_IRQ_IIR,       "GT_IRQ_IIR"),
    INTEL_REG(INTEL_GEN_GT_IRQ_IER,       "GT_IRQ_IER"),
    INTEL_REG(INTEL_GEN_GT_IRQ_IMR,       "GT_IRQ_IMR"),
    INTEL_REG(INTEL_FORCEWAKE_MT,         "FORCEWAKE_MT"),
    INTEL_REG(INTEL_FORCEWAKE_ACK_MT,     "FORCEWAKE_ACK_MT"),
    INTEL_REG(INTEL_XE_GUC_WOPCM_OFFSET,  "XE_GUC_WOPCM_OFFSET"),
    INTEL_REG(INTEL_XE_HUC_STATUS,        "XE_HUC_STATUS"),
    VGPU_REG_TABLE_END,
};

/* -------------------------------------------------------------------------
 * Xe2 state
 * ---------------------------------------------------------------------- */
struct Xe2State {
    VGPUState state;   /* MUST be first */
};

/* -------------------------------------------------------------------------
 * Register read override
 *
 * The xe driver probes several registers to determine the hardware
 * generation.  We override the most critical ones here.
 * ---------------------------------------------------------------------- */
static uint64_t xe2_reg_read_override(void *opaque, hwaddr addr,
                                      unsigned size)
{
    switch (addr & ~3) {
    /*
     * FORCEWAKE_ACK_MT: always report GT as awake to avoid driver spin.
     */
    case INTEL_FORCEWAKE_ACK_MT:
        return 0x00000001;
    /*
     * XE_HUC_STATUS: bit 0 = HuC authenticated.
     * Return 0 (not authenticated) — correct for a stub without firmware.
     */
    case INTEL_XE_HUC_STATUS:
        return 0x00000000;
    default:
        break;
    }
    return UINT64_MAX;
}

/* -------------------------------------------------------------------------
 * Chip reset
 * ---------------------------------------------------------------------- */
static void xe2_chip_reset(void *opaque)
{
    VGPUState *s = opaque;
    unsigned int fw_ack_idx = INTEL_FORCEWAKE_ACK_MT >> 2;
    if (s->mmio_data && fw_ack_idx < s->mmio_nr) {
        s->mmio_data[fw_ack_idx] = 0x00000001;
    }
}

/* -------------------------------------------------------------------------
 * VGPUOps for Arc A770
 * ---------------------------------------------------------------------- */
static const VGPUOps xe2_ops = {
    .chip_name          = "ACM-G10",
    .vendor_id          = INTEL_VENDOR_ID,
    .device_id          = XE2_DEVICE_ID,
    .revision           = XE2_REVISION,
    .bar0_size          = INTEL_BAR0_SIZE,
    .bar1_min_size      = INTEL_BAR1_MIN_SIZE,
    .reg_table          = xe2_reg_table,
    .reg_read_override  = xe2_reg_read_override,
    .reg_write_override = NULL,
    .chip_realize       = NULL,
    .chip_reset         = xe2_chip_reset,
    .chip_exit          = NULL,
};

/* -------------------------------------------------------------------------
 * QEMU device lifecycle
 * ---------------------------------------------------------------------- */
static void xe2_realize(PCIDevice *pdev, Error **errp)
{
    Xe2State *g = XE2_ARC_A770(pdev);
    g->state.ops = &xe2_ops;
    intel_common_realize(pdev, errp);
}

static void xe2_exit(PCIDevice *pdev)
{
    vgpu_common_exit(pdev);
}

static void xe2_reset(DeviceState *dev)
{
    vgpu_common_reset(dev);
}

/* -------------------------------------------------------------------------
 * VMState
 * ---------------------------------------------------------------------- */
static const VMStateDescription vmstate_xe2 = {
    .name = TYPE_XE2_ARC_A770,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(state.pdev, Xe2State),
        VMSTATE_END_OF_LIST()
    },
};

/* -------------------------------------------------------------------------
 * Properties
 * ---------------------------------------------------------------------- */
static const Property xe2_properties[] = {
    VGPU_COMMON_PROPS(Xe2State, state),
};

/* -------------------------------------------------------------------------
 * Class and type registration
 * ---------------------------------------------------------------------- */
static void xe2_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass    *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k  = PCI_DEVICE_CLASS(klass);

    k->realize   = xe2_realize;
    k->exit      = xe2_exit;
    k->vendor_id = INTEL_VENDOR_ID;
    k->device_id = XE2_DEVICE_ID;
    k->revision  = XE2_REVISION;
    k->class_id  = PCI_CLASS_DISPLAY_VGA;

    dc->desc  = "Intel Arc A770 (ACM-G10 Xe-HPG Alchemist)";
    dc->vmsd  = &vmstate_xe2;
    device_class_set_props_n(dc, xe2_properties, ARRAY_SIZE(xe2_properties));
    device_class_set_legacy_reset(dc, xe2_reset);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static void xe2_instance_init(Object *obj)
{
    PCI_DEVICE(obj)->cap_present |= QEMU_PCI_CAP_EXPRESS;
}

static const TypeInfo xe2_info = {
    .name          = TYPE_XE2_ARC_A770,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(Xe2State),
    .instance_init = xe2_instance_init,
    .class_init    = xe2_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void xe2_register_types(void)
{
    type_register_static(&xe2_info);
}

type_init(xe2_register_types)
