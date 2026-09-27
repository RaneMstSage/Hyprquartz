# Hyprquartz

Hyprquartz is a faithful port of [Hyprland](https://github.com/hyprwm/Hyprland), the Wayland compositor for Linux, to macOS.

**Hyprquartz is an independent project. It is not affiliated with, endorsed by or supported by the Hyprland project or Hypr Development.** Please report problems with Hyprquartz here, not to Hyprland.

> **Status: early work in progress.** Three of the components Hyprland depends on are ported. Hyprland itself is not ported yet, and the `hyprquartz` binary does nothing yet. Details are under [Status](#status).

## What "faithful port" means

Upstream is the specification. Every upstream file, function, class, enum value, option and comment is ported. Only the parts that are specific to Linux or Wayland change, and each of those gets a named macOS replacement. Nothing is dropped because it looks unused or Linux-only.

### Config contract

`hyprland.conf` (hyprlang) and `hyprland.lua` (Lua 5.5, the `hl.*` API) are meant to stay identical to Hyprland's: the same option names, types, defaults, keywords, dispatchers and Lua functions. Options that mean nothing on macOS still register and parse without error. The implementation may differ; the interface may not.

Names that other programs look up stay Hyprland's: `HYPRLAND_INSTANCE_SIGNATURE`, `HYPRLAND_CMD`, `.socket.sock`, `.socket2.sock`, `hyprland.conf` and `hyprland.lua`. Names that identify the program say Hyprquartz: the binary, the log files, the banner and `--version`.

The config contract is a rule for the port. It can't be checked yet, because Hyprland's config code hasn't been ported.

## How the port is done

- **Upstream code stays as it is.** Files with no Linux-specific content are copied byte for byte. Each ported file is checked against its upstream file: a `diff` with every change explained, a count comparison (lines, functions, classes, enum values, `#define`s, options), and a statement that nothing was removed.
- **macOS replacements go under `#if defined(__APPLE__)`**, with upstream's code unchanged in the `#else` branch. That way the non-Apple path stays upstream's code, and it's still built and tested on Linux. A few macOS-only helpers live in their own files; each of them is empty on other platforms.
- **Claims about macOS need evidence:** a man page, an SDK header path and line, Apple documentation, or a measured test. Behavior is measured with probe programs, and compared with the same probes on a Linux reference machine.
  - Probes and their outputs live in `probes/`, which is local and not committed (see `.gitignore`). The results are recorded in `CLAUDE.md`.
- **Suspected upstream bugs are ported as they are** and listed in `CLAUDE.md` to report upstream.
- **Some things macOS can't reproduce are recorded as known limitations** in `CLAUDE.md`, with the measurements, for example some named-FIFO and pty states in the event loop.
- **Upstream tests are ported unchanged** and must pass on macOS and on Linux.

`CLAUDE.md` is the working rulebook. It holds every decision, known macOS limitation, open decision and suspected upstream bug.

## Status

| Component | Upstream | Status | Tests |
|---|---|---|---|
| hyprutils | 0.14.2, commit 95983ee | Ported (128 files) | All 23 upstream test files pass: **51/51** on macOS and on Linux |
| hyprlang | 0.6.8, commit 9508458 | Ported (30 files) | Both upstream tests pass: **2/2** (Parsing, Fuzz) on macOS and on Linux |
| hyprland-protocols | 0.7.1, commit cc9a8fd | Ported (17 files, all byte-identical) | Upstream has no tests; nothing here is built or run on its own |
| Hyprland | commit 23118f9f | **Not ported.** A few early files exist (see below) | None |

Test runs, all on 2026-09-25:

| | Builds, test targets and sanitizer | Linux copy tested |
|---|---|---|
| **hyprutils on macOS** | AppleClang 17, Debug, Ninja, with AddressSanitizer | |
| **hyprutils on Linux** | GCC 16.2.1, Debug, with AddressSanitizer | commit a1bdada |
| **hyprlang on macOS** | AppleClang 17, Debug, Ninja. Upstream's CMake adds no sanitizer | |
| **hyprlang on Linux** | GCC 16.2.1, Debug | commit e048c03 |

The only compiler warnings are in upstream's own test code, which is ported as it is.

**What hyprutils needed:** macOS replacements for the Linux-specific parts. Each one is described in `CLAUDE.md`.
- **Event loop:** epoll-style results emulated on kqueue.
- **Timers:** a clock that stops while the Mac sleeps.
- **Descriptor state:** Linux `poll()` results reproduced for file descriptors.
- **Runtime folder:** a replacement for `XDG_RUNTIME_DIR`, and the semaphore lock file that lives there.
- **Logger:** local-time formatting.
- **Locale detection.**

**Hyprland's early files:** `src/` holds `includes.hpp`, `SharedDefs.hpp`, `debug/log/Logger.*` and `helpers/math/Math.*`, plus `compat/linux/input-event-codes.h` (vendored verbatim). They were started before the dependency libraries were done and aren't compiled yet: `src/main.cpp` is a placeholder that returns 0, and it's the only file the `hyprquartz` target builds.

**Next:** hyprwayland-scanner, then hyprcursor, hyprgraphics, hyprwire, aquamarine, hyprtoolkit, hyprland-guiutils, then Hyprland.
- **The big design questions are still open**, listed under "Open decisions" in `CLAUDE.md`:
  - what replaces the Wayland protocol layer, since macOS apps aren't Wayland clients;
  - the aquamarine backend (outputs, input, buffers);
  - the renderer;
  - keyboard mapping;
  - cursor themes;
  - plugins;
  - how Hyprquartz is launched.
- **Hyprquartz can't manage windows yet**, and none of this should be read as a promise that it will.

## Repository layout

```
CMakeLists.txt              top-level build (target: hyprquartz)
VERSION                     Hyprquartz version (0.1.0)
CLAUDE.md                   porting rules, decisions, limitations, open questions
LICENSES/                   licences of ported upstream code (Hyprland-BSD-3-Clause.txt)
src/                        Hyprland's sources as they are ported (early files only)
compat/linux/               vendored Linux header (input-event-codes.h)
subprojects/hyprutils/      hyprutils port, with its upstream tests
subprojects/hyprlang/       hyprlang port, with its upstream tests
subprojects/hyprland-protocols/  hyprland-protocols, verbatim
```

**How the build uses them:**
- **hyprutils and hyprlang** are added with `add_subdirectory`. hyprlang links the in-project hyprutils target instead of a copy installed on the system.
- **hyprland-protocols is data only.** It isn't added to the build, because Hyprland's build reads its XML files by path.

## Building

Hyprquartz builds only on macOS; the top-level `CMakeLists.txt` stops on other systems.

**Requirements:**
- AppleClang 17 (Xcode). The project uses C++26.
- CMake 3.30 or newer.
- pkg-config.
- The system libraries, from [MacPorts](https://www.macports.org): pixman and GoogleTest for hyprutils, pugixml for hyprwayland-scanner, and libzip, cairo and librsvg for hyprcursor, which also needs toml++ (still from Homebrew for now). GoogleTest has to be built from source with Xcode's SDK; `CLAUDE.md` explains why and how.

**Tested with:** macOS 15.7, Xcode 26.2, CMake 4.4.3 (Kitware's build), and MacPorts' libraries and pkg-config 0.29.2; toml++ still comes from Homebrew for now. Any minimum deployment target set later must be macOS 13.3 or newer (see `CLAUDE.md`).

The recorded builds were Debug builds in CLion, using Ninja and the `build/` folder. The equivalent from a shell is below. It is the standard CMake form and wasn't recorded separately.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPKG_CONFIG_EXECUTABLE=/opt/local/bin/pkg-config "-DCMAKE_PREFIX_PATH=/opt/local;/usr/local/opt/tomlplusplus"
cmake --build build
ctest --test-dir build/subprojects/hyprutils
ctest --test-dir build/subprojects/hyprlang
```

**Where the tests are:** tests are registered inside each subproject, not at the top level, so run `ctest` in each subproject's build folder.
- **hyprutils** registers its tests in Debug builds.
- **hyprlang** registers its tests in every build type, as upstream does.

## Licence

Hyprquartz's own licence is not decided yet, so the repository has no root `LICENSE` file. Each ported library keeps its upstream licence file:

| Component | Licence | Licence files |
|---|---|---|
| hyprutils | BSD-3-Clause | `subprojects/hyprutils/LICENSE` |
| hyprlang | LGPL-3.0-only | `subprojects/hyprlang/LICENSE`, `subprojects/hyprlang/COPYRIGHT` |
| hyprland-protocols | BSD-3-Clause | `subprojects/hyprland-protocols/LICENSE` |
| `compat/linux/input-event-codes.h` | GPL-2.0-only WITH Linux-syscall-note (its SPDX line) | |

The code in `src/` comes from Hyprland, which is BSD-3-Clause. Hyprland's licence is in `LICENSES/Hyprland-BSD-3-Clause.txt`, copied verbatim from Hyprland's `LICENSE`.

## Acknowledgements

Hyprland, hyprutils, hyprlang and hyprland-protocols are made by vaxerski and Hypr Development, together with their contributors. Hyprquartz exists because of their work.
