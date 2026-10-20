# Guitar synth family

Four blocks in the Pitch category, chip "Synth". All DSP is written for this project (no external code); the
algorithm families come from the published literature (zero-crossing pitch tracking, flip-flop octave division,
two-grain time-domain shifting, band-wise cross-synthesis / channel vocoder).

| Block | Needs a tracker | Takes chords | What it does |
|---|---|---|---|
| Guitar-to-Bass | yes (grain length) | no | tracked two-grain shifter down 1 or 2 octaves + bass voicing |
| Sub-Octave Synth | no | poorly (comparator chatters) | ÷2 / ÷4 flip-flops, envelope filter, ring mod, drive |
| Tracked Synth | yes | no (monophonic) | oscillator + sub + detuned twin at the tracked pitch, envelope filter |
| Spectral Morph | no | yes | 16-band flatten + re-weight by a formant shape, morph A->B with LFO |

## PitchTracker (`GuitarSynth/SynthParts.h`)
4th-order low-pass that follows the tracked pitch (cutoff 2.5 x f, 180 Hz..3.5 kHz) -> hysteresis zero crossing
(12 % of the running peak) with sub-sample interpolation -> two consecutive periods must agree within 6 %.
One estimate per period: latency is about one to two periods of the played note (12 ms on the low E at 82 Hz,
plus the audio device buffer). Range 38..1500 Hz. Known limits: a very strong second harmonic can read an octave
high on the first period; chords are not tracked.

## Guitar-to-Bass
1. Mono sum -> delay line -> two read pointers half a grain apart, sliding at (1 - ratio) samples per sample,
   cross-faded with a smoothstep window pair that always sums to 1. Ratio 0.5 (down 1) or 0.25 (down 2).
2. The grain is `m * period / ratio` (m whole periods, grain >= 12 ms, <= 120 ms): each grain reads a whole number of
   input periods and the half-grain offset between the reads is a whole number of periods, so the shifted note does
   not comb-filter, for -1 and -2 octaves. Smoothed over 30 ms; held when unvoiced.
3. Bass voicing: 35 Hz high-pass, low shelf at 130 Hz (Body), 4th-order tone low-pass (450 Hz .. 4.5 kHz),
   FINGER / PICK (+4 dB shelf at 1.8 kHz, brighter) / ROUND (darker, +3 dB body), soft grit, squash compressor,
   and a little of the guitar low-passed at 200 Hz for the string's own attack.

## Sub-Octave Synth
LP at 700 Hz (4th order) -> comparator with 15 % hysteresis -> flip-flop (÷2) -> second flip-flop on every second
edge (÷4). Square subs go through an envelope-swept SVF low-pass and a level that follows the guitar's envelope.
Ring modulator = input x internal sine. No tracking latency.

## Tracked Synth
Tracker + envelope follower (1.5 ms attack, user release) -> gate (sensitivity) and amplitude (Dynamics) ->
PolyBLEP oscillator (saw / square / triangle / sine) + detuned twin + square sub one octave down -> SVF low-pass
whose cutoff is swept by the envelope -> soft clip. Glide smooths the tracked frequency.

## Spectral Morph
16 band-pass filters (Q 4.5, log-spaced 90 Hz..7 kHz). Per band: envelope follower (3 ms / Response); the band is
multiplied by `(1 - flatten) + flatten * ref / (env + eps)` (flattening) and by the target gain of the current
shape. Shapes are sums of Gaussian bumps in log-frequency (formants of vowels, brass, strings, reed, ...) plus a
tilt, normalised to unit RMS across bands. Output is gated by the input envelope so the flattened bands do not
amplify the noise floor.

## Measured (tests: `GuitarSynthTests`)
See `Tests/GuitarSynthTests.cpp`; pitch accuracy, octave division and the vowel shape are asserted there.
CPU is recorded in the effect bench (`BENCH_FILTER=GuitarToBass,SubOctaveSynth,TrackedSynth,SpectralMorph`).

## Not done / next
Polyphonic tracking (needs a hexaphonic pickup or a spectral multi-pitch estimator), a neural timbre-transfer
block (does not fit the 5 % core budget), per-string pitch trackers.
