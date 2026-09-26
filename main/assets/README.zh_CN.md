<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 固件素材（命定星宠）

本目录存放嵌入"命定星宠"固件的**生成型**二进制素材。这里的每个文件都由
[`../../tools/starpet`](../../tools/starpet) 下的生成器产出，请勿手改生成文件；
需要调整时修改素材源或生成器后重新生成。

| 目录 | 内容 | 生成器 | 门禁 |
| --- | --- | --- | --- |
| [`fonts/`](fonts/) | `sp_font_16.c/.h`、`sp_font_20.c/.h`——LVGL 中文子集字体 | [`gen_fonts.py`](../../tools/starpet/gen_fonts.py) | [`test_sp_font_coverage.py`](../../tests/test_sp_font_coverage.py) |
| [`voice/`](voice/) | 62 个 `*.adpcm` 语音片段（16 kHz 单声道 IMA-ADPCM） | [`gen_voice.py`](../../tools/starpet/gen_voice.py)，清单来自 [`voice_inventory.json`](../../tools/starpet/voice_inventory.json) | [`test_voice_inventory.py`](../../tests/test_voice_inventory.py) |

这些字体和语音通过 CMake 的 `EMBED_FILES` 指令（见
[`../CMakeLists.txt`](../CMakeLists.txt)）链入 factory 应用，在 Flash 上
XIP 就地执行，无需文件系统。

## 字体

- 源字体：[`../../assets/fonts/NotoSansSC-Regular.woff`](../../assets/fonts/NotoSansSC-Regular.woff)，
  Google **Noto Sans SC** Regular，按 SIL 开源字体许可证 1.1（OFL）授权，
  全文见 [`../../assets/fonts/LICENSE-OFL.txt`](../../assets/fonts/LICENSE-OFL.txt)。
- 两个生成文件共用同一套字形子集，只含固件实际用到的字符（采集自
  `main/*.c` 的中文常量、[`sp_text.c`](../sp_text.c)、语音清单与
  [`ui_chars.txt`](../../tools/starpet/ui_chars.txt)）。ASCII 字形仍由
  构建配置中的 LVGL Montserrat 字体提供。
- 新增、删除或修改任何中文界面文案后必须重新生成：

  ```bash
  python tools/starpet/gen_fonts.py
  ```

  依赖 Node.js 与 `lv_font_conv`；可用环境变量 `LV_FONT_CONV` 指定其可执行
  文件路径。

## 语音片段

- 62 个片段均为合成语音：将短词语（数字、星座名、运势、颜色、幸运物等）通过
  在线 **edge-tts** 服务以 `zh-CN-XiaoxiaoNeural` 音色合成，解码为
  16-bit 16 kHz 单声道 PCM，再由
  [`adpcm_codec.py`](../../tools/starpet/adpcm_codec.py) 编码为 IMA-ADPCM。
- `edge-tts` Python 包本身为 GPL-3.0 许可，但它只是生成期工具，不会链入固件、
  也不随固件分发。合成语音经由微软在线服务产出，生成音频的再分发与使用须遵守
  生成时适用的微软服务条款。
- 链接清单 [`../sp_voice_manifest.c`](../sp_voice_manifest.c) 及其头文件与
  语音同时生成；片段枚举顺序在 [`../sp_voice_script.h`](../sp_voice_script.h)
  中声明。
- 重新生成（需要联网，且 Python 环境装有 `edge-tts` 与 `miniaudio`）：

  ```bash
  python tools/starpet/gen_voice.py             # 重新生成全部语音
  python tools/starpet/gen_voice.py --check     # 离线一致性校验
  ```

## 提示音

9 个界面提示音（点击、选择、喂食等反馈音）不含任何录音素材，是
[`../sp_audio.c`](../sp_audio.c) 在运行时合成的短促音调，不涉及第三方素材
许可。
