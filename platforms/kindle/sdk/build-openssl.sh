#!/bin/sh
# Cross-builds OpenSSL for the Kindle in the builder image. Qt links it for TLS.
#
#   KINDLE_SYSROOT      koxtoolchain kindlehf sysroot
#   KINDLE_OPENSSL_SRC  an unpacked OpenSSL source tree (3.5.9 was used, an LTS release)
#   KINDLE_OPENSSL      empty directory that receives the install prefix (lib/, include/)
set -eu
: "${KINDLE_SYSROOT:?}" "${KINDLE_OPENSSL_SRC:?}" "${KINDLE_OPENSSL:?}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$KINDLE_OPENSSL"
# The default certificate store is /etc/ssl, where the Kindle keeps its own CA bundle (ca-certificates.crt).
docker run --rm --platform linux/arm64 \
  -v "$KINDLE_OPENSSL_SRC":/src:ro -v "$work":/build -v "$KINDLE_OPENSSL":/ossl -v "$KINDLE_SYSROOT":/kindle-sysroot:ro \
  "${KINDLE_BUILDER:-kobalt-kindle-builder}" bash -euc '
    cd /build
    /src/Configure linux-armv4 --cross-compile-prefix=arm-linux-gnueabihf- --prefix=/ossl --libdir=lib --openssldir=/etc/ssl \
      shared no-tests no-docs no-apps no-module no-legacy no-ssl3 no-comp no-idea no-seed no-rc5 no-md2 no-mdc2 \
      --sysroot=/kindle-sysroot -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -O2
    make -j${JOBS:-4} build_libs
    make install_sw'
