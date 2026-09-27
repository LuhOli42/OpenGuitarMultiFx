# LA-2A-Style Compressor

Sources: Universal Audio LA-2A manual (specs and figures 5-7: T4 divider R6 68K / R7 2.7K; side-chain 12AX7 + 6AQ5; output 12AX7 + 12BH7A).
Published: attack ~10 ms; release **50% in ~60 ms, full 0.5 s .. 5 s**; up to 40 dB of reduction; THD < 0.35% at +10 dBm.

## Model
* **T4 cell**, behavioural: the reduction (in dB) has two components. Fast (weight 0.75, release tau 50 ms -> 50% of the total in ~55 ms, measured) and slow
  (weight 0.25, release tau 0.3 + 2.0 x memory s). **Memory** (0..1) is the cell's exposure: it rises with the depth of reduction (tau 4 s) and decays over 12 s,
  so a cell lit for 40 s takes 2.5x longer to finish releasing than one lit for 0.4 s (test: 1.35 s vs 0.54 s to 90%). Attack tau 10 ms (fast part), 30 ms (slow).
  The exact time constants are fits to the manual's numbers, not physical measurements.
* **Feedback side-chain** from the output amplifier (before the Gain knob): the light is the peak envelope (0.5 ms / 5 ms) times the Peak Reduction gain; the
  divider ratio Rs/Rc is a power law `(light / ref)^n`: n = 3 in Compress (~2.7:1 measured between -8 and 0 dBFS), 14 in Limit (~9:1; the asymptote is n + 1). Capped at 40 dB.
* **Output stage**: the static transfer of a Koren 12AX7 common-cathode stage (Rp 100K, B+ 250 V, bias -1.5 V) as a 4096-point table, unit small-signal
  gain, grid swing 0.109 V per full-scale unit: THD 0.350% (nearly all second harmonic) at -8 dBFS = +10 dBm. It is a static curve: no transformer, no
  reactive load, no feedback network.
* Peak Reduction: side-chain gain 10^((PR/100 - 1) x 2). Gain: -30 .. +30 dB around 50.

Estimates: the law exponents, the memory constants, the referencePeak (0.05 full scale), the flat side-chain. Controls: Peak Reduction, Gain, Compress / Limit.
Test: `Tests/StudioCompressorTests.cpp` (THD, ratio, release/memory, attack).
