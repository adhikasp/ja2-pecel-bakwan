#!/usr/bin/env bash
#
# Set up the CI environment
#
# Requires the following environment variables:
#   CI_TARGET - target we are building for: linux/linux-mingw/mac
# Optional:
#   TARGET_GCC_MAJOR_VERSION - build the linux target with this GCC instead of the one in
#                              ./min-gcc-version (an experiment, never a pin)

set -e
set -x

echo "CI_TARGET: $CI_TARGET"

source "$(dirname "${BASH_SOURCE[0]}")/ci-functions.sh"

echo "## prepare environment ##"
if [[ "$CI_TARGET" == "linux" ]]; then
    # The compiler this project builds and tests with. ./min-gcc-version is the pin every
    # workflow shares, the same way ./min-rust-version is; TARGET_GCC_MAJOR_VERSION
    # overrides it for a one-off experiment. It was GCC 10 until #289, which made the CI
    # compiler the oldest thing in the loop: too old for -std=c++23 (GCC 11 has it first),
    # six major versions behind the GCC 16 the tree is developed on. Raise it together with
    # the runner image in github-ci.yml, and check that the image has a package for it —
    # 14 is in ubuntu-24.04's universe, 16 is in no runner image yet.
    GCC_VER="${TARGET_GCC_MAJOR_VERSION:-$(cat ./min-gcc-version)}"

    # compiler toolchain
    linux-install-via-apt-get "gcc-$GCC_VER" "g++-$GCC_VER"

    # make gcc-N / g++-N the default gcc / g++, so cmake finds the pinned compiler
    linux-set-gcc-version "$GCC_VER"

    # and check that it really is: a build on an older compiler than the pin is the failure
    # mode this pin exists to prevent, so it has to fail loudly rather than pass quietly
    linux-assert-gcc-at-least "$GCC_VER"

    # sccache for compilation caching
    linux-install-sccache

    # Rust via Rustup
    unix-install-rustup

    # Appimage build tools (linuxdeploy and appimagelint)
    linux-install-appimage-build-tools
elif [[ "$CI_TARGET" == "linux-mingw64" ]]; then
    # Deliberately no GCC pin here. The cross compiler is whatever the runner image ships,
    # and mingw-w64 is one GCC version per Ubuntu release: ubuntu-24.04 has 13.2 and no
    # newer, so this job cannot track ./min-gcc-version without pulling in a third-party
    # toolchain (the mingw-w64 builds are upstream's own, not Ubuntu's). It used to read
    # TARGET_GCC_MAJOR_VERSION into a variable nothing used, which looked like a pin and
    # was not one. The version actually used is printed at the end of this script; treat
    # the native MSYS2 build (COMPILATION.md, "Compiler versions") as the authority for the
    # Windows toolchain.

    # MinGW compiler for cross-compiling
    linux-install-via-apt-get build-essential mingw-w64 g++-mingw-w64-x86-64-posix

    # sccache for compilation caching
    linux-install-sccache

    # Rust via Rustup
    unix-install-rustup x86_64-pc-windows-gnu
elif [[ "$CI_TARGET" == "mac" ]]; then
    # sccache for compilation caching
    macOS-install-via-brew sccache

    # gtest
    macOS-install-via-brew googletest

    # Google Cloud SDK for Artifact Upload
    macOS-install-via-brew-cask google-cloud-sdk
    source "$(brew --prefix)/share/google-cloud-sdk/path.bash.inc"

    # Rust via Rustup
    unix-install-rustup
elif [[ "$CI_TARGET" == windows-* ]]; then
    # sccache for compilation caching
    windows-install-via-chocolatey sccache

    # Google Cloud SDK for Artifact Upload
    windows-install-google-cloud-sdk

    # Rust via Rustup
    if [[ "$CI_TARGET" == "windows-msvc-x86" ]]; then
        windows-install-rustup i686-pc-windows-msvc
    elif [[ "$CI_TARGET" == "windows-msvc-amd64" ]]; then
        windows-install-rustup x86_64-pc-windows-msvc
    else
        echo "unexpected target ${CI_TARGET}"
        exit 1
    fi
elif [[ "$CI_TARGET" == "android" ]]; then
    # Ninja build system
    linux-install-via-apt-get ninja-build

    # sccache for compilation caching
    linux-install-sccache

    # Rust via Rustup
    unix-install-rustup armv7-linux-androideabi aarch64-linux-android i686-linux-android x86_64-linux-android

    # Specific version of Android NDK
    linux-install-via-android-sdkmanager "ndk;29.0.14206865"

    linux-setup-android-signing-keys
else
    echo "unexpected target ${CI_TARGET}"
    exit 1
fi

# print build environment info
rustup show
which rustc
rustc -V
which cargo
cargo -V
cargo clippy -- -V
cargo fmt -- -V
which cmake
cmake --version
sccache -V
which gcloud
gcloud version
# The compiler version belongs in the log next to rustc's: it is the one number a reader
# needs to know when a warning or an error is version-dependent, and it is what proves the
# pin in ./min-gcc-version (Linux) or the runner image (cross) was actually applied.
if [[ "$CI_TARGET" == "linux" ]]; then
    which gcc
    gcc --version
    which g++
    g++ --version
    linuxdeploy --version
    appimagelint --version
elif [[ "$CI_TARGET" == "linux-mingw64" ]]; then
    which x86_64-w64-mingw32-gcc
    x86_64-w64-mingw32-gcc --version
    which x86_64-w64-mingw32-g++
    x86_64-w64-mingw32-g++ --version
fi
