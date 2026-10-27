# Kobalt on the Kindle Scribe

An unofficial standalone port of the Kobalt reader to a jailbroken Kindle Scribe. It was built and verified on a Scribe (2022, Bellatrix3 / MediaTek, ARMv7 hard-float, glibc 2.35) running firmware 5.18.6. Other Kindles are untested. Kobalt installs beside KOReader, never replaces or patches it, and does not depend on it.

The port is its own platform under `platforms/kindle/`. It reuses the shared reader, library and interface code and adds the pieces a Kindle needs: a display and input plugin, a launcher that takes the screen from the Amazon UI and always gives it back, a front-light and battery adapter, an on-screen keyboard and a sleep screen.

## What works

- The Kobalt library (home, list and cover grid, search, sort, folders) and the reader with all of its settings.
- Touch and the Wacom pen. The pen taps, turns pages, drags and selects text like a finger. Palm contacts are cancelled.
- Automatic rotation between the two portrait orientations.
- Front light brightness and warmth.
- Battery percentage and charging state, in the reader's menu and in the header and footer fields.
- An on-screen keyboard for the library search and the in-book search, with letters, symbols and accented letters.
- A sleep screen (book cover, or the dimmed page, with progress and battery) that stays on the panel while the device sleeps.
- Images keep their proportions and are sized like KOReader's: CSS lengths use 160 ppi (`KOBALT_KINDLE_CSS_DPI`).
- Starting from the Library (a script entry with a cover), and exiting back to the Amazon UI.

Not available yet, or not supported: dictionaries (the placeholder service only reports that none is installed), HTTPS (Qt is built without TLS), the browser, page-turn animations, library folder picking (Kobalt reads one folder), and any Kindle other than the Scribe 2022. See [wished features](wished-features.md).

## How it fits together

