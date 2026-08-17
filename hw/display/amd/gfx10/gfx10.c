/*
 * AMD Navi22 (GFX10.3 / RDNA 2) — RX 6700 XT PCI Device Emulation
 *
 * Implements the AMD Navi22 RDNA 2 GPU as a QEMU PCI device.
 * Targets the amdgpu kernel driver.
 *
 * PCI IDs: Vendor 0x1002 (AMD), Device 0x73DF (RX 6700 XT)
 *
 * RDNA 2 (Navi2x) characteristics vs GCN:
 *  - Completely redesigned shader processor (RDNA ISA)
 *  - Hardware ray tracing units (Ray Accelerators)
 *  - GDDR6 memory
 *  - PCIe 4.0 x16
 *  - New DCN 3.x display engine
 *  - GFX IP version: 10.3
 *  - Infinity Cache (last-level cache on-die)
 *  - Smart Access Memory (AMD's BAR resize / Resizable BAR)
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
#include "../../amd/amd_core.h"

/* -------------------------------------------------------------------------
 * Device type
 * ---------------------------------------------------------------------- */
#define TYPE_GFX10_NAVI22  "amd-rx6700xt"
OBJECT_DECLARE_SIMPLE_TYPE(GFX10State, GFX10_NAVI22)

#define GFX10_DEVICE_ID  0x73DF
#define GFX10_REVISION   0xC0

/* -------------------------------------------------------------------------
 * Navi22-specific register name table
 * ---------------------------------------------------------------------- */
static const VGPURegEntry gfx10_reg_table[] = {
    AMD_REG(AMD_CONFIG_MEMSIZE,        "CONFIG_MEMSIZE"),
    AMD_REG(AMD_GFX_GRBM_STATUS,      "GRBM_STATUS"),
    AMD_REG(AMD_GFX_GRBM_STATUS2,     "GRBM_STATUS2"),
    AMD_REG(AMD_GFX_GRBM_SOFT_RESET,  "GRBM_SOFT_RESET"),
    AMD_REG(AMD_GFX_SRBM_STATUS,      "SRBM_STATUS"),
    AMD_REG(AMD_IH_RB_BASE,           "IH_RB_BASE"),
    AMD_REG(AMD_IH_STATUS,            "IH_STATUS"),
    AMD_REG(AMD_SMC_MSG,              "SMC_MSG"),
    AMD_REG(AMD_SMC_MSG_ARG,          "SMC_MSG_ARG"),
    AMD_REG(AMD_SMC_RESP,             "SMC_RESP"),
    AMD_REG(AMD_HDP_HOST_PATH_CNTL,   "HDP_HOST_PATH_CNTL"),
    AMD_REG(AMD_HDP_FLUSH_INVALIDATE, "HDP_FLUSH_INVALIDATE"),
    AMD_REG(AMD_BIF_FB_EN,            "BIF_FB_EN"),
    AMD_REG(AMD_SDMA0_STATUS_REG,     "SDMA0_STATUS_REG"),
    /* DCN 3.x display engine */
    AMD_REG(AMD_DCN_DCHUBBUB_STATUS,  "DCN_DCHUBBUB_STATUS"),
    VGPU_REG_TABLE_END,
};

/* -------------------------------------------------------------------------
 * GFX10 state
 * ---------------------------------------------------------------------- */
struct GFX10State {
    VGPUState state;   /* MUST be first */
};

/* -------------------------------------------------------------------------
 * Chip reset
 * ---------------------------------------------------------------------- */
static void gfx10_chip_reset(void *opaque)
{
    VGPUState *s = opaque;
    if (s->mmio_data && s->mmio_nr > (AMD_GFX_GRBM_STATUS >> 2)) {
        s->mmio_data[AMD_GFX_GRBM_STATUS  >> 2] = 0x00000000;
        s->mmio_data[AMD_GFX_GRBM_STATUS2 >> 2] = 0x00000000;
        s->mmio_data[AMD_SMC_RESP         >> 2] = 0x00000001;
    }
}

/* -------------------------------------------------------------------------
 * VGPUOps for GFX10/Navi22
 * ---------------------------------------------------------------------- */
static const VGPUOps gfx10_ops = {
    .chip_name          = "Navi22",
    .vendor_id          = AMD_VENDOR_ID,
    .device_id          = GFX10_DEVICE_ID,
    .revision           = GFX10_REVISION,
    .bar0_size          = AMD_BAR0_SIZE,
    .bar1_min_size      = AMD_BAR1_MIN_SIZE,
    .reg_table          = gfx10_reg_table,
    .reg_read_override  = NULL,
    .reg_write_override = NULL,
    .chip_realize       = NULL,
    .chip_reset         = gfx10_chip_reset,
    .chip_exit          = NULL,
};

/* -------------------------------------------------------------------------
 * QEMU device lifecycle
 * ---------------------------------------------------------------------- */
static void gfx10_realize(PCIDevice *pdev, Error **errp)
{
    GFX10State *g = GFX10_NAVI22(pdev);
    g->state.ops = &gfx10_ops;
    amd_common_realize(pdev, errp);
}

static void gfx10_exit(PCIDevice *pdev)
{
    vgpu_common_exit(pdev);
}

static void gfx10_reset(DeviceState *dev)
{
    vgpu_common_reset(dev);
}

/* -------------------------------------------------------------------------
 * VMState
 * ---------------------------------------------------------------------- */
static const VMStateDescription vmstate_gfx10 = {
    .name = TYPE_GFX10_NAVI22,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(state.pdev, GFX10State),
        VMSTATE_END_OF_LIST()
    },
};

/* -------------------------------------------------------------------------
 * Properties
 * ---------------------------------------------------------------------- */
static const Property gfx10_properties[] = {
    VGPU_COMMON_PROPS(GFX10State, state),
};

/* -------------------------------------------------------------------------
 * Class and type registration
 * ---------------------------------------------------------------------- */
static void gfx10_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass    *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k  = PCI_DEVICE_CLASS(klass);

    k->realize   = gfx10_realize;
    k->exit      = gfx10_exit;
    k->vendor_id = AMD_VENDOR_ID;
    k->device_id = GFX10_DEVICE_ID;
    k->revision  = GFX10_REVISION;
    k->class_id  = PCI_CLASS_DISPLAY_VGA;

    dc->desc  = "AMD Radeon RX 6700 XT (Navi22 RDNA2)";
    dc->vmsd  = &vmstate_gfx10;
    device_class_set_props_n(dc, gfx10_properties, ARRAY_SIZE(gfx10_properties));
    device_class_set_legacy_reset(dc, gfx10_reset);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static void gfx10_instance_init(Object *obj)
{
    PCI_DEVICE(obj)->cap_present |= QEMU_PCI_CAP_EXPRESS;
}

static const TypeInfo gfx10_info = {
    .name          = TYPE_GFX10_NAVI22,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(GFX10State),
    .instance_init = gfx10_instance_init,
    .class_init    = gfx10_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void gfx10_register_types(void)
{
    type_register_static(&gfx10_info);
}

type_init(gfx10_register_types)
