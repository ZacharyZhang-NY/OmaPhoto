#!/usr/bin/env bash
# Installs OmaPhoto's latest DEB on Ubuntu and its derivatives.
set -euo pipefail
base=https://github.com/ZacharyZhang-NY/OmaPhoto/releases/latest/download
package=omaphoto_amd64.deb
. /etc/os-release
case " ${ID:-} ${ID_LIKE:-} " in
*" ubuntu "*) ;;
*)
    echo "This script is for Ubuntu and its derivatives; this system is ${PRETTY_NAME:-unknown}." >&2
    exit 1
    ;;
esac
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
# apt reads the file as its own user.
chmod a+rx "$work"
sudo apt-get install -y "$work/$package"
