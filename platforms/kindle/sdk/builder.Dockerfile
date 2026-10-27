FROM --platform=linux/arm64 ubuntu:22.04
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake ninja-build perl python3 pkg-config \
    libgl-dev libglib2.0-dev libdbus-1-dev \
    gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf binutils-arm-linux-gnueabihf && \
    rm -rf /var/lib/apt/lists/*
