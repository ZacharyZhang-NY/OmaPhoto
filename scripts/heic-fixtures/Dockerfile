FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ qt6-base-dev libheif-examples libheif-plugin-x265 libheif-plugin-libde265 \
    && rm -rf /var/lib/apt/lists/*
