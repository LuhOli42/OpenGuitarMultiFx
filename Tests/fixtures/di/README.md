# DI source tracks

Dry direct-injection takes for offline renders (see `RenderFx.cpp`) and for
auditioning effects hands-free. Dry in, processor, wet out — the difference is
the device under test and nothing else.

| File | Content | Source | License |
|---|---|---|---|
| `bass-di.wav` | Bass DI, 0:34, 24-bit mono 44.1kHz | Cambridge-MT "Mixing Secrets" Multitrack Library, MTK001 `JohnMcKay_DaisyDaisy/04_BassDI.wav`, via `retr0h/toneharness` (`resources/dry/bass-di-short.wav`) | MIT |
| `guitar-di.wav` | Electric guitar DI loop, 0:38, stereo 44.1kHz | Freesound #721687 by josefpres, via `jpfaria/OpenRig` (`assets/di-loops/slowcore-guitar-dry-50bpm.wav`) | CC0 1.0 |

To render through an effect:

```sh
build/Tests/OpenGuitarMultiFx_RenderFx_artefacts/Release/OpenGuitarMultiFx_RenderFx \
  Tests/fixtures/di/bass-di.wav /tmp/svt.wav SVTStyleAmplifier svt_gain=0.6
```
