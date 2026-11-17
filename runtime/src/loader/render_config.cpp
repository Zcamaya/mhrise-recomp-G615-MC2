#include "loader/render_config.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace render_config {
namespace {

// get_MotionBlurEnable, get_DepthOfFiledEnable (sic), get_GodRayEnable, get_BloomEnable, get_LensFlareEnable,
// get_ChromaticAberration, get_FilmGrainEnable: each "ldrb w0, [x0, #field]; ret", called only from their reflection
// thunks (none is shared with another type's getter).
struct Getter {
    uint32_t at, code;
};
constexpr Getter kGetters[] = {
    {0x78a0f84, 0x3942C800}, {0x78a0f98, 0x3942CC00}, {0x78a1168, 0x3942D000}, {0x78a117c, 0x3942D400},
    {0x78a1184, 0x3942D800}, {0x78a1198, 0x3942DC00}, {0x78a11ac, 0x3942E000},
};
constexpr uint32_t kRet = 0xD65F03C0;
constexpr size_t kStubBytes = 32;
constexpr size_t kStubsFromEnd = 512;  // the end of the patch area, past what the debug probes use

volatile uint64_t g_slot = 0;  // writable memory: the patch area is read-only once it's set up

uint32_t branch(const void* from, const void* to) {
    const intptr_t delta = (reinterpret_cast<intptr_t>(to) - reinterpret_cast<intptr_t>(from)) / 4;
    return 0x14000000 | (static_cast<uint32_t>(delta) & 0x3FFFFFF);
}

}  // namespace

bool install(Module& mod, std::string& err) {
    (void)err;
    for (const Getter& g : kGetters) {
        if (g.at + 8 > mod.seg_off[0] + mod.seg_size[0]) return true;
        const auto* site = reinterpret_cast<const uint32_t*>(mod.base + g.at);
        if (site[0] != g.code || site[1] != kRet) {
            printf("[render config] main+%#x isn't the known getter: not catching it\n", g.at);
            return true;
        }
    }
    uint8_t* end = mod.patch_area + Module::kPatchAreaSize;
    for (size_t k = 0; k < std::size(kGetters); k++) {
        auto* site = reinterpret_cast<uint32_t*>(mod.base + kGetters[k].at);
        auto* stub = reinterpret_cast<uint32_t*>(end - kStubsFromEnd + k * kStubBytes);
        stub[0] = 0x58000090;          // ldr x16, [pc, #16]: g_slot's address below
        stub[1] = 0xF9000200;          // str x0, [x16]
        stub[2] = site[0];             // the getter's own ldrb
        stub[3] = kRet;                // ret
        const uint64_t at = reinterpret_cast<uint64_t>(&g_slot);
        memcpy(&stub[4], &at, sizeof at);
        site[0] = branch(site, stub);
    }
    printf("[render config] catching the game's RenderConfig from %zu getters\n", std::size(kGetters));
    return true;
}

const volatile uint64_t* slot() { return &g_slot; }

}  // namespace render_config
