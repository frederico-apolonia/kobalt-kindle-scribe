#!/bin/sh
# Makes the sysroot that Kobalt builds against: a copy of the koxtoolchain kindlehf sysroot with zlib and libpng
# added as static libraries. The Kobo SDK ships both; the Kindle sysroot ships neither, and Qt keeps its own copies
# private. With them in the sysroot the shared dependency build finds libpng through find_package(PNG) and links -lz
# as it does for Kobo, so dependencies/ needs no Kindle case. The toolchain's own sysroot is left as it is.
#
#   KINDLE_SYSROOT        koxtoolchain kindlehf sysroot (.../arm-kindlehf-linux-gnueabihf/sysroot), read only here
#   KINDLE_SYSROOT_LIBS   empty directory that receives the copy; later steps use it as KINDLE_SYSROOT
#   KINDLE_ZLIB_SRC       unpacked zlib source (1.3.1)
#   KINDLE_PNG_SRC        unpacked libpng source (1.6.59)
#
# Run once. The versions are the ones the other targets pin in dependencies/manifest.json.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
: "${KINDLE_SYSROOT:?}" "${KINDLE_SYSROOT_LIBS:?}" "${KINDLE_ZLIB_SRC:?}" "${KINDLE_PNG_SRC:?}"
mkdir -p "$KINDLE_SYSROOT_LIBS"
[ -z "$(ls -A "$KINDLE_SYSROOT_LIBS")" ] || { echo "build-sysroot-libs: $KINDLE_SYSROOT_LIBS is not empty" >&2; exit 1; }
# The toolchain builds its sysroot read only.
cp -R "$KINDLE_SYSROOT/." "$KINDLE_SYSROOT_LIBS/"
chmod -R u+w "$KINDLE_SYSROOT_LIBS"
docker run --rm --platform linux/arm64 \
  -v "$KINDLE_ZLIB_SRC":/zlib-src:ro -v "$KINDLE_PNG_SRC":/png-src:ro -v "$KINDLE_SYSROOT_LIBS":/kindle-sysroot \
  -v "$here/toolchain.cmake":/toolchain.cmake:ro \
  "${KINDLE_BUILDER:-kobalt-kindle-builder}" bash -euc '
    # The zlib CMake renames a file in its own source tree, so both build from copies.
    cp -r /zlib-src /zlib && cp -r /png-src /png
    flags="-DCMAKE_TOOLCHAIN_FILE=/toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/kindle-sysroot/usr
           -DCMAKE_POSITION_INDEPENDENT_CODE=ON"
    cmake -S /zlib -B /tmp/zlib -G Ninja $flags -DZLIB_BUILD_EXAMPLES=OFF
    cmake --build /tmp/zlib && cmake --install /tmp/zlib
    # Only the static archive: nothing may depend on a libz.so at run time.
    rm -f /kindle-sysroot/usr/lib/libz.so*
    cmake -S /png -B /tmp/png -G Ninja $flags -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF
    cmake --build /tmp/png && cmake --install /tmp/png'
