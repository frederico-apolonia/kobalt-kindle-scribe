# Wished features

Ideas for the Kindle Scribe port that are not built yet. Nothing here is committed to.

## Settings

- An in-app interface scale, separate from the book text size. Today the scale is the density constant
  (default 390, override with `KOBALT_KINDLE_DPI` at launch), which means editing the launcher to change it.
- More on the library's Settings page. It has the front light, dark mode and the sleep screen; the rest of the reader's settings (font size,
  line spacing, margins, alignment, weight, sync, dictionaries) still need a book to be open, because the reader's settings panel is
  built on an open book.
- Page-turn refresh policy: fast, normal, or clean, plus how often to force a full flash to clear ghosting.
- Choice of library folder from the device, instead of the `KOBALT_LIBRARY` launcher variable.
- Screen orientation lock, and a choice of whether to start in the orientation the accelerometer reports.
- Sleep behaviour: what Kobalt shows on the lock screen and whether it reopens the book on wake.

## Platform gaps

- Keyboard layouts for other languages, long-press alternates for accented letters, and a shortcut for the dictionary lookup field.
- Dictionaries (StarDict). HTTPS works now, but the dictionary service is still the placeholder in
  `platforms/kindle/services.cpp`; the downloadable-dictionary service of the other hosts needs porting.
- KOReader sync against a real sync server. The client builds with TLS but has not been tried on the device.
- Pen and palm-rejection handling.
- Calibre. Getting books from a Calibre library without a cable: browsing and downloading from Calibre's content
  server (its OPDS feed), and perhaps Calibre's wireless device connection, which KOReader supports as well. TLS is
  in place now; what is missing is a network page in the library (Wi-Fi is the Amazon UI's, so Kobalt would only
  connect when it is already up) and the download and import path into the books folder.
