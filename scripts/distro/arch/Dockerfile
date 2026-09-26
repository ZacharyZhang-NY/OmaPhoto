# Arch and Omarchy: Qt, GCC and ONNX Runtime as shipped.
FROM archlinux:latest
RUN pacman -Syu --noconfirm --needed base-devel cmake ninja pkgconf qt6-base qt6-imageformats libheif libde265 \
        fontconfig onnxruntime-cpu ttf-dejavu curl \
    && pacman -Scc --noconfirm
# U²-Net for Remove Background, pinned by its checksum.
RUN mkdir -p /usr/local/share/omaphoto \
    && curl -fsSL -o /usr/local/share/omaphoto/u2net.onnx \
        https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net.onnx \
    && echo "8d10d2f3bb75ae3b6d527c77944fc5e7dcd94b29809d47a739a7a728a912b491  /usr/local/share/omaphoto/u2net.onnx" | sha256sum -c -
