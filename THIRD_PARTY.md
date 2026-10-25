# Third-party notices

- Application source: this project does not yet declare a public distribution license; choose one before public release.
- Qt 6.8.3: dynamically linked Core, Gui, Widgets, Sql, Network, Multimedia, WebSockets, Concurrent and Windows platform/SQLite/Schannel/Windows multimedia plugins. See bundled licenses and [Qt licensing](https://doc.qt.io/qt-6/licensing.html). Applicable Qt sources are available from [Qt 6.8.3 archives](https://download.qt.io/archive/qt/6.8/6.8.3/single/). Preserve the ability to replace/relink applicable LGPL dynamic libraries.
- Matching MinGW 13.1 runtime DLLs are deployed alongside the application. See bundled MinGW notices and GCC Runtime Library Exception.
- Windows SAPI, system voices, audio devices and Windows Media Foundation are supplied by Windows, not redistributed here.
- No FFmpeg multimedia plugin, FFmpeg DLLs or Live2D SDK/model are bundled.
- The fixed character asset assets/shuangling-chibi.png was created with built-in ImageGen using the user's nine-panel reference as visual direction. It is an immutable rendered illustration, not a Cubism rig. Production prompts and provenance are preserved in assets/character-prompt.txt and identity metadata in assets/character.json.

This personal prototype has not undergone a full public redistribution/license audit. Preserve the license files in the portable package.

- Local semantic memory: BAAI bge-small-zh-v1.5 (MIT), quantized ONNX conversion from onnx-community at revision 9507db33464b5da99a532ac26b2a251767cbc62b. The fixed public weights, model card, checksums and upstream MIT license are downloaded separately into embedding/model by embedding/prepare.py; model weights are excluded from this source repository. ONNX Runtime, NumPy and Hugging Face tokenizers run in a separately installed local Python environment under their respective licenses. No Python runtime, private embeddings or user memory database is redistributed in the ZIP.

- Alibaba Cloud Qwen-TTS is an optional online service. No cloud voice packages, SDK or user-generated cloud audio are redistributed. Selected voice IDs follow the official Qwen-TTS catalog.
- Twelve public interaction phrases were generated locally with the Apache-2.0 Qwen3-TTS checkpoint, preset Serena, and bundled as PCM audio. See assets/interaction/manifest.json for text, model revision, PCM format and SHA-256. These fixed local-model clips do not represent Momo, Chelsie, Bella or Katerina. Private user audio caches are excluded from release archives.

- Optional offline speech: Qwen3-TTS code/checkpoint under the published Apache-2.0 license, preserved in offline/LICENSE-Qwen3-TTS.txt. The large checkpoint, Python, PyTorch and optional CUDA components are downloaded separately, not redistributed in the lightweight ZIP. Their own licenses and hardware requirements apply.
