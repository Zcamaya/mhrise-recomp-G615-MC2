#pragma once
// The port's own frame generator: no Lossless.dll, nothing past Vulkan 1.1 (compute shaders, storage images of the
// formats every driver has: R32F, RGBA32F, RGBA8). What framegen.cpp uses when there's no DLL, or when Lossless's
// chain can't be built on this GPU (an old driver), and the menu's choice for it.
//
// Per real frame n (record_shared): its luma at 1/4, 1/8 and 1/16 of the picture, kept for the next pair. The picture
// between n - 1 (A) and n (B) (record_generate):
//   1. at 1/16, a vector per 4x4 texels (64x64 pixels) by a full search of +-3 texels: what's at p in the picture
//      between was at p - d in A and is at p + d in B;
//   2. at 1/8 and 1/4, each block takes the best of its coarser neighbours' vectors (doubled), their average and no
//      motion (and at 1/4 its own from the last picture made, so the field stays steady), then the best one texel step
//      around it; at 1/4 a parabola puts it below a texel;
//   3. a 3x3 median of the field;
//   4. each pixel may pick its own vector (of its four nearest blocks' and none, then a half pixel step, only if one
//      matches A at x - d and B at x + d clearly better than the field's mix), so edges stay sharp without busy
//      textures shimmering; bicubic samples, so the made picture is as sharp as the real ones; A and B averaged where
//      they agree, else B's sample alone; a pixel the same in A and B (the HUD, still scenery) keeps B's, and where no block
//      matched (a cut, a flash) B is shown.
// Shaders: shaders/fg_*.comp (tools/shaders.sh). A build whose spirv.h doesn't have them yet can't make one
// (available() false): the presenter goes on with LSFG alone.

#include <string>
#include <vector>

#include "nnsdk/nvn/lsfg/generator.h"

namespace nvn::lsfg {

class Motion final : public Generator {
public:
    static bool available();

    Motion() = default;
    Motion(const Motion&) = delete;
    Motion& operator=(const Motion&) = delete;
    ~Motion() override { destroy(); }

    // For a picture of that size and format (the window's). false with the reason (and nothing made) if it fails.
    bool create(VkExtent2D extent, VkFormat format, std::string* why);
    void destroy();

    const char* name() const override { return "built-in"; }
    bool initialized() const override { return initialized_; }
    void record_init(VkCommandBuffer cmd) override;
    const Image& input(uint64_t n) const override { return frames_[n % 2]; }
    const Image& output() const override { return output_; }
    void record_shared(VkCommandBuffer cmd, uint64_t n) const override;
    void record_generate(VkCommandBuffer cmd, uint64_t n) const override;
    VkExtent2D extent() const override { return extent_; }
    VkFormat format() const override { return format_; }
    float flow() const override { return 0.25f; }  // (the field's finest level: a quarter of the picture)
    size_t images() const override { return all_.size(); }
    VkDeviceSize bytes() const override { return bytes_; }
    uint32_t sets() const override { return kSets; }

private:
    enum Pass { kLuma, kDown, kSearch, kRefine, kMedian, kWarp, kPasses };
    struct Pipeline {
        VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
    };
    // The sets, each for the parity of B's frame (n % 2): luma, down 1/4 -> 1/8, down 1/8 -> 1/16, search, refine at
    // 1/8, refine at 1/4, warp; and the median's one
    enum Set { kSetLuma, kSetDown1, kSetDown2, kSetSearch, kSetRefine2, kSetRefine1, kSetWarp, kSetKinds };
    static constexpr uint32_t kSets = kSetKinds * 2 + 1;

    void add(Image& image, VkExtent2D extent, VkFormat format, VkFormat view_format, VkImageUsageFlags usage,
             VkImageCreateFlags flags = 0);
    bool build_pipelines(std::string* why);
    bool build_images(std::string* why);
    bool build_sets(std::string* why);
    void dispatch(VkCommandBuffer cmd, Pass pass, VkDescriptorSet set, VkExtent2D size, VkExtent2D src, int flags,
                  int extra) const;

    VkExtent2D extent_{};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    bool initialized_ = false;
    Pipeline passes_[kPasses];
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet sets_[kSetKinds][2]{}, median_set_ = VK_NULL_HANDLE;
    std::vector<Image*> all_;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize bytes_ = 0;
    mutable uint64_t made_ = 0;  // pictures made (record_generate): the first has no last field to start from

    Image frames_[2], lum1_[2], lum2_[2], lum3_[2];  // the pictures and their luma levels (1/4, 1/8, 1/16), by parity
    Image field3_, field2_, field1_, field_;         // the vectors at 1/16, 1/8, 1/4 and the median's
    Image output_;
};

}  // namespace nvn::lsfg
