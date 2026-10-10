# Garnet Herzog (H-ZOG)

The Garnet Herzog is a small Canadian tube overdrive unit made by Garnet Amplifier Co. (Winnipeg) in the 1960s —
famous as the box Randy Bachman chained into the input of a second amplifier for the "American Woman" tone. It is
not a pedal and not a speaker amp: it is a tiny amplifier whose output transformer drives a **dummy load**, and the
signal tapped off that load goes into the next amp's input at instrument level.

## Sources

- Garnet's own drawing **"H-ZOG RANDY BACHMAN"** (Amplifier Co. Ltd., 611 Ferry Road, Winnipeg 21, Canada), in the
  el34world.com Garnet schematic collection (`Garnet.pdf`, the "H-ZOG RANDY BACHMAN" sheet). Component values below
  are read straight off that sheet.
- Corroborating topology: builder discussions on GroupDIY / The Gear Page / WCGill (single 12AX7 → single-ended 6V6 →
  Champ-type OT → ~6 Ω / 10 W load → attenuated tap).

## What the real circuit is

- **V1A** (1/2 12AX7): input through 220 Ω + 68 k grid stopper, 1 M grid leak, 220 k plate load, 1k5 cathode + 25 µF
  bypass. B+ node "C" ≈ 295 V.
- **Coupling**: .022 µF from V1A's plate; the **S.P.S.T. Deep switch** brings in a second .022 µF path that
  bypasses the volume control and feeds V1B's grid directly.
- **Input Volume**: 1 M log pot between V1A and V1B.
- **V1B** (1/2 12AX7): 100 k plate, 1k5 + 25 µF cathode.
- **6V6GT** single-ended: .047 µF coupling, 220 k grid leak, 470 R / 1 W cathode + 25 µF, screen at the rail,
  .003 µF / 1600 V across the primary.
- **Output transformer**: Garnet 145A189, a Champ-type ~5 k single-ended primary. Secondary feeds the **6 Ω / 10 W
  load resistor**, across which sits the **Output Volume** 1 M pot; the wiper passes through a 150 k series resistor
  to the output jack.
- **Supply**: PT 6K3229 → silicon diodes (800 V PIV, 1 A) → 320+ → 1k (1 W) → 315+ → 10 k → 295+ (node C).
- A D.P.D.T "Herzog in/out" bypass switch on the drawing — not modelled (the plugin bypasses upstream).

## What is modelled

Everything above, in the standard three-block structure:

1. `pre` — V1A → .022 → 1 M Input Volume → V1B, on the ~295 V preamp node. Deep adds a second .022 from
   V1A's plate straight into V1B's grid — a volume-bypassing fuller path.
2. `power` — .047 / 220 k input → single-ended 6V6 (470 R + 25 µF cathode) → 2-winding SE transformer (~5 k : 6 Ω)
   → 6 Ω load → Output Volume + 150 k → output node. The 1 M leak at the output node stands in for the next amp's
   input impedance.
3. `supply` — silicon rectifier → plate reservoir → 1 k screen dropper; the 10 k preamp dropper is folded into the
   preamp-rail RC (sag via `Tube Feel`, which scales the rectifier resistance).

Tubes: Koren ECC83 for both 12AX7 halves; the published Koren 6V6GT set (mu 10, Kg1 1400) for the output tube.

## Controls (page 1)

| Param | Real control |
|---|---|
| Volume | Input Volume (1 M log, between the two triodes) |
| Deep | Deep switch — 0..1 crossfade of the V1B-direct coupling cap |
| Level | Output Volume (1 M log across the dummy load) |

Page 2 (synthetic, matching the amp family conventions): Power Drive (master ahead of the 6V6 grid), Tube Feel
(supply sag), Output (plug-in level trim; the real unit has none — it relies on Level).

## Honest simplifications

- The output network assumes a ~1 MΩ destination input (the real unit was always followed by an amplifier).
- Exact OT inductance and the transformer's winding capacitance are estimated at Champ-family values; the plate-side
  .003 µF snubber is on the drawing and is modelled.
- The Deep switch on the drawing is a literal S.P.S.T. in series with a second .022; wired exactly as drawn
  (a parallel coupling cap) it has no audible effect — the .022 already passes the whole guitar band, so the
  model interprets the second path as landing on V1B's grid past the volume pot, which is what makes the
  switch audible (and 'deeper' — fuller, earlier clipping). The cap value is crossfaded instead of hard-switched
  so it can't zipper.
- Cathode resistors/plate loads are as printed; no second 6V6 variant (some late units used a 6L6) is modelled.

## Verification targets

- Idle 6V6 ≈ 30–40 mA at ~300–320 V plate, cathode ≈ 15–20 V.
- Rails ≈ 320 / 315 / 295 V as marked on the drawing.
- Output is instrument-level (a few volts), NOT speaker-level — `outputScale = 1/6` maps the load-tap volts to full
  scale; the registry `trimmed()` restores unity at noon.
- Deep adds low end (measured at 80 Hz); Level scales the tap almost linearly.
