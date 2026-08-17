/*
 * AMD GPU Emulation — Common AMD base layer
 *
 * Provides AMD-specific constants, register offsets, and helpers
 * shared across AMD GPU generations (GCN, RDNA, ...).
 *
 * Register layout references: AMD GPU ISA documentation,
 * linux/drivers/gpu/drm/amd/amdgpu/ source tree.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_DISPLAY_AMD_CORE_H
#define HW_DISPLAY_AMD_CORE_H

#include "../core/vgpu_core.h"

/* -------------------------------------------------------------------------
 * PCI Vendor IDs
 * ---------------------------------------------------------------------- */
#define AMD_VENDOR_ID   0x1002  /* AMD/ATI */

/* -------------------------------------------------------------------------
 * MMIO register offsets (byte addresses within BAR0)
 *
 * AMD GPUs use an indirect register access scheme via INDEX/DATA pairs,
 * but the most important identification and interrupt registers are
 * directly accessible in the first 256 KB of BAR0.
 * ---------------------------------------------------------------------- */

/* ---- Config / ROM ---- */
#define AMD_CONFIG_MEMSIZE      0x0005428  /* VRAM size (in MB)              */

/* ---- GFX (3D engine) ---- */
#define AMD_GFX_RB_RPTR         0x0000800  /* Ring buffer read pointer       */
#define AMD_GFX_RB_WPTR         0x0000804  /* Ring buffer write pointer      */
#define AMD_GFX_SRBM_STATUS     0x000E50  /* SRBM (bus) status              */
#define AMD_GFX_GRBM_STATUS     0x008010  /* GRBM (graphics) status         */
#define AMD_GFX_GRBM_STATUS2    0x008014
#define AMD_GFX_GRBM_SOFT_RESET 0x008020  /* Soft reset control             */

/* ---- IH — Interrupt Handler ---- */
#define AMD_IH_RB_BASE          0x003AC8  /* IH ring buffer base            */
#define AMD_IH_RB_RPTR          0x003AC4
#define AMD_IH_RB_WPTR         0x003AC0
#define AMD_IH_CNTL             0x003AC0  /* IH control                     */
#define AMD_IH_STATUS           0x003B20

/* ---- SMC / SMU — System Management Unit ---- */
#define AMD_SMC_MSG             0x000228  /* SMU message register           */
#define AMD_SMC_MSG_ARG         0x00022C  /* SMU message argument           */
#define AMD_SMC_RESP            0x000230  /* SMU response                   */

/* ---- HDP — Host Data Path ---- */
#define AMD_HDP_HOST_PATH_CNTL  0x002C00
#define AMD_HDP_NONSURFACE_BASE 0x002C04
#define AMD_HDP_FLUSH_INVALIDATE 0x002C10

/* ---- BIF — Bus Interface ---- */
#define AMD_BIF_FB_EN           0x000504  /* Framebuffer enable             */

/* ---- SDMA — System DMA ---- */
#define AMD_SDMA0_STATUS_REG    0x000D28
#define AMD_SDMA1_STATUS_REG    0x001D28

/* ---- DCE / DCN — Display (pre-RDNA / RDNA) ---- */
#define AMD_DCE_CRTC0_CONTROL   0x01B0AC

/* DCE CRTC timing registers (CRTC0 at 0x6500 base) */
#define AMD_DCE_CRTC0_H_TOTAL           0x06500
#define AMD_DCE_CRTC0_H_BLANK_START_END 0x06504
#define AMD_DCE_CRTC0_H_SYNC_A          0x06508
#define AMD_DCE_CRTC0_V_TOTAL           0x06510
#define AMD_DCE_CRTC0_V_BLANK_START_END 0x06514
#define AMD_DCE_CRTC0_V_SYNC_A          0x06518
#define AMD_DCE_CRTC0_OFFSET            0x06520
#define AMD_DCE_CRTC0_PITCH             0x06524

#define AMD_DCN_DCHUBBUB_STATUS 0x05979C

/* -------------------------------------------------------------------------
 * Chip revision ID register (located at PCI config space offset 0x08,
 * also mirrored in MMIO at various offsets per generation)
 * ---------------------------------------------------------------------- */
#define AMD_REV_ID_GFX8_POLARIS10   0x67DF  /* RX 480 device ID            */
#define AMD_REV_ID_GFX10_NAVI22     0x73DF  /* RX 6700 XT device ID        */

/* -------------------------------------------------------------------------
 * Standard AMD BAR layout
 *
 * BAR 0: 256 MB MMIO (register space + framebuffer aperture)
 * BAR 2: 256 KB I/O space (legacy)
 * BAR 5: 512 KB doorbell aperture (GCN4+)
 *
 * For emulation purposes we expose BAR 0 (MMIO) and BAR 1 (VRAM).
 * ---------------------------------------------------------------------- */
#define AMD_BAR0_SIZE       (16 * MiB)  /* Reduced from real 256 MB for emu */
#define AMD_BAR1_MIN_SIZE   (32 * MiB)

/* -------------------------------------------------------------------------
 * Common AMD register name table
 * ---------------------------------------------------------------------- */
extern const VGPURegEntry amd_common_reg_table[];

#define AMD_REG(off, nm)  { (off), (nm) }

/* -------------------------------------------------------------------------
 * AMD common realize helper
 *
 * Sets up the MMIO backing store, initialises the config registers
 * (CONFIG_MEMSIZE etc.), and calls vgpu_common_realize().
 * ---------------------------------------------------------------------- */
void amd_common_realize(PCIDevice *pdev, Error **errp);

#endif /* HW_DISPLAY_AMD_CORE_H */
