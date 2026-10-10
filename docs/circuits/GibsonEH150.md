# Gibson EH-150 (Style 4, 1941–42)

The Gibson EH-150 is the archetypal jazz-box amplifier — the amp Charlie Christian used. It went through several
revisions ("Styles"); the model here is **Style 4** (1941–42), the late version with the octal preamp complement and
the 6N7 phase inverter, the one generally meant by "the Charlie Christian EH-150".

## Sources

- Radiomuseum model pages for the EH-150 (Style 4): 3× 6SQ7 high-mu triodes (one instrument stage, two microphone
  stages, one shared), 6N7 twin-triode phase inverter (replacing the earlier Styles' interstage transformer),
  5U4G rectifier, 2× 6L6 push-pull, ~12" speaker.
- Vintage Guitar magazine's EH-150 retrospectives and Amp Garage build threads (Zottola "Tube Guitar Amplifiers"
  chapter): cathode-biased 6L6s, ~15–21 W class-A-ish output, single tone pot sweeping treble↔bass.
- No verified factory schematic with component values could be obtained for this revision; the netlist follows the
  documented Style-4 topology with era-typical values (marked *estimated* below). The earlier transformer-coupled
  Styles are deliberately not modelled — this is the version associated with Christian.

## What the real circuit is

- **Instrument channel**: 6SQ7 → Volume → second ("common") 6SQ7 stage → single Tone control (a treble-cut network:
  a capacitor to ground through the pot's variable resistance).
- **6N7 twin-triode phase inverter**: twin triode (mu ≈ 35, much beefier than a 12AX7) wired as a paraphase — half A
  amplifies the signal to one output tube while a ~1/20 divider off its plate feeds half B, which produces the
  inverted phase for the other.
- **2× 6L6** push-pull, **cathode biased** on a shared ~500 Ω string (~30 mA/tube, ≈15–18 W), ~5 kΩ plate-to-plate
  output transformer, no global negative feedback.
- **5U4G** rectifier feeding a small period filter chain — a noticeably sagging supply.

## What is modelled

Three blocks, same structure as the Trainwreck Express model:

1. `pre` — 6SQ7 stage (250 k plate — the high plate loads Gibson ran in this era — 1k5 + 10 µF cathode) → .022 →
   1 M Volume → 6SQ7 stage (250 k, 2k2 unbypassed) → treble-cut Tone (variable R + 10 nF to ground).
2. `power` — .047 + 470 k input → 6N7 paraphase (100 k plates, shared 2k + 25 µF cathode, 470 k/24 k paraphase
   divider) → .047 couplings, 470 k grid leaks to ground → 2× 6L6 on a shared ~500 Ω + 50 µF cathode → centre-tapped
   ~5 k primary → 8 Ω tap → speaker model.
3. `supply` — 5U4G-equivalent rectifier resistance → 16 µF reservoir → 1k5 → screens; PI and preamp taps RC-filtered
   below the screen node (~300 V PI, ~250 V preamp, estimated).

Tubes: Koren ECC83 stands in for the 6SQ7 (same mu ≈ 100 curve family — the 6SQ7 is the octal ancestor of the
12AX7). The 6N7 gets a fitted low-mu set (mu 35, Kg1 1500 — ~9 mA at 250 V / −5 V). Output tubes are Koren's 6L6GC
set (close family to the original 19 W 6L6G/GA).

## Controls (page 1)

| Param | Real control |
|---|---|
| Volume | Instrument-channel Volume |
| Tone | The single Tone pot (treble at full up, bass rolled off fully CCW) |

Page 2 (synthetic): Power Drive (master ahead of the PI — the real amp has no master), Bias (sweeps the shared
cathode resistor 250–750 Ω — cathode bias has no negative supply to trim), Tube Feel (sag), Speaker (4/8/16 Ω on the
8 Ω tap), Output.

Only the instrument channel is modelled; the second (microphone) channel shares the second 6SQ7 and would not change
the guitar tone.

## Honest simplifications

- All component values except the tube complement and topology are **estimates** (no verified schematic for Style 4):
  the 250 k plate loads, cathode values, the paraphase divider ratio, the tone network's cap, the ~5 k OT, and the
  rail voltages are chosen for era-plausible operating points and documented as such.
- The 6N7 paraphase is drawn as a standard paraphase (divided plate feed to the second half); some drawings show a
  floating-paraphase variant — audibly equivalent.
- The field-coil speaker of early units vs. the PM speaker of late units is covered by the shared speaker model; no
  field-coil supply interaction is modelled.

## Verification targets

- Rails ≈ 360 / 330 V plates/screens; PI plates ~150–200 V; output-tube cathode ~15–35 V; matched idle current
  ~25–40 mA per 6L6.
- No global feedback → stable silence (AC RMS < 0.01), no motorboating.
- Tone sweeps treble-cut; Bias moves idle current (hotter when turned up, matching the other amps' convention).
