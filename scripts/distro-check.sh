#!/usr/bin/env bash
# Builds and tests the app in one distribution's own container.
set -euo pipefail
distro=${1:?"usage: distro-check.sh arch|fedora|nixos|resolute"}
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/.." && pwd)
# Ubuntu 26.04 is the dev image on another base.
if [ "$distro" = resolute ]; then
    docker build -t omaphoto-resolute --build-arg BASE=ubuntu:26.04 -f "$repo/Dockerfile.dev" "$here"
else
    docker build -t "omaphoto-$distro" "$here/distro/$distro"
fi
steps="cmake -S . -B /tmp/build -G Ninja -DOMAPHOTO_WERROR=ON && cmake --build /tmp/build && ctest --test-dir /tmp/build --output-on-failure --timeout 120"
# Not root, as dev.sh: tests refuse what root may do.
user=1000:1000
# Nix writes its store as root; the tests drop it.
if [ "$distro" = nixos ]; then
    steps="HOME=/root nix build path:/src --no-link && HOME=/root nix develop path:/src --command chroot --userspec=1000:1000 --skip-chdir / env HOME=/scratch TMPDIR=/scratch sh -c '$steps'"
    user=0:0
fi
# The tree read-only; home and temporary files in RAM.
docker run --rm -u "$user" -e QT_QPA_PLATFORM=offscreen -e HOME=/scratch -e TMPDIR=/scratch --tmpfs /scratch:rw,exec,size=4g \
    -v "$repo:/src:ro" -w /src "omaphoto-$distro" sh -c "$steps"
