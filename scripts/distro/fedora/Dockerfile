# Fedora's own Qt and ONNX Runtime; HEVC from RPM Fusion.
FROM fedora:42
RUN dnf install -y https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-42.noarch.rpm \
    && dnf install -y gcc-c++ cmake ninja-build pkgconf qt6-qtbase-devel qt6-qtimageformats libheif-devel libheif-freeworld \
        fontconfig-devel onnxruntime-devel dejavu-sans-fonts dejavu-serif-fonts dejavu-sans-mono-fonts curl rpm-build file \
    && dnf clean all
# U²-Net for Remove Background, pinned by its checksum.
RUN mkdir -p /usr/local/share/omaphoto \
    && curl -fsSL -o /usr/local/share/omaphoto/u2net.onnx \
        https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net.onnx \
    && echo "8d10d2f3bb75ae3b6d527c77944fc5e7dcd94b29809d47a739a7a728a912b491  /usr/local/share/omaphoto/u2net.onnx" | sha256sum -c -
