#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
image=omaphoto-dev
runtime=/run/user/$(id -u)

docker build -q -t "$image" -f "$repo/Dockerfile.dev" "$repo" >/dev/null

in_container() {
    exec docker run --rm --init -u "$(id -u):$(id -g)" -e HOME=/tmp --tmpfs /tmp:rw,exec,size=4g \
        --tmpfs "$runtime:uid=$(id -u),gid=$(id -g),mode=0700" -e XDG_RUNTIME_DIR="$runtime" \
        -v "$repo:$repo" -w "$repo" "$@"
}

configure_and_build='cmake -S . -B build -G Ninja -DOMAPHOTO_WERROR=ON && cmake --build build'

case "${1:-}" in
build)
    in_container "$image" sh -c "$configure_and_build"
    ;;
test)
    in_container "$image" sh -c "$configure_and_build && ctest --test-dir build --output-on-failure --timeout 120"
    ;;
run)
    socket=$WAYLAND_DISPLAY
    [[ $socket = /* ]] || socket=$XDG_RUNTIME_DIR/$socket
    socket=$(readlink -f "$socket")
    # The home folder at its own path: files, settings, theme.
    mounts=(-v "$HOME:$HOME" -e HOME="$HOME")
    # The GPU's render nodes, which Mesa's EGL needs.
    for node in /dev/dri/renderD*; do
        [[ -e $node ]] && mounts+=(--device "$node" --group-add "$(stat -c %g "$node")")
    done
    in_container "${mounts[@]}" -v "$socket:$runtime/wayland-0" -e WAYLAND_DISPLAY=wayland-0 \
        -e QT_QPA_PLATFORM=wayland "$image" sh -c "$configure_and_build && exec build/omaphoto \"\$@\"" sh "${@:2}"
    ;;
*)
    echo "usage: dev.sh build|test|run [args]" >&2
    exit 2
    ;;
esac
