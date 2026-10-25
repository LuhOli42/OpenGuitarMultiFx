# Ampeg V-4B (1971)

## Source

The model follows Ampeg's factory drawing **Model V-4B, part 4010267, dated 5-71**. Two photographs of it circulate:
one on ampegv4.com, and one on the irationaudio V-4B write-up. The second is sharper around the 6K11. The drawing
prints its DC voltages, taken with a 20 kΩ/V meter and no signal. Every value below comes from it unless it is
marked *estimated*.

The V-4 guitar head and VT-22 drawing (`Ampegvt22_v4_reva.pdf`) shares the power amp and the 6K11 mid stage. It adds
reverb and a 6CG7, and is not this amp.

The previous model was built from the SVT preamp and power amp. Its R-numbers were the SVT's.

## What the real circuit is

- **V1a (12AX7)**
  - J1 goes straight to the grid, with R2 5.6M to ground.
  - R6 390k plate (155 V). R5 3.3k cathode, **unbypassed** (1.48 V).
  - C3 .1 feeds R101 100k to ground, then the **Ultra-Lo** switch. OFF passes the signal straight through. ON
    routes it through C101 .015 to ground and three series C102–C104 .0047, with R102 1.5M and R103 1.2M legs.
- **Volume:** VR101, 1M **linear**.
- **Ultra-Hi** does two things:
  - it switches C105 500 pF from the Volume's top to the wiper;
  - it lifts V1b's cathode bypass (C106 6.8 µF) behind R107 47k.
- **V1b:** R13 68k plate (230 V), R14 3.3k cathode (2.1 V).
- **Tone network.** C7 .1 feeds the bass/treble network, P.E.C. 6470000: Bass VR105 and Treble VR103 are 1M log,
  joined by R109 120k between the wipers. Channel 2's V2b joins the same node through C22 .03.
- **6K11 mid stage (three triodes):**
  - **T1:** R204 220k plate (192 V). Cathode Q sits on R202 560 + R203 7.5k (22 V). Its R201 1M grid leak is
    bootstrapped to the 560 / 7.5k joint.
  - **T2:** C202 .01 in, R207 470k plate, R206 3.3k cathode.
  - **T3:** a cathode follower (140 V). It feeds back to Q through R208 56k. It also drives the R211 47k / R210 6.8k
    divider S, which is the stage output.
  - **Mid-Range pot:** VR104 is 50k linear. One end goes to Q through R209 470 + C203 .33; turning toward it shunts
    the feedback and boosts. The other end goes to S through R212 620 + C204 .33; turning toward it shunts the
    output and cuts.
  - The wiper carries L101 and the SW3 capacitor (C113 .033, C114 .15) to ground.
- **12DW7, first half:** S → C205 .1 (R16 180k) → C8 .01 → a cathode follower. Its cathode sits on R19 1k + R20 47k
  (200 V), and its R18 1M grid leak is bootstrapped to the 1k / 47k joint. Its output goes through C9 .47 and R22
  10k to the Ext Amp jacks (R21 100k).
- **12DW7, second half:** R23 10k in, R24 220k plate (205 V), R25 3.3k + **R26 33 Ω** cathode. Global NFB comes in
  from the 8 Ω tap through R34 4.7k ∥ C14 .001, onto R26.
- **12AU7 long-tailed pair:**
  - C10 .01 in. R29 / R27 1M grid leaks, with the second grid held at AC ground (C12 .1).
  - R28 1.5k tail (cathodes 9.7 V). R30 / R31 47k plates (200 V).
  - R33 820k and R32 1M from each plate back to the cathodes.
- **Output stage:**
  - C11 / C13 .33 into R35 / R36 100k from the **−64 V** bias supply, then 47k grid stoppers.
  - 4× 7027A in two banks: 540 V plates, 527 V screens through 470 Ω each.
  - Output transformer T2 (8950028) with 2 / 4 / 8 Ω taps.
- **Supply:**
  - A bridge gives 545 V on the plates, then R55 470 7 W to the 540 V screens.
  - R53 4.7k, R52 4.7k and R51 2.2k step down to the marked 360 V (long-tailed pair) and 325 V (6K11, 12DW7) rails,
    and to the V1 / V2 rail.
  - Bias: C16 .047, D6, R49 / R50, R60 10k, 10 µF.

