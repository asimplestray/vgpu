/*
 * NVIDIA GPU Emulation — Common NVIDIA base implementation
 *
 * Implements the shared register name table and the common NVIDIA
 * realize helper used by all NVIDIA GPU device emulations.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "nv_core.h"

/* -------------------------------------------------------------------------
 * Common NVIDIA register name table
 *
 * Covers register offsets that are present (at the same address) across
 * multiple GPU generations.  Chip-specific tables may reference this or
 * declare their own extended version.
 * ---------------------------------------------------------------------- */
const VGPURegEntry nv_common_reg_table[] = {
    /* PMC — Master Control */
    NV_REG(NV_PMC_BOOT_0,        "PMC_BOOT_0"),
    NV_REG(NV_PMC_BOOT_1,        "PMC_BOOT_1"),
    NV_REG(NV_PMC_INTR_0,        "PMC_INTR_0"),
    NV_REG(NV_PMC_INTR_EN_0,     "PMC_INTR_EN_0"),
    NV_REG(NV_PMC_ENABLE,        "PMC_ENABLE"),

    /* PTIMER */
    NV_REG(NV_PTIMER_INTR_0,     "PTIMER_INTR_0"),
    NV_REG(NV_PTIMER_INTR_EN_0,  "PTIMER_INTR_EN_0"),
    NV_REG(NV_PTIMER_TIME_0,     "PTIMER_TIME_0"),
    NV_REG(NV_PTIMER_TIME_1,     "PTIMER_TIME_1"),

    /* PBUS */
    NV_REG(NV_PBUS_INTR_0,       "PBUS_INTR_0"),
    NV_REG(NV_PBUS_INTR_EN_0,    "PBUS_INTR_EN_0"),
    NV_REG(NV_PDEV_INFO,         "PDEV_INFO"),

    /* PMU */
    NV_REG(NV_PMU_INTR_0,        "PMU_INTR_0"),
    NV_REG(NV_PMU_INTR_EN_0,     "PMU_INTR_EN_0"),

    /* PFUSE */
    NV_REG(NV_PFUSE_STATUS_OPTIONB, "PFUSE_STATUS_OPTIONB"),

    /* PFFB */
    NV_REG(NV_PFFB_UNK0800,      "PFFB_UNK0800"),

    /* PGRAPH */
    NV_REG(NV_PGRAPH_INTR,       "PGRAPH_INTR"),
    NV_REG(NV_PGRAPH_INTR_EN,    "PGRAPH_INTR_EN"),
    NV_REG(NV_PGRAPH_CTXCTL,     "PGRAPH_CTXCTL"),
    NV_REG(NV_PGRAPH_STATUS,     "PGRAPH_STATUS"),

    /* PDISPLAY */
    NV_REG(NV_PDISPLAY_BEGIN,    "PDISPLAY_BEGIN"),

    VGPU_REG_TABLE_END,
};

/* -------------------------------------------------------------------------
 * nv_common_realize
 *
 * Initialises PMC_BOOT_0 in the register backing store with the
 * chip-specific boot0 value, then delegates to vgpu_common_realize().
 * ---------------------------------------------------------------------- */
void nv_common_realize(PCIDevice *pdev, uint32_t boot0, Error **errp)
{
    VGPUState *s = (VGPUState *)pdev;

    /* vgpu_common_realize allocates mmio_data; call it first */
    vgpu_common_realize(pdev, errp);
    if (*errp) {
        return;
    }

    /*
     * PMC_BOOT_0 is at offset 0x000000 → index 0.
     * This is the very first thing Nouveau (and other drivers) read to
     * identify the GPU.
     */
    if (s->mmio_data && s->mmio_nr > 0) {
        s->mmio_data[NV_PMC_BOOT_0 >> 2] = boot0;
    }
}
