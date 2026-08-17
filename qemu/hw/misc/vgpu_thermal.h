#ifndef HW_MISC_VGPU_THERMAL_H
#define HW_MISC_VGPU_THERMAL_H

#include "hw/pci/pci.h"

#define TYPE_VGPU_THERMAL "vgpu-thermal"
#define VGPU_THERMAL(obj) OBJECT_CHECK(VGPUThermalState, (obj), TYPE_VGPU_THERMAL)

struct VGPUThermalState {
    PCIDevice parent_obj;
};

#endif