## What's modelled

1. **`pre`** is channel 1 exactly as above:
   - V1a, the switched Ultra-Lo ladder, the linear Volume, Ultra-Hi (cap and cathode bypass), V1b and the tone
     network;
   - the full 6K11 stage with its DC-coupled feedback, the mid pot and the L101 leg;
   - the cathode follower to the jack node.
   Channel 2's idle V2b is a 33k load through C22 (*estimated*).
2. **`power`** takes the jack node through 19k (R23 plus the jack node's source). It contains:
   - the 12DW7's second half with R26 and the NFB;
   - the 12AU7 long-tailed pair as drawn;
   - .33 couplings, 100k feeds from the −64 V rail, and 23.5k stoppers (2 × 47k per composite bank);
   - two composite 7027A banks (the Koren 6L6GC set, doubled);
   - the output transformer as coupled inductors at the 4 Ω tap, and the speaker model.
   R34 / C14 are referred from the 8 Ω tap to the modelled 4 Ω winding: 4.7k / √2 ∥ .001 × √2 carries the same
   current.
3. **`supply`** models 545 V (150 µF, 200k bleed) and R55 470 to the 540 V screen node (40 µF). The 360 / 325 / 318 V
   rails follow the screen node at the drawing's marked ratios, with the 4.7k × 40 µF time constant.

**12DW7 sections.** The drawing's DC voltages decide which half is which. The cathode follower's 200 V cathode at
~4 mA needs the 12AU7 half. V3b's 1.8 V cathode at 0.54 mA is the 12AX7 half.

**Measured DC versus the drawing:**

| Point | Model vs. drawing |
|---|---|
| V1a, V1b, 6K11 T1, V3b, long-tailed pair plates and cathodes | all within 25 % |
| 7027A grids | −64 V |
| Plates | 545 V |
| Screens | 528 V (540 V marked) |
| 7027A idle, four tubes | 104 mA |

**Mid control at 800 Hz:** +18 dB / −16 dB, against ±20 dB on Ampeg's spec sheet.

## Controls

| Param | Real control |
|---|---|
| Volume | VR101, 1M linear |
| Ultra Lo / Ultra Hi | The faceplate rockers |
| Bass / Treble | 1M log |
| Middle | VR104, 50k linear (up = boost) |
| Mid Freq | SW3: 300 / 800 / 3000 Hz |
| Input | Synthetic 0 / −15 dB pad |
| Master | Synthetic: a linear level into the power amp. The real amp has only the channel Volume. |

Page 2 (synthetic):

- **Power Drive**
- **Bias:** the −64 V rail swept from −71 V to −57 V.
- **Tube Feel:** rectifier sag and the NFB amount.
- **Speaker:** 2 / 4 / 8 Ω.
- **Output**

## Honest simplifications

- **Tone network.** The parts inside the P.E.C. 6470000 module are not printed. The model uses the James network on
  Ampeg's B-15 drawing (220k, .001 / .01, 22k, .00047 / .0047). This is *estimated*.
- **L101 / SW3.**
  - The drawing gives C113 .033 and C114 .15, but its wafer routing is not legible.
  - The model is a series-resonant leg tuned to the spec sheet's 300 / 800 / 3000 Hz: 1.54 H with both caps, 264 mH
    with .15, 85 mH with .033.
  - The coil resistances (460 / 80 / 25 Ω) are *estimated*.
- **Other estimates.** The output transformer (~4k plate-to-plate) and the bridge + PT resistance (110 Ω) are not on
  the drawing. The 6K11 triodes use the 12AX7 Koren set.
- **Channel 2.** Not built (see the 33k load above).

## Reduced-order power stage (shipped default)

As in the rest of the bass family, the app ships a behavioural power stage (`reducedOrder`, set in
EffectRegistry.cpp). It has:

- an envelope-followed sag rail;
- an asymmetric tanh knee with gain0 120 and ymax 0.078;
- a DC blocker and the low shelf;
- the bass-amp tail voicing, with its low-pass opened to 6 kHz and air to 8 kHz.

These were fitted with `AMP_ONLY=v4b AmpFullVsReducedConsistency`. The reduced path tracks the full one within 0.66 dB
of level and 2.41 dB of band shape (bound 2 / 4 dB). The unchanged registry trim leaves PedalUnityLevel at −0.43 dB.
