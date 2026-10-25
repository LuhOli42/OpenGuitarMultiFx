# TraceElliotGP12StyleAmplifier — Trace Elliot GP12 SMX Bass Preamp

## What the real unit is

The Trace Elliot GP12 SMX is the classic solid-state bass preamp (the "SMX" is its
dual-band compressor). Per the owner's manual, the front panel is:

- **Input Gain**, **Pre Shape** (two fixed EQ curves: the classic TE scoop — boosted
  lows and presence with a mid cut — and a wider rock variant),
- the **SMX dual compressor**: the input is split into a low band and a high band, each
  compressed separately (the high band gets the faster attack; the low band keeps
  sustained bass even), with **Low Compression** and **High Compression** controls that
  add level as well as squeeze as they advance,
- **EQ Balance**, which crossfades the two compressed bands and is flat at centre,
- the famous **12-band graphic EQ** (30 / 40 / 60 / 100 / 180 / 340 / 660 Hz,
  1.3k / 2.6k / 5k / 10k / 15k Hz), switchable with the **Graphic** button and followed
  by **Graphic Level**, then **Output**.

No valves and no speaker interaction — it is a clean solid-state preamp, so there is no
power-stage/speaker-impedance model here.

## Sources

- **Trace Elliot's service manual for the GP12 Series 6 preamp** (1999; circulates as el34world
  `Trace_elliot/Trace_Elliot_GP12_serie6-preamp.pdf`). It contains:
  - the "GP12 Circuit" drawing (3-1-89);
  - the circuit description, which names the Mid Pre Shape network and the gyrator bands;
  - the spec sheet (Graphic ±15 dB at 12 centres; Mid Pre Shape +6 dB at 50 Hz and 2 kHz, −6 dB at 400 Hz);
  - the main-board parts list.
- **What the drawing does not cover.** The Series 6 GP12 has **no SMX compressor**: it has a noise gate and a
  Graphic/Flat balance slider instead. The SMX compressor below therefore stays behavioural. So does Pre Shape's
  second curve.

## What is modelled

- **Pre-shape**
  - **Shape 1 is the drawing's Mid Pre Shape circuit** (`pre` NodalCircuit):
    - a twin-T band reject: C6 / C7 10n and R13 / R16 33k series legs, C5 100n and R14 6k8 ∥ C96 4n7 shunts, fed
      from TR2's emitter;
    - IC1a follower;
    - IC2 then selects R17 2k2 instead of R15 10k into IC1b (C8 1µ5, R18 33k inverting).
    The smaller R17 is the "top and bottom" boost the manual describes. The output is normalised by the straight
    path's 33k/10k, so Flat stays unity. Measured relative to Flat it gives +6.7 dB at 50 Hz, −9.0 dB at 400 Hz and
    +6.3 dB at 2 kHz, against the sheet's +6 / −6 / +6.
  - **Shape 2** is the SMX's second curve, which is not on the drawing. It stays an estimate on the straight path:
    +4 dB at 80 Hz, −3 dB at 400 Hz, +2 dB at 4 kHz.
- **Crossover** — a complementary first-order split at 250 Hz (`hi = x − lo`), so the
  two bands recombine exactly and EQ BALANCE is truly flat at centre.
- **Compressors** — a peak detector per band (low: 25 ms / 300 ms; high: 1.5 ms / 80 ms)
  feeding a soft static curve (threshold 60 mV, ratio 1:1 → ~4:1 over the knob) plus the
  manual's documented gain-added-by-the-control makeup.
- **EQ Balance** — crossfade of the two compressed bands, normalised so centre = unity.
- **Graphic EQ** — the drawing's circuit: the two halves of IC5 (TL072), each with 100k into (+) (R48 / R45),
  100k feedback to (−) (R47 / R46) and six 100k sliders spanning them. The bands alternate between the halves as on
  the board: 30 / 60 / 180 / 660 / 2k6 / 10k on the first, 40 / 100 / 340 / 1k3 / 5k / 15k on the second.
  - **Each band** is a transistor gyrator: the wiper's series C into the base of an emitter follower that sits on
    4k7 + 4k7 to ground, with C mid from their joint to the emitter.
  - **With the follower ideal** that is exactly 9k4 in series with L = C mid × 4k7 × 4k7.
  - **The drawing's parts** put every band on its printed centre: 30 Hz → 29.7, 40 → 39.8, 60 → 58.9, 100 → 107,
    180 → 186, 340 → 324, 660 → 589, 1k3 → 1257, 2k6 → 3.06k, 5k → 4.86k, 10k → 10.7k, 15k → 15.8k. Parallel pairs
    on the board are summed.
  - **Graphic = out** bypasses the two stages, as on the real button.
  - **Graphic Level** is a ±15 dB post-EQ gain.

## Deliberate simplifications

- The compressor law (threshold/ratio/makeup) is the model's own curve implementing the
  manual's stated behavior — the SMX's exact timing constants are not published.
- Pre-shape curve values are estimated from the documented shape descriptions, not a
  measured BOM.
- Input/output buffers and the preamp gain block are linear gains (they are clean
  solid-state stages).
- No power amp, cabinet or speaker-impedance stage: the unit is a preamp/DI.
