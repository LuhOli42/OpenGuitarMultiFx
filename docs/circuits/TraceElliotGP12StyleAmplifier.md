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

## What is modelled

- **Pre-shape** — three biquads (low shelf, mid cut, high shelf) per shape:
  Shape 1 ≈ +6 dB at 60 Hz, −6 dB at 400 Hz, +5 dB at 3 kHz; Shape 2 ≈ +4 dB at 80 Hz,
  −3 dB at 400 Hz, +2 dB at 4 kHz. Flat leaves the signal alone.
- **Crossover** — a complementary first-order split at 250 Hz (`hi = x − lo`), so the
  two bands recombine exactly and EQ BALANCE is truly flat at centre.
- **Compressors** — a peak detector per band (low: 25 ms / 300 ms; high: 1.5 ms / 80 ms)
  feeding a soft static curve (threshold 60 mV, ratio 1:1 → ~4:1 over the knob) plus the
  manual's documented gain-added-by-the-control makeup.
- **EQ Balance** — crossfade of the two compressed bands, normalised so centre = unity.
- **Graphic EQ** — the same gyrator-into-op-amp topology as the Boss graphic EQs (an
  inductor `L = C_gyr R_gyr R_loss` + `R_loss` series branch per band, resonating with
  `C_series` at the band centre, joining the op-amp's (+)/(−) nodes through the slider).
  The 12 bands are distributed across **two cascaded 6-band stages** — both how real
  multi-band graphics parcel their bands across op-amps and what keeps each NodalCircuit
  inside its 48-unknown limit. Graphic = out bypasses the two stages, as on the real
  button. Graphic Level is a ±15 dB post-EQ gain.
- Gyrator values are tuned to the published band centres (the GP12's BOM is not
  published; the frequencies themselves are spec).

## Deliberate simplifications

- The compressor law (threshold/ratio/makeup) is the model's own curve implementing the
  manual's stated behavior — the SMX's exact timing constants are not published.
- Pre-shape curve values are estimated from the documented shape descriptions, not a
  measured BOM.
- Input/output buffers and the preamp gain block are linear gains (they are clean
  solid-state stages).
- No power amp, cabinet or speaker-impedance stage: the unit is a preamp/DI.
