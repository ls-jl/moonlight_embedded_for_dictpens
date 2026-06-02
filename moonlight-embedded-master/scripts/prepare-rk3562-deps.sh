#!/usr/bin/env bash
set -euo pipefail

deps_root="${1:-${RK3562_DEPS_ROOT:-}}"
rootfs="${2:-${RK3562_ROOTFS:-$HOME/rootfs-reverse/spacex/spacex-rootfs-full}}"
wpe_deps="${WPE_DEPS:-$HOME/wpe_lite/deps-aarch64-fullfat}"

if [[ -z "${deps_root}" ]]; then
  echo "usage: $0 <deps-root> [rootfs]" >&2
  exit 2
fi

mkdir -p "${deps_root}/usr/include" "${deps_root}/usr/lib/pkgconfig" "${deps_root}/usr/share/pkgconfig" "${deps_root}/src"

if [[ ! -d "${rootfs}/usr/lib" ]]; then
  echo "missing rootfs usr/lib: ${rootfs}/usr/lib" >&2
  exit 1
fi

cp -a "${rootfs}/usr/lib/"*.so* "${deps_root}/usr/lib/"

copy_header_dir() {
  local src="$1"
  local dst="$2"
  if [[ -e "${src}" ]]; then
    rm -rf "${dst}"
    cp -a "${src}" "${dst}"
  fi
}

copy_header_file() {
  local src="$1"
  local dst="$2"
  if [[ -e "${src}" ]]; then
    cp -a "${src}" "${dst}"
  fi
}

copy_header_dir "${wpe_deps}/include/openssl" "${deps_root}/usr/include/openssl"
copy_header_dir "${wpe_deps}/include/libdrm" "${deps_root}/usr/include/libdrm"
copy_header_dir "${wpe_deps}/include/libevdev-1.0" "${deps_root}/usr/include/libevdev-1.0"
copy_header_file "${wpe_deps}/include/xf86drm.h" "${deps_root}/usr/include/"
copy_header_file "${wpe_deps}/include/xf86drmMode.h" "${deps_root}/usr/include/"
copy_header_file "${wpe_deps}/include/libudev.h" "${deps_root}/usr/include/"
copy_header_file "${wpe_deps}/include/expat.h" "${deps_root}/usr/include/"
copy_header_file "${wpe_deps}/include/expat_external.h" "${deps_root}/usr/include/"
copy_header_file "${wpe_deps}/include/zlib.h" "${deps_root}/usr/include/"
copy_header_file "${wpe_deps}/include/zconf.h" "${deps_root}/usr/include/"
copy_header_dir "/usr/include/uuid" "${deps_root}/usr/include/uuid"

download() {
  local url="$1"
  local out="$2"
  if [[ ! -e "${out}" ]]; then
    curl -L --retry 3 --fail -o "${out}" "${url}"
  fi
}

src_dir="${deps_root}/src"

curl_src="${src_dir}/curl-7.79.1"
if [[ ! -d "${curl_src}" ]]; then
  download "https://curl.se/download/curl-7.79.1.tar.xz" "${src_dir}/curl-7.79.1.tar.xz"
  tar -C "${src_dir}" -xf "${src_dir}/curl-7.79.1.tar.xz"
fi
copy_header_dir "${curl_src}/include/curl" "${deps_root}/usr/include/curl"

opus_src="${src_dir}/opus-1.3.1"
if [[ ! -d "${opus_src}" ]]; then
  download "https://archive.mozilla.org/pub/opus/opus-1.3.1.tar.gz" "${src_dir}/opus-1.3.1.tar.gz"
  tar -C "${src_dir}" -xf "${src_dir}/opus-1.3.1.tar.gz"
fi
mkdir -p "${deps_root}/usr/include/opus"
cp -a "${opus_src}/include/"*.h "${deps_root}/usr/include/opus/"

alsa_src="${src_dir}/alsa-lib-1.2.5.1"
if [[ ! -d "${alsa_src}" ]]; then
  download "https://www.alsa-project.org/files/pub/lib/alsa-lib-1.2.5.1.tar.bz2" "${src_dir}/alsa-lib-1.2.5.1.tar.bz2"
  tar -C "${src_dir}" -xf "${src_dir}/alsa-lib-1.2.5.1.tar.bz2"
fi
mkdir -p "${deps_root}/usr/include/alsa"
cp -a "${alsa_src}/include/"*.h "${deps_root}/usr/include/alsa/"

mpp_src="${src_dir}/mpp"
if [[ ! -d "${mpp_src}/.git" ]]; then
  rm -rf "${mpp_src}"
  git clone --depth 1 https://github.com/rockchip-linux/mpp.git "${mpp_src}"
fi
mkdir -p "${deps_root}/usr/include/rockchip"
find "${mpp_src}" -type f -name '*.h' -exec cp -f {} "${deps_root}/usr/include/rockchip/" \;

cat > "${deps_root}/usr/lib/pkgconfig/libevdev.pc" <<'PC'
prefix=/usr
exec_prefix=${prefix}
libdir=${exec_prefix}/lib
includedir=${prefix}/include

Name: libevdev
Description: evdev device access library
Version: 1.13.0
Cflags: -I${includedir}/libevdev-1.0
Libs: -L${libdir} -levdev
PC

cat > "${deps_root}/usr/lib/pkgconfig/libudev.pc" <<'PC'
prefix=/usr
exec_prefix=${prefix}
libdir=${exec_prefix}/lib
includedir=${prefix}/include

Name: libudev
Description: udev device access library
Version: 3.2.14
Cflags: -I${includedir}
Libs: -L${libdir} -ludev
PC

cat > "${deps_root}/usr/lib/pkgconfig/libdrm.pc" <<'PC'
prefix=/usr
exec_prefix=${prefix}
libdir=${exec_prefix}/lib
includedir=${prefix}/include

Name: libdrm
Description: Userspace interface to kernel DRM services
Version: 2.4.114
Cflags: -I${includedir} -I${includedir}/libdrm
Libs: -L${libdir} -ldrm
PC

cat > "${deps_root}/usr/lib/pkgconfig/opus.pc" <<'PC'
prefix=/usr
exec_prefix=${prefix}
libdir=${exec_prefix}/lib
includedir=${prefix}/include

Name: Opus
Description: Opus audio codec
Version: 1.3.1
Cflags: -I${includedir}/opus
Libs: -L${libdir} -lopus
PC

echo "prepared RK3562 deps at ${deps_root}"
