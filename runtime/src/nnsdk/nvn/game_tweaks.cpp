// Graphics settings that live in the game itself (via.render.RenderConfig and friends), for MH Rise 16.0.1 only.
// The PC version shows these in its options menu; the Switch build has the same code and just never changes them.
// Everything is found through the engine's reflection table (the get_/set_ names RenderConfig registers) and the
// setters it points to:
//   Shadow quality   set_ShadowQuality (main+0x78a0c58) picks the shadow map size and calls the shadow system's
//                    setter (main+0x7564f1c, size at +348) on *(main+0x12950580). 384/512/768/1024/2048 on PC.
//   Model detail     set_MeshQuality (main+0x78a0d68) stores a float on *(main+0x12950568): +60 or +64 depending on
//                    the LOD mode (main+0x7467ac8 / 0x7467ad0 are those two stores).
//   Texture detail   set_TextureLoadLevelBias (main+0x78a10d8): Renderer+6728, mip levels the streamer skips (0..12).
// Each one is read once from the game as its default, changed while its switch is on and put back when it's off.
// Called from the thread that presents, once per frame: the game calls these setters from its own threads too, and
// the shadow setter only stores the new size and lets the engine rebuild the maps.
// Before touching anything the instructions at those addresses are checked, so another build of main is left alone.
// What's in effect goes to <datos>/game_settings.txt (one line per setting) whenever it changes, for the settings menu.
// Switches (live): shadow_half, shadow_quarter / model_detail_low, model_detail_lowest, model_detail_high /
// texture_bias_1, texture_bias_2.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "nnsdk/nvn/commands.h"
#include "nnsdk/nvn/flags.h"
#include "nnsdk/nvn/nvn.h"
#include "nnsdk/runtime.h"

