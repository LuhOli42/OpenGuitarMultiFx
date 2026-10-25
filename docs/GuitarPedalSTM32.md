# GuitarML STM32

`GuitarPedalSTM32Processor` is a mono combined reverb and delay based on the
MIT-licensed [GuitarPedalSTM32 firmware](https://github.com/GuitarML/GuitarPedalSTM32).
The forward/octave delay and reverse-delay line are ports of the firmware's
MIT-licensed delay helpers. The firmware's `ReverbSc` implementation is
LGPL-2.1, so it is not used or translated here; the reverb is an independent
implementation of the specified Costello-style eight-line feedback delay
network.

## Controls

| Processor control | Hardware control |
|---|---|
| Reverb Mix | Knob 1, reverb level |
| Reverb Time | Knob 2, reverb decay |
| Reverb Filter | Knob 3, reverb tone/filter |
| Delay Mix | Knob 4, delay level |
| Delay Time | Knob 5, delay time; normalized 0–1 maps to 0–1 second |
| Delay Feedback | Knob 6, delay feedback |
| Routing | Left toggle: Dly>Rev, Parallel, Rev>Dly |
| Delay Mode | Right toggle: Normal, Octave, Reverse |
| Hold | Left footswitch; freezes the delay/reverb input and sets both feedbacks to 1 |
| MIDI Learn on Delay Time | Expression-pedal assignment |

## Deliberate differences

- Dry-level compensation is `1.0`; the firmware's `1.2` compensates for its
  hardware analog bypass path.
- Delay buffers use floats rather than `int16_t`, retaining the firmware's
  `[-1, 1]` write clamp.
- The independent reverb FDN also clamps its float line writes and filter states
  to `[-1, 1]` to keep unity-feedback operation bounded.
- Firmware timing constants are scaled from 48 kHz to the active sample rate.
- Dry and wet signals use the firmware's additive law. The processor does not
  use `WetLevelMatcher` or a dry/wet crossfade.
