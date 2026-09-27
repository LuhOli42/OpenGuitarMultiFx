# Mix law: what a Mix knob is, and how every effect was checked (2026-09-26)

## The complaint
"The Mix knob of the reverbs: at half it does not sound like 50% wet and 50% dry."

## What was wrong
Every effect with a Mix knob is a linear crossfade, `dry * (1 - mix) + wet * mix`, which is right **if the wet signal is as loud as the dry
one**. It was not. `Tests/MixLawTests.cpp` measures the energy of the output over 40 s of plucked notes (a reverb tail counts) at Mix 0 / 1 / 0.5:

| Effect | Mix 0 before | Mix 1 before (wet vs dry) |
|---|---|---|
| Ambient (juce::dsp::Reverb) | **+6.0 dB** (it doubles the dry level internally) | +6.4 dB |
| Hall | 0 | **+10.3 dB** (+5.7 .. +18.3 across the Decay knob) |
| Plate | 0 | **+14.8 dB** |
| Gated Reverb | 0 | +4.0 dB |
| Room / Spring / Shimmer | 0 | +1.4 / +1.6 / +2.0 (Room +4.4 and Spring +10 at high Decay) |
| Multi-Tap Delay / Dual Delay | 0 | -5.4 / -3.9 dB |
| IR "Reverb" | 0 | about **-18 dB** (JUCE normalises a loaded IR to 0.125 / sqrt (sum of squares)) |

So with the Hall or Plate, noon was 90%+ reverb; with the IR reverb it was almost all dry; and the Hall's level moved by 12 dB across
Decay alone. The Ambient's Mix 0 was not even the input at unity.

## What changed
* **`Source/Effects/WetLevelMatcher.h`**: keeps a reverb's wet path as loud as its dry input (per channel: the left and right comb banks are
  tuned differently and a mono guitar came out of them up to 7 dB apart). It averages the wet and dry mean squares over ~10 s (only while the
  input is above -70 dB, so a tail after silence does not raise the gain) and moves the wet gain slowly (2 s), starting from a per-effect
  calibrated gain and not moving for the first 2 s. It scales only the output of the reverb, outside its loops, so stability is untouched, and a fixed
  setting settles to a fixed gain (no pumping). Used by Hall, Room, Plate, Spring, Shimmer, Gated Reverb, Ambient and the IR Reverb role
  (not the Cab role: a cab keeps its own level).
* **Ambient**: the JUCE reverb runs fully wet at unity and the crossfade is done in `process()` (its internal 3x wet / 2x dry scale factors are gone).
* **Multi-Tap / Dual Delay**: the wet is normalised by the square root of the taps' energy (four / two decorrelated copies add in energy), not by
  their sum.

## Result (energy vs dry, plucked notes, 40 s)
Mix 0 = 0.00 dB on every effect (UniVibe -0.5: its own throb gain). **Mix 1 within 0.15 dB of the dry on every reverb across the whole grid of
its other knobs** (Decay / Tone / Size / Width at 0, 0.5, 1) and 0.11 dB for the IR reverb. Mix 0.5 is then a true half of each; a linear crossfade of
two uncorrelated signals dips by ~3 dB at noon (-2.3 Hall, -3.4 Plate) -- that is the crossfade, not a level error.

Left as they are (not level errors of the wet path, the effect's own nature): Rotary -3.6 dB and Phaser +3.1 dB at Mix 1 (amplitude
modulation / resonance), Tape Delay +2.8, Flanger -4.5 at noon (comb filtering).

## Found on the way (not changed)
* **Hall and Room: Tone at maximum kills the tail.** `damp1 = tone` reaches 1, so the comb's damping filter never passes anything and the
  feedback is dead: the reverb turns into 8 single echoes. Freeverb keeps its damping under ~0.4 for this reason. Worth limiting the range.
* Mix is read once per block (no smoothing) in the reverbs: moving it fast steps the level every block.
