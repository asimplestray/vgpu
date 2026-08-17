/*
 * NVIDIA GK208 (GeForce GT 730) — PCI Device Emulation
 *
 * Implements the NVIDIA GK208 Kepler GPU as a QEMU PCI device.
 * Designed for driver development and debugging (Nouveau).
 *
 * Uses the vGPU framework (core/vgpu_core.h) and NVIDIA base layer
 * (nvidia/nv_core.h) for shared MMIO handling and register logging.
 *
 * PCI IDs: Vendor 0x10de (NVIDIA), Device 0x1287 (GT 730 GK208B)
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
#define TYPE_GK208  "nvidia-gt730"
OBJECT_DECLARE_SIMPLE_TYPE(GK208State, GK208)

/* PCI device IDs */
#define GK208_DEVICE_ID  0x1287
#define GK208_REVISION   0xa1

/* -------------------------------------------------------------------------
 * GK208-specific register name table
 *
 * Extends the common NVIDIA table with Kepler-specific register offsets.
 * ---------------------------------------------------------------------- */
static const VGPURegEntry gk208_reg_table[] = {
    /* PMC — reuse from nv_common_reg_table via the ops lookup loop */
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
    NV_REG(NV_PBUS_INTR_EN_0,      "PBUS_INTR_EN_0"),

    /* PFIFO — Kepler offsets */
    NV_REG(NV_GK208_PFIFO_INTR_0,  "PFIFO_INTR_0"),
    NV_REG(NV_GK208_PFIFO_INTR_EN, "PFIFO_INTR_EN_0"),
    NV_REG(NV_GK208_PFIFO_RAMFC,   "PFIFO_RAMFC"),
    NV_REG(NV_GK208_PFIFO_CHAN,    "PFIFO_CHAN"),

    /* PRM */
    NV_REG(0x00A000,                "PRM_PEC"),

    /* PMU */
    NV_REG(NV_PMU_INTR_0,          "PMU_INTR_0"),
    NV_REG(NV_PMU_INTR_EN_0,       "PMU_INTR_EN_0"),

    /* PFFB */
    NV_REG(NV_PFFB_UNK0800,        "PFFB_UNK0800"),

    /* PGRAPH */
    NV_REG(NV_PGRAPH_CTXCTL,       "PGRAPH_CTXCTL"),
    NV_REG(NV_PGRAPH_INTR,         "PGRAPH_INTR"),
    NV_REG(NV_PGRAPH_STATUS,       "PGRAPH_STATUS"),

    /* PDISPLAY */
    NV_REG(NV_PDISPLAY_BEGIN,      "PDISPLAY_BEGIN"),

    VGPU_REG_TABLE_END,
};

/* -------------------------------------------------------------------------
 * GK208 state
 *
 * VGPUState MUST be the first member so that PCIDevice is the very
 * first field in memory (required by QEMU's object model).
 * ---------------------------------------------------------------------- */
/* -------------------------------------------------------------------------
 * VBIOS PROM shadow
 *
 * Nouveau reads the VBIOS at BAR0 offset 0x300000 (PROM window).  We load
 * the PCI option ROM image into this buffer during chip_realize so that
 * the driver can parse the real clock / memory tables.
 * ---------------------------------------------------------------------- */
#define GK208_PROM_OFFSET   0x300000u
#define GK208_PROM_SIZE     0x010000u  /* 64 KiB PROM window */

/* -------------------------------------------------------------------------
 * FB / Memory Controller registers
 *
 * Nouveau reads these during ram_create to determine the number of
 * framebuffer partitions and which are active.
 *
 * GK208 (GT 730) has a single 64-bit memory interface → 1 FBP.
 * ---------------------------------------------------------------------- */
/* NV_PRAM_SYS_MEM_NISO_PARTS_NB (FBP count, Kepler+) */
#define NV_GK208_FBP_COUNT   0x022438u
#define GK208_FBP_COUNT_VAL  1          /* 1 framebuffer partition */

/*
 * NV_PRAM_SYS_MEM_NISO_PARTS_MASK — per-partition active bitmask.
 * A SET bit means that partition is ENABLED.  Returning 0 causes
 * "FBP 0: disabled".  Bit 0 must be 1 for GK208's single FBP.
 */
#define NV_GK208_FBP_PMASK      0x022554u
#define GK208_FBP_PMASK_VAL     0x00000001u  /* FBP 0 active */

