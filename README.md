<p align="center"><img src="docs/icon.png" width="128" alt=""></p>

# Monster Hunter Rise port for Retroid Pocket 5 & Flip 2

**Made for the Adreno 650.** A native Android port of the console version of **Monster Hunter Rise**, built exclusively for the
**Retroid Pocket 5** and the **Retroid Pocket Flip 2** (Snapdragon 865), from **your own copy** of the game. The game's
own ARM64 code runs directly on the device's CPU, while this project reimplements the parts of the console it talks to.

> **Free, unofficial fan project.** Not affiliated with, endorsed or sponsored by the console maker or
> Capcom. All trademarks belong to their owners. This repository contains **no game code and no game assets**. To use
> it you need a copy of the game that you bought, with its files extracted on your own console.

## Status

**Release 1.2.0 — playable.** Kamura village and hunts run at a locked 30 fps: when a scene gets too heavy the
port lowers the game's internal resolution for a moment (dynamic resolution) instead of dropping frames. Upscaled to
1080p with AMD FSR 1. Sunbreak and the other DLC load if you provide them.

- First visits to new areas can hitch while shaders are compiled; it gets smoother as the shader cache fills up.
- Known issues: occasional speckles on the first loading screen after choosing a save; some effects may look wrong.

## Why only the Retroid Pocket 5 and Flip 2

This port was written for one chip, the **Snapdragon 865 with its Adreno 650 GPU**, and tuned to its limits instead of
trying to run everywhere:

- **Run the game, not a whole console.** Translating the full console system costs CPU, GPU and memory this chip does not
  have to spare. Here the game's own ARM64 code runs natively and only what it actually calls (files, threads, input,
  audio, graphics) is reimplemented, so almost the whole device is left for the game.
- **Built around the Adreno 650 and its stock Qualcomm Vulkan driver** (Vulkan 1.1). The graphics layer uses exactly the
  features this driver offers and works around its quirks; for example, GPU shader code that relies on features the
  Adreno 650 only has in compute shaders is rewritten so it renders correctly.
- **A 33 ms budget per frame.** The GPU usually runs at ~587 MHz, so the target is a **locked 30 fps**: the game renders
  at its handheld resolution (around 720p) and AMD FSR 1 upscales it to the 1080p screen. Where a game can get heavier
  than that budget, the internal resolution drops for a moment instead of the frame rate.
- **About 2.7 GB of free memory** out of 8 GB while a game runs. GPU caches have fixed limits so a big area never
  pushes the game out of memory, and translated shaders are stored on the device so each one is only built once.
- **Eight cores, used deliberately.** The game's threads run on the four big Cortex-A77 cores; background work (shader
  building, pipeline compiling) goes to the small Cortex-A55 cores so it does not steal time from the game.
- **Optional frame generation** to show ~60 fps, only when the GPU has room left after the real 30.

Developed and tested on the **Retroid Pocket Flip 2** (Android 13). The **Retroid Pocket 5** has the same chip, memory and
driver, so it should behave the same. Other Snapdragon 865 / Adreno 650 devices might work but are untested.

**Other phones (experimental, untested).** The port now also tries to run on other GPUs and on weaker phones:

- **Mali (MediaTek, Exynos) and PowerVR** can't read the BC texture formats the game uses. On those, each texture is
  decompressed on the CPU as it loads (BC1 to BC7, checked against a reference decoder) and kept uncompressed. Loading
  takes longer and textures use 4 to 8 times the memory.
