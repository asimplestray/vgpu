/*
 * Intel GPU Emulation — Common Intel base layer
 *
 * Provides Intel-specific constants, register offsets, and helpers
 * shared across Intel GPU generations (Gen9, Xe-LP, Xe-HPG Arc, ...).
 *
 * Register layout references:
 *  - Intel® Graphics Developer's Reference (GDR) / PRM
 *  - linux/drivers/gpu/drm/i915/ and linux/drivers/gpu/drm/xe/ source trees
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_DISPLAY_INTEL_CORE_H
#define HW_DISPLAY_INTEL_CORE_H

#include "../core/vgpu_core.h"

/* -------------------------------------------------------------------------
 * PCI Vendor ID
 * ---------------------------------------------------------------------- */
#define INTEL_VENDOR_ID  0x8086

/* -------------------------------------------------------------------------
 * MMIO register offsets (within BAR0, GTTMMADR aperture)
 *
 * Intel GPUs map their registers into the GTTMMADR BAR which is 16 MB
 * (or 256 MB for newer Gen).  The first 4 MB are register space.
 * ---------------------------------------------------------------------- */

/* ---- Display (pipe A/B/C) ---- */
#define INTEL_PIPEACONF         0x070008  /* Pipe A configuration             */
#define INTEL_PIPEBCONF         0x071008  /* Pipe B configuration             */
#define INTEL_PIPEASTAT         0x070024  /* Pipe A status                    */

/* ---- GT (Graphics Technology) core ---- */
#define INTEL_GEN_RPNSWREQ      0x0A008   /* Requested P-state                */
#define INTEL_GEN_RC6_CONTROL   0x0A090   /* RC6 power gating control         */
#define INTEL_GEN_RC6_STATE     0x0A094   /* Current RC6 state                */

/* ---- Interrupt registers ---- */
#define INTEL_GEN_MASTER_IRQ    0x044200  /* Master interrupt control         */
#define INTEL_GEN_GT_IRQ_ISR    0x044210  /* GT IRQ interrupt status          */
#define INTEL_GEN_GT_IRQ_IIR    0x044214  /* GT IRQ interrupt identity        */
#define INTEL_GEN_GT_IRQ_IER    0x044218  /* GT IRQ interrupt enable          */
#define INTEL_GEN_GT_IRQ_IMR    0x04421C  /* GT IRQ interrupt mask            */

/* ---- Forcewake (engine-wakeup protocol) ---- */
#define INTEL_FORCEWAKE_MT      0x00A188  /* Multi-threaded forcewake         */
#define INTEL_FORCEWAKE_ACK_MT  0x00130060 /* Forcewake acknowledge           */

/* ---- GT Multicontext (Xe / Arc) ---- */
#define INTEL_XE_GUC_WOPCM_OFFSET 0x190008 /* GuC WOPCM config (Xe driver)  */
#define INTEL_XE_HUC_STATUS       0x112C0   /* HuC (video firmware) status    */

/* ---- PCH / South display ---- */
#define INTEL_GMBUS_CLOCK_SEL   0x0C5008  /* GMBUS clock select               */

/* -------------------------------------------------------------------------
 * Standard Intel BAR layout
 *
 * BAR 0: GTTMMADR — 16 MB (Gen9) / 16 MB (Arc) MMIO + GTT aperture
 * BAR 2: GMADR   — 256 MB stolen memory aperture (pre-Iris)
 * BAR 4: IOBAR   — 64 B I/O space (legacy)
 *
 * For emulation purposes:
 *   BAR 0: register MMIO (16 MB)
 *   BAR 1: "VRAM" / framebuffer aperture
 * ---------------------------------------------------------------------- */
#define INTEL_BAR0_SIZE       (16 * MiB)
#define INTEL_BAR1_MIN_SIZE   (32 * MiB)

/* -------------------------------------------------------------------------
 * Common Intel register name table
 * ---------------------------------------------------------------------- */
extern const VGPURegEntry intel_common_reg_table[];

#define INTEL_REG(off, nm)  { (off), (nm) }

/* -------------------------------------------------------------------------
 * Intel common realize helper
 * ---------------------------------------------------------------------- */
void intel_common_realize(PCIDevice *pdev, Error **errp);

#endif /* HW_DISPLAY_INTEL_CORE_H */
