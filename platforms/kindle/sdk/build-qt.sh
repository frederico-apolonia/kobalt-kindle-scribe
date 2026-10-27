#!/bin/bash
set -euo pipefail
src=/src
build=/build
jobs=${JOBS:-2}

cmake -S "$src" -B "$build/host" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
  -DFEATURE_gui=OFF -DFEATURE_widgets=OFF -DFEATURE_network=OFF \
  -DFEATURE_sql=OFF -DFEATURE_dbus=OFF -DFEATURE_icu=OFF \
  -DFEATURE_glib=OFF
cmake --build "$build/host" --target host_tools --parallel "$jobs"

cmake -S "$src" -B "$build/target" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$build/install" \
  -DCMAKE_TOOLCHAIN_FILE=/toolchain.cmake -DQT_HOST_PATH="$build/host" \
  -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
  '-DQT_QPA_PLATFORMS=offscreen;minimal' -DQT_QPA_DEFAULT_PLATFORM=offscreen \
  -DINPUT_opengl=no -DFEATURE_opengl=OFF -DFEATURE_egl=OFF -DFEATURE_eglfs=OFF \
  -DFEATURE_xcb=OFF -DFEATURE_xlib=OFF -DFEATURE_vulkan=OFF \
  -DFEATURE_linuxfb=OFF -DFEATURE_vnc=OFF -DFEATURE_directfb=OFF \
  -DFEATURE_dbus=OFF -DFEATURE_glib=OFF -DFEATURE_icu=OFF -DFEATURE_fontconfig=OFF \
  -DFEATURE_system_zlib=OFF -DFEATURE_system_pcre2=OFF \
  -DFEATURE_system_freetype=OFF -DFEATURE_system_harfbuzz=OFF \
  -DFEATURE_openssl=OFF -DFEATURE_printsupport=OFF \
  -DFEATURE_accessibility=ON -DFEATURE_sessionmanager=OFF \
  -DFEATURE_evdev=OFF -DFEATURE_libinput=OFF -DFEATURE_tslib=OFF
cmake --build "$build/target" --parallel "$jobs"
cmake --install "$build/target"
