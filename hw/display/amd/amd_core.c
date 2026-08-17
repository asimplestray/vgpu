/*
 * AMD GPU Emulation — Common AMD base implementation
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "amd_core.h"

/* -------------------------------------------------------------------------
 * Common AMD register name table
 * ---------------------------------------------------------------------- */
const VGPURegEntry amd_common_reg_table[] = {
    AMD_REG(AMD_CONFIG_MEMSIZE,       "CONFIG_MEMSIZE"),
    AMD_REG(AMD_GFX_GRBM_STATUS,     "GRBM_STATUS"),
    AMD_REG(AMD_GFX_GRBM_STATUS2,    "GRBM_STATUS2"),
    AMD_REG(AMD_GFX_GRBM_SOFT_RESET, "GRBM_SOFT_RESET"),
    AMD_REG(AMD_GFX_SRBM_STATUS,     "SRBM_STATUS"),
    AMD_REG(AMD_IH_RB_BASE,          "IH_RB_BASE"),
    AMD_REG(AMD_IH_STATUS,           "IH_STATUS"),
    AMD_REG(AMD_SMC_MSG,             "SMC_MSG"),
    AMD_REG(AMD_SMC_RESP,            "SMC_RESP"),
    AMD_REG(AMD_HDP_HOST_PATH_CNTL,  "HDP_HOST_PATH_CNTL"),
    AMD_REG(AMD_HDP_FLUSH_INVALIDATE,"HDP_FLUSH_INVALIDATE"),
    AMD_REG(AMD_BIF_FB_EN,           "BIF_FB_EN"),
    AMD_REG(AMD_SDMA0_STATUS_REG,    "SDMA0_STATUS_REG"),
    AMD_REG(AMD_SDMA1_STATUS_REG,    "SDMA1_STATUS_REG"),
    VGPU_REG_TABLE_END,
};

/* -------------------------------------------------------------------------
 * amd_common_realize
 * ---------------------------------------------------------------------- */
void amd_common_realize(PCIDevice *pdev, Error **errp)
{
    VGPUState *s = (VGPUState *)pdev;

    vgpu_common_realize(pdev, errp);
    if (*errp) {
        return;
    }

    /*
     * Initialise CONFIG_MEMSIZE so amdgpu can discover VRAM size.
     * Value is stored in MB in the register.
     */
    if (s->mmio_data && s->mmio_nr > (AMD_CONFIG_MEMSIZE >> 2)) {
        /* vram_size was set by vgpu_common_realize, read it back */
        uint64_t vram_mb = s->vram_size / MiB;
        s->mmio_data[AMD_CONFIG_MEMSIZE >> 2] = (uint32_t)vram_mb;
    }

    /*
     * GRBM_STATUS: report all engines idle (0 = idle).
     * amdgpu checks this during init to know the GPU is quiescent.
     */
    if (s->mmio_data && s->mmio_nr > (AMD_GFX_GRBM_STATUS >> 2)) {
        s->mmio_data[AMD_GFX_GRBM_STATUS >> 2]  = 0x00000000;
        s->mmio_data[AMD_GFX_GRBM_STATUS2 >> 2] = 0x00000000;
    }

    /*
     * SMC_RESP: pre-populate with 0x1 (OK) so SMU queries don't hang.
     */
    if (s->mmio_data && s->mmio_nr > (AMD_SMC_RESP >> 2)) {
        s->mmio_data[AMD_SMC_RESP >> 2] = 0x00000001;
    }
}
