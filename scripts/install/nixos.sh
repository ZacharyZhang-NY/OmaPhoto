#!/usr/bin/env bash
# Installs OmaPhoto on NixOS from its flake.
set -euo pipefail
if ! command -v nix >/dev/null; then
    echo "This script needs Nix; NixOS carries it." >&2
    exit 1
fi
if [ "$(uname -m)" != x86_64 ]; then
    echo "OmaPhoto's flake builds for x86_64 alone." >&2
    exit 1
fi
# The newest release's tag, as GitHub redirects to it.
latest=$(curl -fsSLI --proto '=https' -o /dev/null -w '%{url_effective}' https://github.com/ZacharyZhang-NY/OmaPhoto/releases/latest)
tag=${latest##*/}
case "$tag" in
v[0-9]*) ;;
*)
    echo "No release found at $latest." >&2
    exit 1
    ;;
esac
nix --extra-experimental-features "nix-command flakes" profile install "github:ZacharyZhang-NY/OmaPhoto/$tag"