namespace nvn {

namespace {

constexpr size_t kRendererPointer = 0x12950538;  // via.render.Renderer*
constexpr size_t kShadowPointer = 0x12950580;    // the shadow system (what set_ShadowQuality talks to)
constexpr size_t kLodPointer = 0x12950568;       // mesh LOD state (set_MeshQuality, set_ShaderLodGlobalValue)

constexpr size_t kShadowSetter = 0x7564f1c;  // void (shadows*, uint32_t size)
constexpr size_t kShadowSize = 348;
constexpr size_t kLodRate0 = 60, kLodRate1 = 64;  // float, LOD mode 0 and 1
constexpr size_t kTextureBias = 6728;             // uint32_t, Renderer

struct Check {
    size_t at;
    uint32_t code;
};
constexpr Check kChecks[] = {
    {0x7564f1c, 0xF81E0FF3},  // str x19, [sp, #-32]!     (shadow size setter)
    {0x7564f28, 0xB9415C08},  // ldr w8, [x0, #348]
    {0x7564f40, 0xB9015C01},  // str w1, [x0, #348]
    {0x78a0c68, 0xF942C113},  // ldr x19, [x8, #1408]       (set_ShadowQuality loads the shadow system)
    {0x7467ac8, 0xBD003C00},  // str s0, [x0, #60]         (LOD rate, mode 0)
    {0x7467ad0, 0xBD004000},  // str s0, [x0, #64]         (LOD rate, mode 1)
    {0x78a0d90, 0xF942B508},  // ldr x8, [x8, #1384]        (set_MeshQuality loads the LOD state)
    {0x78a10dc, 0xF9429D08},  // ldr x8, [x8, #1336]        (set_TextureLoadLevelBias loads the Renderer)
    {0x78a10ec, 0xB91A4909},  // str w9, [x8, #6728]
};

const uint8_t* known_main() {
    static const uint8_t* base = [] () -> const uint8_t* {
        const uint8_t* b = nnsdk::host().main_base;
        if (!b || nnsdk::host().main_size <= kShadowPointer + 8) return nullptr;
        for (const Check& c : kChecks) {
            uint32_t w;
            memcpy(&w, b + c.at, 4);
            if (w != c.code) {
                NVN_LOG("game settings: main+%#zx doesn't match 16.0.1, leaving the game's settings alone\n", c.at);
                return nullptr;
            }
        }
        NVN_LOG("game settings: MH Rise 16.0.1 (shadows, model detail and texture detail available)\n");
        return b;
    }();
    return base;
}

template <typename T>
T* object_at(const uint8_t* base, size_t pointer) {
    const uint64_t p = *reinterpret_cast<const volatile uint64_t*>(base + pointer);
    return p ? reinterpret_cast<T*>(p) : nullptr;
}

// Divides the game's own shadow map size (the Switch default isn't the PC's 2048), never below 256
uint32_t wanted_shadow_divisor() {
    if (NVN_FLAG("shadow_quarter")) return 4;
    if (NVN_FLAG("shadow_half")) return 2;
    return 1;
}

float wanted_lod_scale() {
    if (NVN_FLAG("model_detail_lowest")) return 0.5f;
    if (NVN_FLAG("model_detail_low")) return 0.75f;
    if (NVN_FLAG("model_detail_high")) return 1.5f;
    return 1.0f;
}

uint32_t wanted_texture_bias() {
    if (NVN_FLAG("texture_bias_2")) return 2;
    if (NVN_FLAG("texture_bias_1")) return 1;
    return 0;
}

// What the game has right now, for game_settings.txt
struct Status {
    uint32_t shadow = 0, shadow_game = 0;
    float lod = 0, lod_game = 0, lod_scale = 1;
    uint32_t bias = 0, bias_game = 0;
    bool shadow_seen = false, lod_seen = false, bias_seen = false;
};
Status g_status;

void write_status() {
    const Status& st = g_status;
    char text[512];
    int n = 0;
    if (st.shadow_seen)
        n += snprintf(text + n, sizeof text - n, "shadow %u %u\n", st.shadow, st.shadow_game);
    if (st.lod_seen)
        n += snprintf(text + n, sizeof text - n, "model %.3f %g %g\n", st.lod_scale, st.lod, st.lod_game);
    if (st.bias_seen) n += snprintf(text + n, sizeof text - n, "texture %u %u\n", st.bias, st.bias_game);
    static std::string last;
    if (last == text) return;
    last = text;
    const char* data = getenv("NIER_DATA");
    const std::string path = std::string(data ? data : ".") + "/game_settings.txt";
    if (FILE* f = fopen(path.c_str(), "w")) {
        fputs(text, f);
        fclose(f);
    }
}

void shadows(const uint8_t* base) {
    uint8_t* sys = object_at<uint8_t>(base, kShadowPointer);
    if (!sys) return;
    auto* size = reinterpret_cast<volatile uint32_t*>(sys + kShadowSize);
    static uint32_t game_size = 0;  // what the game had before we touched it
    static uint32_t ours = 0;       // the last size we set (0: none)
    if (!*size) return;  // not set up yet
    if (!ours || *size != ours) game_size = *size;  // the game changed it (a load, a cutscene): that's the new default
    const uint32_t divisor = wanted_shadow_divisor();
    const uint32_t target = divisor > 1 ? std::max<uint32_t>(game_size / divisor, 256) : game_size;
    g_status.shadow_seen = true;
    g_status.shadow = *size, g_status.shadow_game = game_size;
    if (*size == target) return;
    using Setter = void (*)(void*, uint32_t);
    reinterpret_cast<Setter>(const_cast<uint8_t*>(base) + kShadowSetter)(sys, target);
    NVN_LOG("game settings: shadow maps %u -> %u (game default %u)\n", ours ? ours : game_size, target, game_size);
    ours = divisor > 1 ? target : 0;
    g_status.shadow = *size;
}

void model_detail(const uint8_t* base) {
    uint8_t* lod = object_at<uint8_t>(base, kLodPointer);
    if (!lod) return;
    auto* rate = reinterpret_cast<volatile float*>(lod + kLodRate0);
    auto* rate1 = reinterpret_cast<volatile float*>(lod + kLodRate1);
    static float game[2] = {0, 0}, ours[2] = {0, 0};
    for (int k = 0; k < 2; k++) {
        volatile float* r = k ? rate1 : rate;
        if (ours[k] == 0.0f || *r != ours[k]) game[k] = *r;
    }
    const float scale = wanted_lod_scale();
    static float logged = 1.0f;
    for (int k = 0; k < 2; k++) {
        volatile float* r = k ? rate1 : rate;
        const float target = game[k] * scale;
        if (*r != target) *r = target;
        ours[k] = scale != 1.0f ? target : 0.0f;
    }
    g_status.lod_seen = true;
    g_status.lod = *rate, g_status.lod_game = game[0], g_status.lod_scale = scale;
    if (scale != logged) {
        NVN_LOG("game settings: model detail x%.2f (LOD rates %g / %g -> %g / %g)\n", scale, game[0], game[1],
                game[0] * scale, game[1] * scale);
        logged = scale;
    }
}

void texture_detail(const uint8_t* base) {
    uint8_t* renderer = object_at<uint8_t>(base, kRendererPointer);
    if (!renderer) return;
    auto* bias = reinterpret_cast<volatile uint32_t*>(renderer + kTextureBias);
    static uint32_t game = 0, ours = ~0u;
    if (ours == ~0u || *bias != ours) game = *bias;
    const uint32_t want = wanted_texture_bias();
    const uint32_t target = want ? std::min<uint32_t>(game + want, 12) : game;
    if (*bias != target) {
        *bias = target;
        NVN_LOG("game settings: texture streaming skips %u mip levels (game default %u)\n", target, game);
    }
    ours = want ? target : ~0u;
    g_status.bias_seen = true;
    g_status.bias = *bias, g_status.bias_game = game;
}

}  // namespace

void game_tweaks() {
    const uint8_t* base = known_main();
    if (!base) return;
    shadows(base);
    model_detail(base);
    texture_detail(base);
    write_status();
}

}  // namespace nvn
