/*
 * NVIDIA GPU Emulation — Common NVIDIA base layer
 *
 * Provides NVIDIA-specific constants, register tables, and helpers
 * shared across all NVIDIA GPU generations (Kepler, Maxwell, Pascal,
 * Turing, ...).
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_DISPLAY_NV_CORE_H
#define HW_DISPLAY_NV_CORE_H

#include "../core/vgpu_core.h"

/* -------------------------------------------------------------------------
 * PCI Vendor ID
 * ---------------------------------------------------------------------- */
#define NV_VENDOR_ID    0x10de

/* -------------------------------------------------------------------------
 * PMC — Master Control
 * 0x000000 – 0x000FFF
 * ---------------------------------------------------------------------- */
#define NV_PMC_BOOT_0           0x000000  /* GPU identification / chip revision */
#define NV_PMC_BOOT_1           0x000004  /* Endianness                        */
#define NV_PMC_INTR_0           0x000100  /* Interrupt pending bits             */
#define NV_PMC_INTR_EN_0        0x000140  /* Interrupt enable mask              */
#define NV_PMC_ENABLE           0x000200  /* Engine enable/reset bits           */

/* -------------------------------------------------------------------------
 * PTIMER — Programmable Timer
 * 0x009000 – 0x009FFF
 * ---------------------------------------------------------------------- */
#define NV_PTIMER_INTR_0        0x009100
#define NV_PTIMER_INTR_EN_0     0x009140
#define NV_PTIMER_NUM_0         0x009400  /* Numerator                          */
#define NV_PTIMER_DEN_0         0x009410  /* Denominator                        */
#define NV_PTIMER_TIME_0        0x009410  /* Current time (low 32 bits)         */
#define NV_PTIMER_TIME_1        0x009408  /* Current time (high 5 bits)         */

/* -------------------------------------------------------------------------
 * PBUS — PCI/PCIe Bus Interface
 * 0x001000 – 0x001FFF
 * ---------------------------------------------------------------------- */
#define NV_PBUS_INTR_0          0x001100
#define NV_PBUS_INTR_EN_0       0x001140
#define NV_PBUS_PCI_NV_0        0x001800  /* PCI config space mirror (VID/DID)  */
#define NV_PBUS_PCI_NV_1        0x001804
#define NV_PDEV_INFO            0x001200  /* Device info                        */

/* -------------------------------------------------------------------------
 * PFIFO — Command FIFO (channel / push buffer submission)
 * 0x002000 – 0x002FFF (older) / 0x800000+ (Kepler+)
 * ---------------------------------------------------------------------- */
#define NV_PFIFO_INTR_0         0x002100
#define NV_PFIFO_INTR_EN_0      0x002140
#define NV_PFIFO_CACHE1_PUSH0   0x003200
#define NV_PFIFO_CACHE1_PUSH1   0x003204

/* GK208 / Kepler PFIFO offsets */
#define NV_GK208_PFIFO_INTR_0   0x008000
#define NV_GK208_PFIFO_INTR_EN  0x008040
#define NV_GK208_PFIFO_RAMFC    0x008100
#define NV_GK208_PFIFO_CHAN     0x008230

/* -------------------------------------------------------------------------
 * PGRAPH — 3D / Compute Engine
 * 0x400000 – 0x407FFF
 * ---------------------------------------------------------------------- */
#define NV_PGRAPH_INTR          0x400100
#define NV_PGRAPH_INTR_EN       0x400108
#define NV_PGRAPH_CTXCTL        0x400300  /* Context control                    */
#define NV_PGRAPH_STATUS        0x400700  /* Idle/busy status                   */

/* -------------------------------------------------------------------------
 * PMU — Power Management Unit
 * 0x010000 – 0x01FFFF
 * ---------------------------------------------------------------------- */
#define NV_PMU_INTR_0           0x010200
#define NV_PMU_INTR_EN_0        0x010204

/* -------------------------------------------------------------------------
 * PFUSE — Fuse registers (chip SKU / feature enable)
 * 0x021000 – 0x021FFF
 * ---------------------------------------------------------------------- */
#define NV_PFUSE_STATUS_OPTIONB 0x021108  /* Feature fuse bits                  */

/* -------------------------------------------------------------------------
 * PFFB — Frame Buffer / Memory Controller
 * 0x100000 – 0x100FFF
 * ---------------------------------------------------------------------- */
#define NV_PFFB_UNK0800         0x100800

/* -------------------------------------------------------------------------
 * PDISPLAY — Display Engine
 * 0x610000 – 0x6FFFFF
 * ---------------------------------------------------------------------- */
#define NV_PDISPLAY_BEGIN       0x610000
#define NV_PDISPLAY_SOR_REGS    0x61C000  /* SOR (Serial Output Resource)       */

/* -------------------------------------------------------------------------
 * BOOT_0 encoding helpers
 *
 * PMC_BOOT_0 layout (Fermi+):
 *   [7:0]   implementation (stepping)
 *   [11:8]  architecture  (chip generation within the GPU family)
 *   [19:12] major revision
 *   [23:20] minor revision
 *   [27:24] implementation ID
 *   [31:28] GPU_ID (identifies the GPU family: 0x1 = GK, 0x1 = GM …)
 *
 * We encode this as: (gpu_id << 28) | (impl << 24) | (major << 12) | impl_step
 * ---------------------------------------------------------------------- */
#define NV_BOOT0(gpu_id, impl, major, step) \
    (((gpu_id) << 28) | ((impl) << 24) | ((major) << 12) | (step))

/* Known BOOT_0 values */
#define NV_GK208_BOOT_0   0x10800000   /* Kepler  GK208 (GT 730)          */
#define NV_GM107_BOOT_0   0x11700000   /* Maxwell GM107 (GTX 750 Ti)       */
#define NV_GP104_BOOT_0   0x1B040000   /* Pascal  GP104 (GTX 1080)         */
#define NV_TU102_BOOT_0   0x162000A1   /* Turing  TU102 (RTX 2080)         */

/* -------------------------------------------------------------------------
 * Common NVIDIA register name table (shared across generations)
 * Chip-specific tables should include these and extend them.
 * ---------------------------------------------------------------------- */
extern const VGPURegEntry nv_common_reg_table[];

/* -------------------------------------------------------------------------
 * Helper: build a VGPURegEntry array terminator
 * ---------------------------------------------------------------------- */
#define NV_REG(off, nm)  { (off), (nm) }

/* -------------------------------------------------------------------------
 * Standard NVIDIA BAR layout
 * ---------------------------------------------------------------------- */
#define NV_BAR0_SIZE        (16 * MiB)  /* MMIO register space              */
#define NV_BAR1_MIN_SIZE    (32 * MiB)  /* Minimum VRAM aperture            */

/* -------------------------------------------------------------------------
 * NVIDIA-specific realize helper
 *
 * Called by chip-specific realize() after setting s->state.ops.
 * Initialises PMC_BOOT_0 with the correct BOOT_0 value and calls
 * vgpu_common_realize().
 * ---------------------------------------------------------------------- */
void nv_common_realize(PCIDevice *pdev, uint32_t boot0, Error **errp);

#endif /* HW_DISPLAY_NV_CORE_H */
