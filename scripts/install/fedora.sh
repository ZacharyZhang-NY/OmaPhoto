#!/usr/bin/env bash
# Installs OmaPhoto's latest RPM on Fedora 43 or 44.
set -euo pipefail
base=https://github.com/ZacharyZhang-NY/OmaPhoto/releases/latest/download
. /etc/os-release
release=
case " ${ID:-} ${ID_LIKE:-} " in
*" fedora "*) release=${VERSION_ID:-} ;;
esac
# Each RPM names its release's LibRaw.
case "$release" in
43 | 44) ;;
*)
    echo "This script is for Fedora 43 or 44; this system is ${PRETTY_NAME:-unknown}." >&2
    exit 1
    ;;
esac
package=omaphoto-fc${release}.x86_64.rpm
if [ "$(uname -m)" != x86_64 ]; then
    echo "OmaPhoto's packages are built for x86_64 alone." >&2
    exit 1
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
curl -fL --proto '=https' -o "$work/$package" "$base/$package"
curl -fsSL --proto '=https' -o "$work/SHA256SUMS" "$base/SHA256SUMS"
# The package must match the checksum published beside it.
(cd "$work" && grep " $package\$" SHA256SUMS | sha256sum -c -)
sudo dnf install -y "$work/$package"
# Fedora's own libheif decodes no HEVC; RPM Fusion's does.
if ! rpm -q libheif-freeworld >/dev/null 2>&1; then
    echo "HEIC import needs RPM Fusion's libheif-freeworld: https://rpmfusion.org/Configuration, then sudo dnf install libheif-freeworld"
fi
