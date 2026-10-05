# Compilation

## Dependencies

- SDL3 >= `3.0.0` (version `3.4.16` is included in this repo for Windows and macOS).
- cmake
- Rust and Cargo
- Your systems compiler

## Optional dependencies

Pecel Bakwan bundles a few other projects for development purposes. If you have them installed already,
the system version will be used. This holds for: gtest and string theory.

## Python tooling

The e2e harness (`tools/ja2ctl.py`, `tests/e2e/`) and the asset tools
(`tools/assets/`) are Python; everything except the golden-image resolution
tests runs on the standard library alone. Those tests need Pillow (PNG decode)
and numpy (per-pixel compare), managed by [uv](https://docs.astral.sh/uv/) and
locked in `uv.lock`:

```sh
uv sync
```

This creates `.venv/` in the repo root; a fresh cmake configure picks that
interpreter up for `ctest`. Run the tools through it with
`uv run python tools/ja2ctl.py ...`, or activate it (`source .venv/bin/activate`;
on Windows `.venv\Scripts\activate`).

## The raw commands behind `tools/dev.py`

[`tools/dev.py`](tools/dev.py) is the one entry point for day-to-day work on
macOS and Windows (see [AGENTS.md](AGENTS.md)); it runs everything below in the
right environment and is safe to run repeatedly. Use the raw commands when you
need something the wrapper does not cover.

### macOS

The build directory is `build`:

```sh
cmake -S . -B build -G Ninja            # once; drop "-G Ninja" if you have no ninja
cmake --build build --parallel $(sysctl -n hw.logicalcpu)
./build/ja2 -unittests
./build/ja2 -res 1280x720
```

Incremental — `cmake --build` only recompiles what changed, and re-runs cmake
itself when `CMakeLists.txt` changed.

### Windows (MSYS2 MinGW64)

The build directory is `_bin`, and every build command needs the MinGW64
environment: run it through a login shell with `MSYSTEM=MINGW64` set, because
plain PowerShell/cmd does not have `gcc`/`cmake`/`cargo` on `PATH`:

```sh
MSYSTEM=MINGW64 "/c/msys64/usr/bin/bash.exe" -lc "cd '/c/Workspace/ja2-stracciatella/_bin' && cmake --build . --parallel \$(nproc)"
```

Configure a fresh build directory with Ninja (much faster than MSYS Makefiles;
a build directory keeps the generator it was configured with, so switch by
configuring a new one):

```sh
MSYSTEM=MINGW64 "/c/msys64/usr/bin/bash.exe" -lc "cd '/c/Workspace/ja2-stracciatella/_bin' && cmake .. -G Ninja"
```

For a package build use the old generator: `cmake .. -G 'MSYS Makefiles' -DCPACK_GENERATOR=ZIP && make package`.

### Neither: the roadmap page

`roadmap` and `dep` need no toolchain and no MSYS2 environment — they are host-side
Python and `gh`, and they run the same on both platforms:

```sh
python tools/dev.py roadmap                                  # docs/roadmap/roadmap.html
python tools/dev.py roadmap --offline                        # from the committed snapshot
python tools/roadmap.py check                                # prose/relation drift, cycles
python tools/dev.py dep 220 --blocked-by "#101,#260"          # native issue relations
```

See [The roadmap page](AGENTS.md#the-roadmap-page).

Test and run:

```sh
MSYSTEM=MINGW64 "/c/msys64/usr/bin/bash.exe" -lc "cd '/c/Workspace/ja2-stracciatella/_bin' && ./ja2.exe -unittests"
MSYSTEM=MINGW64 "/c/msys64/usr/bin/bash.exe" -lc "cd '/c/Workspace/ja2-stracciatella/_bin' && ./ja2.exe -res 1280x720"
```

**Long paths.** Agent worktrees live in deep directories and the build tree goes
deeper still (cargo's `target/` is the worst offender). The deepest path this
tree actually produces is ~212 characters, in cargo's fingerprint directories,
which Windows 10+ long-path support handles without help — so builds run at
their real path and no `subst` is needed.

## General Notes

We use cmake as our build system, which is aimed at an out-of-source build. That means that you should call
cmake from a directory that is different from the source directory. You can create a directory inside the source
directory (`_bin` is ignored by git). Cmake only needs to be executed once unless you want to change options.

```sh
mkdir _bin && cd _bin
```

## Faster builds

Nothing below is required — each one is picked up automatically when the machine offers it.

- **sccache** — with `sccache` on `PATH` when cmake runs, it becomes the compiler launcher and
  Cargo's `RUSTC_WRAPPER` (`-DUSE_SCCACHE=OFF` to opt out). The cache is shared by *every* build
  directory, so a second worktree, a branch switch or a reverted change reuses the objects instead
  of recompiling them: `pacman -S mingw-w64-x86_64-sccache` (MSYS2), `brew install sccache`
  (macOS), `cargo install sccache --locked` (elsewhere).
  **Cache sharing across worktrees** — sccache hashes absolute paths, so naively two checkouts at
  different paths are two cache islands and the second worktree recompiles the world.
  `SCCACHE_BASEDIR` fixes this: sccache rewrites paths under that directory to be *relative*
  before hashing, so checkouts at different absolute paths produce the same key.
  `tools/dev.py` sets it to the worktree root, which covers every source file and every generated
  header under `_bin`. Measured here, one translation unit compiled from two different roots:

  | | result |
  | --- | --- |
  | no `SCCACHE_BASEDIR`, root A then root B | 6.41 s MISS, then 6.36 s **MISS** |
  | `SCCACHE_BASEDIR=<root>` per checkout | 6.41 s MISS, then 0.09 s **HIT** |

  This is what lets worktrees build side by side with a shared cache. The previous design pinned
  every worktree to one machine-wide `subst` drive under an exclusive lock, which produced the
  same sharing but serialised the machine behind it: on a 9-worktree box a **0.8 s** no-op build
  spent **215 s** waiting for the lock. If you build outside `tools/dev.py`, set
  `SCCACHE_BASEDIR` to your checkout root or you get a private cache island.
- **lld** — `-DUSE_LLD=ON` (the default) links with `ld.lld` when the toolchain provides it, which
  cuts the link of the monolithic `ja2` binary to a fraction of GNU ld's time. MSYS2:
  `pacman -S mingw-w64-x86_64-lld`. Where `ld.lld` is absent the option is skipped silently.
- **Ninja** — configure a *fresh* build directory with `-G Ninja` and build with `cmake --build .`
  (append `--parallel` for a job count). Ninja does the scheduling itself and does not start a
  shell per recipe line, which is what makes the MSYS Makefiles generator the slow choice on
  Windows. MSYS2: `pacman -S mingw-w64-x86_64-ninja`. A build directory keeps the generator it was
  configured with: switch by configuring a new one.
- **Precompiled headers** — `ENABLE_PCH` (default `ON`) compiles the standard library once into a
  `cmake_pch.hxx.gch` snapshot and compiles every translation unit against it instead of parsing
  `<vector>`, `<map>`, `<string>` and friends again in each of them. The snapshot covers the standard
  headers the codebase includes in bulk (`src/CMakeLists.txt`); `<iostream>` is deliberately absent
  because libstdc++ puts a static `ios_base::Init` in it. Measured numbers and the toolchain/CI
  interactions are in [Precompiled headers: measured](#precompiled-headers-measured) below.
- **Unity builds** — `-DENABLE_UNITY_BUILD=ON` (default `OFF`) compiles batches of translation
  units together, following the `UNITY_GROUP`s set in `src/**/CMakeLists.txt` (sources that must
  stay on their own carry `SKIP_UNITY_BUILD_INCLUSION`). It cuts total work but is a **net loss
  on a many-core machine**, because a group is one un-parallelisable translation unit: it pays
  back in lost parallelism more than it saves, and it makes the commonest edit — one file —
  rebuild the whole group. Measured here (Windows/MSYS2 GCC 16, Ninja, 12 threads), same group
  built both ways:

  | group | unity (1 TU) | per file (CPU → wall @ -j12) | single-file edit |
  | --- | --- | --- | --- |
  | `Tactical` (67 files) | 53.3 s | 263 s → **21.9 s** | 50.1 s vs ~4 s |
  | `sgp` (43 files) | 28.7 s | 121 s → **10.1 s** | 23.1 s vs ~3 s |

  It also wrecks cache granularity: one 53 s cache entry covers 67 files, so editing any one of
  them invalidates all of it. Enable it for a release/CI build, or on a machine with few cores.
- **Dependency bumps** — a changed pin refreshes the downloaded sources for you
  (`cmake/DepRefresh.cmake`), so bumping a dependency in an existing build directory rebuilds what
  depends on it instead of silently linking the old objects against the new headers.

### Precompiled headers: measured

Measured on Windows with the MSYS2 MinGW64 toolchain (GCC, `-O2 -g`, Ninja, sccache, 6-way
parallelism on a 12-thread machine), one *semantic* edit per run so every affected translation unit
is a real compile — a comment-only change is a cache hit and measures nothing. Same edit with
`ENABLE_PCH=OFF` and `ON`, wall clock:

| Rebuild after a semantic edit in | TUs recompiled | `ENABLE_PCH=OFF` | `ENABLE_PCH=ON` | |
| --- | --- | --- | --- | --- |
| `src/game/Tactical/Soldier_Control.cc` (the per-TU cost, isolated compile) | 1 | 11.0 s | 9.7 s | −12 % |
| `src/game/Tactical/Soldier_Control.h` | 216 | 5 min 47 s – 6 min 18 s | 4 min 48 s | −17…−24 % |
| `src/sgp/Types.h` | 431 | 9 min 07 s – 9 min 17 s | 8 min 28 s – 8 min 37 s | −8 % |

> **Re-measured later, on GCC 16.1, the snapshot no longer pays for itself.** Interleaving the two
> variants on the real build path, best of 3 each, the snapshot came out **2.4 %–13.5 % *slower***
> on 4 of 4 translation units:
>
> | TU | with PCH | without PCH | |
> | --- | --- | --- | --- |
> | `Strategic_AI.cc` | 5.76 s | 5.61 s | +2.7 % |
> | `MapScreen.cc` | 7.73 s | 7.48 s | +3.4 % |
> | `SoundMan.cc` | 21.83 s | 21.31 s | +2.4 % |
> | `FrontMainMenu.cc` | 6.12 s | 5.39 s | +13.5 % |
>
> The numbers above and these disagree, and the difference is the compiler: GCC 16 parses these
> headers fast enough that loading the snapshot costs more than re-parsing. The switch is still
> `ENABLE_PCH`; re-measure before trusting either table on a toolchain you have not tried.

The isolated per-TU row is the precise figure (the same translation unit with and without the
snapshot, interleaved back to back, so the machine's background load hits both equally). The
cascade rows are wall clock on a machine shared with other builds and e2e runs and drift a few
percent per hour, hence the ranges — `Types.h`, measured inside one 25-minute window, is the
representative cascade number. Lighter translation units benefit more (their front-end share is
bigger): the same comparison goes 8.3 s → 7.3 s on `StrategicMap.cc` (90 KB) and 4.8 s → 4.1 s on
`Video.cc` (51 KB).

Why the ceiling is where it is: `-ftime-report` on a typical translation unit splits roughly 40 %
front end (parser, name lookup, template instantiation) and 60 % code generation, optimisation and
debug info (`symout`, variable tracking) at `-O2 -g`. A snapshot only removes the standard
library's share of the front end, so this is a free ~10 %, not a factor of two — and an edit in a
*project* header still recompiles every translation unit that includes it, because the snapshot
does not cover our own headers. For the bigger levers see sccache and unity builds above.

What was tried and dropped: adding `<string_theory/string>` and `<string_theory/format>` (included
by 247 and 126 TUs) to the snapshot measured no further gain. string_theory's cost is template
instantiation at the point of use, which no snapshot can precompute.

**sccache** — unaffected in both directions: a compile that uses the snapshot is still cacheable,
still a cache hit after a revert, and still worth sharing across worktrees. The snapshot is part
of the preprocessed output sccache hashes, so the first build after switching `ENABLE_PCH` or
after editing the header list below recompiles everything once.

**MSYS2/MinGW64** — works with the GCC in MSYS2. `-Winvalid-pch` is already in the compile flags,
so a snapshot GCC refuses warns instead of silently costing time. The one trap: a tool that
re-quotes a compile command (`-DINSTALL_LIB_DIR=\"lib\"` must keep its backslashes until GCC sees
it) makes GCC reject the snapshot with `not used because 'INSTALL_LIB_DIR' defined as ...`; the
build system passes the quoting correctly, hand-rolled tooling around `compile_commands.json` can
trip over it.

**CI and cross builds** — `ENABLE_PCH` is a plain `target_precompile_headers` on the one `ja2`
target, so every build gets it: the Linux and mingw64 CI jobs, package builds, cross builds from
Linux, macOS and Android. The snapshot is generated per build directory and per toolchain, so
nothing is shared between platforms and nothing can leak across them.

**Existing build directories** — the value in `CMakeCache.txt` wins over the new default, so a
build directory configured before this keeps its setting. `cmake -B <dir> -DENABLE_PCH=ON` (or
`=OFF`) switches one over; the next build recompiles everything once.

## Compiler versions

The minimum compiler is **GCC 14**, recorded in [`min-gcc-version`](min-gcc-version) and read by
[`.ci/ci-setup.sh`](.ci/ci-setup.sh) — one pin with many consumers, the way
[`min-rust-version`](min-rust-version) works. Newer is fine: nothing in the tree is
version-gated (`git grep __GNUC__` finds nothing outside vendored dependencies), and the tree
builds with both the pinned GCC 14 and the GCC 16.1 that MSYS2 ships.

It was **GCC 10** until #289, and that made the CI compiler the oldest thing in the loop:
`-std=c++23` first exists in GCC 11, so CI could not verify a standard bump was green, a defect
that only reproduces on a modern compiler shipped silently, and a developer could build green
locally and have CI disagree.

| build | compiler |
|---|---|
| Linux CI, and the Coverity scan | the pin from `min-gcc-version`, installed with apt, made the default with `update-alternatives`, and asserted by `linux-assert-gcc-at-least` — a build that quietly got an older compiler fails instead of passing |
| Windows (MSYS2 MinGW64) | MSYS2's `mingw-w64-x86_64-gcc`; GCC 16.1 is what the tree is developed on |
| Windows (Visual Studio) | Visual C++, see [Generate Visual Studio Solution](#generate-visual-studio-solution) |
| Windows, cross-built from Linux | the runner image's `mingw-w64`, currently **GCC 13.2** — see below |
| macOS | Apple Clang |

The mingw-w64 cross build cannot follow the pin: Ubuntu ships one mingw-w64 GCC per release and
`ubuntu-24.04` has 13.2, so matching GCC 14 there means importing a third-party toolchain. Every
CI setup step prints the compiler it ended up with (`gcc --version`,
`x86_64-w64-mingw32-g++ --version`), so a build log always says which compiler produced it.

To build with a GCC other than the system default (Debian and Ubuntu ship GCC 13 as `gcc`):

```sh
sudo apt-get install gcc-14 g++-14
cmake -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 path/to/source
```

**What a Linux build needs at runtime.** The AppImage and the packages CI produces are built on
`ubuntu-24.04`, so they want glibc 2.39 and `libstdc++6` 14. Neither is beyond the image's own
baseline: GCC 14 is what `libstdc++6` 14.2 is built from, so the compiler bump adds no runtime
dependency the image did not already have. The cost of the image bump is glibc, so test Linux
packages on Ubuntu 24.04 or newer — see [Release checklist](docs/Release-checklist.md).

## Rust notes

We suggest to install Rust and Cargo using [rustup](http://rustup.rs/). This way you will get the most recent version
installed in your home directory. As rust is a rapidly developing language the binaries provided by your distribution
might be too old to build ja2-pecel-bakwan and its dependencies. When using rustup the correct version of rust should
be automatically selected.

If you don't want to use rustup, you can always look up the currently required version in the
[min-rust-version file](min-rust-version)

## Build on Linux or freeBSD

```sh
cmake path/to/source
make
```

If you want to be able to install the resulting binary on your system, please ensure that `CMAKE_INSTALL_PREFIX` matches
with `EXTRA_DATA_DIR`. Example: `cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DEXTRA_DATA_DIR=/usr/local/share/ja2 path/to/source`

## Build on OpenBSD (tested on -current as of mid-November 2021)

```sh
# The bundled/downloaded GTest sources fail to build.
doas pkg_add gtest

cmake path/to/source -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-openbsd.cmake
make
```

## Build on NixOS

Open a nix-shell that downloads all required dependencies, builds, and then automatically exits the nix-shell.

```sh
nix-shell -p rustc cargo sdl3 fltk cmake libGL --run "cmake path/to/source && make"
```

## Build for Windows on Linux using MinGW (cross build)

Additional requirements: MinGW compiler

```sh
cmake -DCMAKE_TOOLCHAIN_FILE=./cmake/toolchain-mingw.cmake path/to/source
make
```

If you are using rustup, you might need to add the MinGW target to the rust toolchain before compiling.

When building for 64-bit:

```sh
rustup target add x86_64-pc-windows-gnu
```

When building for 32-bit:

```sh
rustup target add i686-pc-windows-gnu
```

## Build on macOS

```sh
cmake -DCMAKE_TOOLCHAIN_FILE=./cmake/toolchain-macos.cmake path/to/source
make
```

## Build on Windows using MSYS2

Install [msys2](https://www.msys2.org/).

Open the msys2 shell.
Use "MSYS MinGW 64-bit" to build 64-bit and "MSYS MinGW 32-bit" to build 32-bit.

Update msys2, you might have to restart the msys2 shell and run the command again:

```sh
pacman -Syu
```

Install the build environment and dependencies:

```sh
pacman -S base-devel
```

to build 64-bit:

```sh
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-rust mingw-w64-x86_64-cmake mingw-w64-x86_64-SDL3 mingw-w64-x86_64-fltk
```

to build 32-bit:

```sh
pacman -S mingw-w64-i686-toolchain mingw-w64-i686-rust mingw-w64-i686-cmake mingw-w64-i686-SDL3 mingw-w64-i686-fltk
```

Get ja2-pecel-bakwan, cd into it, and build the package:

```sh
mkdir _bin && cd _bin
cmake .. "-GMSYS Makefiles" -DCPACK_GENERATOR=ZIP
make package
```

For faster iteration, configure a fresh build directory with `-G Ninja` instead and build with
`cmake --build .` — see [Faster builds](#faster-builds).

You now have a zip file with the game, including the dll dependencies.

## Build for Android

The Android project uses Gradle with CMake for the native code (see `android/app/build.gradle`).
The supported ABIs are `armeabi-v7a`, `arm64-v8a`, `x86`, and `x86_64`.

Install

- Android NDK (see `ndkVersion` in [`android/app/build.gradle`](android/app/build.gradle))
- Rust Android targets: `armv7-linux-androideabi`, `aarch64-linux-android`, `i686-linux-android`, `x86_64-linux-android`
  (install via `rustup target add <target>`)

The build with

```sh
cd android
./gradlew assembleDebug
```

For a release build (requires a signing keystore, see [`ci-setup.sh`](.ci/ci-setup.sh)):

```sh
./gradlew assembleRelease
```

You can also use Android Studio to compile the project. For this, just open the `android` directory in Android Studio with all prerequisites installed.

## Generate Visual Studio Solution

If you are most familiar using Visual Studio for development you can generate a solution from the sources.

Install Visual C++, CMake tools, MSBuild and Windows SDK with Visual Studio Installer.

Then in Visual Studio's Developer Command Prompt, change to the ja2-pecel-bakwan project directory, and generate the solution with CMake:

```sh
mkdir _bin
cd _bin
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-msvc.cmake ..
```

__Note__: If you add, move or delete any files. Please make sure to reflect your changes in the `CMakeLists.txt` files,
rerun cmake and reload your Solution before making any additional changes. Otherwise other build systems might fail
 when trying to build your changes.

## Generate XCode Project

If you are most familiar using XCode for development you can generate a project from the sources.

```sh
cmake -DCMAKE_TOOLCHAIN_FILE=./cmake/toolchain-macos.cmake -G "XCode" path/to/source
```

__Note__: If you add, move or delete any files. Please make sure to reflect your changes in the `CMakeLists.txt` files,
rerun cmake and reload your XCode project before making any additional changes. Otherwise other build systems might fail
 when trying to build your changes.

## Sanitizers

The game can be instrumented with GCC/Clang sanitizers, which is how a lot of this
codebase's legacy C-style memory handling gets caught. `-DWITH_SANITIZERS` takes a
semicolon-separated list, so quote it in the shell:

```sh
cmake -DWITH_SANITIZERS="address;undefined" path/to/source
```

`address`, `undefined` and `thread` are accepted; anything else is a configure error.
Sanitizers are also never applied to `rust/` — that code is frozen.

### What CI does

Every Debug **Linux** build — every pull request and every push to `master` — is
compiled with `-DWITH_SANITIZERS=address;undefined` and then runs the C++ unit tests
(`ja2 -unittests`) and the Phase 0 spike tests under it. The runtime options are set so a
report cannot pass as a success:

```sh
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:print_stacktrace=1
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
```

`halt_on_error` matters most for UBSan, which by default only prints a diagnostic and
keeps going; without it a report would leave a zero exit code and the job would go green.
`detect_leaks=0` disables LeakSanitizer: the game and its tests hold process-lifetime
globals by design and report at exit, so leak detection would fail every run for
allocations nobody is going to free. Turning it on is a deliberate, separate exercise, not
a default.

Release and nightly builds are `ReleaseWithDebInfo` and published, so they are *not*
instrumented, and a sanitized build skips the AppImage packaging step entirely.

### Windows: not supported, and it says so at configure time

`-fsanitize=address` and friends are supported on Linux and macOS. On Windows they are not,
and were never going to be:

* **MSYS2 MinGW64** — `libasan`, `libubsan` and `libtsan` live in the separate
  `mingw-w64-*-gcc-libs` packages, which are not part of the toolchain this project
  documents installing. Without them the flag still *compiles* perfectly and then fails in
  the linker with `cannot find -lasan`, minutes into a ~500-translation-unit build.
* **MSVC** — it has its own ASan (`/fsanitize=address`), which this flag does not select and
  which is not exercised here.

So `WITH_SANITIZERS` does not accept a toolchain it cannot link. At configure time it
compiles and links a one-line program with the same flags and, if that fails, stops with an
error naming the compiler, the system and the missing runtime. That check is the whole
point: the option used to claim to work everywhere and only revealed the problem at link
time (or, worse, silently produced a binary with no instrumentation). The check lives in
`src/CMakeLists.txt`.

If you are on Windows and want sanitizer coverage, build on Linux (CI does it for every
pull request) or use kresna, the off-site ARM64 Linux box.

## Additional Options

If you want to configure the build differently, you can pass additional options to
cmake. The supported options are:

| Switch        | Description           | Default  |
| ------------- |-------------| -----|
| `EXTRA_DATA_DIR` | Directory to read externalized data from relative to binary location. Useful for creating installable packages that have a fixed data path. | `` |
| `LOCAL_SDL_LIB` | Use SDL library from this directory. | `` |
| `WITH_UNITTESTS` | Build with unit tests | `ON` |
| `ENABLE_PCH` | Precompile the standard library; every translation unit compiles against the snapshot (see [Faster builds](#faster-builds)) | `ON` |
| `ENABLE_UNITY_BUILD` | Batch sources into per-area unity translation units. Cuts total work but is a net loss on a many-core machine — see [Faster builds](#faster-builds) | `OFF` |
| `WITH_FIXMES` | Build with fixme messages | `OFF` |
| `WITH_MAEMO` | Build with right click mapped to F4 (menu button) | `OFF` |
| `WITH_SANITIZERS` | Instrument the game with `address`, `undefined` and/or `thread`. Linux and macOS only, and the configure step checks that the runtime actually links — see [Sanitizers](#sanitizers) | `` |
| `WITH_EDITOR_SLF` | Download the latest free editor.slf during build | `OFF` |

Example:

```sh
cmake -DCMAKE_TOOLCHAIN_FILE=./cmake/toolchain-macos.cmake -DWITH_FIXMES=ON path/to/source
make
```