- **PowerVR:** pipelines are built on one thread, because its shader compiler crashes building several at once.
- **GPUs without multi-viewport** get shaders that don't ask for it.
- **Phones with less than 5 GB of RAM** keep smaller caches (spare staging and buffer mirrors at half).
- Other Adreno generations (6xx, 7xx) use the same Qualcomm paths as the Adreno 650.
- **A GPU driver of your own (Adreno only).** If the stock driver draws parts of the game black or refuses some of its
  shaders (seen on an Adreno 730), *GPU driver* (in the in-game settings, under Graphics, or on the start screen) adds a
  driver from a `.zip` in the usual AdrenoTools format (`meta.json` plus the `.so`, e.g. a Turnip build) and uses it from
  the next start, through [libadrenotools](https://github.com/bylaws/libadrenotools). If the game can't draw with it, it
  goes back to the system's driver by itself and the screen says why.

None of this has been tested on such a device yet: the log (`log.txt` in the game folder) says what the port chose.

## What you need

- **The game files of version 16.0.1**, already extracted on your own console, as plain folders:
  - **ExeFS** — the folder with `main`, `main.npdm`, `sdk`, … of the game *with update 16.0.1 applied*
    (the app checks the build ID of `main`: `55d50ca1805e9c5bba508d601add852f255a32ba`).
  - **RomFS** — the game's RomFS with the same update applied.
- **DLC (optional, e.g. Sunbreak):** the RomFS of each DLC you own, each in its own folder. Keep the DLC title ID in the
  folder name as your extraction tool named it (for example `... (0100B04011743001) ...`): that is how the app tells the DLC
  apart. Folders named `aoc/<index>` also work.
- Enough free space for the game plus about 1 GB for the shader cache and saves (internal storage or SD card).

**Where do the files come from?** From your own copy of the game: if your console runs custom firmware, its homebrew
tools can extract the ExeFS and RomFS for you. The app only reads those plain folders (it doesn't open packaged or
protected files), which keeps it simple, and it will tell you exactly what is missing if a folder isn't usable. Questions
about the app itself are always welcome in the issues; just please don't share or ask for game files there.

## Install and first run

1. Download `mhriseport-vX.Y.Z.apk` from the [latest release](https://github.com/IamWilmar/mhrise-recomp/releases/latest) (or build
   it yourself, below) and install it on the device.
2. Copy your extracted game folders to the device, anywhere you like (for example one folder `MHRise` with the ExeFS
   and RomFS folders inside).
3. Open the app, grant *All files access*, browse to that folder with the touch screen or the d-pad and choose
   **Use this folder**. The app checks that the files are this game, the right version and not encrypted.
4. Press **Start game**. From then on the app starts the game directly; to choose another folder, switch on
   *Change folder* in the settings panel (see below) or open the app twice in a row (within ten seconds).

Saves, the shader cache and `log.txt` are written into that same folder. The first time you visit each place the game
may stutter while its shaders are translated and compiled; they are cached for the next time.

## Controls and the settings panel

- **The face buttons follow the device's controller mode.** In both of the Retroid's modes (Xbox and Retro) every button
  does what the button in that same place does on the game's original console: B at the bottom, A on the right, X on top,
  Y on the left. You can change the mode from the quick settings while the game is running. Any other controller, over
  USB or Bluetooth (a DualSense, for example), is read by position too: its bottom button is B.
- **Settings panel.** Swipe in from the right edge of the screen (the system's back gesture, the one that shows an arrow)
  and a panel slides out over the game. It has a switch for the FPS counter (your choice is remembered) and *Change
  folder*, which brings the folder screen back the next time you open the app. The same swipe, a tap outside the panel
  or a few seconds without touching it hide it again.
- **Touch controls.** A full controller on the screen, laid out like a traditional one: both sticks, the D-pad, A B X Y,
  L R ZL ZR, − and +, and L3 / R3 (also a short tap on a stick, without moving it). A finger can slide from one button to
  the next. Touches that land on no control still reach the settings panel above.
- **Controls menu.** The gear between − and + opens it:
  - *Touch:* on or off, opacity, size, vibration (off, light, medium, strong), sliding between buttons, the sticks' dead
    zone, and the tap for L3 / R3.
  - *Edit layout:* drag each control where you want it, change its size or hide it from the bar at the top, or put the
    traditional layout back.
  - *Combos:* up to four. Each presses several buttons at once (A+X, say), from its own button on the screen and, if you
    choose one, from a controller button or a pair of them (L3, −+R…).
  - *Controller:* the sticks' dead zone, how far an analog trigger travels before it is ZL / ZR, and where the right stick
    is read from. *By controller* picks Z/RZ or RX/RY from the axes each pad says it has, with triggers on LTRIGGER /
    RTRIGGER, BRAKE / GAS or Z/RZ, so Xbox, DualSense and generic pads all work. The log says what it chose for each pad.

## Options

The in-game settings menu (the gear of the touch controls) sets the common ones by section; choices with more than two options open a drop-down, and Display > Menu size scales the menu's text: **Display** (base
resolution, dynamic resolution, FSR, FPS limit, FPS counter), **Graphics** (shadows, texture arrays, anisotropic filtering, GPU driver),
**Performance** (performance hints, fast cores, low memory, shader warm-up, pipelines on one thread), and the touch
controls, combos and controller. Underneath, each is an empty file (or folder) with one of these names in the game
folder, which the game checks once a second; the ones only read at start-up say so.

| Name | Effect |
|---|---|
| `lsfg/` | Frame generation (Lossless Scaling's LSFG) to show ~60 fps. Needs **your own** `Lossless.dll` from the Windows app *Lossless Scaling* (sold on Steam), copied into that `lsfg` folder or to `Download/Lossless.dll`. Not included here. |
| `resolution_576`, `resolution_648`, `resolution_720` | Base resolution of the 3D scene: 1024x576, 1152x648 or 1280x720 instead of the game's 960x540 (720p fills the whole 720p picture, with no scaling inside the game). Live. |
| `no_dynamic_resolution` | Keeps the scene at the base resolution (no dynamic resolution). Live. |
| `no_fsr` | Plain linear scaling to the screen instead of FSR 1. Live. |
| `no_adpf` | No Android performance hints (Android 13+: the frame's threads report their work each frame so the system raises the clocks in time). Live. |
| `no_affinity` | Doesn't pin the frame's three threads to the fast cores (they're only pinned when the phone has at least three fast cores). Live. |
| `fps_20`, `fps_25`, `fps_40`, `fps_45` | FPS limit other than the game's 30 (`60fps` for 60). The game measures each frame's length, so it keeps its normal speed: a lower limit saves GPU, heat and battery without slow motion. Loading screens still run at 60. Live. |
| `no_shadows` | Shadows off: the shadow maps aren't drawn and every shadow lookup reads as lit. Live. |
| `shadow_half`, `shadow_quarter` | Shadow maps at half or a quarter of the game's own size (never below 256), through the game's own shadow setting (the PC version's shadow quality). Blurrier edges, much less GPU work. Live. MH Rise 16.0.1 only. |
| `model_detail_low`, `model_detail_lowest`, `model_detail_high` | Scales the game's mesh LOD rate (the PC version's mesh quality) by 0.75, 0.5 or 1.5. Live. MH Rise 16.0.1 only. |
| `texture_bias_1`, `texture_bias_2` | The texture streamer skips one or two more of the largest mip levels (the game's TextureLoadLevelBias). Live. MH Rise 16.0.1 only. |
| `60fps` | Unlocks 60 fps mode (the Flip 2 GPU reaches ~45 in the village, less in hunts). |
| `no_fps` | Hides the FPS counter (the same switch as in the settings panel). |
| `xbox_buttons`, `retro_buttons` | Force the face buttons to be read by position (`xbox_buttons`) or by name (`retro_buttons`) if a controller comes out swapped. |
| `bc_on_cpu` | Decompresses BC textures on the CPU even where the GPU reads them, the way Mali and PowerVR GPUs need (to try that path on an Adreno). |
| `short_subgroups`, `no_short_subgroups` | Subgroup operations on GPUs with subgroups under 32 (Mali). On by default with PanVK, which needs them for a complete picture (Eden's single-lane fallback loses quad derivatives); off with ARM's driver, where they stalled the GPU. These force one or the other. Read when shaders are translated. |
| `no_bindless_arrays`, `bindless_arrays` | Texture arrays in the shaders off or on (the menu's Graphics > Texture arrays). Auto, with neither: off with Qualcomm's own driver on an Adreno 7xx or 8xx (512.863 on an Adreno 730 rejected most of the scene's pixel shaders that index them, so things came out black), on elsewhere (Turnip, the Adreno 650). Read at start-up. |
| `no_spirv_opt_brx` | Graphics shaders that contain a switch (a BRX) are normally also passed through spirv-opt, as compute shaders always were: the control flow Eden builds for a switch is what Qualcomm's driver miscompiled in the hunter's face skinning, and what a Mali-G615 hung on every frame. This turns that off. Read at start-up. |
| `no_int_attribs` | Vertex attributes in integer formats (bone indices, IDs) are normally declared as integers in the vertex shader, so the shader receives their raw bits the way the Switch does. Mali GPUs converted them to floats when they were declared float, which made skinned characters stretch into spikes. This declares everything float again, as before. Read at start-up. |
| `keep_dead_functions` | Helper functions that a translated shader never calls are normally removed before the shader goes to the driver, along with the "variable pointers" capability they bring. On Qualcomm's own driver (512.863) every pipeline with such a pixel shader was rejected, which left characters black. This sends Eden's SPIR-V unchanged. Read at start-up. |
| `no_return_mali` | On Mali GPUs (ARM's driver and PanVK), the impossible branch Eden leaves after a switch is written as a plain return instead of an "unreachable", as on Qualcomm's driver, so the compiler can't make assumptions from it in the skinning shaders. This puts the "unreachable" back. Read at start-up. |
| `no_safe_translation` | Turns off the automatic retry for pipelines a driver rejects. Normally, when a driver refuses one (Qualcomm's own driver does with many of the scene's pixel shaders on newer Adrenos), its vertex and pixel shaders are translated again more conservatively, step by step (no NonUniform marking, then also optimized by spirv-opt, then also without subgroup operations), and the first that works is kept and remembered for the warm-up. |
| `breadcrumbs` | Debugging: GPU breadcrumbs. If the GPU hangs, or the driver aborts a stuck job, the log names the render pass or dispatch and its shaders. Everything on the GPU runs in order: slow. |
| `low_memory` | Smaller caches, as on a phone with less than 5 GB of RAM. |
| `no_af` | No anisotropic filtering: a little cheaper for a weak GPU, a little blurrier at a distance. |
| `single_thread_pipelines` | Builds every pipeline on the renderer's own thread, with no background threads or warm-up (on by itself on a PowerVR GPU, whose shader compiler crashes building several at once). `parallel_pipelines` turns that off. |
| `verbose_log` | Verbose log (line by line) plus a watchdog that prints the game's stack if it hangs. Slower: for debugging. |

## How it works

```
APK
├── SetupActivity (Java)   finds and checks the user's game files, then starts the native activity
└── NativeActivity
    ├── loader             maps main + subsdk* from the ExeFS, applies relocations, links imports,
    │                      patches the few instructions that cannot run on Android (svc, tpidrro_el0, counters)
    └── libnnsdk.so        reimplementation of the console SDK the game imports: nn::os (threads, sync, memory),
        │                  nn::fs (RomFS, saves), nn::hid (controller), audio (AAudio), codecs, …
        └── nvn/           the NVN graphics API on top of Vulkan; the game's Maxwell shaders are translated to
                           SPIR-V with Eden's shader recompiler, then cached
```

The SDK and NVN layers were written from scratch by observing what the game calls and with which values (there are no
official SDK headers or code in this repository); comments next to each deduced value give the evidence. Code comments
are mostly in Spanish.

## Building

Requirements (macOS or Linux): Android SDK with **NDK 28.2.13676358**, build-tools 35.0.0 and platform android-33; a JDK
(17 or newer); CMake ≥ 3.22; Python 3; git, curl and zip.

```bash
tools/setup.sh       # once: fetches the pinned third-party sources (Eden's shader recompiler, sirit, fmt, Boost…)
tools/build_apk.sh   # builds build/mhriseport.apk
adb install -r build/mhriseport.apk
```

`ANDROID_HOME` selects the SDK (default `~/Library/Android/sdk`). The APK is signed with the Android debug key unless you
pass `KEYSTORE=… KS_PASS=…`. `RELEASE=1` makes the build for players: `-O3` without debug info, unused code and duplicate
functions removed, symbols stripped, and an APK that isn't debuggable. Releases are built that way by the `release`
workflow when a `v*` tag is pushed, signed with the key in the `RELEASE_KEYSTORE_B64` and `RELEASE_KS_PASS` repository
secrets (alias `recomp`). For development, `tools/apk.sh` builds, installs, starts the game on the device connected by
adb and prints its log. The Python scripts in `tools/` are analysis helpers that work on your own extracted files in
`work/` (ignored by git).

## Support

Everything here is free and stays free: every build is public and the same for everyone, and donations do not unlock
anything. If this port saved you a weekend and you want to say thanks, you can buy me a coffee at
[ko-fi.com/iamwilmar](https://ko-fi.com/iamwilmar). It helps me keep working on these ports. Thank you!

## License

GPL-3.0-or-later (see [LICENSE](LICENSE)), because it builds on Eden's shader recompiler (GPL-3.0-or-later). Third-party
components and their licenses are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

---

## En español

Port nativo para Android de la versión de consola de **Monster Hunter Rise**. El código ARM64 del juego corre directo en la
CPU y este proyecto reimplementa el SDK de la consola y la API gráfica NVN sobre Vulkan.

- **Necesitas tu propia copia del juego**, versión 16.0.1, con el ExeFS y la RomFS ya extraídos en tu consola, como
  carpetas normales. Salen de tu propia copia: si tu consola tiene firmware modificado, sus herramientas homebrew los
  extraen. La app solo lee esas carpetas y te dice qué falta si algo no está. Este repositorio no tiene código ni
  archivos del juego.
- Descarga la APK de [Releases](https://github.com/IamWilmar/mhrise-recomp/releases/latest) e instálala. Copia esas carpetas al
  dispositivo, abre la app, dale el permiso de archivos, elige la carpeta y pulsa *Iniciar juego*.
  Las partidas y la caché de shaders se guardan en esa misma carpeta.
- Hecho exclusivamente para la Retroid Pocket 5 y la Retroid Pocket Flip 2 (Snapdragon 865, Adreno 650): todo está
  ajustado a ese chip y a sus límites (30 fps fijos con presupuesto de 33 ms por frame, ~2,7 GB de memoria libre, núcleos
  grandes para el juego). Probado en la Flip 2.
- Los botones quedan donde están en la consola original del juego en los dos modos del mando de la Retroid (Xbox y
  Retro), y cualquier otro mando, por USB o Bluetooth, se lee por posición. Deslizando un dedo desde el borde derecho de
  la pantalla sale un panel de ajustes, con el interruptor del contador de FPS y el de volver a elegir la
  carpeta. Con la carpeta ya elegida, la app abre el juego directamente.
- Proyecto de fans, gratis (las donaciones son opcionales y no desbloquean nada), sin relación con el fabricante de la consola ni
  con Capcom.
- Si quieres apoyar el trabajo: [ko-fi.com/iamwilmar](https://ko-fi.com/iamwilmar). Es opcional y no desbloquea nada: todas las versiones son gratis e iguales para todos.
