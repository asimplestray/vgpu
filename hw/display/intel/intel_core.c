/*
 * Intel GPU Emulation — Common Intel base implementation
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "intel_core.h"

/* -------------------------------------------------------------------------
 * Common Intel register name table
 * ---------------------------------------------------------------------- */
const VGPURegEntry intel_common_reg_table[] = {
    INTEL_REG(INTEL_PIPEACONF,           "PIPEACONF"),
    INTEL_REG(INTEL_PIPEBCONF,           "PIPEBCONF"),
    INTEL_REG(INTEL_PIPEASTAT,           "PIPEASTAT"),
    INTEL_REG(INTEL_GEN_RPNSWREQ,        "GT_RPNSWREQ"),
    INTEL_REG(INTEL_GEN_RC6_CONTROL,     "GT_RC6_CONTROL"),
    INTEL_REG(INTEL_GEN_RC6_STATE,       "GT_RC6_STATE"),
    INTEL_REG(INTEL_GEN_MASTER_IRQ,      "GEN_MASTER_IRQ"),
    INTEL_REG(INTEL_GEN_GT_IRQ_ISR,      "GT_IRQ_ISR"),
    INTEL_REG(INTEL_GEN_GT_IRQ_IIR,      "GT_IRQ_IIR"),
    INTEL_REG(INTEL_GEN_GT_IRQ_IER,      "GT_IRQ_IER"),
    INTEL_REG(INTEL_GEN_GT_IRQ_IMR,      "GT_IRQ_IMR"),
    INTEL_REG(INTEL_FORCEWAKE_MT,        "FORCEWAKE_MT"),
    INTEL_REG(INTEL_FORCEWAKE_ACK_MT,    "FORCEWAKE_ACK_MT"),
    INTEL_REG(INTEL_XE_GUC_WOPCM_OFFSET, "XE_GUC_WOPCM_OFFSET"),
    INTEL_REG(INTEL_XE_HUC_STATUS,       "XE_HUC_STATUS"),
    VGPU_REG_TABLE_END,
};

/* -------------------------------------------------------------------------
 * intel_common_realize
 * ---------------------------------------------------------------------- */
void intel_common_realize(PCIDevice *pdev, Error **errp)
{
    VGPUState *s = (VGPUState *)pdev;

    vgpu_common_realize(pdev, errp);
    if (*errp) {
        return;
    }

    /*
     * Forcewake ACK: the i915 / xe driver writes FORCEWAKE_MT to wake
     * up the GT, then polls FORCEWAKE_ACK_MT.  Pre-set to 0x1 (awake)
     * so the driver doesn't spin forever.
     *
     * Note: FORCEWAKE_ACK_MT offset 0x130060 >> 2 must be within mmio_nr.
     */
    unsigned int fw_ack_idx = INTEL_FORCEWAKE_ACK_MT >> 2;
    if (s->mmio_data && fw_ack_idx < s->mmio_nr) {
        s->mmio_data[fw_ack_idx] = 0x00000001;
    }

    /*
     * GEN_MASTER_IRQ: bit 31 = master enable.  Start disabled (0).
     * The driver will enable it during init.
     */
    unsigned int irq_idx = INTEL_GEN_MASTER_IRQ >> 2;
    if (s->mmio_data && irq_idx < s->mmio_nr) {
        s->mmio_data[irq_idx] = 0x00000000;
    }
}
