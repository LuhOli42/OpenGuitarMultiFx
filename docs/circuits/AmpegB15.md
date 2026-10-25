# Ampeg B-15 Portaflex (B-15-N, rev C)

## Source

Ampeg's factory drawing **B-15-N, part 591722, revision C** (Linden, NJ; "U.S. Patent 3183305"; tubes 6SL7 ×3,
6L6GC ×2, 5AR4). It circulates as `Ampeg_B-15N.pdf`. The drawing prints its DC voltages, measured with a 20 kΩ/V
meter and no signal. Every value below comes from that drawing unless it is marked *estimated*. The later B-12-NF /
B-15-NF drawing (`Ampeg_B-15NF.pdf`) is a different revision and is not modelled.

## What the real circuit is

- **Two identical channels.** Each channel has a NORMAL jack (R2 47k into the grid) and a BRIGHT jack (R1 100k ∥
  C1 .005). The NORMAL jack's shorting contact grounds its 47k when that jack is unused. Grid leak R5 is 5.6M.
- **V1a (6SL7):**
  - R7 470k plate (165 V) and R8 5.6k cathode, **unbypassed** (2.2 V).
  - C3 .1 feeds the **Ultra-Lo** network: C4 .02 + R9 39k + R10 1M to ground. The rocker grounds the R9/R10
    junction, which puts a .02 + 39k shunt on the signal.
  - C19 .01 then feeds the **Volume**, VR1 1M **linear**.
  - **Ultra-Hi** switches C9 500 pF from the Volume's top to its wiper.
- **V1b (6SL7):** R15 220k plate (175 V) and R16 2.2k cathode, **unbypassed** (2 V).
- **Passive tone network** (P.E.C. 250702), fed through C10 .1:
  - 220k into the Bass pot (VR2, 1M log), with .001 from its top to the wiper and .01 from its bottom to the wiper.
    The bottom goes to ground through 22k.
  - .00047 into the Treble pot (VR3, 1M log), whose bottom goes to ground through .0047.
  - R17 120k joins the Bass wiper to the Treble wiper, and the Treble wiper is the output.
- **Mixer.** R18 270k (R22 270k for channel 2) into the grid of V3a.
- **V3 (6SL7), floating paraphase:**
  - R26 / R27 are the 120k plates (235 / 225 V).
  - R28 470k from plate A and R29 510k from plate B meet at a node. That node drives V3b's grid through C11 .022,
    with R23 470k to ground.
  - V3b sits on R24 1k ∥ 25 µF. V3a's cathode sits above it through R25 220 Ω.
  - **R40 10k global negative feedback** runs from the 8 Ω winding onto V3a's cathode.
- **Power stage:**
  - C13 / C14 .022 into R30 / R31 270k grid leaks to the **−50 V** bias rail, then R33 / R32 1k grid stoppers.
  - 2× 6L6GC, cathodes grounded, 450 V plates, 445 V screens, output transformer OT-214.
- **Supply:**
  - 5AR4 → C17 30 µF (plates, R35 56k bleeder) → R39 1k → 40 µF (both screens).
  - From the screens, R37 / R38 22k → 40 µF feed the V3 rail and the V1/V2 rail respectively.
  - Bias: 100k from the high-tension winding, then a diode and 47k / 10k / 10 µF filtering.

## What's modelled

1. **`pre`** is channel 1 exactly as above:
   - the jack network (Input = Normal / Bright)
   - V1a with the switched Ultra-Lo shunt
   - the 1M linear Volume with the switched Ultra-Hi cap
   - V1b and the tone network
   - R18 270k into the mixer node
   The idle channel 2 loads that node through its own 270k and its tone network. That load is *estimated* as 390k
   to AC ground.
2. **`power`** is driven from the mixer node through 180k. That is its Thevenin resistance (270k ∥ 390k plus the
   tone network's output).
   - V3 is the floating paraphase as drawn: 120k plates, the 470k / 510k summing pair, .022 / 470k, the shared
     1k ∥ 25 µF cathode, and R25 220 Ω with the 10k NFB on V3a's cathode.
   - The output side is .022 couplings, 270k leaks to the bias rail, 1k stoppers, 2× 6L6GC (Koren), the output
     transformer as coupled inductors, the speaker model and the 4/8/16 Ω selector.
3. **`supply`** has four nodes: plates (30 µF + 56k), R39 1k to the screens (40 µF), and the two 22k / 40 µF
   branches. The plate and screen currents come from the pentodes. The V3 and V1/V2 currents are measured at idle
   (2.7 mA each).

**Measured DC versus the drawing:**

| Point | Model | Drawing |
|---|---|---|
| Plates | 448 V | 450 V |
| Screens | 437 V | 445 V |
| V1a plate | 178 V | 165 V |
| V1b plate | 178 V | 175 V |
| V3 plates | 227 / 217 V | 235 / 225 V |
| 6L6 grids | −50 V | −50 V |

The 6L6 pair idles at 54 mA total.

## Controls

| Param | Real control |
|---|---|
| Input | Normal / Bright jack |
| Volume | VR1, 1M linear |
| Ultra Lo / Ultra Hi | The faceplate rockers |
| Bass / Treble | VR2 / VR3, 1M log |
| Master | Synthetic: a linear level into V3. The real amp has only the channel Volume. |

Page 2 is all synthetic:

- **Power Drive**
- **Bias:** the −50 V rail swept from −57 V to −43 V, with −50 V at noon.
- **Tube Feel:** rectifier sag and the NFB amount.
- **Speaker:** 4 / 8 / 16 Ω.
- **Output**

## Honest simplifications

- The 6SL7 Koren parameters are estimated (mu 70, ex 1.4, kg1 1200, kp 400), because no published fit exists. The
  plate voltages land within 8 % of the drawing.
- The drawing names the output transformer only as OT-214, so its ~6.6k plate-to-plate is *estimated*. So is the
  5AR4 plus winding resistance (260 Ω).
- Channel 2 is not built. Its load on the mixer node is the estimated 390k.

## Reduced-order power stage (shipped default)

Like the rest of the bass and tube-amp family, the app ships a behavioural power stage
(`AmpegB15StyleAmplifierProcessor::reducedOrder`, set in EffectRegistry.cpp). The preamp netlist stays real. V3, the
6L6 pair, the output transformer, the NFB and the speaker are replaced by:

- an envelope-followed sag rail;
- an asymmetric tanh knee: gain0 25, ymax 0.095, level trim 0.9;
- a DC blocker;
- a 70 Hz shelf at 0.6;
- the bass-amp tail voicing, with its low-pass opened to 8 kHz and air to 10 kHz. The tone network on the drawing
  keeps much more top than the old estimated one did.

These were fitted with `AMP_ONLY=b15 AmpFullVsReducedConsistency`. The reduced path tracks the full one within
0.62 dB of level and 2.88 dB of band shape (bound 2 / 4 dB). The registry trim (−3.20 dB) gives unity at noon.
