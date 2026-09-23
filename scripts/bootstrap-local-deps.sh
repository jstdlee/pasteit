#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "${script_dir}/.." && pwd)"
package_dir="${project_dir}/.deps/packages"
sysroot_dir="${project_dir}/.deps/sysroot"

mkdir -p "${package_dir}" "${sysroot_dir}"

packages=(
  libgl-dev
  libglvnd-core-dev
  libglx-dev
  libopengl-dev
  libpthread-stubs0-dev
  libx11-dev
  libxau-dev
  libxcb1-dev
  libxcursor-dev
  libxdmcp-dev
  libxext-dev
  libxi-dev
  libxinerama-dev
  libxrandr-dev
  libxrender-dev
  x11proto-dev
  xtrans-dev
)

cd "${package_dir}"
apt-get download "${packages[@]}"
find . -maxdepth 1 -name '*.deb' -exec dpkg-deb -x {} "${sysroot_dir}" \;

echo "PasteIt headers are ready in ${sysroot_dir}. No system packages were installed."
