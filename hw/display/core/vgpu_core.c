/*
 * vGPU Framework — Shared MMIO and lifecycle implementation
 *
 * Implements the generic MMIO read/write handlers and common device
 * lifecycle routines (realize, reset, exit) shared by all GPU emulations.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "vgpu_core.h"
#include "migration/vmstate.h"
#include "../amd/amd_core.h"
#include "qemu/timer.h"

/* -------------------------------------------------------------------------
 * Generic MMIO read
 * ---------------------------------------------------------------------- */
uint64_t vgpu_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    VGPUState *s = opaque;
    unsigned int idx = addr >> 2;
    uint64_t val = 0;

    /* Chip-specific override takes priority */
    if (s->ops->reg_read_override) {
        uint64_t ov = s->ops->reg_read_override(s, addr, size);
        if (ov != UINT64_MAX) {
            val = ov;
            goto log_and_return;
        }
    }

    if (idx < s->mmio_nr) {
        val = s->mmio_data[idx];
    }

log_and_return:
    if (s->log_all) {
        const char *rn = vgpu_reg_name(s->ops->reg_table, addr & ~3);
        if (rn) {
            qemu_log("%s: RD %-25s = 0x%08x\n",
                     s->ops->chip_name, rn, (uint32_t)val);
        } else {
            qemu_log("%s: RD 0x%06" HWADDR_PRIx " = 0x%08x\n",
                     s->ops->chip_name, addr, (uint32_t)val);
        }
    }
    return val;
}

/* -------------------------------------------------------------------------
 * Generic MMIO write
 * ---------------------------------------------------------------------- */
void vgpu_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    VGPUState *s = opaque;
    unsigned int idx = addr >> 2;
    uint32_t v = (uint32_t)val;

    /* Chip-specific override takes priority */
    if (s->ops->reg_write_override) {
        if (s->ops->reg_write_override(s, addr, val, size)) {
            goto log_and_return;
        }
    }

    if (idx < s->mmio_nr) {
        s->mmio_data[idx] = v;
    }

log_and_return:
    if (s->log_all) {
        const char *rn = vgpu_reg_name(s->ops->reg_table, addr & ~3);
        if (rn) {
            qemu_log("%s: WR %-25s = 0x%08x\n",
                     s->ops->chip_name, rn, v);
        } else {
            qemu_log("%s: WR 0x%06" HWADDR_PRIx " = 0x%08x\n",
                     s->ops->chip_name, addr, v);
        }
    }
}

const MemoryRegionOps vgpu_mmio_ops = {
    .read  = vgpu_mmio_read,
    .write = vgpu_mmio_write,
    .endianness          = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size  = 4,
    .impl.max_access_size  = 4,
};

/* -------------------------------------------------------------------------
 * Display update callback
 * ---------------------------------------------------------------------- */
static void vgpu_display_update(void *opaque)
{
    VGPUState *s = opaque;
    if (!s->con || !s->enable_display) {
        qemu_log("VGPU DISPLAY: con=%p enable_display=%d - returning\n", s->con, s->enable_display);
        return;
    }
    
    qemu_log("VGPU DISPLAY: callback called for device_id=0x%04x\n", s->ops->device_id);
    
    /* Re-arm timer for next frame (60 Hz) */
    if (s->display_timer) {
        timer_mod(s->display_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + (1000000000 / 60));
    }

    uint32_t width = 0, height = 0, pitch = 0, offset = 0;
    bool has_mode = false;

    /* R500 dynamic screen rendering */
    if (s->ops->device_id == 0x7145 || s->ops->device_id == 0x7152 || s->ops->device_id == 0x7194) {
        width = s->mmio_data[0x0200 >> 2] & 0xFFFF;
        height = s->mmio_data[0x0208 >> 2] & 0xFFFF;
        pitch = s->mmio_data[0x0224 >> 2];
        offset = s->mmio_data[0x0220 >> 2];
        has_mode = (width > 0 && height > 0 && pitch > 0);
    }

    /* Polaris/GFX8 (RX 480/580) dynamic screen rendering */
    if (s->ops->device_id == 0x67DF || s->ops->device_id == 0x67EF || s->ops->device_id == 0x67FF) {
        /* DCE CRTC timing registers */
        uint32_t h_total = s->mmio_data[AMD_DCE_CRTC0_H_TOTAL >> 2];
        uint32_t v_total = s->mmio_data[AMD_DCE_CRTC0_V_TOTAL >> 2];
        uint32_t h_sync = s->mmio_data[AMD_DCE_CRTC0_H_SYNC_A >> 2];
        uint32_t v_sync = s->mmio_data[AMD_DCE_CRTC0_V_SYNC_A >> 2];
        uint32_t crtc_ctrl = s->mmio_data[AMD_DCE_CRTC0_CONTROL >> 2];
        uint32_t pitch_val = s->mmio_data[AMD_DCE_CRTC0_PITCH >> 2];
        uint32_t offset_val = s->mmio_data[AMD_DCE_CRTC0_OFFSET >> 2];
        
        qemu_log("POLARIS DISPLAY: h_total=0x%08x v_total=0x%08x h_sync=0x%08x v_sync=0x%08x crtc_ctrl=0x%08x pitch=%u offset=%u\n",
                 h_total, v_total, h_sync, v_sync, crtc_ctrl, pitch_val, offset_val);

        /* Extract active display size from timing registers */
        width = h_total & 0xFFFF;
        height = v_total & 0xFFFF;
        pitch = pitch_val;
        offset = offset_val;

        has_mode = (width > 0 && height > 0 && pitch > 0 && (crtc_ctrl & 0x1));
    }

    if (has_mode) {
        qemu_log("POLARIS DISPLAY MODE: width=%u height=%u pitch=%u offset=%u\n", width, height, pitch, offset);
        DisplaySurface *surface = qemu_console_surface(s->con);
        if (surface) {
            if (surface_width(surface) != (int)width || surface_height(surface) != (int)height) {
                surface = qemu_create_displaysurface(width, height);
                if (!surface) {
                    qemu_log("POLARIS DISPLAY: Failed to create surface %ux%u\n", width, height);
                    has_mode = false;
                } else {
                    dpy_gfx_replace_surface(s->con, surface);
                }
            }

            if (has_mode) {
                uint8_t *vram_ptr = memory_region_get_ram_ptr(&s->vram);
                uint8_t *dest_ptr = surface_data(surface);
                int dest_stride = surface_stride(surface);

                if (vram_ptr && dest_ptr) {
                    vram_ptr += offset;
                    for (uint32_t y = 0; y < height; y++) {
                        memcpy(dest_ptr + y * dest_stride, vram_ptr + y * pitch, width * 4);
                    }
                }
            }
        }
    }

    int w = qemu_console_get_width(s->con, 0);
    int h = qemu_console_get_height(s->con, 0);
    if (w > 0 && h > 0) {
        dpy_gfx_update(s->con, 0, 0, w, h);
    }
}

