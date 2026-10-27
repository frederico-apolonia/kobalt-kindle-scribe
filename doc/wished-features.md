# Wished features

Ideas for the Kindle Scribe port that are not built yet. Nothing here is committed to.

## Settings

- An in-app interface scale, separate from the book text size. Today the scale is the density constant
  (default 390, override with `KOBALT_KINDLE_DPI` at launch), which means editing the launcher to change it.
- A library-wide settings page reachable without opening a book. Today settings exist only inside the reader
  (font size, line spacing, margins, alignment, weight), so the library offers none.
- Page-turn refresh policy: fast, normal, or clean, plus how often to force a full flash to clear ghosting.
- Front light and warmth controls from the library, not only from the reader.
- Choice of library folder from the device, instead of the `KOBALT_LIBRARY` launcher variable.
- Screen orientation lock, and a choice of whether to start in the orientation the accelerometer reports.
- Sleep behaviour: what Kobalt shows on the lock screen and whether it reopens the book on wake.

## Platform gaps

- Keyboard layouts for other languages, long-press alternates for accented letters, and a shortcut for the dictionary lookup field.
- Dictionaries (StarDict) and the TLS support their downloads need.
- Battery level and clock in the library header (the reader already shows them).
- Pen and palm-rejection handling.