| Piece | Where | Role |
| --- | --- | --- |
| `kobalt` | `platforms/kindle/*.cpp` | The application: window, library, reader, host services. |
| `libqkindlefb.so` | `platforms/kindle/display/` | A Qt QPA plugin (platform `kindlefb`). Draws the window into the framebuffer through FBInk, decodes the touch, pen and accelerometer devices, and pauses output while the device sleeps. |
| `fbguard` | `platforms/kindle/display/fbguard.c` | Saves the framebuffer before Kobalt starts and puts it back when it ends. |
| `kobalt.sh` | `platforms/kindle/package/` | The launcher. See [Takeover and recovery](#takeover-and-recovery). |
| `library-entry.sh` | `platforms/kindle/package/` | The Library script entry, installed as `Kobalt.sh`. |

Host services (`services.cpp`) supply the clock, battery, light, keyboard, presentation and sleep screen that the shared reader asks of its host. `keyboard.cpp` is the on-screen keyboard, drawn by Kobalt in window pixels above the library or reader. `lifecycle.cpp` listens to powerd (`lipc-wait-event`) for sleep and wake. `light_device.cpp` drives the front light through powerd.

Platform differences from the other targets are kept to hooks the shared code already offers, plus a few optional fields on `ReaderHostServices` (`exitLabel`, `stockSleepLabel`, `stockSleepDescription`, `maxSleepPixels`), an optional Exit row in the library's More menu, an aspect-preserving `img[height]` rule, and a separate density for CSS lengths (`cssPpi` in `reading/reading_density.h`).

## Building

The Kindle SDK is prepared once, outside the application build, like the Kobo SDKs. It takes about an hour on Apple Silicon and 2.5 GB of disk. Everything cross-compiles natively in an arm64 Ubuntu 22.04 container, with Ubuntu's `arm-linux-gnueabihf` GCC against the koxtoolchain `kindlehf` sysroot, which avoids emulating x86.

Inputs (paths are yours to choose):

| Variable | What it is |
| --- | --- |
| `KINDLE_SYSROOT` | The sysroot of the koxtoolchain `kindlehf` release (2026.08 was used): `.../arm-kindlehf-linux-gnueabihf/arm-kindlehf-linux-gnueabihf/sysroot`. |
| `KINDLE_QT` | An empty directory that `sdk/build-qt.sh` fills with `host/` and `install/`. Qt 6.11.3 (the `qtbase` checkout used is `5a1194b2d368e2d72c230205c464b04a2f23eccd`). |
| `KINDLE_QTSENSORS` | Install prefix of `qtsensors` 6.11.3, built against that Qt with the same toolchain file and `QT_HOST_PATH=$KINDLE_QT/host`. The reader's orientation code includes `QOrientationSensor`. |
| `KINDLE_FBINK` | A FBInk tree with `Release/libfbink.a` built for the Kindle (revision `13872c0` was used). |

1. Build the builder image: `docker build --platform linux/arm64 -f platforms/kindle/sdk/builder.Dockerfile -t kobalt-kindle-builder platforms/kindle/sdk`.
2. Build Qt. `sdk/build-qt.sh` expects the Qt source at `/src`, the output at `/build`, the toolchain file at `/toolchain.cmake` and the sysroot at `/kindle-sysroot`:

   ```sh
   docker run --rm --platform linux/arm64 -e JOBS=4 \
     -v $QTSRC:/src:ro -v $KINDLE_QT:/build \
     -v $PWD/platforms/kindle/sdk/toolchain.cmake:/toolchain.cmake:ro \
     -v $PWD/platforms/kindle/sdk/build-qt.sh:/build-qt.sh:ro \
     -v $KINDLE_SYSROOT:/kindle-sysroot:ro \
     kobalt-kindle-builder /build-qt.sh
   ```

   Accessibility must stay on (`-DFEATURE_accessibility=ON`): the shared controls set accessible names. Only the `offscreen` and `minimal` QPA plugins are built; the Kindle uses `kindlefb`. There is no OpenSSL, so HTTPS does not work.
3. Build `qtsensors` into `KINDLE_QTSENSORS`.
4. Build FBInk's static library for the Kindle:

   ```sh
   docker run --rm --platform linux/arm64 -v $KINDLE_FBINK:/src -v $KINDLE_SYSROOT:/kindle-sysroot:ro -w /src \
     kobalt-kindle-builder \
     make -j4 staticlib KINDLE=true MINIMAL=true CROSS_TC=/usr/bin/arm-linux-gnueabihf \
     CFLAGS='--sysroot=/kindle-sysroot -O2 -pipe -march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -mthumb'
   ```

   FBInk is GPLv3. It is linked into `libqkindlefb.so` and `fbguard` only, and is not vendored in this repository. A binary distribution of those two files is therefore subject to the GPL.
5. Build Kobalt, from the repository root:

   ```sh
   KINDLE_SYSROOT=... KINDLE_QT=... KINDLE_QTSENSORS=... KINDLE_FBINK=... platforms/kindle/sdk/build.sh
   ```

   The first run downloads and verifies the third-party sources listed in `dependencies/manifest.json`, including zlib, which the Kindle target builds itself. The result is `build/kindle/package/`: `kobalt`, `assets/`, `plugins/platforms/libqkindlefb.so`, `fbguard`, `kobalt.sh` and `library-entry.sh`.

   The `kindle` CMake preset in `platforms/kindle/CMakePresets.json` does the same inside an environment that already has the toolchain (`QT_KINDLE_PATH`, `QT_HOST_PATH`, `QT_KINDLE_SENSORS_PATH`, `FBINK_SOURCE`).

The package does not contain Qt. Copy `lib/` (Qt Core, Gui, Widgets, Network, Sql, Sensors, plus the GCC `libstdc++` and `libgcc_s` that match the compiler) and `plugins/sqldrivers/libqsqlite.so` from the Qt install next to it, and add a `qt.conf` that points at them.

## Installing

Install under `/mnt/us/kobalt` (user storage, so it survives firmware updates and is visible over USB):

```
/mnt/us/kobalt/
  kobalt  kobalt.sh  fbguard  qt.conf
  assets/
  lib/
  plugins/platforms/libqkindlefb.so
  plugins/sqldrivers/libqsqlite.so
```

Make `kobalt`, `kobalt.sh` and `fbguard` executable. Copy `library-entry.sh` to `/mnt/us/documents/Kobalt.sh`. It is a Library script entry with the same header as KOReader's; it appears in the Library as "Kobalt", with a cover. Tap it to start Kobalt.

Books are read from `/mnt/us/books` (the folder the entry passes as `KOBALT_LIBRARY`). KOReader can open the same folder; each reader keeps its own reading positions. Kobalt never modifies a book. Its own data lives in `/mnt/us/kobalt/state` and its log in `/mnt/us/kobalt/kobalt.log`.

## Using it

- Tap **Kobalt** in the Library. The Amazon UI is frozen and Kobalt takes the screen.
- Reading: tap the sides of the page to turn it. The reader's menu has the settings, search, light and a back button labelled **Library**, which returns to Kobalt's library.
- Leaving: **More** (top right of the library) then **Exit Kobalt**. The Amazon UI comes back where it was.
- Sleep: press the power button. The reader saves its place and paints the sleep screen; the panel keeps it. The choice is in the reader's settings under Sleep and wake: the first option leaves the page as it is, the others show the book cover or the dimmed page with progress and battery. A new profile starts with the cover.
- Text entry: tapping a search field opens the keyboard. The chevron strip above it hides it; tapping the field brings it back.

## Takeover and recovery

`kobalt.sh` refuses to start while KOReader runs or another Kobalt holds `/var/tmp/kobalt-display.lock`. It then reads the accelerometer, disables the Amazon pillow overlay, freezes the window manager and the volume daemon (`SIGSTOP`, as KOReader does), snapshots the framebuffer with `fbguard` and runs Kobalt. The Amazon framework keeps running, because powerd ignores the power button while `lab126_gui` is stopped.

On every exit path (normal exit, a crash, `SIGTERM`) the launcher restores the framebuffer, thaws the UI, re-enables pillow, removes the lock and the leftover `/tmp/kobalt-assets-*` folders, and reaps the power-event listeners. A deadman started in its own session thaws the UI after `KOBALT_DEADMAN_DELAY` seconds (4 hours by default) if the launcher itself is killed. A crash prints a backtrace and the module map to `kobalt.log`.

If the screen is ever stuck on a frozen UI over SSH: `killall -CONT awesome volumd; lipc-set-prop com.lab126.pillow disableEnablePillow enable`.

## Display, input and sleep

- **Display.** Every flush is submitted as a partial update of its damaged region. A clean redraw (page turns marked clean, wake, leaving the sleep screen) is a flashing full refresh. The window sets the property `kindlefb_full_refresh`, which the plugin consumes on its next flush.
- **Touch and pen.** The plugin decodes the touch panel (`pt_mt`), the Wacom digitizer and the accelerometer from `/dev/input`. Touch becomes Qt touch events, which Qt turns into mouse events for the widgets. The pen becomes Qt tablet events and, because the widgets ignore those, also a left-button pointer while the pen touches the glass. Hovering sends nothing, so the panel is not refreshed for a pen that only moves above it.
- **Sleep.** powerd announces sleep with `goingToScreenSaver`. The application builds the sleep image and submits it as one full refresh; the plugin then pauses all output so nothing draws while the device suspends. The application tells the plugin it paints a sleep picture with the `kindlefb_sleep_screen` application property, and marks the frame with the window property `kindlefb_sleep_ready`. Without that frame, the plugin pauses after four seconds. On wake the plugin resumes with a full refresh and the reader redraws its page.
- **Density.** The panel is 300 ppi. `KOBALT_KINDLE_DPI` (default 390) scales the interface and the reading text together; larger is bigger. `KOBALT_KINDLE_CSS_DPI` (default 160) sets how CSS pixel lengths and images in books convert to pixels; lower makes images smaller.

## Configuration

Set these in the environment of `kobalt.sh` (the library entry sets `KOBALT_LIBRARY`).

| Variable | Default | Meaning |
| --- | --- | --- |
| `KOBALT_LIBRARY` | `/mnt/us/documents` | The one folder the library scans. |
| `KOBALT_HOME` | `/mnt/us/kobalt` | Where state (`state/`) and the log live. |
| `KOBALT_KINDLE_DPI` | `390` | Interface and text density, 150 to 600. |
| `KOBALT_KINDLE_CSS_DPI` | `160` | Density for CSS lengths and book images, 48 to 600. |
| `KOBALT_DEADMAN_DELAY` | `14400` | Seconds before the deadman thaws the UI. |

## Testing without the panel

Nothing below touches the display or the Amazon UI. Run `kobalt` directly with `LD_LIBRARY_PATH`, `QT_QPA_PLATFORM_PLUGIN_PATH`, `QT_PLUGIN_PATH` and `QT_QPA_PLATFORM=kindlefb`, plus:

| Variable | Effect |
| --- | --- |
| `KOBALT_KINDLEFB_DRYRUN=<file>` | The plugin renders into memory and writes the framebuffer image to `<file>` at exit (8 bits per pixel, 1872 byte stride, two screens high). The panel is untouched. |
| `KOBALT_KINDLE_REPLAY=<dir>` | Plays raw `struct input_event` captures (`event1.bin` accelerometer, `event2.bin` pen, `event3.bin` touch) instead of reading `/dev/input`. |
| `KOBALT_KINDLE_OPEN=<epub>` | Opens a book three seconds after start. |
| `KOBALT_KINDLE_EXIT_AFTER=<s>` | Exits after that many seconds. |
| `KOBALT_KINDLE_TYPE=<text>` | Taps the text out on the keyboard, in the library search or, with `KOBALT_KINDLE_OPEN`, the book search. `<shift>`, `<back>`, `<space>`, `<enter>`, `<?123>`, `<abc>`, `<áé>`, `<dismiss>` and `<submit>` name other keys and actions. |
| `KOBALT_KINDLE_SLEEP=<ms>[,<ms>]` | Runs the sleep path that long after start, and the wake path at the second time. |
| `KOBALT_KINDLE_TRACE=1` | Logs touch, mouse and tablet events and the battery reading. |
| `KOBALT_KINDLE_ACCEL=U\|D` | The orientation at start (upright or upside down). |

Run these on the device from a copy in `/tmp` if Kobalt is also running live, because a running executable cannot be replaced. Keep `/tmp` in mind: it is a 64 MB RAM disk shared with the system.

## Known limits

- The first scan of a newly added folder crashed twice during development and could not be reproduced afterwards. A crash now leaves a backtrace in `kobalt.log`.
- One earlier session hung until the device was rebooted. The cause is unknown.
- Suspend with a finger or the pen down, ten consecutive sleep cycles, the cover (hall sensor) event and USB with Kobalt running were not exercised.
- The sleep screen exists only inside a book. With the library open, the screen is left as it is.
- The device's SSH server from KOReader is a passwordless root shell on the local network. Turn it off when not developing.
