#include "nnsdk/nvn/lsfg/motion.h"

#include <algorithm>
#include <cstdio>

#include "nnsdk/nvn/nvn.h"
#include "nnsdk/nvn/shaders/spirv.h"

namespace nvn::lsfg {

namespace {

#if defined(HAVE_fg_luma_SPV) && defined(HAVE_fg_down_SPV) && defined(HAVE_fg_search_SPV) && defined(HAVE_fg_refine_SPV) && \
    defined(HAVE_fg_median_SPV) && defined(HAVE_fg_warp_SPV)
#define FG_SHADERS 1
#else
#define FG_SHADERS 0
#endif

// The push constants every pass has (shaders/fg_*.comp): its output's size, its source's size, 1 / the picture's size,
// a flag and one more value (refine: the coarser field's size; warp: pixels per luma texel)
struct Push {
    int32_t size[2], src_size[2];
    float inv[2];
    int32_t flags, extra;
};

constexpr VkFormat kLumaFormat = VK_FORMAT_R32_SFLOAT, kFieldFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
constexpr VkFormat kOut = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkImageUsageFlags kWork = VK_IMAGE_USAGE_STORAGE_BIT;

VkFormat unorm(VkFormat f) {  // (an sRGB picture is read and written as its stored values, as LSFG's chain does)
    switch (f) {
        case VK_FORMAT_R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_UNORM;
        case VK_FORMAT_A8B8G8R8_SRGB_PACK32: return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
        default: return f;
    }
}

uint32_t memory_type(uint32_t bits) {
    const VkPhysicalDeviceMemoryProperties& m = vk()->memory;
    for (uint32_t k = 0; k < m.memoryTypeCount; k++)
        if ((bits & (1u << k)) && (m.memoryTypes[k].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return k;
    return UINT32_MAX;
}

VkExtent2D div(VkExtent2D e, uint32_t d) { return {std::max(1u, (e.width + d - 1) / d), std::max(1u, (e.height + d - 1) / d)}; }

void compute_barrier(VkCommandBuffer cmd) {
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &b, 0,
                         nullptr, 0, nullptr);
}

}  // namespace

bool Motion::available() { return FG_SHADERS != 0; }

void Motion::add(Image& image, VkExtent2D extent, VkFormat format, VkFormat view_format, VkImageUsageFlags usage,
                 VkImageCreateFlags flags) {
    image = Image{};
    image.extent = {std::max(1u, extent.width), std::max(1u, extent.height)};
    image.format = format;
    image.view_format = view_format;
    image.usage = usage;
    image.flags = flags;
    all_.push_back(&image);
}

bool Motion::create(VkExtent2D extent, VkFormat format, std::string* why) {
    destroy();
    if (!available()) {
        *why = "this build has no shaders for it (tools/shaders.sh)";
        return false;
    }
    extent_ = extent;
    format_ = format;
    if (!build_pipelines(why) || !build_images(why) || !build_sets(why)) {
        destroy();
        return false;
    }
    return true;
}

void Motion::destroy() {
    VkContext* c = vk();
    if (!c) return;
    VkDevice d = c->device;
    for (Image* i : all_) {
        if (i->view) vkDestroyImageView(d, i->view, nullptr);
        if (i->image) vkDestroyImage(d, i->image, nullptr);
        *i = Image{};
    }
    all_.clear();
    if (memory_) vkFreeMemory(d, memory_, nullptr);
    memory_ = VK_NULL_HANDLE;
    bytes_ = 0;
    if (pool_) vkDestroyDescriptorPool(d, pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
    if (sampler_) vkDestroySampler(d, sampler_, nullptr);
    sampler_ = VK_NULL_HANDLE;
    for (Pipeline& p : passes_) {
        if (p.pipeline) vkDestroyPipeline(d, p.pipeline, nullptr);
        if (p.layout) vkDestroyPipelineLayout(d, p.layout, nullptr);
        if (p.set_layout) vkDestroyDescriptorSetLayout(d, p.set_layout, nullptr);
        p = Pipeline{};
    }
    initialized_ = false;
}

bool Motion::build_pipelines(std::string* why) {
#if FG_SHADERS
    VkContext* c = vk();
    constexpr VkDescriptorType S = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, T = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    struct Def {
        const uint32_t* code;
        size_t bytes;
        std::vector<VkDescriptorType> types;
    };
    const Def defs[kPasses] = {
        {k_fg_luma_spv, sizeof k_fg_luma_spv, {T, S}},
        {k_fg_down_spv, sizeof k_fg_down_spv, {S, S}},
        {k_fg_search_spv, sizeof k_fg_search_spv, {S, S, S}},
        {k_fg_refine_spv, sizeof k_fg_refine_spv, {S, S, S, S}},
        {k_fg_median_spv, sizeof k_fg_median_spv, {S, S}},
        {k_fg_warp_spv, sizeof k_fg_warp_spv, {T, T, S, S}},
    };
    for (int k = 0; k < kPasses; k++) {
        Pipeline& p = passes_[k];
        std::vector<VkDescriptorSetLayoutBinding> b(defs[k].types.size());
        for (uint32_t i = 0; i < b.size(); i++) b[i] = {i, defs[k].types[i], 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount = static_cast<uint32_t>(b.size());
        li.pBindings = b.data();
        if (vkCreateDescriptorSetLayout(c->device, &li, nullptr, &p.set_layout) != VK_SUCCESS)
            return *why = "vkCreateDescriptorSetLayout", false;
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &p.set_layout;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &range;
        if (vkCreatePipelineLayout(c->device, &pl, nullptr, &p.layout) != VK_SUCCESS)
            return *why = "vkCreatePipelineLayout", false;
        VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        mi.codeSize = defs[k].bytes;
        mi.pCode = defs[k].code;
        VkShaderModule module = VK_NULL_HANDLE;
        if (VkResult r = vkCreateShaderModule(c->device, &mi, nullptr, &module); r != VK_SUCCESS)
            return *why = std::string("vkCreateShaderModule: ") + vk_result_name(r), false;
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        ci.stage.module = module;
        ci.stage.pName = "main";
        ci.layout = p.layout;
        const VkResult r = vkCreateComputePipelines(c->device, VK_NULL_HANDLE, 1, &ci, nullptr, &p.pipeline);
        vkDestroyShaderModule(c->device, module, nullptr);
        if (r != VK_SUCCESS) return *why = std::string("vkCreateComputePipelines: ") + vk_result_name(r), false;
    }
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    if (vkCreateSampler(c->device, &si, nullptr, &sampler_) != VK_SUCCESS) return *why = "vkCreateSampler", false;
    return true;
#else
    *why = "no shaders";
    return false;
#endif
}

bool Motion::build_images(std::string* why) {
    VkContext* c = vk();
    const VkFormat view = unorm(format_);
    const bool srgb = view != format_;
    // The pictures: as the window's (copied or blitted in), read through a UNORM view; the output too, written through one
    for (Image& f : frames_)
        add(f, extent_, format_, view, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            srgb ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0);
    const VkExtent2D l1 = div(extent_, 4), l2 = div(l1, 2), l3 = div(l2, 2);
    for (int i = 0; i < 2; i++) {
        add(lum1_[i], l1, kLumaFormat, kLumaFormat, kWork);
        add(lum2_[i], l2, kLumaFormat, kLumaFormat, kWork);
        add(lum3_[i], l3, kLumaFormat, kLumaFormat, kWork);
    }
    add(field3_, div(l3, 4), kFieldFormat, kFieldFormat, kWork);
    add(field2_, div(l2, 4), kFieldFormat, kFieldFormat, kWork);
    add(field1_, div(l1, 4), kFieldFormat, kFieldFormat, kWork);
    add(field_, div(l1, 4), kFieldFormat, kFieldFormat, kWork);
    // (the window's own format with a UNORM view for the shader when it's an sRGB one: the blit to the swapchain then
    // converts the made picture as it does the real one)
    add(output_, extent_, srgb ? VK_FORMAT_R8G8B8A8_SRGB : kOut, kOut,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        srgb ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT : 0);

    auto has = [&](VkFormat f, VkFormatFeatureFlags need) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(c->gpu, f, &fp);
        return (fp.optimalTilingFeatures & need) == need;
    };
    char buf[160];
    for (Image* i : all_) {
        VkFormatFeatureFlags need = 0;
        if (i->usage & VK_IMAGE_USAGE_SAMPLED_BIT) need |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if (i->usage & VK_IMAGE_USAGE_STORAGE_BIT) need |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
        if (!has(i->view_format, need)) {
            snprintf(buf, sizeof buf, "format %d can't be used for it", i->view_format);
            return *why = buf, false;
        }
    }
    uint32_t bits = ~0u;
    for (Image* i : all_) {
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.flags = i->flags;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = i->format;
        ii.extent = {i->extent.width, i->extent.height, 1};
        ii.mipLevels = ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = i->usage;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (VkResult r = vkCreateImage(c->device, &ii, nullptr, &i->image); r != VK_SUCCESS) {
            snprintf(buf, sizeof buf, "vkCreateImage %ux%u format %d: %s", i->extent.width, i->extent.height, i->format,
                     vk_result_name(r));
            return *why = buf, false;
        }
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(c->device, i->image, &req);
        bits &= req.memoryTypeBits;
        i->offset = (bytes_ + req.alignment - 1) / req.alignment * req.alignment;
        bytes_ = i->offset + req.size;
    }
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};  // (one allocation for all, as LSFG's chain)
    ai.allocationSize = bytes_;
    ai.memoryTypeIndex = memory_type(bits);
    if (ai.memoryTypeIndex == UINT32_MAX) return *why = "no memory type for all the images", false;
    if (VkResult r = vkAllocateMemory(c->device, &ai, nullptr, &memory_); r != VK_SUCCESS)
        return *why = std::string("vkAllocateMemory: ") + vk_result_name(r), false;
    for (Image* i : all_) {
        if (VkResult r = vkBindImageMemory(c->device, i->image, memory_, i->offset); r != VK_SUCCESS)
            return *why = std::string("vkBindImageMemory: ") + vk_result_name(r), false;
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        VkImageViewUsageCreateInfo usage{VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO};
        usage.usage = i->usage & (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        if (i->flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT) vi.pNext = &usage;  // (storage only through the UNORM view)
        vi.image = i->image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = i->view_format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (VkResult r = vkCreateImageView(c->device, &vi, nullptr, &i->view); r != VK_SUCCESS)
            return *why = std::string("vkCreateImageView: ") + vk_result_name(r), false;
    }
    return true;
}

bool Motion::build_sets(std::string* why) {
    VkContext* c = vk();
    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kSets * 4},
                                     {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSets * 2}};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = kSets;
    pi.poolSizeCount = 2;
    pi.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(c->device, &pi, nullptr, &pool_) != VK_SUCCESS) return *why = "vkCreateDescriptorPool", false;
    auto make = [&](Pass pass, VkDescriptorSet* out) {
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &passes_[pass].set_layout;
        return vkAllocateDescriptorSets(c->device, &ai, out) == VK_SUCCESS;
    };
    // Writes a set: each image in binding order, sampled (with the linear sampler) or storage
    std::vector<VkDescriptorImageInfo> infos;
    std::vector<VkWriteDescriptorSet> writes;
    auto write = [&](VkDescriptorSet set, std::initializer_list<std::pair<const Image*, bool>> images) {
        uint32_t binding = 0;
        for (const auto& [image, sampled] : images) {
            infos.push_back({sampled ? sampler_ : VK_NULL_HANDLE, image->view, VK_IMAGE_LAYOUT_GENERAL});
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = set;
            w.dstBinding = binding++;
            w.descriptorCount = 1;
            w.descriptorType = sampled ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes.push_back(w);
        }
    };
    infos.reserve(kSets * 4);
    const Pass kinds[kSetKinds] = {kLuma, kDown, kDown, kSearch, kRefine, kRefine, kWarp};
    for (int k = 0; k < kSetKinds; k++)
        for (int p = 0; p < 2; p++)
            if (!make(kinds[k], &sets_[k][p])) return *why = "vkAllocateDescriptorSets", false;
    if (!make(kMedian, &median_set_)) return *why = "vkAllocateDescriptorSets", false;
    for (int p = 0; p < 2; p++) {  // p: B's parity; A is the other
        const int a = 1 - p;
        write(sets_[kSetLuma][p], {{&frames_[p], true}, {&lum1_[p], false}});
        write(sets_[kSetDown1][p], {{&lum1_[p], false}, {&lum2_[p], false}});
        write(sets_[kSetDown2][p], {{&lum2_[p], false}, {&lum3_[p], false}});
        write(sets_[kSetSearch][p], {{&lum3_[a], false}, {&lum3_[p], false}, {&field3_, false}});
        write(sets_[kSetRefine2][p], {{&field3_, false}, {&lum2_[a], false}, {&lum2_[p], false}, {&field2_, false}});
        write(sets_[kSetRefine1][p], {{&field2_, false}, {&lum1_[a], false}, {&lum1_[p], false}, {&field1_, false}});
        write(sets_[kSetWarp][p], {{&frames_[a], true}, {&frames_[p], true}, {&field_, false}, {&output_, false}});
    }
    write(median_set_, {{&field1_, false}, {&field_, false}});
    for (size_t k = 0; k < writes.size(); k++) writes[k].pImageInfo = &infos[k];  // (infos no longer grows)
    vkUpdateDescriptorSets(c->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return true;
}

void Motion::record_init(VkCommandBuffer cmd) {
    std::vector<VkImageMemoryBarrier> b(all_.size());
    for (size_t k = 0; k < all_.size(); k++) {
        b[k] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b[k].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                             VK_ACCESS_TRANSFER_WRITE_BIT;
        b[k].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b[k].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b[k].srcQueueFamilyIndex = b[k].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[k].image = all_[k]->image;
        b[k].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(b.size()), b.data());
    initialized_ = true;
}

void Motion::dispatch(VkCommandBuffer cmd, Pass pass, VkDescriptorSet set, VkExtent2D size, VkExtent2D src, int flags,
                      int extra) const {
    const Pipeline& p = passes_[pass];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, 0, 1, &set, 0, nullptr);
    Push push{{static_cast<int32_t>(size.width), static_cast<int32_t>(size.height)},
              {static_cast<int32_t>(src.width), static_cast<int32_t>(src.height)},
              {1.0f / static_cast<float>(extent_.width), 1.0f / static_cast<float>(extent_.height)},
              flags,
              extra};
    vkCmdPushConstants(cmd, p.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, &push);
    vkCmdDispatch(cmd, (size.width + 7) / 8, (size.height + 7) / 8, 1);
}

