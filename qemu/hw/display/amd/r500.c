/*
 * ATI Radeon R500 (RV515) — PCI Device Emulation
 *
 * Implements the ATI Radeon R500 / RV515 GPU as a QEMU PCI device.
 * Targets the custom ApolloOS graphics driver.
 *
 * PCI IDs: Vendor 0x1002 (AMD/ATI), Device 0x7145 (RV515 X1300/X1400)
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
#include "amd_core.h"

#define TYPE_R500 "amd-r500"
OBJECT_DECLARE_SIMPLE_TYPE(R500State, R500)

#define R500_VENDOR_ID          0x1002
#define R500_DEVICE_ID_RV515    0x7145

struct R500State {
    VGPUState state;

    uint32_t pll_index;
    uint32_t pll_regs[64];
};

static const VGPURegEntry r500_reg_table[] = {
    { 0x0000, "MM_INDEX" },
    { 0x0004, "MM_DATA" },
    { 0x000C, "CLOCK_CNTL_INDEX" },
    { 0x0010, "CLOCK_CNTL_DATA" },
    { 0x0030, "BUS_CNTL" },
    { 0x0050, "CRTC_GEN_CNTL" },
    { 0x0054, "CRTC_EXT_CNTL" },
    { 0x0058, "DAC_CNTL" },
    { 0x0200, "CRTC_H_TOTAL_DISP" },
    { 0x0204, "CRTC_H_SYNC_STRT_WID" },
    { 0x0208, "CRTC_V_TOTAL_DISP" },
    { 0x020C, "CRTC_V_SYNC_STRT_WID" },
    { 0x0220, "CRTC_OFFSET" },
    { 0x0224, "CRTC_PITCH" },
    { 0x0B00, "SURFACE_CNTL" },
    { 0x2C00, "D1MODE_MASTER_EN" },
    { 0x2C04, "D1MODE_TIMING_H" },
    { 0x2C08, "D1MODE_TIMING_V" },
    { 0x2C0C, "D1MODE_SURFACE" },
    { 0x2C10, "D1MODE_DATAPATH" },
    VGPU_REG_TABLE_END,
};

static uint64_t r500_reg_read_override(void *opaque, hwaddr addr, unsigned size)
{
    R500State *s = opaque;
    uint32_t a = (uint32_t)(addr & ~3u);

    if (a == 0x0010) { // R500_CLOCK_CNTL_DATA
        uint32_t idx = s->pll_index;
        if (idx < 64) {
            return s->pll_regs[idx];
        }
        return 0;
    }

    return UINT64_MAX; // Use default MMIO data
}

static bool r500_reg_write_override(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    R500State *s = opaque;
    uint32_t a = (uint32_t)(addr & ~3u);

    if (a == 0x000C) { // R500_CLOCK_CNTL_INDEX
        s->pll_index = val & 0x3F;
        return false; // Let it write to mmio_data as well
    }

    if (a == 0x0010) { // R500_CLOCK_CNTL_DATA
        uint32_t idx = s->pll_index;
        if (idx < 64) {
            s->pll_regs[idx] = val;
        }
        return false; // Let it write to mmio_data as well
    }

    if (a == 0x0050) { // R500_CRTC_GEN_CNTL
        // Alternate display active vblank/crtc status bit 15 based on display enable bit 0
        if (val & (1 << 0)) {
            s->state.mmio_data[0x0050 >> 2] = val | (1 << 15);
        } else {
            s->state.mmio_data[0x0050 >> 2] = val & ~(1 << 15);
        }
        return true; // Fully handled
    }

    return false;
}

static void r500_chip_reset(void *opaque)
{
    R500State *s = opaque;
    s->pll_index = 0;
    memset(s->pll_regs, 0, sizeof(s->pll_regs));
}

static const VGPUOps r500_ops = {
    .chip_name          = "R500",
    .vendor_id          = R500_VENDOR_ID,
    .device_id          = R500_DEVICE_ID_RV515,
    .revision           = 0x01,
    .bar0_size          = 65536,
    .bar1_min_size      = 32 * MiB,
    .reg_table          = r500_reg_table,
    .reg_read_override  = r500_reg_read_override,
    .reg_write_override = r500_reg_write_override,
    .chip_realize       = NULL,
    .chip_reset         = r500_chip_reset,
    .chip_exit          = NULL,
};

static void r500_realize(PCIDevice *pdev, Error **errp)
{
    R500State *s = R500(pdev);
    s->state.ops = &r500_ops;
    amd_common_realize(pdev, errp);
}

static void r500_exit(PCIDevice *pdev)
{
    vgpu_common_exit(pdev);
}

static void r500_reset(DeviceState *dev)
{
    vgpu_common_reset(dev);
}

static const VMStateDescription vmstate_r500 = {
    .name = TYPE_R500,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(state.pdev, R500State),
        VMSTATE_END_OF_LIST()
    },
};

static const Property r500_properties[] = {
    VGPU_COMMON_PROPS(R500State, state),
};

static void r500_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass    *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k  = PCI_DEVICE_CLASS(klass);

    k->realize   = r500_realize;
    k->exit      = r500_exit;
    k->vendor_id = R500_VENDOR_ID;
    k->device_id = R500_DEVICE_ID_RV515;
    k->revision  = 0x01;
    k->class_id  = PCI_CLASS_DISPLAY_VGA;

    dc->desc  = "ATI Radeon R500 (RV515) GPU";
    dc->vmsd  = &vmstate_r500;
    device_class_set_props_n(dc, r500_properties, ARRAY_SIZE(r500_properties));
    device_class_set_legacy_reset(dc, r500_reset);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static void r500_instance_init(Object *obj)
{
    PCI_DEVICE(obj)->cap_present |= QEMU_PCI_CAP_EXPRESS;
}

static const TypeInfo r500_info = {
    .name          = TYPE_R500,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(R500State),
    .instance_init = r500_instance_init,
    .class_init    = r500_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void r500_register_types(void)
{
    type_register_static(&r500_info);
}

type_init(r500_register_types)
