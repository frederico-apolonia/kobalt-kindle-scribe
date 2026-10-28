#!/bin/sh
# Installs Kobalt builds on a Kindle over SSH, as two installations: the one you read with and a development copy.
#
#   /mnt/us/kobalt       reading install. Changes only through `promote` and `rollback`.
#   /mnt/us/kobalt-dev   development install ("Kobalt Dev" in the Library), with its own state and log.
#   /mnt/us/kobalt-prev  the app files `promote` replaced, kept for `rollback`.
#
# Usage: platforms/kindle/deploy.sh <command>
#   init-dev   create the development install from the reading one (app files and a copy of its state) and its
#              Library entry. Run once.
#   dev        install the build in $KINDLE_PACKAGE into the development install.
#   seed-dev   replace the development state with a fresh copy of the reading state.
#   promote    make the reading install run what the development install runs. State stays where it is.
#   rollback   swap the reading install back to the files `promote` replaced. Running it again swaps forward.
#
# Environment: KINDLE_SSH (default root@192.168.2.18), KINDLE_SSH_PORT (default 2222), KINDLE_PACKAGE (the
# `package` directory a build produced; only for `dev`).
#
# Nothing is touched while a Kobalt runs: the running binary cannot be replaced, and the launcher must not be
# changed under a running session.
set -eu

HOST=${KINDLE_SSH:-root@192.168.2.18}
PORT=${KINDLE_SSH_PORT:-2222}
READING=/mnt/us/kobalt
DEV=/mnt/us/kobalt-dev
PREV=/mnt/us/kobalt-prev
DOCUMENTS=/mnt/us/documents
# What belongs to the program, as opposed to its state (state/ and kobalt.log).
APP_ITEMS="kobalt kobalt.sh fbguard qt.conf assets lib plugins"
HERE=$(cd "$(dirname "$0")" && pwd)

remote() {
    ssh -p "$PORT" -o ConnectTimeout=10 "$HOST" "$@"
}
send() {
    scp -P "$PORT" -o ConnectTimeout=10 -r "$@"
}
require_idle() {
    remote '[ ! -d /var/tmp/kobalt-display.lock ] && ! pgrep -x kobalt >/dev/null' ||
        { echo "deploy: a Kobalt is running on the Kindle; exit it first (More > Exit Kobalt)" >&2; exit 1; }
}

case "${1:-}" in
init-dev)
    require_idle
    remote "[ ! -e $DEV ]" || { echo "deploy: $DEV exists already" >&2; exit 1; }
    remote "cp -a $READING $DEV && rm -f $DEV/kobalt.log"
    # scp on the Kindle (Dropbear) does not take a space in the remote name, so rename afterwards.
    send "$HERE/package/library-entry-dev.sh" "$HOST:$DOCUMENTS/Kobalt-Dev.tmp"
    remote "mv $DOCUMENTS/Kobalt-Dev.tmp '$DOCUMENTS/Kobalt Dev.sh'"
    echo "deploy: created $DEV and its Library entry"
    ;;
dev)
    : "${KINDLE_PACKAGE:?KINDLE_PACKAGE must name the package directory of a build}"
    [ -x "$KINDLE_PACKAGE/kobalt" ] || { echo "deploy: no $KINDLE_PACKAGE/kobalt" >&2; exit 1; }
    require_idle
    remote "rm -rf $DEV/.incoming && mkdir -p $DEV/.incoming/plugins/platforms"
    send "$KINDLE_PACKAGE/kobalt" "$KINDLE_PACKAGE/kobalt.sh" "$KINDLE_PACKAGE/fbguard" "$KINDLE_PACKAGE/assets" "$HOST:$DEV/.incoming/"
    send "$KINDLE_PACKAGE/plugins/platforms/libqkindlefb.so" "$HOST:$DEV/.incoming/plugins/platforms/"
    remote "cd $DEV && rm -rf assets && mv .incoming/assets assets && mv .incoming/kobalt .incoming/kobalt.sh .incoming/fbguard . \
        && mv .incoming/plugins/platforms/libqkindlefb.so plugins/platforms/libqkindlefb.so \
        && chmod +x kobalt kobalt.sh fbguard && rm -rf .incoming && md5sum kobalt kobalt.sh plugins/platforms/libqkindlefb.so"
    ;;
seed-dev)
    require_idle
    remote "rm -rf $DEV/state && cp -a $READING/state $DEV/state"
    echo "deploy: the development state is a fresh copy of the reading state"
    ;;
promote)
    require_idle
    remote "[ -x $DEV/kobalt ]" || { echo "deploy: there is no development install" >&2; exit 1; }
    # Copy first, swap after: an interrupted copy leaves the reading install as it was.
    remote "set -e; rm -rf $PREV $READING/.incoming; mkdir -p $PREV $READING/.incoming
        for item in $APP_ITEMS; do cp -a $DEV/\$item $READING/.incoming/\$item; done
        for item in $APP_ITEMS; do mv $READING/\$item $PREV/\$item; mv $READING/.incoming/\$item $READING/\$item; done
        rmdir $READING/.incoming; md5sum $READING/kobalt $READING/kobalt.sh"
    echo "deploy: the reading install now runs the development build; rollback restores the previous one"
    ;;
rollback)
    require_idle
    remote "[ -d $PREV ]" || { echo "deploy: nothing to roll back to" >&2; exit 1; }
    remote "set -e; rm -rf $PREV.swap; mkdir -p $PREV.swap
        for item in $APP_ITEMS; do mv $READING/\$item $PREV.swap/\$item; mv $PREV/\$item $READING/\$item; mv $PREV.swap/\$item $PREV/\$item; done
        rmdir $PREV.swap; md5sum $READING/kobalt $READING/kobalt.sh"
    echo "deploy: the reading install runs the other build again"
    ;;
*)
    sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
    exit 2
    ;;
esac
