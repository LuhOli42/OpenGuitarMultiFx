# DM-2-Style Delay (Boss DM-2, key `DM2StyleDelay`)

Processor: `Source/Effects/Delay/BbdDelayProcessor.{h,cpp}` (shared engine; this model is a `case` in `prepare()`). Delay time: the clock is an RC oscillator whose period is proportional to the Delay pot's resistance, so the delay is linear in that resistance (through the pot's taper) between the published minimum and maximum.

Source: Boss DM-2 schematic (`Delay Echo and Samplers/BOSS DM-2.jpg`). MN3005 (4096 stages) + MN3101, NE570 compander
(IC2: compressor before the BBD, expander after), the CE-2's emphasis (IC1a 47K, 10K + 6.8n, 47K || 100p; IC1b mixer the
same values as de-emphasis). Anti-alias: R14–R16 10K with C11 6.8n, C13 82n (to Q2's emitter), C14 330p. Reconstruction
is **5th order**: Q3 (10K × 3, 2.2n, 33n, 1n) then Q4 (10K × 2, 39n, 330p) — why the repeats are so dark. Intensity = the
feedback (VR4), Echo = the wet level (VR5), Repeat Rate = VR6 1MB in the clock → **20–300 ms** (Boss's spec). Full
Intensity loop gain 1.08: it runs away into oscillation like the pedal, held by the op-amp rails.
