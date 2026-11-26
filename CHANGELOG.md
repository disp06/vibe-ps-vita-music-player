# Changelog

## [Unreleased]

### Added
- **Language selector** in Settings (9 languages: English (default), Русский, Українська, 简体中文, Español, Português, हिन्दी, العربية, Français). The choice is saved as `lang=` in `ux0:data/Vibe/config.txt`.
- **Text shaping** (`src/shape.c`): Arabic letter joining with lam-alef ligatures, right-to-left run ordering, and Devanagari vowel-sign reordering.
- **Script fallback fonts** in `assets/fonts/`: Noto Sans SC (GB2312 level 1, 3,755 characters), Noto Sans Devanagari, and Noto Sans Arabic. All are subsets under the SIL OFL, with the licence in `OFL-NotoSans.txt`.
- **i18n module** (`src/i18n.c`, `src/i18n.h`) with translated UI strings for all skins.

### Changed
- Settings list now has a language row before "about" (10 rows).
- Tag text is converted to valid UTF-8: Latin-1 fallback for ID3v1 and RIFF INFO, and truncation respects character boundaries.
- Device labels (memory card, USB, internal storage) are translated.
- Retro skin's HUD labels, tabs, and hints are translated. Technical tokens such as SCOPE, CH-A, and KBPS stay in English.
- README: languages, VPK size (about 2.1 MB), font licences, and source layout.
- `tools/preview/Makefile`: added `shape.c` and `i18n.c`.

### Fixed
- **Flicker on track skip (L/R):** the glyph atlas was cleared in the middle of a frame, so text already queued for that frame blinked. Atlas compaction now happens only at frame start.
- **Cover decode stutter:** cover art for a skipped track is decoded only after skipping settles (0.4 s), so rapid L/R presses no longer trigger repeated JPEG decodes.
- **Clock on wake:** the CPU clock rises to the UI level as soon as the screen wakes, so the first frames aren't drawn at screen-off clocks.
- **`ux0:music` not opening:** the browser now tries spelling and case variants, then the exact on-disk name from the device root listing. Errors still show as a toast with the error code.

### Known limitations
- The flicker, `ux0:music`, and font rendering fixes are not yet confirmed on hardware.
- Hindi conjunct ligatures are not implemented (no HarfBuzz, to keep the binary small).
- Arabic is reordered but lists are still left-aligned.
- Chinese characters outside GB2312 level 1 render blank, and Chinese GBK ID3v1 tags are not decoded.
- Script fallback fonts are Regular weight; the Light weight applies to Latin and Cyrillic only.
