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
- Portuguese. Books in Portuguese hyphenate already (the shared assets carry the Portuguese and Brazilian
  patterns), but the rest is missing on the Kindle: a Portuguese dictionary for lookups (the catalogue has English and
  Spanish to Portuguese; a Portuguese to Portuguese one would need adding, on top of the dictionary service above),
  a Portuguese interface (only `assets/en.strings` exists), and a keyboard layout with the accented letters on the
  first page.
