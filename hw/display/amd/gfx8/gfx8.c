/*
 * AMD Polaris (GFX8) — RX 480 PCI Device Emulation
 *
 * Implements the AMD Polaris10 (GFX8 / GCN 4th gen) GPU as a QEMU
 * PCI device.  Targets the amdgpu kernel driver.
 *
 * PCI IDs: Vendor 0x1002 (AMD), Device 0x67DF (RX 480)
 *
 * GCN 4 (Polaris) characteristics:
 *  - 14nm FinFET (first AMD GPU on that node)
 *  - 36 CUs, 2304 stream processors
 *  - 8 GB GDDR5 (256-bit bus)
 *  - PCIe 3.0 x16
 *  - Supports amdgpu and radeon drivers
 *  - GFX IP version: 8.0
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
#define TYPE_GFX8_POLARIS  "amd-rx480"
OBJECT_DECLARE_SIMPLE_TYPE(GFX8State, GFX8_POLARIS)

#define GFX8_DEVICE_ID  0x67DF
#define GFX8_REVISION   0xC7   /* Polaris10 XTX stepping */

/* -------------------------------------------------------------------------
 * Polaris-specific register name table
 * ---------------------------------------------------------------------- */
static const VGPURegEntry gfx8_reg_table[] = {
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
    AMD_REG(AMD_SDMA1_STATUS_REG,     "SDMA1_STATUS_REG"),
    AMD_REG(AMD_DCE_CRTC0_CONTROL,    "DCE_CRTC0_CONTROL"),
    AMD_REG(AMD_DCE_CRTC0_H_TOTAL,    "DCE_CRTC0_H_TOTAL"),
    AMD_REG(AMD_DCE_CRTC0_V_TOTAL,    "DCE_CRTC0_V_TOTAL"),
    AMD_REG(AMD_DCE_CRTC0_OFFSET,     "DCE_CRTC0_OFFSET"),
    AMD_REG(AMD_DCE_CRTC0_PITCH,      "DCE_CRTC0_PITCH"),
    VGPU_REG_TABLE_END,
};

/* -------------------------------------------------------------------------
 * GFX8 state
 * ---------------------------------------------------------------------- */
struct GFX8State {
    VGPUState state;   /* MUST be first */
};

/* -------------------------------------------------------------------------
 * Chip reset
 * ---------------------------------------------------------------------- */
static void gfx8_chip_reset(void *opaque)
{
    VGPUState *s = opaque;
    /* Re-assert idle GRBM status after generic memset */
    if (s->mmio_data && s->mmio_nr > (AMD_GFX_GRBM_STATUS >> 2)) {
        s->mmio_data[AMD_GFX_GRBM_STATUS  >> 2] = 0x00000000;
        s->mmio_data[AMD_GFX_GRBM_STATUS2 >> 2] = 0x00000000;
        s->mmio_data[AMD_SMC_RESP         >> 2] = 0x00000001;
    }
}

/* -------------------------------------------------------------------------
 * VGPUOps for GFX8/Polaris
 * ---------------------------------------------------------------------- */
static const VGPUOps gfx8_ops = {
    .chip_name          = "Polaris10",
    .vendor_id          = AMD_VENDOR_ID,
    .device_id          = GFX8_DEVICE_ID,
    .revision           = GFX8_REVISION,
    .bar0_size          = AMD_BAR0_SIZE,
    .bar1_min_size      = AMD_BAR1_MIN_SIZE,
    .reg_table          = gfx8_reg_table,
    .reg_read_override  = NULL,
    .reg_write_override = NULL,
    .chip_realize       = NULL,
    .chip_reset         = gfx8_chip_reset,
    .chip_exit          = NULL,
};

/* -------------------------------------------------------------------------
 * QEMU device lifecycle
 * ---------------------------------------------------------------------- */
static void gfx8_realize(PCIDevice *pdev, Error **errp)
{
    GFX8State *g = GFX8_POLARIS(pdev);
    g->state.ops = &gfx8_ops;
    amd_common_realize(pdev, errp);
}

static void gfx8_exit(PCIDevice *pdev)
{
    vgpu_common_exit(pdev);
}

static void gfx8_reset(DeviceState *dev)
{
    vgpu_common_reset(dev);
}

/* -------------------------------------------------------------------------
 * VMState
 * ---------------------------------------------------------------------- */
static const VMStateDescription vmstate_gfx8 = {
    .name = TYPE_GFX8_POLARIS,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(state.pdev, GFX8State),
        VMSTATE_END_OF_LIST()
    },
};

/* -------------------------------------------------------------------------
 * Properties
 * ---------------------------------------------------------------------- */
static const Property gfx8_properties[] = {
    VGPU_COMMON_PROPS(GFX8State, state),
};

/* -------------------------------------------------------------------------
 * Class and type registration
 * ---------------------------------------------------------------------- */
static void gfx8_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass    *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k  = PCI_DEVICE_CLASS(klass);

    k->realize   = gfx8_realize;
    k->exit      = gfx8_exit;
    k->vendor_id = AMD_VENDOR_ID;
    k->device_id = GFX8_DEVICE_ID;
    k->revision  = GFX8_REVISION;
    k->class_id  = PCI_CLASS_DISPLAY_VGA;

    dc->desc  = "AMD Radeon RX 480 (Polaris10 GCN4)";
    dc->vmsd  = &vmstate_gfx8;
    device_class_set_props_n(dc, gfx8_properties, ARRAY_SIZE(gfx8_properties));
    device_class_set_legacy_reset(dc, gfx8_reset);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static void gfx8_instance_init(Object *obj)
{
    PCI_DEVICE(obj)->cap_present |= QEMU_PCI_CAP_EXPRESS;
}

static const TypeInfo gfx8_info = {
    .name          = TYPE_GFX8_POLARIS,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(GFX8State),
    .instance_init = gfx8_instance_init,
    .class_init    = gfx8_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void gfx8_register_types(void)
{
    type_register_static(&gfx8_info);
}

type_init(gfx8_register_types)
