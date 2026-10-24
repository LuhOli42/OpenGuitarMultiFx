# Noise Gates

All noise-gate / noise-suppressor processors live in this folder. Every gate
registers as `EffectRegistry::Category::dynamics` with subcategory `"Gate"`,
so the Add Effect browser groups them under Dynamics → the "Gate" chip.

## Processors

### Noise Gate — `GateProcessor` (`GateProcessor.{h,cpp}`)

- Registry key: `NoiseGate`; display name: "Noise Gate".
- Icon: `Assets/Icons/gate.svg`.
- Parameters:
  - `gate_threshold` — Threshold: −80..0 dB, default −50 dB.
  - `gate_attack` — Attack: 0.1..50 ms (skew 0.4), default 1 ms.
  - `gate_release` — Release: 10..1000 ms (skew 0.4), default 100 ms.
  - `gate_hysteresis` — Hysteresis: 0..20 dB, default 6 dB.
  - `gate_hold` — Hold: 0..500 ms (skew 0.4), default 10 ms.
- DSP: a fast fixed 2 ms/2 ms `EnvelopeFollower` tracks the input peak
  (max across channels). A two-threshold state machine opens the gate when
  the detected level reaches Threshold and only closes it after the level
  has stayed below Threshold − Hysteresis for Hold — so a signal hovering
  near the threshold can't chatter the gate. The 0/1 gate gain runs through
  a second `EnvelopeFollower` timed by Attack/Release, keeping transitions
  click-free.
- No circuit writeup (generic DSP, not a hardware model).
- Tests: `Tests/GateProcessorTests.cpp`.

### DS201 — `DS201StyleNoiseGateProcessor` (`DS201StyleNoiseGateProcessor.{h,cpp}`)

- Registry key: `DS201StyleNoiseGate`; display name: "DS201".
- Icon: `Assets/Icons/ds201_gate.svg`.
- Parameters:
  - `ds201_threshold` — Threshold: −72..−18 dB, default −50 dB.
  - `ds201_range` — Range: −80..0 dB, default −80 dB.
  - `ds201_attack` — Attack: 0.01..1000 ms (log), default 1 ms.
  - `ds201_hold` — Hold: 2..2000 ms (log), default 60 ms.
  - `ds201_decay` — Decay: 2..4000 ms (log), default 200 ms.
  - `ds201_lf` — L.F. Filter: 25..4000 Hz (log), default 25 Hz.
  - `ds201_hf` — H.F. Filter: 250..35000 Hz (log), default 35000 Hz.
  - `ds201_key` — Key Listen: off/on, default off.
- DSP: a Drawmer DS201-style gate — the first frequency-conscious gate.
  The side-chain runs through L.F./H.F. filters so (e.g.) low hum can't
  open the gate. While the key stays above the threshold the hold cycle
  restarts; when it falls below, the gate holds for Hold, then the gain
  travels the full Range in Decay time; Attack is the time to reopen.
  Key Listen outputs the filtered side-chain for tuning the filters.
- Circuit writeup: `docs/circuits/DS201StyleNoiseGate.md`.
- Tests: `Tests/NoiseGateTests.cpp` (suite "NoiseGates").

### NS-2 — `NS2StyleNoiseSuppressorProcessor` (`NS2StyleNoiseSuppressorProcessor.{h,cpp}`)

- Registry key: `NS2StyleNoiseSuppressor`; display name: "NS-2".
- Icon: `Assets/Icons/ns2_suppressor.svg`.
- Parameters:
  - `ns2_threshold` — Threshold: −80..−30 dB, default −60 dB.
  - `ns2_decay` — Decay: 0..1 (normalised knob), default 0.3.
  - `ns2_mute` — Mute: Reduction/Mute switch, default Reduction.
- DSP: a Boss NS-2-style suppressor — a downward expander, not a hard
  gate: above the threshold the signal passes at unity; below it the gain
  drops on a 1:3 slope (10 dB under the threshold → 20 dB more
  attenuation), bounded at −60 dB. It opens in ~1 ms and closes over a
  Decay-scaled 40 ms..2 s. Mute silences the output outright.
- Circuit writeup: `docs/circuits/NS2StyleNoiseSuppressor.md`.
- Tests: `Tests/NoiseGateTests.cpp` (suite "NoiseGates").

## Planned (not implemented yet)

- **Decimator-style adaptive release** (ISP Decimator II/X) — fast release on
  staccato, slow on sustained notes, via the "integration release window"
  described in US patent 11,641,183. Also the base of a reusable adaptive
  release block for the other gates.
- **Rocktron Hush-style** — downward expander (SA572) plus a dynamic low-pass
  VCF (LM13600) with Variable Integrated Release: removes hiss while playing,
  not only in silence. Circuit model from the traced Hush The Pedal schematic.
- **MXR Smart Gate-style** (M135) — closing filters instead of muting, with
  Hiss / Mid / Full noise bands, Hi Trigger Range and adaptive release;
  match the manual's gate-closed response (e.g. Hiss −3 dB @ 1 kHz, −22 dB @ 20 kHz).
- **Fortin Zuul-style key gate** — hard gate detected from the clean guitar
  (key) while attenuating the noisy post-drive/amp signal (4-cable method).
  Needs a key/side-chain routing design first.
- **Multiband gate** (TC Sentry-style) — Linkwitz-Riley crossover, one
  gate/expander per band, plus a hard-gate mode.
- **Hum remover** (EHX Hum Debugger-style) — notch comb at 50/60 Hz and
  harmonics that tracks the mains frequency; Normal = odd harmonics,
  Strong = odd + even.
- **Boss NF-1** — 1979 vintage gate (Sens/Decay), circuit model from the
  public schematic (uPC741C, 2SK30A as a variable resistor).
- **Spectral gate** — learns a noise profile in silence and attenuates per
  FFT bin with time/frequency smoothing.
- **Neural noise reduction** — RNNoise-style network retrained on guitar
  (DI + hiss/hum/buzz/amp-noise mixes, 5 ms frames); only registered if a DI
  render shows no audible loss of harmonics or attack.

## Conventions for adding a gate here

- Put `NewGateProcessor.{h,cpp}` in this folder and add the `.cpp` to
  `OGMFX_CORE_SOURCES` in the root `CMakeLists.txt` under
  `Source/Effects/NoiseGates/`.
- Register in `Source/EffectRegistry.cpp` with
  `EffectRegistry::Category::dynamics` and subcategory `"Gate"` — the UI
  depends on that grouping.
- Headers inside this folder include shared headers one level up with
  relative paths (`"../EffectProcessor.h"`, `"../../Engine/Biquad.h"`);
  outside code includes via `"Effects/NoiseGates/Foo.h"` (`Source/` is the
  public include root — don't add new include dirs).
- Add a test in `Tests/` covering at least open-pass, closed-attenuation
  and the timing behaviour.
- If it's a circuit/hardware model, write a `docs/circuits/<Name>.md`
  per the existing writeups (they stay in `docs/circuits/`, not here).
- Verify DI renders with `OpenGuitarMultiFx_RenderFx` on
  `Tests/fixtures/di/guitar-di.wav`: output must not be silent and must
  resemble the input while the gate is open.
