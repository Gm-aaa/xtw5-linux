#!/usr/bin/env bash
# Run inside the distribution container as root. No host USB is passed through.
set -euo pipefail
platform=${1:?distribution required}
cd /source
case "$platform" in
  ubuntu-*|debian-*)
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y --no-install-recommends build-essential cmake ninja-build pkg-config qt6-base-dev libusb-1.0-0-dev libssl-dev libgl-dev libopengl-dev qt6-qpa-plugins dpkg-dev file fonts-noto-cjk
    generator=DEB
    ;;
  fedora-*|rocky-*)
    if [[ "$platform" == rocky-* ]]; then
        dnf install -y dnf-plugins-core epel-release
        dnf config-manager --set-enabled crb
    fi
    dnf install -y gcc-c++ cmake ninja-build pkgconf-pkg-config qt6-qtbase-devel libusb1-devel openssl-devel rpm-build diffutils tar gzip findutils
    generator=RPM
    ;;
  opensuse-*)
    zypper --non-interactive install gcc-c++ cmake ninja pkg-config qt6-base-devel libusb-1_0-devel libopenssl-devel rpm-build tar gzip findutils
    generator=RPM
    ;;
  arch-*)
    pacman -Syu --noconfirm --needed base-devel cmake ninja qt6-base libusb openssl
    generator=ARCH
    ;;
  *) echo "Unknown platform: $platform" >&2; exit 1 ;;
esac
build="/source/build-$platform"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DXTW_PACKAGE_PLATFORM="$platform"
cmake --build "$build" --parallel 2
# Tests use virtual transports, and never access USB hardware.
ctest --test-dir "$build" --output-on-failure
mkdir -p /source/dist
(cd "$build" && cpack -G TGZ)
cp "$build"/*.tar.gz /source/dist/
if [[ "$generator" == ARCH ]]; then
    DESTDIR="$build/stage" cmake --install "$build" --strip
    mkdir -p "$build/arch-package"
    cp packaging/PKGBUILD "$build/arch-package/"
    useradd -m xtwbuild
    chown -R xtwbuild:xtwbuild "$build/arch-package"
    (cd "$build/arch-package" && runuser -u xtwbuild -- env XTW_STAGE="$build/stage" makepkg --nodeps --force)
    cp "$build/arch-package"/*.pkg.tar.zst /source/dist/
else
    (cd "$build" && cpack -G "$generator")
    if [[ "$generator" == DEB ]]; then
        cp "$build"/*.deb /source/dist/
        dpkg -i "$build"/*.deb
    else
        cp "$build"/*.rpm /source/dist/
        rpm -Uvh "$build"/*.rpm
    fi
fi
# Smoke test the archive without inheriting user settings or requiring a display/USB.
mkdir -p "$build/archive-check"
tar -xzf "$build"/*.tar.gz -C "$build/archive-check"
archive_bin=$(find "$build/archive-check" -type f -path '*/bin/xtw5-linux' -print -quit)
test -n "$archive_bin"
QT_QPA_PLATFORM=offscreen "$archive_bin" --demo --screenshot "$build/archive-check/demo.png"
test -s "$build/archive-check/demo.png"
find /source/dist -type f -exec chmod a+r '{}' +
