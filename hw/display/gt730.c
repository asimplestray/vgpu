/*
 * NVIDIA GT 730 (GK208) PCI Device Emulation
 *
 * Emulates an NVIDIA GK208 GPU as a QEMU PCI device.
 * Designed for driver development and debugging (e.g. Nouveau).
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "hw/pci/pci_device.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "ui/console.h"
#include "qom/object.h"

#define TYPE_GT730 "nvidia-gt730"
OBJECT_DECLARE_SIMPLE_TYPE(GT730State, GT730)

#define GT730_VENDOR_ID   0x10de
#define GT730_DEVICE_ID   0x1287
#define GT730_REVISION    0xa1

#define GT730_BAR0_SIZE   (16 * MiB)
#define GT730_BAR1_SIZE_MIN (32 * MiB)

#define GK208_GPU_ID      0x108
#define GK208_BOOT_0      0x10800000

struct GT730State {
    PCIDevice pdev;

    MemoryRegion mmio;
    MemoryRegion vram;

    uint32_t *mmio_data;
    unsigned int mmio_nr;

    uint64_t vram_size;

    QemuConsole *con;
    bool enable_display;
    bool log_all;
};

static const struct {
    uint32_t offset;
    const char *name;
} reg_names[] = {
    { 0x000000, "PMC_BOOT_0" },
    { 0x000100, "PMC_INTR_0" },
    { 0x000140, "PMC_INTR_EN_0" },
    { 0x000080, "PTIMER_TIME_0" },
    { 0x000098, "PTIMER_NUM_FBITS" },
    { 0x001000, "PBUS_BUDDY" },
    { 0x001200, "PDEV_INFO" },
    { 0x008000, "PFIFO_INTR_0" },
    { 0x008040, "PFIFO_INTR_EN_0" },
    { 0x008100, "PFIFO_RAMFC" },
    { 0x008228, "PFIFO_RAMFC" },
    { 0x008230, "PFIFO_CHAN" },
    { 0x00A000, "PRM_PEC" },
    { 0x010000, "PMU_INTR_0" },
    { 0x100800, "PFFB_UNK0800" },
    { 0x400000, "PGRAPH_CTXCTL" },
    { 0x610000, "PDISPLAY_BEGIN" },
    { 0, NULL }
};

static const char *reg_name(hwaddr offset)
{
    for (int i = 0; reg_names[i].name; i++) {
        if (reg_names[i].offset == offset) return reg_names[i].name;
    }
    return NULL;
}

static uint64_t gt730_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    GT730State *s = opaque;
    unsigned int idx = addr >> 2;
    uint64_t val = 0;

    if (addr == 0) {
        return GK208_BOOT_0;
    }

    if (idx < s->mmio_nr) {
        val = s->mmio_data[idx];
    }

    if (s->log_all) {
        const char *rn = reg_name(addr & ~3);
        if (rn) {
            qemu_log("GT730: RD %s = 0x%08x\n", rn, (uint32_t)val);
        } else {
            qemu_log("GT730: RD 0x%06" HWADDR_PRIx " = 0x%08x\n",
                     addr, (uint32_t)val);
        }
    }
    return val;
}

static void gt730_mmio_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    GT730State *s = opaque;
    unsigned int idx = addr >> 2;
    uint32_t v = val;

    if (idx < s->mmio_nr) {
        s->mmio_data[idx] = v;
    }

    if (s->log_all) {
        const char *rn = reg_name(addr & ~3);
        if (rn) {
            qemu_log("GT730: WR %s = 0x%08x\n", rn, v);
        } else {
            qemu_log("GT730: WR 0x%06" HWADDR_PRIx " = 0x%08x\n", addr, v);
        }
    }
}

static const MemoryRegionOps gt730_mmio_ops = {
    .read = gt730_mmio_read,
    .write = gt730_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void gt730_display_update(void *opaque)
{
    GT730State *s = opaque;
    if (!s->con || !s->enable_display) return;

    int w = qemu_console_get_width(s->con, 0);
    int h = qemu_console_get_height(s->con, 0);
    if (w > 0 && h > 0) {
        dpy_gfx_update(s->con, 0, 0, w, h);
    }
}

static const GraphicHwOps gt730_gfx_ops = {
    .gfx_update  = gt730_display_update,
};

static void gt730_realize(PCIDevice *pdev, Error **errp)
{
    GT730State *s = GT730(pdev);
    Object *obj = OBJECT(pdev);
    int ret;

    s->mmio_nr = GT730_BAR0_SIZE / sizeof(uint32_t);
    s->mmio_data = g_malloc0(GT730_BAR0_SIZE);
    s->mmio_data[0] = GK208_BOOT_0;

    uint64_t vram_size = pow2ceil(s->vram_size);
    if (vram_size < GT730_BAR1_SIZE_MIN) {
        vram_size = GT730_BAR1_SIZE_MIN;
    }

    memory_region_init_io(&s->mmio, obj, &gt730_mmio_ops, s,
                          "gt730.mmio", GT730_BAR0_SIZE);

    memory_region_init_ram(&s->vram, obj, "gt730.vram", vram_size, errp);
    if (*errp) return;

    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
    pci_register_bar(pdev, 1,
                     PCI_BASE_ADDRESS_MEM_PREFETCH | PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &s->vram);

    if (s->enable_display) {
        s->con = graphic_console_init(DEVICE(pdev), 0, &gt730_gfx_ops, s);
    }

    if (pci_bus_is_express(pci_get_bus(pdev))) {
        ret = pcie_endpoint_cap_init(pdev, 0x80);
        assert(ret > 0);
    }

    qemu_log("GT730: GK208 @ %s VRAM=%luMB\n",
             pci_bus_is_express(pci_get_bus(pdev)) ? "PCIe" : "PCI",
             (unsigned long)(vram_size / MiB));
}

static void gt730_exit(PCIDevice *pdev)
{
    GT730State *s = GT730(pdev);
    g_free(s->mmio_data);
    s->mmio_data = NULL;
    if (s->con) {
        graphic_console_close(s->con);
        s->con = NULL;
    }
}

static void gt730_reset(DeviceState *dev)
{
    GT730State *s = GT730(dev);
    if (s->mmio_data) {
        memset(s->mmio_data, 0, GT730_BAR0_SIZE);
        s->mmio_data[0] = GK208_BOOT_0;
    }
}

static const Property gt730_properties[] = {
    DEFINE_PROP_SIZE("vram_size", GT730State, vram_size, 256 * MiB),
    DEFINE_PROP_BOOL("log_all", GT730State, log_all, true),
    DEFINE_PROP_BOOL("display", GT730State, enable_display, false),
};

static const VMStateDescription vmstate_gt730 = {
    .name = "nvidia-gt730",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(pdev, GT730State),
        VMSTATE_END_OF_LIST()
    },
};

static void gt730_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = gt730_realize;
    k->exit = gt730_exit;
    k->vendor_id = GT730_VENDOR_ID;
    k->device_id = GT730_DEVICE_ID;
    k->revision = GT730_REVISION;
    k->class_id = PCI_CLASS_DISPLAY_VGA;

    dc->vmsd = &vmstate_gt730;
    dc->desc = "NVIDIA GT 730 (GK208) GPU";
    device_class_set_props_n(dc, gt730_properties, ARRAY_SIZE(gt730_properties));
    device_class_set_legacy_reset(dc, gt730_reset);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static void gt730_init(Object *obj)
{
    PCIDevice *dev = PCI_DEVICE(obj);
    dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
}

static const TypeInfo gt730_info = {
    .name = TYPE_GT730,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(GT730State),
    .instance_init = gt730_init,
    .class_init = gt730_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void gt730_register_types(void)
{
    type_register_static(&gt730_info);
}

type_init(gt730_register_types)