static const GraphicHwOps vgpu_gfx_ops = {
    .gfx_update = vgpu_display_update,
};

/* -------------------------------------------------------------------------
 * Common realize
 * ---------------------------------------------------------------------- */
void vgpu_common_realize(PCIDevice *pdev, Error **errp)
{
    VGPUState *s = (VGPUState *)pdev;
    Object *obj = OBJECT(pdev);

    g_assert(s->ops != NULL);

    /* Allocate BAR 0 backing store */
    s->mmio_nr   = s->ops->bar0_size / sizeof(uint32_t);
    s->mmio_data = g_malloc0(s->ops->bar0_size);

    /* Round VRAM up to power-of-two, enforce minimum */
    uint64_t vram_size = pow2ceil(s->vram_size);
    if (vram_size < s->ops->bar1_min_size) {
        vram_size = s->ops->bar1_min_size;
    }

    /* Set up MMIO region (BAR 0) */
    memory_region_init_io(&s->mmio, obj, &vgpu_mmio_ops, s,
                          "vgpu.mmio", s->ops->bar0_size);

    /* Set up VRAM region (BAR 1, 64-bit prefetchable) */
    memory_region_init_ram(&s->vram, obj, "vgpu.vram", vram_size, errp);
    if (*errp) {
        return;
    }

    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
    pci_register_bar(pdev, 1,
                     PCI_BASE_ADDRESS_MEM_PREFETCH |
                     PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &s->vram);

    /* PCIe endpoint capability */
    if (pci_bus_is_express(pci_get_bus(pdev))) {
        int ret = pcie_endpoint_cap_init(pdev, 0x80);
        assert(ret > 0);
    }

    /* Configure PCI Interrupt Pin A */
    pci_config_set_interrupt_pin(pdev->config, 1);

    qemu_log("VGPU REALIZE: enable_display=%d\n", s->enable_display);
    /* Optional display console */
    if (s->enable_display) {
        s->con = graphic_console_init(DEVICE(pdev), 0, &vgpu_gfx_ops, s);
        qemu_log("VGPU REALIZE: console created=%p\n", s->con);
        
        /* Force initial display update */
        vgpu_display_update(s);
        
        /* Set up a timer to periodically update display (60 Hz) */
        s->display_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, vgpu_display_update, s);
        timer_mod(s->display_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + (1000000000 / 60));
    }

    /* Chip-specific additional setup */
    if (s->ops->chip_realize) {
        s->ops->chip_realize(s, pdev, errp);
        if (*errp) {
            return;
        }
    }

    qemu_log("%s: %s @ %s VRAM=%lu MiB\n",
             s->ops->chip_name,
             s->ops->chip_name,
             pci_bus_is_express(pci_get_bus(pdev)) ? "PCIe" : "PCI",
             (unsigned long)(vram_size / MiB));
}

/* -------------------------------------------------------------------------
 * Common exit
 * ---------------------------------------------------------------------- */
void vgpu_common_exit(PCIDevice *pdev)
{
    VGPUState *s = (VGPUState *)pdev;

    if (s->ops->chip_exit) {
        s->ops->chip_exit(s);
    }

    if (s->display_timer) {
        timer_free(s->display_timer);
        s->display_timer = NULL;
    }

    g_free(s->mmio_data);
    s->mmio_data = NULL;

    if (s->con) {
        graphic_console_close(s->con);
        s->con = NULL;
    }
}

/* -------------------------------------------------------------------------
 * Common reset
 * ---------------------------------------------------------------------- */
void vgpu_common_reset(DeviceState *dev)
{
    VGPUState *s = (VGPUState *)dev;

    if (s->mmio_data) {
        memset(s->mmio_data, 0, s->ops->bar0_size);
    }

    if (s->ops->chip_reset) {
        s->ops->chip_reset(s);
    }
}