/*
 * VRAM lower-region boundary registers (0x022700 / 0x022704):
 * Nouveau reads (lo_base_page, lo_top_page) and computes:
 *   lower_size = (lo_top - lo_base) << 12  bytes.
 * All-zero → Total: 0 MiB and ramcfg parse failure.
 *
 * For 1 GiB (0x40000000 bytes = 0x40000 × 4 KiB pages):
 *   lo_base = 0x000000, lo_top = 0x040000
 */
#define NV_GK208_VRAM_LO_BASE   0x022700u
#define NV_GK208_VRAM_LO_TOP    0x022704u
#define GK208_VRAM_LO_BASE_VAL  0x00000000u
#define GK208_VRAM_LO_TOP_VAL   0x00040000u  /* 1 GiB */

struct GK208State {
    VGPUState state;   /* MUST be first */

    uint8_t  vbios_shadow[GK208_PROM_SIZE]; /* VBIOS bytes served at 0x300000 */
    uint32_t vbios_size;                    /* actual bytes loaded (≤ PROM_SIZE) */
};

/* -------------------------------------------------------------------------
 * Register read override
 *
 * PMC_BOOT_0 is set in the backing store by nv_common_realize(), but we
 * also handle it here as a belt-and-suspenders guard for the case where
 * it is read before realize completes (shouldn't happen, but safe).
 * ---------------------------------------------------------------------- */
static uint64_t gk208_reg_read_override(void *opaque, hwaddr addr,
                                        unsigned size)
{
    GK208State *g = opaque;
    uint32_t a = (uint32_t)(addr & ~3u);

    /* PMC_BOOT_0 — belt-and-suspenders chip ID */
    if (a == NV_PMC_BOOT_0) {
        return NV_GK208_BOOT_0;
    }

    /*
     * FBP count (0x022438):
     * Nouveau reads this to determine the number of framebuffer partitions.
     * GK208 has exactly 1 FBP (64-bit memory bus).
     * Without this the driver reports "0 FBP(s)", computes 0 MiB VRAM and
     * fails with -ENOSYS in the ramcfg parsing path.
     */
    if (a == NV_GK208_FBP_COUNT) {
        qemu_log("GK208: FBP count query → %u\n", GK208_FBP_COUNT_VAL);
        return GK208_FBP_COUNT_VAL;
    }

    /*
     * FBP partition active mask (0x022554):
     * A SET bit means that partition is ENABLED.  Bit 0 must be 1 for
     * FBP 0 to be active; returning 0 causes "FBP 0: disabled".
     */
    if (a == NV_GK208_FBP_PMASK) {
        qemu_log("GK208: FBP pmask query → 0x%08x\n", GK208_FBP_PMASK_VAL);
        return GK208_FBP_PMASK_VAL;
    }

    /*
     * VRAM lower-region boundary registers (0x022700 / 0x022704):
     * Nouveau computes lower VRAM size = (lo_top - lo_base) << 12.
     * All-zero → 0 MiB → ramcfg parse fails.
     */
    if (a == NV_GK208_VRAM_LO_BASE) {
        return GK208_VRAM_LO_BASE_VAL;
    }
    if (a == NV_GK208_VRAM_LO_TOP) {
        qemu_log("GK208: VRAM lo_top → 0x%08x (1 GiB)\n", GK208_VRAM_LO_TOP_VAL);
        return GK208_VRAM_LO_TOP_VAL;
    }

    /*
     * PROM window (0x300000 – 0x30FFFF):
     * Nouveau reads the VBIOS here as a shadow copy of the PCI Option ROM.
     * We serve the loaded ROM image byte-by-byte, packed into 32-bit reads.
     */
    if (addr >= GK208_PROM_OFFSET &&
        addr <  GK208_PROM_OFFSET + GK208_PROM_SIZE) {
        uint32_t off = (uint32_t)(addr - GK208_PROM_OFFSET);
        if (off + 4 <= g->vbios_size) {
            uint32_t v;
            memcpy(&v, &g->vbios_shadow[off], 4);
            return v;
        }
        return 0xffffffff;
    }

    return UINT64_MAX; /* use generic path */
}

/* -------------------------------------------------------------------------
 * Chip-specific realize — called at end of vgpu_common_realize()
 * ---------------------------------------------------------------------- */
