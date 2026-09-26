#!/bin/sh
# Runs inside the container: /generator read-only, /fixtures to fill.
set -eu

qt=/usr/include/x86_64-linux-gnu/qt6
g++ -std=c++20 -fPIC -I$qt -I$qt/QtCore -I$qt/QtGui /generator/sources.cpp -o sources -lQt6Gui -lQt6Core
./sources
encode="heif-enc -q 100 -p chroma=444"
$encode -o /fixtures/red.heic red.png
$encode --premultiplied-alpha -o /fixtures/premultiplied.heic premultiplied.png
$encode -o /fixtures/turned-p3.heic turned-p3.jpg
# Lossless, no ICC profile: the nclx box names Display P3.
heif-enc -L -p chroma=444 --matrix_coefficients=0 --colour_primaries 12 --transfer_characteristic 13 -o /fixtures/p3-nclx.heic warm.png
for name in red premultiplied turned-p3 p3-nclx; do
    heif-info /fixtures/$name.heic | grep -E "^image|color profile|angle"
done
