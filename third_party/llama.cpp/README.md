# llama.cpp (MIT) and the Gemma 4 models (Apache 2.0)

Helios's LLM mode (0.64.0) runs a language model with **llama.cpp**, built into the helper program `sun-loops-helios`
(`tools/helios-llm`). llama.cpp is not copied here: CMake downloads it at configure time, pinned to a tag.

| Library | Upstream | Revision | Licence |
| --- | --- | --- | --- |
| llama.cpp (with ggml) | https://github.com/ggml-org/llama.cpp | tag `b12377` (14 Nov 2026) | MIT, `LICENSE-llama.cpp.txt` |

Its licence text is kept in this folder and goes with every copy of the program that holds the helper (the AppImage installs it
under `share/doc/sun-loops-studio`, the Mac app holds it in `Contents/Resources`).

The **models** are not part of the program and are not redistributed by it: on the user's request the program downloads
`gemma-4-E2B-it-Q4_0.gguf` or `gemma-4-E4B-it-Q4_0.gguf` from https://huggingface.co/ggml-org (GGUF conversions of Google's
Gemma 4 E2B / E4B instruction-tuned models made by the llama.cpp project). Gemma 4 is published by Google under the Apache
License 2.0. Helios gives itself its own name in conversation and says, when asked, that it is based on Gemma 4.
