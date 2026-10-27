#!/bin/sh
# Cross-builds platforms/kindle in the arm64 builder image (see doc/kindle.md for the inputs). Output: build/kindle/package.
#
#   KINDLE_SYSROOT   koxtoolchain kindlehf sysroot (.../arm-kindlehf-linux-gnueabihf/sysroot)
#   KINDLE_QT        Qt 6.11.3 build directory made by sdk/build-qt.sh (holds host/ and install/)
#   KINDLE_QTSENSORS install prefix of qtsensors 6.11.3 built for the Kindle
#   KINDLE_FBINK     FBInk tree with Release/libfbink.a built for the Kindle
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
: "${KINDLE_SYSROOT:?}" "${KINDLE_QT:?}" "${KINDLE_QTSENSORS:?}" "${KINDLE_FBINK:?}"
mkdir -p "$root/build/kindle"
exec docker run --rm --platform linux/arm64 \
  -v "$root":/src -v "$root/build/kindle":/out -v "$KINDLE_QT":/build -v "$KINDLE_QTSENSORS":/ssinstall:ro \
  -v "$KINDLE_FBINK":/fbink:ro -v "$here/toolchain.cmake":/toolchain.cmake:ro -v "$KINDLE_SYSROOT":/kindle-sysroot:ro \
  "${KINDLE_BUILDER:-kobalt-kindle-builder}" bash -euc "
    cmake -S /src/platforms/kindle -B /out -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=/build/install/lib/cmake/Qt6/qt.toolchain.cmake -DQT_HOST_PATH=/build/host \
      -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH=/ssinstall -DQT_NO_PRIVATE_MODULE_WARNING=ON -DFBINK_SOURCE=/fbink \
      -DKOBALT_DEPENDENCY_JOBS=\${JOBS:-4} \
    && cmake --build /out --target package --parallel \${JOBS:-4}"
