# Mesa/Boogie Bass 400+

## Sources

- **Mesa/Boogie's factory drawing of the Bass 400** (© 1983, two pages; circulates as el34world
  `Mesa_boogie/Boogie_bass400.pdf`). This drawing is the netlist. It prints its DC voltages.
- **The Bass 400 Plus owner's manual** (Mesa/Boogie, hosted by Gibson). It describes the same architecture:
  - two channels, with Pull Bright on Volume 1, Treble Shift and a pull Bass Shift;
  - the effects loop and the graphic EQ ahead of the Master;
  - a driver circuit and a buffer circuit feeding twelve power tubes.
  The Plus's differences are a **seven-band** EQ (the 400 has six) and footswitchable channels.

No Plus schematic is public. So the model is the 400's circuit with the Plus's EQ frequencies. The previous model was
an SVT-family stand-in (12AU7 drivers, level shifters, global NFB). None of that is on the drawing.

## What the real circuit is

- **V1A (12AX7):** Input 1 → .047 → grid (1M leak). Plate 100k (247 V); cathode 1500 ∥ 15 µF (1.84 V).
- **Volume 1:** .047 → Volume 1 (1M), with Bright switching 120 pF from its top to the wiper.
- **Channel mixer:** the Volume wipers meet on V2A's grid, channel 1 through 1.5M and channel 2 through 470k. 470k
  goes to ground.
- **V2A:**
  - Plate 100k (224 V).
  - Cathode 1000 Ω, with .15 µF (a treble bypass) and 15 µF + 33k.
  - **Bass Shift** grounds the 33k's far end, which fully bypasses the cathode.
- **Tone stack** straight off V2A's plate:
  - 250 pF (+750 pF with **Treble Shift**, 10M bleed) into the Treble pot.
  - A 100k slope resistor feeds .047 to the Treble pot's bottom / Bass top, and .1 to the Bass bottom / Mid top.
  - Bass 250k and Mid 10k are rheostats.
  - The Treble wiper → .047 → V2B.
- **V2B:** a cathode follower (plate on C, 33k cathode, 172 V) → .1 → 15k → the effects send. The 1M Effects Blend
  sits on the loop.
- **Graphic EQ,** a discrete op-amp:
  - Q1 is the emitter-follower input, Q2/Q3 a differential pair, Q4 the output.
  - 3300 Ω in and 3300 Ω ∥ 10 pF feedback.
  - Six 50k sliders span its two inputs. Each wiper carries R + L + C to AC ground:

    | Band | R | L | C |
    |---|---|---|---|
    | 1 | 470 | 1 H | 22 µF |
    | 2 | 470 | 1 H | 2.2 µF |
    | 3 | 470 | .35 H | 1 µF |
    | 4 | 470 | .22 H | .22 µF |
    | 5 | 470 | .068 H | .15 µF |
    | 6 | 680 | .033 H | .047 µF |

- **Master:** 250k.
- **Driver (V3, 12AX7) — a long-tailed pair:**
  - 33k plates on C (350 V), 33k tail to F (−68 V).
  - V3B's grid sits on the junction of two 62k resistors from the two .047-coupled plate outputs, so the pair
    balances itself.
- **Buffer (V4, 12AX7):**
  - Driven through 470k / 220k from each side.
  - Shared 470 Ω cathode (2 V), plates 276 V. The plate resistors are unmarked; 120k from B is what those voltages
    need.
- **Output stage:**
  - .1 600 V couplings, 100k to D (−63 V), 2200 Ω grid stoppers.
  - **Twelve 6L6s**, six per side: 540 V plates, 526 V screens through 2700 5 W each.
  - 4 / 8 Ω secondary.
  - **No global negative feedback.**
- **Supply:** a bridge feeds A (540 V), then the filter choke to B (535 V), then 10k 2 W to C (370 V, 30 µF). Bias
  supplies are D −63 V and F −68 V.

