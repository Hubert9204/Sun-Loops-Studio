# Signalsmith Stretch and Signalsmith Linear (MIT)

Header-only libraries by Geraint Luff / Signalsmith Audio, copied unchanged and used for time-stretching and
pitch-shifting audio clips (`src/audio/TimeStretch.*`).

| Library | Upstream | Revision | Licence |
| --- | --- | --- | --- |
| Signalsmith Stretch | https://github.com/Signalsmith-Audio/signalsmith-stretch | `88061fd4eda56a2353c5d65c74aa3f4ccf76f9da` (22 Oct 2026) | MIT, `LICENSE-signalsmith-stretch.txt` |
| Signalsmith Linear (FFT, STFT) | https://github.com/Signalsmith-Audio/linear | tag 0.6.6, `7bb1c8f36794d6980080ce817c91f8da8fe56a66` | MIT, `LICENSE-signalsmith-linear.txt` |

`signalsmith-stretch.h` includes `signalsmith-linear/stft.h`, hence the directory name. Only the standard-library
fallback FFT is used (no pffft, Accelerate, IPP or CMSIS-DSP). The directory is added as a `SYSTEM` include path.
