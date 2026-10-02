# Halo: Combat Evolved for PlayStation Vita, Linux, Windows and Android

This project is a native port of the Halo: Combat Evolved decompilation to **PlayStation Vita**, **Linux**, **Windows**, and **Android**. The decompilation is based on the Xbox build 2342 (`cachebeta.exe`, SHA-256 `4cc87b45f721270392a96f1674ed2b5cd4a7bb4355faeab4531d1cf1884d9520`).

<img width="1289" height="995" alt="The game on Linux" src="https://github.com/user-attachments/assets/0d3ad50f-f8b8-46cf-aef8-e3661da2a7d7" />

The port builds upon the decompilation work of [bnunu/halo-1](https://github.com/bnunu/halo-1), [punpckhdq/halo](https://github.com/punpckhdq/halo), and the multiplatform base from [cybersecurity/halo-ce-universal](https://github.com/cybersecurity/halo-ce-universal).

---

## 🎮 PlayStation Vita Port Status

The PS Vita port focuses on bringing Halo: Combat Evolved to Sony's handheld (ARMv7 Cortex-A9), featuring hardware-accelerated rendering through **VitaGL**, a relocatable memory arena, and a native platform abstraction layer.

### Validated Milestones (Hardware Tested)
* **Native LibGxm & Full Campaign Engine (30 FPS Gameplay)**:
  - Full campaign and multiplayer gameplay running natively at 30 FPS target via direct LibGxm hardware rendering and Cg shaders.
  - Multi-threaded game tick and render epoch synchronization eliminating race conditions.
  - Offline pre-compiled shader cache stored in `ux0:data/haloce-vita/shaders/`.
  - Fixes for transparent geometry, cryo-pod models, and lens flare look-calibration lights in `a10` (*The Pillar of Autumn*).
  - In-game settings and performance overlay (hold **Select + Start** for 1 second).
  - Based on the native LibGxm architecture by [BirchWoodGod/halo-ce-vita](https://github.com/BirchWoodGod/halo-ce-vita).
* **Milestone 2.0 (GPU Textures & Maps Baseline)**:
  - Validated on real PS Vita hardware with smooth VitaGL hardware rendering.
  - Native loading of original `.map` files: `ui.map`, `bloodgulch.map`, and `a10.map`.
  - Real BSP collision geometry, first-person camera, gravity, jumping, and movement.
  - DXT1, DXT3, DXT5, and AY8 texture decoding directly from map bitmap tags.
  - Half-Lambert lighting and 32-bit packed vertex normal decompression.
  - Reference: [`port/vita/releases/2.0.md`](port/vita/releases/2.0.md).
* **Milestone 0.2 (Platform Contracts Pass)**:
  - Executed on hardware with `PLATFORM_CONTRACT: PASS` (`halo-vita-platform-0.2.vpk`, Title ID `HVIT00002`).
  - Validates Win32 file I/O, asynchronous alertable Ex callbacks, real `ui.map` header parsing, directory enumeration, thread suspension/resumption, per-thread last-error isolation, high-resolution RTC timing, memory management, and Xbox controller mapping via `sceCtrl`.
  - Reference: [`port/vita/PLATFORM_STATUS.md`](port/vita/PLATFORM_STATUS.md).
* **CPU Recompilation & Engine Audit**:
  - **468/468 units compiled**: All 466 C units available in the engine plus Vita platform adapters compile cleanly for ARMv7 hard-float with Clang.
  - Reproducible symbol audit tracks remaining external dependencies (Direct3D 8, Win32 kernel, XAudio) towards full engine main loop integration.
  - Comprehensive architectural report: [`port/vita/README_PORT_STATUS.md`](port/vita/README_PORT_STATUS.md).

For detailed documentation, architecture, and developer notes on the Vita port, see [`port/vita/README.md`](port/vita/README.md).

---

## 💻 Supported Platforms

| Platform | Architecture & Stack | Documentation |
| :--- | :--- | :--- |
| **PlayStation Vita** | ARMv7-A Cortex-A9 (hard-float), VitaGL (GXM), VitaSDK, sceCtrl | [`port/vita/README.md`](port/vita/README.md) |
| **Linux** | 32-bit x86, OpenGL 4.5, SDL3 | [`port/linux/README.md`](port/linux/README.md) |
| **Windows** | 32-bit x86, OpenGL 4.5, SDL3 | [`port/windows/README.md`](port/windows/README.md) |
| **Android** | arm64, OpenGL ES 3, SDL3 | [`port/android/README.md`](port/android/README.md) |

The Linux README also documents game controls, configuration settings, and multiplayer features, which are shared across platforms.

---

## 📦 Game Data

The port does **not** include proprietary game data. You must provide your own legally acquired Halo: Combat Evolved files from an original Xbox disc image (`.iso` / `.xiso`) or retail `.map` files.

### PS Vita
1. Create the maps directory on your Vita: `ux0:data/halo/maps/`.
2. Copy your map files into this directory (at minimum `ui.map`, plus campaign or multiplayer maps like `bloodgulch.map` or `a10.map`).
3. Launch the installed VPK.
4. Diagnostic and execution logs are written to `ux0:data/halo-vita-diagnostic/`.

### Linux and Windows
1. Start the game executable.
2. On first launch, the game prompts for your Xbox disc image (`.iso` or `.xiso`).
3. The game extracts the `maps/` directory next to the executable and launches.

### Android
1. Copy the Xbox disc image to your device storage.
2. Select it on first launch; the app extracts `maps/` into its internal data folder. Refer to [`port/android/README.md`](port/android/README.md).

---

## 🛠️ Building

### 1. PlayStation Vita

**Requirements:**
- [VitaSDK](https://vitasdk.org/) with environment variable `VITASDK` set and added to `PATH`.
- Clang compiler (version 22.x recommended, with ARMv7 target support).
- CMake 3.16+ and Python 3.
- Native build tool (`ninja` or `make` / `mingw32-make` on Windows).

**Fast Native Build (Ninja):**
```sh
# Configure for Vita (Release build)
python configure.py --lto off --pgo off --portable --release

# Build VPK
ninja vita
```
Output: `build/vita/halo.vpk`

**Alternative CMake Build:**
```sh
# Configure CMake for Vita
cmake -S port/vita -B build/vita-core -DHALO_CLANG=clang

# Build VPK packages
cmake --build build/vita-core -j$(nproc)
```
*(On Windows, add `-G "MinGW Makefiles"` or specify `CMAKE_MAKE_PROGRAM` if needed).*

**Primary Build Targets:**
| Target / Artifact | Description |
| :--- | :--- |
| `halo-vita-menu-bloodgulch-2.0.vpk` | Milestone 2.0 VPK (Blood Gulch & UI menu test runner with VitaGL) |
| `halo-vita-platform-0.2.vpk` | Milestone 0.2 VPK (Platform contracts validation suite) |
| `vita-engine-audit` | Developer target: compiles all 468 engine units and regenerates link audit reports |
| `vita-engine-link` | Developer target: diagnostic link probe with VitaSDK libraries |

**Host Unit Tests:**
To test the Vita memory arena allocator natively on your host machine without Vita APIs:
```sh
cc -std=c11 -Wall -Wextra -Iport/vita/include port/vita/src/arena.c port/vita/tests/test_arena.c -o test_arena
./test_arena
```

---

### 2. Linux, Windows, and Android

The native desktop and Android ports use Python configuration and the `ninja` build system. Cleanroom Xbox SDK headers are provided in [`port/include/xdk`](port/include/xdk/README.md).

**Build steps:**
1. Install Python 3 and [ninja](https://ninja-build.org/).
2. Install platform-specific toolchains (see each platform's README).
3. Run configuration:
   ```sh
   python configure.py
   ```
4. Build with ninja:
   | Command | Target Output |
   | :--- | :--- |
   | `ninja linux` | `build/linux/halo` |
   | `ninja windows` | `build/windows/halo.exe` and `SDL3.dll` |
   | `ninja android_apk` | `port/android/app/build/outputs/apk/debug/app-debug.apk` |

Entering `ninja` without arguments builds for the host computer. For CI replication, use `python tools/ci_build.py <platform> <release|debug>`.

#### Build Options (`configure.py`)
| Option | Description |
| :--- | :--- |
| *(default)* | Debug build (assertions enabled, stops on failure). |
| `--release` | Release build (assertions disabled for maximum performance). |
| `--portable` | Builds with generic x86-64 (SSE2) instructions instead of `-march=native`. |
| `--lto=thin` / `--lto=off` | Select link-time optimization level (`full` by default). |
| `--pgo=off` | Disables profile-guided optimization. |
| `--pgo=train` | Trains and records a profile for the platform (requires game data in `assets/`). |

---

## 🌐 Multiplayer

The game supports System Link multiplayer over local networks and the internet:
- Up to 128 players per match across up to 128 machines.
- Cross-platform play: Linux, Windows, and Android clients can participate in the same match.
- Direct join via invite links without requiring dedicated central servers.
- Modernized netcode with client-side responsive movement and host authoritative game state. Refer to [`port/linux/NETCODE.md`](port/linux/NETCODE.md).

---

## 📁 Repository Structure

```
├── port/
│   ├── vita/           # PlayStation Vita port (VitaGL, memory arena, XAPI translation, tests)
│   ├── linux/          # Linux port implementation and platform layer
│   ├── windows/        # Windows port implementation
│   ├── android/        # Android Gradle project and GLES3 backend
│   └── include/xdk/    # Cleanroom Xbox Development Kit headers
├── source/             # Original decompiled C engine source code
│   ├── cache/          # Tag cache and map decompression
│   ├── rasterizer/     # Geometry, shaders, and rendering logic
│   ├── memory/         # Memory managers, CRC, and zlib
│   └── ...             # Game subsystems (physics, AI, networking, etc.)
├── tools/              # Auditing scripts, objdiff integration, and build generators
└── docs/               # Technical notes, matching methodology, and architectural audits
```

---

## 📜 Credits & Acknowledgments

- [punpckhdq/halo](https://github.com/punpckhdq/halo) - Initial Xbox Halo decompilation project.
- [bnunu/halo-1](https://github.com/bnunu/halo-1) - Enhanced decompilation and reverse-engineering fork.
- [cybersecurity/halo-ce-universal](https://github.com/cybersecurity/halo-ce-universal) - Universal multiplatform base (Linux, Windows, Android).
- [VitaSDK](https://vitasdk.org/) and [vitaGL](https://github.com/Rinnegatamante/vitaGL) by Rinnegatamante - PlayStation Vita homebrew development toolchain and OpenGL wrapper.
- [BirchWoodGod/halo-ce-vita](https://github.com/BirchWoodGod/halo-ce-vita) - PlayStation Vita native LibGxm renderer, offline shader cache, audio mixer optimizations, and multi-thread epoch synchronization.