## What's modelled

1. **`pre`** is channel 1 exactly as above:
   - V1A, the Volume with Bright, the mixer, and V2A with its Bass Shift cathode network;
   - the Treble-Shift tone stack;
   - the V2B follower into the 15k send node.
   Channel 2's Volume (down) adds its 470k to ground. V2B's grid has 3.3M from C on the drawing; a 2.8M to ground
   (*estimated*) puts it at the marked 170 V.
2. **`eq`** is the discrete op-amp as a gain-10⁴ block. It has the drawing's 3300 Ω legs and 10 pF, and 50k sliders
   with series R + L + C at the Plus's seven panel frequencies:
   - 40 / 80 / 160 / 320 / 750 / 2200 / 6600 Hz;
   - L from the drawing's parts family (1, 1, .35, .22, .068, .033, .033 H) and C solved for the frequency;
   - 470 Ω series, 680 Ω on the top two bands.
   This is *estimated*: the Plus's board values are not published. Slider up boosts.
3. **`power`** is driven through 60k (the Master wiper's source). It contains:
   - the self-balancing V3 pair and the V4 buffer pair;
   - .1 couplings into 100k / D, and 367 Ω stoppers (2200 / 6 per composite sextet);
   - two composite sextets (the Koren 6L6GC set ×6) with screens through 450 Ω (2700 / 6);
   - the output transformer at the 4 Ω tap (~1.2k plate-to-plate, *estimated*), and the speaker.
4. **`supply`:** A (200 µF), choke (50 Ω DCR, *estimated*), B (20 µF, 300k balancing), 10k, C (30 µF). C's idle
   load is set to the drawing's marked 535 → 370 V drop.

**Measured DC versus the drawing:**

| Point | Model vs. drawing |
|---|---|
| V1A, V2A, V2B, V3 and V4 points | all within 25 % (V3 within 15 %) |
| 6L6 grids | −63 V |
| A rail | 540 V |
| Screens | 532 V (526 V marked) |
| C rail | 374 V (370 V marked) |
| Twelve 6L6s idle | 366 mA |

## Controls

Page 1 holds the real panel:

| Param | Real control |
|---|---|
| Volume | Volume 1 (1M) |
| Bright | Pull Bright on Volume 1 |
| Bass / Middle / Treble | Bass 250k rheostat, Mid 10k rheostat, Treble 250k (unmarked, *estimated*) |
| Master | 250k, after the EQ |
| Treble Shift / Bass Shift | The pull switches |
| 7 EQ sliders | The graphic EQ |

Page 2 (synthetic):

- **Power Drive**
- **Bias:** D, swept from −70 V to −56 V, with −63 V at noon.
- **Tube Feel:** sag.
- **Speaker:** 2 / 4 / 8 Ω.
- **Output**

## Honest simplifications

- **Channel 2.** Only channel 1 is built. The Plus's channel-select footswitching is not modelled.
- **The EQ's seventh band and its exact L / C** follow the 400's parts family (see above).
- **Effects loop.** The send / return and Blend are left out: no loop is patched.
- **Estimates not on the drawing:**
  - the output transformer;
  - the bridge + PT and choke resistance;
  - the Treble pot's value and taper.

## Reduced-order power stage (shipped default)

As in the rest of the bass family, the app ships a behavioural power stage (`reducedOrder`, set in
EffectRegistry.cpp). It has:

- an envelope-followed sag rail;
- an asymmetric tanh knee: gain0 400, ymax 0.13, level trim 1.0;
- a DC blocker;
- a 70 Hz shelf at 0.55;
- the bass-amp tail voicing, opened to 8 kHz with air at 10 kHz.

These were fitted with `AMP_ONLY=mesa400 AmpFullVsReducedConsistency`. The reduced path tracks the full one within
0.86 dB of level and 2.08 dB of band shape (bound 2 / 4 dB). The registry trim (12.10 dB) gives unity at noon.
