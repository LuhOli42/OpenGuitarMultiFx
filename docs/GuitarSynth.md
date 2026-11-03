# Guitar synth family

Four blocks in the Pitch category, chip "Synth". All DSP is written for this project (no external code); the
algorithm families come from the published literature (zero-crossing and spectral multi-pitch tracking, flip-flop
octave division, correlation-spliced time-domain shifting, phase vocoder with identity phase locking, band-wise
cross-synthesis / channel vocoder).

| Block | Needs a tracker | Takes chords | What it does |
|---|---|---|---|
| Guitar-to-Bass | no | yes (MONO: thirds ~14 c sharp; POLY: in tune) | fixed-ratio shifter down 1 or 2 octaves + bass voicing |
| Sub-Octave Synth | no | poorly (comparator chatters) | ÷2 / ÷4 flip-flops, envelope filter, ring mod, drive |
| Tracked Synth | yes (polyphonic) | yes, up to 6 notes | one oscillator voice per tracked note + sub, envelope filter |
| Spectral Morph | no | yes | 16-band flatten + re-weight by a formant shape, morph A->B with LFO |

## PitchTracker (`GuitarSynth/SynthParts.h`)
4th-order low-pass that follows the tracked pitch (cutoff 2.5 x f, 180 Hz..3.5 kHz) -> hysteresis zero crossing
(12 % of the running peak) with sub-sample interpolation -> two consecutive periods must agree within 6 %.
One estimate per period: latency is about one to two periods of the played note (12 ms on the low E at 82 Hz,
plus the audio device buffer). Range 38..1500 Hz. Known limits: a very strong second harmonic can read an octave
high on the first period; chords are not tracked. Used by the Sub-Octave/legacy paths; Tracked Synth uses
`PolyPitchTracker`.

## PolyPitchTracker (`GuitarSynth/PolyPitchTracker.h`)
Input decimated to ~16 kHz, analysed every ~6 ms with a 96 ms asymmetric Hann window (2048-point FFT; long enough
to split a major third at A2). Spectral peaks -> f0 candidates from harmonics 1..3 -> up to six persistent slots
with ids (a slot turns on after consecutive detections, so a new note gets a new id and a held one keeps it).
Notes an octave apart share partials and can merge into one slot.

## Guitar-to-Bass
Fixed ratio 0.5 (down 1) or 0.25 (down 2), no pitch tracker, so the whole waveform -- chords included -- moves.
Mode picks the shifter:
- MONO (`SpliceShifter.h`, ~8 ms): resampling read tap, jumps of 10..40 ms chosen by normalised correlation so the
  splices land where the input repeats itself, cross-faded. Single notes and power chords within ~3 cents; the
  third of a chord lands ~14 cents sharp one octave down and close voicings smear two octaves down.
- POLY (`PhaseVocoderShifter.h`, ~32 ms synthesis latency, ~41 ms median onset): phase vocoder with identity
  phase locking -- each spectral peak moves to ratio x its true frequency with its lobe intact. The analysis window
  is 8192 points (~170 ms at 48 kHz, enough to split the G-major 49/62/73 Hz cluster) but asymmetric: slow rise,
  then the fall of a 32 ms synthesis Hann; only that newest 32 ms is resynthesised (hop 8 ms), so latency is the
  synthesis length. Chord sheet: every note within ~2 cents at -1 oct (one +14 c outlier at -2 oct). A symmetric
  4096 window measured the same tuning at 85 ms. Measured and rejected: 21 ms synthesis (thirds off by up to 25 c).
- HYBRID (both shifters run): MONO's onset (~9 ms), POLY's tuning in the sustain. An envelope follower pair (1 ms vs
  30 ms attack) spots each pick attack; 50 ms in, `PhaseVocoderShifter::alignToReference()` makes the next frame
  start every partial at the phase MONO's output has it (both read at the analysis window's peak, then advanced by
  the synthesis delay); once that frame is out (+40 ms) a 40 ms raised-cosine fade moves to POLY, and the next
  attack fades back to MONO in 4 ms. Without the alignment the two paths are ~30 ms apart and the fade cancels
  (worst -4.4 dB, single notes down to -8 dB at mid-fade); aligned the mid-fade loss is median -0.2 dB on the DI notes.
  What is left is made up by a gain that holds the mix at the level a coherent blend of the two would have (capped
  at +6 dB, only during the fade). Measured hole vs that coherent blend: worst -2.3 dB single notes, -2.9 dB chord
  shapes, median -0.5 dB. Aligning earlier (20 ms) seeds phases from the pick transient and leaves some notes in
  anti-phase. Legato note changes (no new attack) stay on POLY. Costs MONO + POLY together.
  Switching mode restarts both shifters from silence.
Then bass voicing: 35 Hz high-pass, low shelf at 130 Hz (Body), 4th-order tone low-pass (450 Hz .. 4.5 kHz),
FINGER / PICK (+4 dB shelf at 1.8 kHz, brighter) / ROUND (darker, +3 dB body), soft grit, squash compressor,
and a little of the guitar low-passed at 200 Hz for the string's own attack.
Both FFT users go through `RealFft.h` (juce::dsp::FFT's fallback engine was ~10x slower at 4096 points).

## Sub-Octave Synth
LP at 700 Hz (4th order) -> comparator with 15 % hysteresis -> flip-flop (÷2) -> second flip-flop on every second
edge (÷4). Square subs go through an envelope-swept SVF low-pass and a level that follows the guitar's envelope.
Ring modulator = input x internal sine. No tracking latency.

## Tracked Synth
PolyPitchTracker -> one voice per slot (up to 6): envelope follower (1.5 ms attack, user release) -> gate
(sensitivity) and amplitude (Dynamics) -> PolyBLEP oscillator (saw / square / triangle / sine) + detuned twin, a
square sub one octave under the lowest note -> SVF low-pass swept by the envelope -> soft clip. Glide smooths each
voice's frequency; the mix is normalised by voice count so a chord sits at the level of a single note.

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
A neural timbre-transfer block (does not fit the 5 % core budget), per-string pitch trackers (hexaphonic pickup),
a polyphonic shifter under ~30 ms (resolving a third at A1 needs ~170 ms of history; shrinking the synthesis
window below ~32 ms detunes the thirds).
