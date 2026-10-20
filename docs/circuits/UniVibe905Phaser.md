# Uni-Vibe 905 (Shin-ei / Univox, key `UniVibe905StylePhaser`)

Processor: `Source/Effects/Modulation/Phaser/PhaserPedalProcessor.{h,cpp}` (one engine; this model is a `case` in `prepare()`), building blocks in `PhaserParts.h`: TPT first-order all-pass stages (exact while R moves every sample), `Jfet` (ohmic-region square law, with the Vds term that makes these pedals distort a little), `LdrCell` (lamp/LED lag + CdS asymmetric response).

Source: "UniVibe (model 905, by Unicord, circa 1968)". Four transistor phase-splitter stages, each turned by a **CdS
photocell** (MXY-7BX4), all four lit by one incandescent lamp driven from a phase-shift oscillator. The capacitors are
deliberately unequal — **15n, 220n, 470p, 4.7n** — so the four notches are spread unevenly, which is the Vibe's sound
together with the lamp: the filament lags (~40 ms) and the cells brighten fast but darken slowly (~12 / 90 ms), so the
sweep throbs. Chorus mode = dry + wet (100K / 100K), Vibrato = wet only. Intensity = the lamp drive depth, Speed (dual
250K) ~0.6–10 Hz. Low input impedance (~47K) — part of how it sits after a guitar. The project's older `UniVibe` (an
algorithm) is kept alongside.
