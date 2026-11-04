#!/usr/bin/env bash
# Builds the patched Dawn package the Linux game uses: dawn-linux-x86_64.tar.gz, a `cmake --install` tree of Dawn
# with tools/dawn/vulkan-msaa-2x-8x.patch applied (2x and 8x MSAA on Vulkan). It is built the way the stock package
# is (github.com/theofficialgman/dawn-build, release v20260603.191052): the same Dawn commit and cache files, clang 18
# on Ubuntu 22.04, here in a Docker container so the package links on older distributions too.
#   tools/dawn/build-dawn-linux.sh            # -> dawn-build/dawn-linux-x86_64.tar.gz (git-ignored) and its SHA-256
# Then upload the tarball as a release asset and update AURORA_DAWN_LINUX_PATCHED_URL / _HASH in
# wiicompiled/aurora-main/cmake/AuroraDawnProvider.cmake. Needs Docker and git; the first build takes a while.
set -euo pipefail

# The Dawn commit behind AURORA_DAWN_VERSION v20260603.191052 (that release's notes), and the dawn-build commit whose
# cache file the stock packages use.
dawn_commit=13abc3bc8ea2d3c2050f9e77a12d012108ceee24
dawn_build_commit=1627fcf1b6a52848f5772794ea97f120af1839ed

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
work="$root/dawn-build"
patch="$root/tools/dawn/vulkan-msaa-2x-8x.patch"
mkdir -p "$work"

fetch() {  # fetch <repo> <commit> <dir>: that exact commit, shallow
    local repo="$1" commit="$2" dir="$3"
    if [[ ! -d "$dir/.git" ]]; then
        git init -q "$dir"
        git -C "$dir" remote add origin "$repo"
    fi
    git -C "$dir" fetch -q --depth 1 origin "$commit"
    git -C "$dir" checkout -q --force FETCH_HEAD
    git -C "$dir" clean -qfdx -e third_party
}
fetch https://github.com/google/dawn "$dawn_commit" "$work/dawn"
fetch https://github.com/theofficialgman/dawn-build "$dawn_build_commit" "$work/dawn-build"
git -C "$work/dawn" apply "$patch"

# Tools need root inside the container; the build itself runs as the calling user so the files stay theirs.
docker run --rm -v "$work:/work" -w /work -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" ubuntu:22.04 bash -euo pipefail -c '
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends ca-certificates curl gnupg git python3 python3-pip lsb-release \
        software-properties-common libwayland-dev libxkbcommon-dev libxrandr-dev libxinerama-dev libxcursor-dev \
        mesa-common-dev libx11-xcb-dev libxi-dev >/dev/null
    curl -fsSL https://apt.llvm.org/llvm.sh -o /tmp/llvm.sh
    bash /tmp/llvm.sh 18 >/dev/null
    ln -sf /usr/bin/clang-18 /usr/local/bin/clang
    ln -sf /usr/bin/clang++-18 /usr/local/bin/clang++
    ln -sf /usr/bin/ld.lld-18 /usr/local/bin/ld.lld
    python3 -m pip install -q cmake ninja
    groupadd -g "$HOST_GID" builder 2>/dev/null || true
    useradd -u "$HOST_UID" -g "$HOST_GID" -M -d /tmp builder 2>/dev/null || true
    su builder -s /bin/bash -c "set -euo pipefail
        export PATH=/usr/local/bin:\$PATH HOME=/tmp
        git config --global --add safe.directory \"*\"
        cmake -S dawn -B build -G Ninja \
            -C dawn/.github/workflows/dawn-ci.cmake -C dawn-build/.github/workflows/dawn-ci.cmake \
            -DCMAKE_BUILD_TYPE=Release
        cmake --build build --config Release
        rm -rf install
        cmake --install build --config Release --prefix install
        tar -czf dawn-linux-x86_64.tar.gz -C install ."
'
echo "DONE: $work/dawn-linux-x86_64.tar.gz"
sha256sum "$work/dawn-linux-x86_64.tar.gz"