void Motion::record_shared(VkCommandBuffer cmd, uint64_t n) const {
    const int p = static_cast<int>(n % 2);
    dispatch(cmd, kLuma, sets_[kSetLuma][p], lum1_[p].extent, extent_, 0, 0);
    compute_barrier(cmd);
    dispatch(cmd, kDown, sets_[kSetDown1][p], lum2_[p].extent, lum1_[p].extent, 0, 0);
    compute_barrier(cmd);
    dispatch(cmd, kDown, sets_[kSetDown2][p], lum3_[p].extent, lum2_[p].extent, 0, 0);
}

void Motion::record_generate(VkCommandBuffer cmd, uint64_t n) const {
    const int p = static_cast<int>(n % 2);
    auto packed = [](VkExtent2D e) { return static_cast<int>(e.width | (e.height << 16)); };
    compute_barrier(cmd);  // (this frame's levels, from record_shared)
    dispatch(cmd, kSearch, sets_[kSetSearch][p], field3_.extent, lum3_[p].extent, 0, 0);
    compute_barrier(cmd);
    dispatch(cmd, kRefine, sets_[kSetRefine2][p], field2_.extent, lum2_[p].extent, 0, packed(field3_.extent));
    compute_barrier(cmd);
    dispatch(cmd, kRefine, sets_[kSetRefine1][p], field1_.extent, lum1_[p].extent, 1, packed(field2_.extent));
    compute_barrier(cmd);
    dispatch(cmd, kMedian, median_set_, field_.extent, field_.extent, 0, 0);
    compute_barrier(cmd);
    dispatch(cmd, kWarp, sets_[kSetWarp][p], extent_, field_.extent, 0, 4);
}

}  // namespace nvn::lsfg
