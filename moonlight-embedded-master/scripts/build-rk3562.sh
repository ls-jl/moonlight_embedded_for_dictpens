#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps_root="${RK3562_DEPS_ROOT:-${project_root}/../rk3562-deps}"
build_dir="${RK3562_BUILD_DIR:-${project_root}/build-rk3562}"
toolchain_root="${RK3562_TOOLCHAIN_ROOT:-/home/pve/MESA_for_pen/toolchains/aarch64--glibc--stable-2018.11-1}"

mkdir -p "${deps_root}"
deps_root="$(cd "${deps_root}" && pwd)"
mkdir -p "$(dirname "${build_dir}")"
build_dir="$(mkdir -p "${build_dir}" && cd "${build_dir}" && pwd)"

export RK3562_DEPS_ROOT="${deps_root}"
export RK3562_TOOLCHAIN_ROOT="${toolchain_root}"
export PKG_CONFIG_LIBDIR="${deps_root}/usr/lib/pkgconfig:${deps_root}/usr/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="${deps_root}"

cmake -S "${project_root}" -B "${build_dir}" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${project_root}/cmake/toolchains/rk3562-buildroot.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DRK3562_DEPS_ROOT="${deps_root}" \
  -DENABLE_SDL=OFF \
  -DENABLE_FFMPEG=OFF \
  -DENABLE_X11=OFF \
  -DENABLE_PULSE=OFF \
  -DENABLE_CEC=OFF \
  -DENABLE_AVAHI_DISCOVERY=OFF \
  -DENABLE_DOCS=OFF \
  -DINSTALL_GAMECONTROLLERDB=OFF \
  -DCURL_INCLUDE_DIR="${deps_root}/usr/include" \
  -DCURL_LIBRARY="${deps_root}/usr/lib/libcurl.so" \
  -DOPENSSL_ROOT_DIR="${deps_root}/usr" \
  -DOPENSSL_INCLUDE_DIR="${deps_root}/usr/include" \
  -DOPENSSL_SSL_LIBRARY="${deps_root}/usr/lib/libssl.so" \
  -DOPENSSL_CRYPTO_LIBRARY="${deps_root}/usr/lib/libcrypto.so" \
  -DEXPAT_INCLUDE_DIR="${deps_root}/usr/include" \
  -DEXPAT_LIBRARY="${deps_root}/usr/lib/libexpat.so" \
  -DLibUUID_INCLUDE_DIR="${deps_root}/usr/include" \
  -DLibUUID_LIBRARY="${deps_root}/usr/lib/libuuid.so" \
  -DALSA_INCLUDE_DIR="${deps_root}/usr/include" \
  -DALSA_LIBRARY="${deps_root}/usr/lib/libasound.so" \
  -DOPUS_INCLUDE_DIR="${deps_root}/usr/include" \
  -DOPUS_LIBRARY="${deps_root}/usr/lib/libopus.so" \
  -DDRM_INCLUDE_DIR="${deps_root}/usr/include/libdrm" \
  -DDRM_LIBRARY="${deps_root}/usr/lib/libdrm.so" \
  -DROCKCHIP_INCLUDE_DIR="${deps_root}/usr/include" \
  -DROCKCHIP_LIBRARY="${deps_root}/usr/lib/librockchip_mpp.so"

cmake --build "${build_dir}" --parallel

file "${build_dir}/moonlight"
