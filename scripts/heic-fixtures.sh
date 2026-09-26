#!/usr/bin/env bash
# Writes the HEIC fixtures; the dev image has no encoder.
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
generator=$repo/scripts/heic-fixtures
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

docker build -q -t omaphoto-heic-fixtures "$generator" >/dev/null
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen \
    -v "$generator:/generator:ro" -v "$work:/work" -v "$repo/tests/fixtures:/fixtures" -w /work \
    omaphoto-heic-fixtures /generator/generate.sh
