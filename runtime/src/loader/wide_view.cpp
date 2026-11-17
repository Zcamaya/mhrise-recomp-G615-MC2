#include "loader/wide_view.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "nnsdk/ini.h"

namespace wide_view {
namespace {

// MH Rise 16.0.1: where the scene view hands its width / height to the main camera as the aspect ratio
// (fdiv s0, s11, s9 and then str s0, [x8, #0x44]). The PC "any aspect ratio" mods replace the same division.
constexpr uint32_t kSite = 0x74c1ff8;
constexpr uint32_t kSiteCode = 0x1E291960;  // fdiv s0, s11, s9
constexpr uint32_t kNextCode = 0xBD004500;  // str s0, [x8, #0x44]

uint32_t branch(const void* from, const void* to) {
    const intptr_t delta = (reinterpret_cast<intptr_t>(to) - reinterpret_cast<intptr_t>(from)) / 4;
    return 0x14000000 | (static_cast<uint32_t>(delta) & 0x3FFFFFF);
}

}  // namespace

bool install(Module& mod, std::string& err) {
    (void)err;
    const float ratio = nnsdk::ini::number("wide_view");
    if (ratio <= 16.0f / 9.0f + 0.01f || ratio > 3.6f) return true;  // (no file, or not wider than the game's own)
    const bool in_text = kSite >= mod.seg_off[0] && kSite + 8 <= mod.seg_off[0] + mod.seg_size[0];
    auto* site = reinterpret_cast<uint32_t*>(mod.base + kSite);
    if (!in_text || site[0] != kSiteCode || site[1] != kNextCode) {
        printf("[wide view] not the known main (main+%#x): off\n", kSite);
        return true;
    }
    // In the debug probes' corner of the patch area, past the stubs they can use (64 of 32 bytes after a 16 byte veneer).
    auto* stub = reinterpret_cast<uint32_t*>(mod.patch_area + Module::kPatchAreaSize - 64);
    stub[0] = 0x1C000040;  // ldr s0, [pc, #8]: the ratio below
    stub[1] = branch(&stub[1], site + 1);
    memcpy(&stub[2], &ratio, sizeof ratio);
    site[0] = branch(site, stub);
    printf("[wide view] camera at %.3f:1 instead of the scene's 16:9\n", ratio);
    return true;
}

}  // namespace wide_view
