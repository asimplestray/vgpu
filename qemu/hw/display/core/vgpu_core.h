/*
 * vGPU Framework — Generic GPU Emulation Interface
 *
 * Provides a common interface (VGPUOps) and shared helpers used by all
 * GPU device implementations (NVIDIA, AMD, Intel).
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_DISPLAY_VGPU_CORE_H
#define HW_DISPLAY_VGPU_CORE_H

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "hw/pci/pci_device.h"
#include "hw/core/qdev-properties.h"
#include "ui/console.h"
#include "qom/object.h"

/* -------------------------------------------------------------------------
 * Register name lookup entry
 * ---------------------------------------------------------------------- */
typedef struct VGPURegEntry {
    uint32_t    offset;
    const char *name;
} VGPURegEntry;

/*
 * Terminate a VGPURegEntry table with this sentinel.
 * Use { 0, NULL } since offset 0 is always handled specially.
 */
#define VGPU_REG_TABLE_END  { 0, NULL }

/* -------------------------------------------------------------------------
 * Per-chip operations table
 *
 * Each GPU implementation fills one of these statically and stores a
 * pointer to it in VGPUState.ops.  The common realize/reset/exit code
 * calls back through this table for chip-specific behaviour.
 * ---------------------------------------------------------------------- */
typedef struct VGPUOps {
    /* Human-readable chip label used in log messages, e.g. "GK208" */
    const char *chip_name;

    /* PCI identification */
    uint16_t    vendor_id;
    uint16_t    device_id;
    uint8_t     revision;

    /* BAR sizes */
    uint64_t    bar0_size;      /* MMIO register space (BAR 0)          */
    uint64_t    bar1_min_size;  /* Minimum VRAM size   (BAR 1, 64-bit)  */

    /*
     * Named register table for log output.
     * Terminated by VGPU_REG_TABLE_END.  May be NULL.
     */
    const VGPURegEntry *reg_table;

    /*
     * Optional fixed-value overrides for specific register reads.
     * Called before the generic mmio_data[] array lookup.
     * Return UINT64_MAX to indicate "no override; use generic path".
     */
    uint64_t (*reg_read_override)(void *s, hwaddr addr, unsigned size);

    /*
     * Optional write interceptor.
     * Return true if the write was fully handled (skip mmio_data[] write).
     */
    bool (*reg_write_override)(void *s, hwaddr addr, uint64_t val,
                               unsigned size);

    /*
     * Chip-specific realize() called at the end of vgpu_common_realize()
     * after BARs and PCIe cap are set up.  May be NULL.
     */
    void (*chip_realize)(void *s, PCIDevice *pdev, Error **errp);

    /*
     * Chip-specific reset() called by vgpu_common_reset().  May be NULL.
     */
    void (*chip_reset)(void *s);

    /*
     * Chip-specific exit() called at the start of vgpu_common_exit().
     * May be NULL.
     */
    void (*chip_exit)(void *s);
} VGPUOps;

/* -------------------------------------------------------------------------
 * Common GPU state
 *
 * Embed this as the FIRST member of each chip's concrete State struct.
 * The PCIDevice pdev member MUST remain the very first field so that
 * QEMU's object model casts work correctly.
 * ---------------------------------------------------------------------- */
typedef struct VGPUState {
    PCIDevice        pdev;           /* MUST be first */

    MemoryRegion     mmio;           /* BAR 0 — register space            */
    MemoryRegion     vram;           /* BAR 1 — video RAM (64-bit prefetch) */

    uint32_t        *mmio_data;      /* Backing store for BAR 0 registers */
    unsigned int     mmio_nr;        /* Number of 32-bit registers        */

    uint64_t         vram_size;      /* QEMU property: vram_size=<N>      */

    QemuConsole     *con;            /* Display console (optional)        */
    bool             enable_display; /* QEMU property: display=on/off     */
    bool             log_all;        /* QEMU property: log_all=on/off     */
    QEMUTimer       *display_timer;  /* Timer for periodic display update */

    const VGPUOps   *ops;           /* Points to chip-specific ops table */
} VGPUState;

/* -------------------------------------------------------------------------
 * Shared helper: symbolic register name lookup
 * ---------------------------------------------------------------------- */
static inline const char *vgpu_reg_name(const VGPURegEntry *table,
                                        hwaddr offset)
{
    if (!table) {
        return NULL;
    }
    for (int i = 0; table[i].name != NULL; i++) {
        if (table[i].offset == offset) {
            return table[i].name;
        }
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * Shared MMIO read/write implementation (all chips use these via ops)
 * ---------------------------------------------------------------------- */
uint64_t vgpu_mmio_read(void *opaque, hwaddr addr, unsigned size);
void     vgpu_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                         unsigned size);
extern const MemoryRegionOps vgpu_mmio_ops;

/* -------------------------------------------------------------------------
 * Common device lifecycle hooks
 *
 * Each chip's realize/reset/exit functions should call these.
 * vgpu_common_realize() handles: BAR allocation, VRAM setup, PCIe cap,
 * display console init.  It calls ops->chip_realize() at the end.
 * ---------------------------------------------------------------------- */
void vgpu_common_realize(PCIDevice *pdev, Error **errp);
void vgpu_common_exit(PCIDevice *pdev);
void vgpu_common_reset(DeviceState *dev);

/* -------------------------------------------------------------------------
 * Convenience macro: declare standard QEMU properties for any chip.
 *
 * Usage inside a chip's property array:
 *
 *   static const Property mychip_properties[] = {
 *       VGPU_COMMON_PROPS(MyChipState, state),
 *       DEFINE_PROP_UINT32("extra_reg", MyChipState, extra_reg, 0),
 *   };
 *
 * StateType  — the concrete chip state struct (e.g. GK208State)
 * field      — the VGPUState member name inside StateType (e.g. "state"
 *              if the struct has `VGPUState state;` as its first member,
 *              but since PCIDevice must be first, chips embed VGPUState
 *              and access through it)
 * ---------------------------------------------------------------------- */
#define VGPU_COMMON_PROPS(StateType, vgpu_field)                              \
    DEFINE_PROP_SIZE("vram_size", StateType, vgpu_field.vram_size, 256 * MiB),\
    DEFINE_PROP_BOOL("log_all",   StateType, vgpu_field.log_all,   true),     \
    DEFINE_PROP_BOOL("display",   StateType, vgpu_field.enable_display, false)

#endif /* HW_DISPLAY_VGPU_CORE_H */
