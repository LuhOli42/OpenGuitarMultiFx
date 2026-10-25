# ODB3StyleOverdrive — Boss ODB-3 Bass Overdrive

## Source

The model follows a complete, legible redraw of the Boss ODB-3. It is a DIYSB / postimg image, 3465×1063, kept
locally as `odb3.jpg` and listed in `docs/circuits/SchematicSources.md`. Every value below comes from it.

The earlier model was built when only a partly legible scan was known. It had:

- an op-amp clipper with silicon diodes;
- the LEDs as a *shunt* clipper;
- an estimated ~480 Hz low-pass "clean" leg;
- a Baxandall stand-in for the tone.

None of that is the real circuit.

## What the real circuit is

- **Input:**
  - R1 10k → C1 47n → Q1 2SK184 source follower (R2 1M to Vref, R3 10k).
  - C2 100n → the Q2 bypass switch (R4 / R5 1M) → C3 100n.
  - The stage-1 gate sits on R6 100k to Vref.
- **Gain stage 1**, a discrete op-amp: the Boss JFET-pair + PNP stage the BD-2 also uses.
  - Q3 / Q4 2SK184 (R8 2k2 drain, R7 4k7 tail) drive Q5 2SA1049 (R11 2k2 collector load).
  - Feedback: R10 2k2 + **Gain VR1a** (250k log), with C5 330p across. R9 1k5 + C4 10µ go to ground.
- **Gain stage 2**, the same stage, DC-coupled from stage 1's collector.
  - Q6 / Q7 (R14 2k2, R15 4k7) drive Q8 (R18 1k5 + R19 820 collector load).
  - The feedback is **the red-LED pair D2 / D3**, anti-parallel, ∥ R17 100k ∥ C8 330p. R16 1k8 + C7 100n go to
    ground.
  - So this stage clips *inside its own feedback loop*, at the LEDs' ~1.7 V knee, from a gain of 1 + 100k/1k8.
- **Filter:** from the R18 / R19 tap, R20 3k3 → C26 2µ2 → a bridged-T (C10 33n across, R21 / R22 4k7 to C11 0.22µ)
  → R28 47k → the **Balance** pot (VR2, 250k lin).
- **Clean path:** the other end of Balance is IC1b (NJM4558), non-inverting from the stage-1 input node.
  - R27 68k + C14 1µ to ground.
  - Feedback: R28 47k + **VR1b**, the Gain pot's second gang, ∥ C15 10p.
  - D9 1SS133 from (−) to (+).
  - So Gain also lifts the clean path, from ×1.7 to ×5.4.
- **Tone:** Balance's wiper → R29 10k → C16 1µ → IC1a (+), with R34 100k to Vref. Feedback is 22k ∥ 47p.
  - **Low** (VR3a, 50k) spans (+) and (−). Its wiper drives C19 0.15µ into a **Q9 gyrator** (R31 3k3 to the emitter,
    C20 47n to the base, R33 100k base bias, R35 10k). That is L = 3k3 × 100k × 47n = 15.5 H in series with 3k3,
    resonating with 0.15 µF near 104 Hz.
  - **High** (VR3b, 50k) has R30 2k2 + C18 15n on its wiper, ~4.8 kHz.
- **Output:** C21 1µ → R44 10k → **Level** VR4 (50k log) → C22 → the Q10 switch → C23 → the Q9' output follower
  → C24 → R43 1k → out (R42 100k).

## What's modelled

Everything above, on NodalCircuit, in four blocks cut at high-impedance or ideal-source boundaries:

- **A:** input follower and coupling.
- **B:** both gain stages, the LEDs, the bridged-T and R28.
- **C:** the IC1b clean stage.
- **D:** Balance, IC1a and the gyrator, Level and the output follower.

Supplies are the Boss ~8 V / 4 V, and the parts are the BD-2's (2SK184 Idss 4.5 mA / Vp −1.2 V; 2SA1049 β 300). The
LEDs use Is 6e-12 with nVt 0.09.

### Shipped build (`reducedOrder`, default on)

Each discrete stage is the BD-2's op-amp macro: finite gain 288 with its common-mode term, one 30 kHz pole, and a
saturating output. Stage 1 sits behind its 2k2 collector load. Stage 2 drives R18 + R19 directly, with a 7.3 V
swing, fitted to the transistor stage.

The LEDs, the feedback networks and everything else stay component-level. The transistor-level netlist
(`reducedOrder = false`) is the test reference. The macros track it within 0.55 dB at five settings: noon, low and
full Gain, 110 Hz to 1 kHz, and both Balance ends.

| Build | CPU | Worst block |
|---|---|---|
| Transistor-level | 14 % | 42 % |
| Macro (shipped) | 4.3 % | 16 % |

## Controls

Level, High, Low, Balance, Gain, in the pedal's front-panel order.

- **Gain:** the ganged 250k log pot, in both drive stages *and* the clean stage.
- **Balance:** up = toward the drive path.
- **Low / High:** up = boost.
- **Level:** 50k log.

## Verification

- **DC:** Q1 gate 4.00 V, source 4.82 V (a JFET follower sits above its gate). Stage outputs 4.16 / 4.67 V.
- **Balance:** at 0 the output is the clean path, linear (×2.0 for ×2 input). At 1 it is the LED-clipped path
  (×1.02).
- **Tone:** Low and High each move their own band.
- **Robustness:** the stress run (random knobs, plucks, hot bursts) has no solve failures.
- **Level:** PedalUnityLevel −0.46 dB at trim 18.19 dB.

## Simplifications

- The JFET bypass switches are their on-resistance (100 Ω).
- Q1 and the output Q9' are ideal followers with their DC offsets.
- The gyrator transistor is an ideal follower too: its 15.5 H does not depend on β.
- The DC jack / capacitance-multiplier regulator is an ideal 8 V / 4 V.
