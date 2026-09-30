# A supported Fedora, 43 unless named; HEVC from RPM Fusion.
ARG RELEASE=43
FROM fedora:${RELEASE}
ARG RELEASE
RUN dnf install -y https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-${RELEASE}.noarch.rpm \
    && dnf install -y gcc-c++ cmake ninja-build pkgconf qt6-qtbase-devel qt6-qtsvg-devel qt6-qtimageformats libheif-devel LibRaw-devel libheif-freeworld \
        fontconfig-devel dejavu-sans-fonts dejavu-serif-fonts dejavu-sans-mono-fonts curl rpm-build file \
    && dnf clean all
# The ONNX Runtime the packages carry: Microsoft's own build.
RUN curl -fsSL -o /tmp/ort.tgz \
        https://github.com/microsoft/onnxruntime/releases/download/v1.21.0/onnxruntime-linux-x64-1.21.0.tgz \
    && echo "7485c7e7aac6501b27e353dcbe068e45c61ab51fbaf598d13970dfae669d20bf  /tmp/ort.tgz" | sha256sum -c - \
    && mkdir -p /tmp/ort /usr/local/include/onnxruntime /usr/local/lib64 \
    && tar -xzf /tmp/ort.tgz -C /tmp/ort --strip-components=1 \
    && cp -r /tmp/ort/include/. /usr/local/include/onnxruntime/ \
    && mkdir -p /usr/local/lib/cmake && mv /tmp/ort/lib/cmake/onnxruntime /usr/local/lib/cmake/ \
    && cp -a /tmp/ort/lib/. /usr/local/lib64/ \
    && mkdir -p /usr/local/share/doc/onnxruntime \
    && cp /tmp/ort/LICENSE /tmp/ort/ThirdPartyNotices.txt /usr/local/share/doc/onnxruntime/ \
    && echo /usr/local/lib64 > /etc/ld.so.conf.d/onnxruntime.conf && ldconfig \
    && rm -rf /tmp/ort /tmp/ort.tgz
# U²-Net for Remove Background, pinned by its checksum.
RUN mkdir -p /usr/local/share/omaphoto \
    && curl -fsSL -o /usr/local/share/omaphoto/u2net.onnx \
        https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net.onnx \
    && echo "8d10d2f3bb75ae3b6d527c77944fc5e7dcd94b29809d47a739a7a728a912b491  /usr/local/share/omaphoto/u2net.onnx" | sha256sum -c -
