<p align="center"><img src="docs/icon.png" width="128" alt=""></p>

# Monster Hunter Rise - Dimensity 7400U Port (TECNO CAMON 50 Ultra)

A device-focused Android port of the Switch version of **Monster Hunter Rise** for the **TECNO CAMON 50 Ultra** (MediaTek Dimensity 7400 Ultimate / Mali-G615 MC2). It runs the game's ARM64 code natively on Android and translates the console NVN graphics API to Vulkan.

> **Free, unofficial fan project.** Not affiliated with, endorsed, or sponsored by Nintendo or Capcom. All trademarks belong to their owners. This repository contains **no game code and no game assets**. You must use files extracted from your own legally purchased copy of the game.

This fork is based on [IamWilmar's mhrise-recomp](https://github.com/IamWilmar/mhrise-recomp) and focuses on stability/smoothness on Mali-G615 MC2.

## Target Device

- **Device:** TECNO CAMON 50 Ultra
- **SoC:** MediaTek Dimensity 7400 Ultimate
- **GPU:** Mali-G615 MC2
- **Target:** stable ~30 FPS with controlled resolution/thermals

Other devices/GPUs are not the focus of this fork.

## Current Status

Playable on the target device, with ongoing performance tuning. Expect first-visit shader compilation stutter; shader caches improve subsequent runs.

### Device-focused fixes/features in this fork

- **Mali-G615 read_vertex_stream stall fix:** Prevents input-triggered staging snapshot churn by mirroring eligible vertex ranges (including staging-chunk boundary ranges) and falling back to GPU mirrors for dynamic/uncached known ranges before staging snapshots. Adds a conservative second mirror-eviction pass for oldest non-current-frame mirrors when mirror budget is exhausted.
- **Background BRX compute spirv-opt deferral (1.0.121-inspired):** BRX compute shaders can bind plain translations immediately while optimized shaders are built in the background and swapped in when ready.
- **Mali-G615 warm-up/cache tuning:** Existing warm-up, driver pipeline cache, and shader preload options are kept for the target device; behavior may vary by driver/session.

See [docs/fixes.md](docs/fixes.md) for technical details.

## What You Need

- **Monster Hunter Rise v16.0.1** files extracted from your own copy as plain folders:
  - **ExeFS** with update 16.0.1 applied (`main` build ID: `55d50ca1805e9c5bba508d601add852f255a32ba`)
  - **RomFS** with the same update applied
- **Optional DLC/Sunbreak:** extracted RomFS folders per DLC title ID.
- Enough free space for game data, shader cache, saves, and logs (about 1 GB extra recommended).

The app reads plain extracted folders; it does not include game files.

## Install and First Run

1. Build or install the APK for this fork.
2. Copy your extracted ExeFS/RomFS (and optional DLC) folders to the device.
3. Open the app, grant **All files access**, and select the game folder.
4. Select a **data folder** for saves, shader cache, `log.txt`, and settings.
5. Press **Start game**.

Saves, shader cache, and logs are stored in the selected data folder.

## Controls and Settings

- Touch controls, controller support, and in-game settings panel are available.
- For debugging/perf tuning on this device, use `perf_stats` and logcat/log file diagnostics.
- See [docs/settings.md](docs/settings.md) and [docs/debugging.md](docs/debugging.md).

## Notes

- This fork is intentionally scoped to Dimensity 7400U / Mali-G615 MC2 behavior.
- Temporary diagnostics may still exist in development builds while investigating stalls.
- Do not share copyrighted game files, keys, or extracted assets.

## Credits / Legal

- Based on [IamWilmar/mhrise-recomp](https://github.com/IamWilmar/mhrise-recomp).
- Upstream/third-party notices: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- License: [LICENSE](LICENSE).

For technical fix history, see [docs/fixes.md](docs/fixes.md).
