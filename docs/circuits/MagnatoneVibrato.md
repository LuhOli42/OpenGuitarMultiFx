# Magnatone-Style Vibrato (Magnatone 280, key `MagnatoneStyleVibrato`)

Processor: `Source/Effects/Modulation/Vibrato/VibratoPedalProcessor.{h,cpp}` (one class; this model is a `case`).

The 1950s–60s Magnatone amps' "true vibrato" (Buddy Holly, Lonnie Mack): a bridge of **varistors**, biased by a sine
LFO, is the resistance of an RC phase-shift network on a 680p cap in V3A's plate circuit — the phase swings, so the
pitch does; a little amplitude modulation rides along because the network is not a perfect all-pass. Modelled as two
first-order phase sections on 680p whose resistance follows an exponential varistor law (~92K–630K at full Intensity)
plus ~6 % AM. Stereo (as the 280): the two sides swing in opposition. Speed 3–9 Hz. Behavioural: varistors are obsolete
and their exact law is not published; the forum measurements and the network's topology set the model.
