#pragma once
// Contexto Vulkan único detrás de NVN: instancia, GPU, dispositivo y cola gráfica.

// (VK_USE_PLATFORM_ANDROID_KHR y VK_NO_PROTOTYPES vienen de CMake: las vk* son los punteros de volk, que llena driver.cpp)
#include <volk.h>

namespace nvn {

struct VkContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props{};
    VkPhysicalDeviceSubgroupProperties subgroup{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceMemoryProperties memory{};
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    // VK_QCOM_render_pass_store_ops activa: los render passes pueden usar VK_ATTACHMENT_STORE_OP_NONE.
    bool store_op_none = false;
    // VK_GOOGLE_display_timing activa: se puede pedir a qué hora se muestra cada imagen (LSFG: el real 16,7 ms
    // después del generado, para que el compositor no tire uno de los dos).
    bool display_timing = false;
    bool push_descriptor = false;  // VK_KHR_push_descriptor activa (el escalado FSR, fsr.cpp)
    bool shader_float16 = false;  // VK_KHR_shader_float16_int8 encendida: los shaders FP16 de LSFG (lsfg/)
    // VK_EXT_descriptor_indexing con shaderSampledImageArrayNonUniformIndexing: los arrays bindless (decals, sondas) se
    // indexan por píxel con NonUniform (nvnshader::Options::nonuniform_indexing).
    bool nonuniform_sampled = false;
    // VK_EXT_custom_border_color (sin formato) y VK_EXT_sampler_filter_minmax: bordes exactos y reducción MIN/MAX.
    bool custom_border_color = false, filter_minmax = false;
    bool multi_viewport = true;
    uint32_t driver_id = 0;  // VkDriverId (VK_DRIVER_ID_MESA_TURNIP…), 0 if the driver doesn't say
    // VK_EXT_subgroup_size_control: the subgroup sizes the driver may pick (Turnip runs some shaders with 64 lanes and
    // some with 128), and the stages pipelines pin to subgroup_pin lanes, the size the shaders were translated for
    // (executor.cpp). 0: nothing pinned.
    uint32_t subgroup_min = 0, subgroup_max = 0, subgroup_pin = 0, subgroup_pin_stages = 0;  // la feature multiViewport (vk.cpp): los shaders que la usan, sin ella si no está
    uint32_t max_custom_border_samplers = 0;
    // VK_KHR_draw_indirect_count: los multi-draw de NVN con el número de draws en la GPU (nulos sin la extensión).
    PFN_vkCmdDrawIndexedIndirectCountKHR draw_indexed_indirect_count = nullptr;
    PFN_vkCmdDrawIndirectCountKHR draw_indirect_count = nullptr;
};

// Se crea en la primera llamada; nullptr si Vulkan no arranca.
VkContext* vk();

const char* vk_result_name(VkResult r);

}  // namespace nvn
