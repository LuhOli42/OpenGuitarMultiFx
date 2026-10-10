# TS9BStyleOverdrive — Ibanez TS9B Bass Tube Screamer

## What the real unit is

The TS9B is Ibanez's bass voicing of the TS9 Tube Screamer. It keeps the TS9's signal
path — emitter-follower input buffer (Q1), a non-inverting op-amp stage with the Drive
pot and a symmetric silicon diode pair in the feedback loop, an op-amp tone stage, Level
pot and a second emitter follower (Q2) — and adds the two things the bass version is
known for:

- **Mix**: blends the driven signal with the clean (buffered, undistorted) input so the
  low fundamental survives the midrange clipping.
- **Bass and Treble** instead of the single Tone control: the op-amp-2 tone stage carries
  a separate shunt leg per band, i.e. a two-band Baxandall arrangement on the same
  bridged-feedback topology as the TS9's single tone pot.
- Front panel, in order: **Drive, Mix, Bass, Treble, Level**.

## What is modelled

Hand-derived Thevenin-chain + Newton solve per sample, identical in form to
`TubeScreamerStyleOverdriveProcessor`:

- **Q1** — ideal emitter follower (`Vb − 0.62`), with the emitter feeding both the
  clipper's (+) bias node (C2 + R5) and the clean path's coupling cap (C_dry + 10K).
- **Clipper** — TS9's own values: R4 4.7K + C3 47 nF to the rail, R6 51K + A500K Drive
  pot in feedback, C4 51 pF and the 1N914/1N4148 pair (Is = 2.52 nA, N = 1.752), solved
  as a 1D Newton-Raphson each sample.
- **Tone stage** — the TS9's bridged-feedback arrangement doubled: the (+) node A is fed
  through R7 and loaded by C5 and R10; the Treble pot and the Bass pot each span node A
  and the (−) node (both pinned at `vA` by the virtual short), each wiper feeding its own
  RC shunt (Treble keeps the TS9's 220 Ω + 0.22 µF; Bass carries 470 Ω + 0.47 µF so its
  band sits low). Op-amp 2 returns the summed B-side current through R9. Closed form per
  sample, no iteration.
- **Mix** — a resistive summer meeting the tone output (wet leg) and the Q1-buffered dry
  signal re-centred on the bias rail (dry leg); the Mix pot crossfades the two legs
  (track much larger than the source impedances, so each end of the knob really mutes
  its path).
- **Level + output** — the TS9's own network: C7 + R11 into the A100K Level pot, closed
  JFET switch, C8, R12-biased Q2 follower, R14 470 Ω + C9 10 µF + R15 100K.

## Deliberate simplifications

- The TS9B's exact tone-stage BOM is not officially published at a level of detail that
  resolves every value; the Bass leg's values (470 Ω / 0.47 µF) and the Mix summer's
  resistances are estimated on the documented topology, matching the real control's
  measured function (bass-voiced second band, clean-blend crossfade).
- Q1/Q2 are ideal followers (no per-sample Newton), as in the TS model.
- The bypass JFET "off" state is not modelled (it conducts only when bypassed).
