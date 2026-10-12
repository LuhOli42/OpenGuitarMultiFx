# DC-2 (Boss DC-2 Dimension C)

Processor: `Source/Effects/Modulation/Chorus/DC2StyleChorusProcessor.{h,cpp}` — key `DC2StyleChorus`.

## Sources
- Boss DC-2 schematic (yuvadm/guitar-effects-schematics, `Chorus/BOSS DC-2.jpg`).
- Fractal Audio forum, "Mother of all effect emulation: Boss DC-2" — modes 1–2 slow, 3–4 fast, two depths; triangle LFO;
  the mode buttons put resistors in parallel with the LFO's timing and output resistors; two BBDs modulated 180° apart.

## What is modelled
- Input C4 .047 / R5 1M; IC1b pre-emphasis −(R10 33K ‖ C8 100p) / (R7 47K ‖ (R8 10K + C5 4.7n)): 0.7× below ~600 Hz, 4× above
  ~3.4 kHz.
- **NE570 compander** (`bbdchorus::Ne570`): compressor (ΔG in feedback, 2:1) before the BBDs, an expander after each,
  rectifier C_RECT 0.47 µ × the chip's 10K (4.7 ms).
- **Two MN3207s** (1024 stages) with identical 3rd-order filters (10K / 470p, 10K + 1.8n to the follower, 10K / 220p) — wider
  than the CE-2's, for the MN3207's higher clock.
- **Antiphase**: one triangle LFO, the two clocks get +LFO and −LFO (IC5a/IC6b invert one side).
- **IC12 mixers** with de-emphasis R75 47K ‖ (R74 10K + C51 4.7n) ‖ C50 47p: each output = dry (33K) + its own BBD (R86 39K /
  R85 47K) − the other BBD through 180K (R80/R81). OUTPUT A left, B right; a mono chain gets A.

## Calibrated (see why)
- The mode switch board's resistors are readable (R97 470K, R98 82K, R99 220K) but not their wiring, and the VCO is a
  current-source design (Q7/Q8) whose operating point is not legible. Used: centre clock 120 kHz (~4.3 ms), LFO 0.25 Hz
  (modes 1–2) / 0.5 Hz (3–4), clock swing ±4 % (modes 1, 3) / ±8 % (2, 4). These match the documented behaviour (subtle,
  two speeds × two depths) but are not derived from parts.
- The crossfeed sign and the compander's unity point (0.3 V rectified) are assumptions.