static void gk208_chip_realize(void *opaque, PCIDevice *pdev, Error **errp)
{
    GK208State *g = opaque;

    /*
     * Shadow the PCI Option ROM into the VBIOS buffer so Nouveau can read
     * the real VBIOS via the BAR0 PROM window at offset 0x300000.
     *
     * pdev->rom contains the Option ROM memory region loaded by QEMU when
     * the user passes romfile=... on the command line.
     */
    if (pdev->romfile && pdev->romfile[0] != '\0') {
        FILE *f = fopen(pdev->romfile, "rb");
        if (f) {
            size_t bytes = fread(g->vbios_shadow, 1, GK208_PROM_SIZE, f);
            g->vbios_size = (uint32_t)bytes;
            fclose(f);
            qemu_log("GK208: shadowed VBIOS %u bytes from %s at BAR0+0x300000\n",
                     g->vbios_size, pdev->romfile);
        } else {
            qemu_log("GK208: failed to open romfile %s — PROM window returns 0xff\n",
                     pdev->romfile);
            memset(g->vbios_shadow, 0xff, GK208_PROM_SIZE);
            g->vbios_size = 0;
        }
    } else {
        qemu_log("GK208: no romfile specified — PROM window returns 0xff\n");
        memset(g->vbios_shadow, 0xff, GK208_PROM_SIZE);
        g->vbios_size = 0;
    }
}

/* -------------------------------------------------------------------------
 * Chip-specific reset — called by vgpu_common_reset()
 * ---------------------------------------------------------------------- */
static void gk208_chip_reset(void *opaque)
{
    VGPUState *s = opaque;
    /* Re-assert BOOT_0 after the generic memset(0) in vgpu_common_reset() */
    if (s->mmio_data && s->mmio_nr > 0) {
        s->mmio_data[NV_PMC_BOOT_0 >> 2] = NV_GK208_BOOT_0;
    }
}

/* -------------------------------------------------------------------------
 * VGPUOps for GK208
 * ---------------------------------------------------------------------- */
static const VGPUOps gk208_ops = {
    .chip_name          = "GK208",
    .vendor_id          = NV_VENDOR_ID,
    .device_id          = GK208_DEVICE_ID,
    .revision           = GK208_REVISION,
    .bar0_size          = NV_BAR0_SIZE,
    .bar1_min_size      = NV_BAR1_MIN_SIZE,
    .reg_table          = gk208_reg_table,
    .reg_read_override  = gk208_reg_read_override,
    .reg_write_override = NULL,
    .chip_realize       = gk208_chip_realize,
    .chip_reset         = gk208_chip_reset,
    .chip_exit          = NULL,
};

/* -------------------------------------------------------------------------
 * QEMU device lifecycle
 * ---------------------------------------------------------------------- */
static void gk208_realize(PCIDevice *pdev, Error **errp)
{
    GK208State *g = GK208(pdev);
    g->state.ops = &gk208_ops;
    nv_common_realize(pdev, NV_GK208_BOOT_0, errp);
}

static void gk208_exit(PCIDevice *pdev)
{
    vgpu_common_exit(pdev);
}

static void gk208_reset(DeviceState *dev)
{
    vgpu_common_reset(dev);
}

/* -------------------------------------------------------------------------
 * VMState
 * ---------------------------------------------------------------------- */
static const VMStateDescription vmstate_gk208 = {
    .name = TYPE_GK208,
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(state.pdev, GK208State),
        VMSTATE_END_OF_LIST()
    },
};

/* -------------------------------------------------------------------------
 * Properties
 * ---------------------------------------------------------------------- */
static const Property gk208_properties[] = {
    VGPU_COMMON_PROPS(GK208State, state),
};

/* -------------------------------------------------------------------------
 * Class and type registration
 * ---------------------------------------------------------------------- */
static void gk208_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass    *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k  = PCI_DEVICE_CLASS(klass);

    k->realize   = gk208_realize;
    k->exit      = gk208_exit;
    k->vendor_id = NV_VENDOR_ID;
    k->device_id = GK208_DEVICE_ID;
    k->revision  = GK208_REVISION;
    k->class_id  = PCI_CLASS_DISPLAY_VGA;

    dc->desc  = "NVIDIA GeForce GT 730 (GK208 Kepler)";
    dc->vmsd  = &vmstate_gk208;
    device_class_set_props_n(dc, gk208_properties, ARRAY_SIZE(gk208_properties));
    device_class_set_legacy_reset(dc, gk208_reset);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static void gk208_instance_init(Object *obj)
{
    PCIDevice *dev = PCI_DEVICE(obj);
    dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
}

static const TypeInfo gk208_info = {
    .name          = TYPE_GK208,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(GK208State),
    .instance_init = gk208_instance_init,
    .class_init    = gk208_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void gk208_register_types(void)
{
    type_register_static(&gk208_info);
}

type_init(gk208_register_types)
