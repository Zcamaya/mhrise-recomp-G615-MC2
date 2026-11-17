#include "nnsdk/nvn/astc.h"

#include <pthread.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "astcenc.h"
#include "nnsdk/ini.h"
#include "nnsdk/nvn/flags.h"
#include "nnsdk/nvn/vk.h"

namespace nvn::astc {

namespace {

// Encoder threads per level. The upload waits for the level, so more threads mean a shorter freeze the first time a
// texture loads; four leaves the game's own threads some room on an 8-core phone.
constexpr unsigned kThreads = 4;

// One context per colour profile, made the first time it's needed (each holds about 1 MB of tables).
struct Encoder {
    std::mutex m;
    astcenc_context* ctx = nullptr;
    bool tried = false;
};
Encoder g_encoders[2];  // [srgb]

astcenc_context* context(Encoder& e, bool srgb) {
    if (e.tried) return e.ctx;
    e.tried = true;
    astcenc_config config;
    if (astcenc_config_init(srgb ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_LDR, 4, 4, 1, ASTCENC_PRE_FASTEST, 0, &config) !=
            ASTCENC_SUCCESS ||
        astcenc_context_alloc(&config, kThreads, &e.ctx, nullptr) != ASTCENC_SUCCESS) {
        e.ctx = nullptr;
        NVN_LOG("ASTC: the encoder didn't start (%s); BC textures stay RGBA8\n", srgb ? "sRGB" : "linear");
    }
    return e.ctx;
}

// In the app's own files folder by default (/data/data/<app>/files, NIER_INTERNO): no FUSE layer like the shared storage
// the data folder lives in, which costs time on every file opened, and a level is a file. <data>/astc_cache_in_data_folder
// (at start-up): in the data folder, as before (it survives reinstalling and can be copied to another phone).
std::string cache_dir() {
    static const std::string dir = [] {
        const char* app = getenv("NIER_INTERNO");
        const bool own = app && *app && !NVN_FLAG("astc_cache_in_data_folder");
        std::string d = (own ? std::string(app) : nnsdk::ini::data_dir()) + "/astc_cache";
        mkdir(d.c_str(), 0755);
        return d;
    }();
    return dir;
}

std::string cache_file(uint64_t key) {
    char name[32];
    snprintf(name, sizeof name, "/%016llx.astc", static_cast<unsigned long long>(key));
    return cache_dir() + name;
}

// The writer: one thread, lowest priority, so the SD card isn't busy while the game reads its own files (spirv.bin
// once saturated it and the game fell to 1 fps). At most 64 MB waiting; past that, levels just aren't cached this time.
std::mutex g_write_m;
std::condition_variable g_write_cv;
std::deque<std::pair<uint64_t, std::vector<uint8_t>>> g_writes;
size_t g_write_bytes = 0;

void writer() {
    pthread_setname_np(pthread_self(), "ASTCCache");
    setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), 19);
    for (;;) {
        std::pair<uint64_t, std::vector<uint8_t>> job;
        {
            std::unique_lock lock(g_write_m);
            g_write_cv.wait(lock, [] { return !g_writes.empty(); });
            job = std::move(g_writes.front());
            g_writes.pop_front();
            g_write_bytes -= job.second.size();
        }
        const std::string path = cache_file(job.first), tmp = path + ".tmp";
        if (FILE* f = fopen(tmp.c_str(), "wb")) {
            const bool ok = fwrite(job.second.data(), 1, job.second.size(), f) == job.second.size();
            fclose(f);
            if (ok) rename(tmp.c_str(), path.c_str());
            else unlink(tmp.c_str());
        }
    }
}

}  // namespace

bool wanted() {
    static const bool yes = [] {
        if (!NVN_FLAG("bc_astc")) return false;
        VkContext* c = vk();
        if (!c) return false;
        for (VkFormat f : {VK_FORMAT_ASTC_4x4_UNORM_BLOCK, VK_FORMAT_ASTC_4x4_SRGB_BLOCK}) {
            VkFormatProperties fp;
            vkGetPhysicalDeviceFormatProperties(c->gpu, f, &fp);
            if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
                NVN_LOG("ASTC: this GPU doesn't sample ASTC 4x4; BC textures stay RGBA8\n");
                return false;
            }
        }
        NVN_LOG("ASTC: BC1/BC2/BC3/BC7 textures stored as ASTC 4x4 (cache in %s)\n", cache_dir().c_str());
        return true;
    }();
    return yes;
}

bool encode(const uint8_t* rgba, uint32_t width, uint32_t height, bool srgb, uint8_t* out) {
    Encoder& e = g_encoders[srgb ? 1 : 0];
    std::lock_guard lock(e.m);
    astcenc_context* ctx = context(e, srgb);
    if (!ctx || !width || !height) return false;
    void* slices[1] = {const_cast<uint8_t*>(rgba)};
    astcenc_image image{};
    image.dim_x = width;
    image.dim_y = height;
    image.dim_z = 1;
    image.data_type = ASTCENC_TYPE_U8;
    image.data = slices;
    const astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};
    const size_t size = size_t{(width + 3) / 4} * ((height + 3) / 4) * 16;
    // Small levels on this thread alone; bigger ones split between kThreads (the context divides the blocks).
    const unsigned threads = size_t{width} * height >= 128 * 128 ? kThreads : 1;
    std::vector<astcenc_error> result(threads, ASTCENC_SUCCESS);
    std::vector<std::thread> workers;
    for (unsigned k = 1; k < threads; k++)
        workers.emplace_back([&, k] { result[k] = astcenc_compress_image(ctx, &image, &swizzle, out, size, k); });
    result[0] = astcenc_compress_image(ctx, &image, &swizzle, out, size, 0);
    for (std::thread& w : workers) w.join();
    astcenc_compress_reset(ctx);
    for (astcenc_error r : result)
        if (r != ASTCENC_SUCCESS) {
            static int logged = 0;
            if (logged++ < 4) NVN_LOG("ASTC: encoding a %ux%u level failed (%s)\n", width, height, astcenc_get_error_string(r));
            return false;
        }
    return true;
}

bool load(uint64_t key, size_t size, uint8_t* out) {
    FILE* f = fopen(cache_file(key).c_str(), "rb");
    if (!f) return false;
    const bool ok = fread(out, 1, size, f) == size && fgetc(f) == EOF;
    fclose(f);
    return ok;
}

void store(uint64_t key, const uint8_t* data, size_t size) {
    static std::once_flag started;
    std::call_once(started, [] { std::thread(writer).detach(); });  // lives as long as the process
    {
        std::lock_guard lock(g_write_m);
        if (g_write_bytes + size > (64u << 20)) return;
        g_writes.emplace_back(key, std::vector<uint8_t>(data, data + size));
        g_write_bytes += size;
    }
    g_write_cv.notify_one();
}

}  // namespace nvn::astc
