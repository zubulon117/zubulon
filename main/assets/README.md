<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Firmware Assets (Fated Star Pet)

This directory contains **generated** binary assets embedded into the
Fated Star Pet firmware. Every file here is produced by a generator under
[`../../tools/starpet`](../../tools/starpet); do not edit the generated
files by hand. Change the source or the generator and regenerate.

| Directory | Content | Generator | Gate |
| --- | --- | --- | --- |
| [`fonts/`](fonts/) | `sp_font_16.c/.h`, `sp_font_20.c/.h` — LVGL CJK subset fonts | [`gen_fonts.py`](../../tools/starpet/gen_fonts.py) | [`test_sp_font_coverage.py`](../../tests/test_sp_font_coverage.py) |
| [`voice/`](voice/) | 62 `*.adpcm` speech clips (16 kHz mono IMA-ADPCM) | [`gen_voice.py`](../../tools/starpet/gen_voice.py) from [`voice_inventory.json`](../../tools/starpet/voice_inventory.json) | [`test_voice_inventory.py`](../../tests/test_voice_inventory.py) |

The clips and fonts are linked into the factory application with the
`EMBED_FILES` CMake directive (see [`../CMakeLists.txt`](../CMakeLists.txt))
and execute in place from flash; no filesystem is required.

## Fonts

- Source font: [`../../assets/fonts/NotoSansSC-Regular.woff`](../../assets/fonts/NotoSansSC-Regular.woff),
  Google **Noto Sans SC** Regular, licensed under the SIL Open Font License
  1.1. See [`../../assets/fonts/LICENSE-OFL.txt`](../../assets/fonts/LICENSE-OFL.txt).
- The two generated files share one subset of the glyphs actually used by
  the firmware (collected from CJK string literals in `main/*.c`,
  [`sp_text.c`](../sp_text.c), the voice inventory, and
  [`ui_chars.txt`](../../tools/starpet/ui_chars.txt)). ASCII glyphs still
  come from the LVGL Montserrat fonts configured in the build.
- Regenerate after adding, removing, or changing any Chinese UI text:

  ```bash
  python tools/starpet/gen_fonts.py
  ```

  Requires Node.js and `lv_font_conv`; the executable path can be
  overridden with the `LV_FONT_CONV` environment variable.

## Voice clips

- The 62 clips are generated speech: short words and phrases (numbers,
  zodiac names, fortunes, colors, lucky objects) are synthesized with the
  online **edge-tts** service using the `zh-CN-XiaoxiaoNeural` voice,
  decoded to 16-bit 16 kHz mono PCM, then encoded to IMA-ADPCM by
  [`adpcm_codec.py`](../../tools/starpet/adpcm_codec.py).
- The `edge-tts` Python package itself is GPL-3.0 licensed; it is a
  generation-time tool only and is not linked into or shipped with the
  firmware. Synthesized audio is produced through a Microsoft online
  service; redistribution and use of the resulting speech must follow the
  applicable Microsoft service terms at generation time.
- The linker manifest [`../sp_voice_manifest.c`](../sp_voice_manifest.c)
  and its header are generated together with the clips; the clip enum
  order is declared in [`../sp_voice_script.h`](../sp_voice_script.h).
- Regenerate (requires network access and a Python environment with
  `edge-tts` and `miniaudio`):

  ```bash
  python tools/starpet/gen_voice.py             # regenerate all clips
  python tools/starpet/gen_voice.py --check     # offline consistency check
  ```

## Earcons

The nine UI earcons (click, select, feed, and similar feedback sounds)
contain no recorded audio: they are short tones synthesized at runtime by
[`../sp_audio.c`](../sp_audio.c), so they carry no third-party asset
license.
