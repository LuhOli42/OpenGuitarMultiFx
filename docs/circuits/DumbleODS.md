# Dumble Overdrive Special (key `DumbleODSStyleAmplifier`)

Processor: `Source/Effects/DumbleODSStyleAmplifierProcessor.{h,cpp}`.

## Sources

There is no factory schematic. The circuit follows the public 1970s ODS trace, four sheets: preamp, "Distorto"
(the overdrive section), power supply and output. The sheets are on schematicheaven.net (`dumbleamps/`) and el34world
(`Dumble/`). The same sites carry a 1997 trace (`97fullschem.pdf`, a 2 x EL34 late ODS), a 90s block diagram and
notes. All of them are kept locally in `~/schematics-audit/ods/`, not in the repo.

The front panel follows the later ODS, serial #124 (1984, revised by Dumble in 1988). Its owner's manual and the
Amp Garage threads describe its controls: Volume, Bright, Mid Boost, Jazz / Rock (with PAB in the middle position),
Treble, Mid, Bass, OD Level, OD Ratio, Master and Presence. The #124 schematic itself sits behind a forum login and
was not used.

The Steel String Singer is a different amp (`DumbleSteelString.md`). The Jazz / PAB / Rock and Mid Boost that earlier
versions of that model carried belong here.

## Signal path

| Stage | Parts |
|---|---|
| Input | 1M to ground, 22K stopper |
| V1A | 12AX7, 100K plate (E), 1.5K ∥ 5µF |
| Tone stack | Treble: 2.2nF in series with 390pF (331pF; the 70s trace has 330pF) → Treble 250K-A. Slope 100K → .1µF → Bass 250K-A (bottom on 1.8K) and .05µF → Middle 100K-L (rheostat) |
| Jazz / PAB / Rock | Rock: the treble pot's bottom to the bass wiper. Jazz: the treble pot's bottom on .005µF to ground, and the bass wiper through 100K into the treble wiper. PAB: the bass wiper lifted through 2 × 22M |
| Mid Boost | shorts the 390pF, leaving 2.2nF |
| Volume | 1M-A off the treble wiper; Bright puts 500pF from its top to its wiper |
| Deep | bass wiper → 270K → 1M → J (10K to ground) ← .01µF ← 270K ← the Volume's wiper |
| V1B | 220K series; 22M from its grid to a .05µF off its own plate; 100K plate (E), 1.5K ∥ 5µF; .01µF out |
| Relay | Normal: straight to the Master. Overdrive: through the two stages below and back |
| V2A | input trim 100K-L → 68K → 12AX7, 100K plate (D), 1.5K ∥ 5µF, 500pF plate to cathode |
| Between | .02µF + 100K → Overdrive 100K-L → 180K |
| V2B | as V2A; .01µF → Ratio 100K-L, back to the relay |
| Master | 10K → 1M-A |
| PI | .01µF → 12AX7 LTP: 100K plates (C), cathodes → 820 → X; X → 24K → N; N → 390 to ground (Accent: 2.2µF across it). Both grids return through 1M to X; the second grid is bypassed to N by .1µF |
| NFB | 3.9K from the speaker into N |
| Power | .1µF couplings, 200K leaks to the bias wiper, 3.3K stoppers, 500 Ω screen resistors, 2 × 6L6GC |
| Bias | the F rail across a pot over 27K |
| Supply | silicon bridge from 345-0-345 → 2 × 100µF in series with 2 × 220K → standby → A 460 V (OT centre tap) → 80 Ω choke → B 454 V (screens) → 2.2K → C 436 V (PI) → 22K → D 306 V (V2) → 1K → E 303 V (V1); 20µF on each node |

## What is modelled

All of the above, in three NodalCircuit blocks:

- **pre:** the whole preamp (V1A, V1B, V2A, V2B) with both relay paths, the Master, and a copy of the PI's .01µF
  and 1M as the Master's load.
- **power:** the PI driven from the Master's wiper through its own .01µF, the power stage, OT, speaker and NFB.
- **supply:** sags off the measured currents.

