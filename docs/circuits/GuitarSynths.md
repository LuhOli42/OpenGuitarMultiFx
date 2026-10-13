# Analog guitar synthesizers (Micro Synth, Bass Micro Synth, Guitar Synth, Brassmaster)

Processor: `Source/Effects/Pitch/GuitarSynthProcessor.{h,cpp}`. No oscillator follows the pitch: every voice is made from
the string — what these 1970s units actually did.

**EHX Micro Synthesizer** (`MicroSynthStyleSynth`, D. Cockerell 1978, Hartery redraw): A2A preamp; a **700 Hz low-pass**
(3.3K / 3.3K / 2.2K) feeds a **full-wave rectifier** (Octave Up), the **track-and-hold sub-octave modulator** (Sub Octave),
a **square-wave shaper** whose amplitude the 2N5087 modulator ties to the envelope (Square Wave); with the Guitar voice
they mix into a **resonant VCF swept from Start to Stop** at Rate each time the **attack detector** fires (a new note),
an **Attack Delay** VCA ramp, a **Trigger** threshold and the stop detector's squelch. All ten sliders.
**Bass Micro Synth** (`BassMicroSynthStyleSynth`): the same architecture, analyser and filter ranges set lower.
**Guitar Synth** (`GuitarSynthStyleSynth`): the Micro Synth's voices plus a second sub (two octaves down) and a filter
decay envelope — behavioural (the 1981 unit's full drawing was not used part by part).
**Maestro Brassmaster BB-1** (`BrassmasterStyleSynth`, Philpott redraw): Q1 preamp (Bass Volume = dry), Sensitivity into
Q2's gain, Q3 phase splitter, **transformer and 1N4001 bridge** (Harmonic Hi = rectified octave up; Low = the clipped
signal un-rectified), Brass 1 / Brass 2 RC band-pass voicings around Q5, Brass Volume. The transistor stage's saturation is
modelled as its rail limit (linear, then a knee), not a tanh.

The VCF is a 2-pole TPT state-variable low-pass (`PitchParts.h::Vcf`), safe while swept.
