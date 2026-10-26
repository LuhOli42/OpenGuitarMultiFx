# Dr. Z Carmen Ghia-Style Amplifier

## Sources

- **Dr. Z's product page:** 18 W, 2× EL84, 2× 12AX7, a 5Y3 rectifier, Volume and Tone. A Master was added in the
  2024 version. [Dr. Z Carmen Ghia](https://drzamps.com/carmen-ghia/)
- **The "Phoenix Project" redraw, rev 1.3** (posted on TDPRI, thread 590924, attachment 305957). This is a hobbyist
  trace of the circuit, not a factory drawing.
  - It is a 700×543 image, the only size available.
  - Its preamp, tone network, NFB and output-stage values are legible.
  - The 5751 phase inverter's tail and grid returns are not: the marked 38 V / 235 V points do not add up with any
    reading at that resolution.

## What the drawing shows

- **V1a (ECC83):**
  - R29 68k stopper, R27 1M input leak.
  - Plate R21 100k (150 V).
  - Cathode R23 2k7 with C13 .68 µF, a **partial** bypass (1.43 V).
- **Tone network.** C11 .022 into C15 250 pF, bridged across R24 + R25 (150k + 150k). Their joint goes to ground
  through the **Tone** pot P2 (500k A) and C17 .01. The far side is the **Volume**, P1 (1M A).
- **V1b:** plate R20 100k (187 V), cathode R22 1k5 with C12 25 µF (1.37 V).
- **PI input and NFB:** C14 .01 → R19 220k → C9 .001 into the 5751 phase inverter (R26 470k). **R10 3M3** global
  negative feedback (2M2 alternate) runs from the speaker onto the R19 / C9 joint.
- **Output stage:**
  - C8 / C7 .022 into R13 / R17 330k grid leaks, then R30 / R31 2700 grid stoppers.
  - 2× EL84 on a shared R12 150 Ω + C15 25 µF cathode (12.4 V).
  - R32 10k screen supply, 8k primary, with C18 .0022 µF across it.
- **Supply:** 5Y3, then R1 500 Ω, 50 µF / 50 µF and the R-C chain (309 / 298 / 290 V…).

## What's modelled

- **Preamp:** exactly as above.
- **Power block:**
  - R19 220k, C9 .001 and the 470k grid leak.
  - R10 3M3 from the speaker onto the joint.
  - 330k power grid leaks, 2700 stoppers, a 150 Ω shared cathode, and C18 .0022 plate to plate.
- **Output-transformer polarity:** the winding sense is chosen so that R10's loop is negative feedback. With the other
  sense the amp oscillates.
- **Kept from the earlier estimate, because the drawing's PI is not legible:**
  - the long-tailed pair's internals (100k plates, 1.2k tail, 12AX7 curves for the 5751);
  - the 5Y3 source;
  - the AC15-family output-transformer / speaker load.
- **Master (synthetic):** the ganged 250k post-PI pot, in series with the 2700 stoppers.

## Controls

Page 1: Volume, Tone, Master. Page 2: Power Drive, Bias, Tube Feel, Speaker, Output.

## Start-up fix shared by the EL84 family

**The bug.** `EL84BoutiqueAmpCore::prepare()` used to solve the supply once at an assumed 0.15 A. It then set the
measured tube current without re-solving, so the rails walked to their real point after start-up. That produced a
thump that scaled with Volume and dominated short measurements. The Carmen Ghia's gain test exposed it once the preamp
was rebuilt.

**The fix.** Prepare now iterates supply → rails → preamp / power → measured current until it settles. With zero
input the output is now exactly zero from the first sample. Matchless, Hot Cat, Astroverb and Carmen Ghia all still
pass their unit, reduced-vs-full and unity tests.

## Reduced-order stage

The level was refitted from 18.5 to 26.4. The reduced path now tracks the netlist within 0.08 dB of level. Its worst
band shape is 7.0 dB of extra treble at the quiet level (bound 8): the netlist's NFB and C18 trim the top at low
level. The registry trim is −12.46 dB, giving unity at noon.