`reducedOrder` (the shipped path) keeps the preamp block. It replaces the PI and power stage with the shared
behavioural stage, fed from the PI-grid copy. Accent becomes a high-frequency shelf lift there.

## Interpretations and estimates

- **Era mix:** the circuit is the 70s trace; the switches are #124's. #124 replaced Deep with Mid Boost in 1988.
  Both are kept here, because Deep is on the trace and Mid Boost is what #124 is known for.
- **Rock / Jazz wiring:** the trace draws the DPDT ambiguously. It is read the way the Steel String's Guitar / Mic
  switch is wired in its netlist (Dumble renamed Guitar / Mic to Rock / Jazz). Rock ties the treble pot's bottom to
  the bass wiper. Jazz is the .005µF position, and it is the quieter one, as the #124 manual says.
- **PAB:** "the bass pot's wiper disconnects from the bottom of the treble pot and connects through two 22 meg
  resistors in series" (Amp Garage, PAB thread).
- **Mid Boost values:** 2.2nF in series with 390pF, the 390pF shorted. Most builders agree on these values; the
  Trinity OSD kit uses 300pF.
- **Ratio:** the trace's second trim (100K-L after V2B), brought to the panel: "trimmer → overdrive → ratio". Its
  default 0.08 puts Overdrive ~10 dB over Normal at noon with a 0.2 V note. At 0.5 the gap is 23 dB, because the
  stages saturate long before the pot. The input trim stays internal (page 2, OD Trim).
- **Master:** not on the 70s trace. It follows the 90s block diagram: 10K into a 1M-A after the relay.
- **Output tubes and transformer:** the output sheet doesn't name the tubes. They are modelled as 6L6GC, which the
  ODS normally uses. The OT is taken as 4K plate to plate into 8 Ω (the Bassman model's iron), and the 3.9K NFB is
  read from that 8 Ω tap.
- **Bias:** the F rail's voltage and the bias pot's value aren't printed. −66 V and 25K give −50 V on the grids and
  71 mA for the pair at noon.
- **Rails D and E:** these sit ~40 V above the printed 306 / 303 V. The printed 22K drop needs ~5.9 mA, and four
  12AX7s on 100K / 1.5K draw ~4.5 mA here. The rails are kept as drawn rather than loaded to match.

## Controls

Page 1: Channel (Normal / Overdrive), Volume, Bright, Deep, Mid Boost, Jazz / PAB / Rock, Treble, Middle, Bass,
Overdrive, Ratio, Master, Accent.

Page 2: OD Trim (the internal input trim), Power Drive, Bias, Tube Feel, Speaker (4 / 8 / 16 Ω), Output, Cab.

## Verification

| | Model | Trace |
|---|---|---|
| Rails A / B / C | 460 / 459 / 444 V | 460 / 454 / 436 V |
| Rails D / E | 345 / 343 V | 306 / 303 V (see above) |
| PI plates | 323 / 323 V | 308 / 303 V |
| PI tail | 58.9 V | 59 V |
| 6L6 plates | 458 V | 457 V |

- **NFB:** closing the loop lowers the gain by 6.5 dB.
- **Switches:** at 700 Hz, PAB is 16 dB over Rock, Jazz is 4.8 dB under it, and Mid Boost lifts Rock by 6.8 dB.
- **Overdrive:** 10× the input gives 4.3× the output (0.01 → 0.1 V).
- **Stability:** no recoveries on a hot pluck in every channel / voicing combination.
- **Reduced vs full:** level 0.46 dB, worst band 3.92 dB (bound 1.5 / 5). The reduced stage is `bmGain0` 9.5,
  `bmYmax` 0.09, `bmKneeN` 4, trim 1.06.
- **Unity trim:** +7.12 dB (PedalUnityLevel 0.00, Normal channel).
- **CPU:** 10.4 % playing, 7.9 % silent, worst block 12–14 % (EffectCostBench).

The tests are in `Tests/DumbleODSStyleAmplifierProcessorTests.cpp`:

- DC against the printed rails;
- silence;
- a hot pluck on every channel / voicing;
- NFB sign;
- Overdrive gain and compression;
- the switches;
- channel balance;
- the reduced path.
