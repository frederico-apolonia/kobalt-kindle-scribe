#!/bin/sh
# Starts Kobalt on a Kindle and always gives the Amazon UI back.
#
# The framework keeps running (powerd ignores the power button while lab126_gui is stopped); its window manager
# and volume daemon are frozen like KOReader does, Kobalt owns the framebuffer, and on every exit path the
# framebuffer is restored and the UI thawed. A deadman in its own session thaws the UI if this script is killed.
#
# Refuses to start while KOReader is running or another Kobalt holds the display lock.
APP=${KOBALT_APP:-$(cd "$(dirname "$0")" && pwd)}
HOME_DIR=${KOBALT_HOME:-/mnt/us/kobalt}
LOCK=/var/tmp/kobalt-display.lock
SNAPSHOT=/tmp/kobalt-framebuffer.raw
LOG=$HOME_DIR/kobalt.log
DEADMAN_DELAY=${KOBALT_DEADMAN_DELAY:-14400}
DONE=/tmp/kobalt-session.done
PATH=$PATH:/usr/sbin:/sbin:/usr/bin:/bin
export PATH

UI_FROZEN=0
AWESOME_STOPPED=0
VOLUMD_STOPPED=0
SNAPSHOT_READY=0
LOCKED=0

mkdir -p "$HOME_DIR" 2>/dev/null
exec >>"$LOG" 2>&1
echo "kobalt: start $(date)"

for process in /proc/[0-9]*; do
    command=$(tr '\000' ' ' <"$process/cmdline" 2>/dev/null) || command=
    case "$command" in
        *"/var/tmp/koreader.sh"*|*"/mnt/us/koreader/koreader.sh"*|*"reader.lua"*)
            echo "kobalt: refusing, KOReader is running"
            exit 73
            ;;
    esac
done

thaw_ui() {
    [ "$VOLUMD_STOPPED" -eq 1 ] && killall -CONT volumd
    [ "$AWESOME_STOPPED" -eq 1 ] && killall -CONT awesome
    lipc-set-prop com.lab126.pillow disableEnablePillow enable 2>/dev/null
    lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home 2>/dev/null
}

restore() {
    result=$?
    trap - EXIT HUP INT TERM
    : >"$DONE"
    if [ "$SNAPSHOT_READY" -eq 1 ]; then
        "$APP/fbguard" restore "$SNAPSHOT" || echo "kobalt: framebuffer restore failed"
        rm -f "$SNAPSHOT"
    fi
    [ "$UI_FROZEN" -eq 1 ] && thaw_ui
    [ "$LOCKED" -eq 1 ] && rm -rf "$LOCK"
    echo "kobalt: exit $result"
    exit "$result"
}

if ! mkdir "$LOCK" 2>/dev/null; then
    echo "kobalt: refusing, display lock $LOCK exists"
    exit 75
fi
LOCKED=1
rm -f "$DONE"
trap restore EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

KOBALT_KINDLE_ACCEL=$(timeout 5 lipc-get-prop com.lab126.winmgr accelerometer 2>/dev/null)
export KOBALT_KINDLE_ACCEL

setsid sh -c '
    sleep "$1"
    [ -e "$2" ] && exit 0
    echo "kobalt deadman: session never finished; thawing the Amazon UI" >>"$3"
    killall -CONT volumd; killall -CONT awesome
    lipc-set-prop com.lab126.pillow disableEnablePillow enable
    exit 0
' deadman "$DEADMAN_DELAY" "$DONE" "$LOG" >/dev/null 2>&1 </dev/null &

UI_FROZEN=1
lipc-set-prop com.lab126.pillow disableEnablePillow disable 2>/dev/null
killall -STOP awesome 2>/dev/null && AWESOME_STOPPED=1
killall -STOP volumd 2>/dev/null && VOLUMD_STOPPED=1
sleep 1
"$APP/fbguard" snapshot "$SNAPSHOT" || exit 77
SNAPSHOT_READY=1

export LD_LIBRARY_PATH="$APP/lib"
export QT_QPA_PLATFORM_PLUGIN_PATH="$APP/plugins"
export QT_PLUGIN_PATH="$APP/plugins"
export QT_QPA_PLATFORM=kindlefb
export KOBALT_KINDLE_AUTOROTATE=1
export KOBALT_HOME="$HOME_DIR"
echo "kobalt: launching"
"$APP/kobalt"
exit $?
