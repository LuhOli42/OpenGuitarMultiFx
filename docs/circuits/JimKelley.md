# Jim Kelley Single Channel Reverb

Component-level model of the Jim Kelley Single Channel Reverb Amplifier — the late-70s/80s
boutique 60 W combo Jim Kelley built in California. Dumble-adjacent but its own circuit; the
published factory schematic (jimkelleyamplifiers.com/schem.htm, via archive.org) and the
Jim Kelly FACS two-channel schematic were the references, with the Amp Garage "single channel
layout" thread supplying the documented drawing errata.

## Signal path

    in -> V1a 12AX7 (68k stopper, 1M leak, 150k plate, 1k5 unbypassed cathode)
       -> 10nF coupling -> GAIN 1M audio
       -> V1b 12AX7 (anti-parallel 1N914 pairs grid->gnd, ~1.2 V soft clip;
                     100k plate, 1k5 + 10uF cathode)
       -> 22nF -> 100k series feed -> Baxandall shelving stack
          (treble leg .01uF -> TREBLE 500k LIN; bass leg .001uF -> BASS 500k LIN
           with .0047uF shelf cap to ground; joined wipers -> .01uF SERIES
           coupling cap -> 120k link -> 2.2M grid leak)
       -> V3a 12AX7 (68k plate / 4k7 cathode, marked ~215 V / ~2 V on the 245 V rail)
       -> .047 -> VOLUME 1M audio
       -> V4a 12AX7 mixer (220k grid leak, 390k plate load to the ~375 V rail,
          cathode 4k7 = global-NFB injection + Presence network + reverb return)
       -> V4b 12AX7 cathodyne PI (.047 coupling; grid leak = 3M6 to rail + 1M5 to
          ground -> grid ~110 V, matching the schematic's 265 V plate mark;
          68k plate / 68k cathode)
       -> .047 x2 -> 270k grid leaks to -50 V fixed bias -> 1k5 stoppers
       -> 4x 6V6GTA (two per side; shared 2k/10W screen feed)
       -> ~4.3k p-p OT -> 8 ohm tap -> speaker
       -> 100k off the speaker tap -> V4a cathode  (global NFB)

## Notes on the research

- **Cathodyne bias.** The published drawing is missing a resistor in the PI grid network
  (Amp Garage: "a 1M5 in series with the 3M6 to ground"). Read as 3M6 grid->rail plus the
  missing 1M5 grid->ground, the divider sits the grid at ~110 V and the marked voltages
  reproduce exactly (cathode ~110 V, plate 265 V on the ~375 V node).
- **The 1N914 pair.** Two series diodes each way between V1b's grid and ground — Kelley's
  soft clipper ahead of the clip stage (~1.2 V), there to tame blocking distortion rather
  than to create distortion per se; Amp Garage describes it exactly that way.
- **Presence is in the feedback loop, not the preamp.** The Gain-pot pull inserts the
  .1uF + 1k5 shunt in the NFB tail at V4a's cathode — same family as Fender presence but
  wired as a pull-switch. Modelling note: the shunt's mid node needs a 100k DC anchor to
  the cathode — a node whose only DC path is a 1e9 'off' resistor never settles in
  relaxToDc and the whole power-netlist DC solve stalls.
- **Pull pots** (owner's manual): Presence on Gain, Bright on Treble (~560 pF across the
  treble leg), Mid Boost on Bass (470 pF across the Gain pot's top leg).
- **Cold idle is the amp's character.** At the marked -50 V bias the 6V6 pair idles near
  cutoff (~2 mA for the pair) — the crossover notch at low Gain and the way the power
  tubes take over the distortion are what Kelley designed for ("keep the amp clean and
  let the power tubes supply the distortion"), not model defects.
- **Reverb.** The real amp drives its tank from a 12AT7 (V3b) and returns into V4a's grid
  through 470k. The tank itself is modelled behaviourally — a light allpass/comb spring
  emulation (send taps the preamp output, return injected through the same 470k leg) —
  unlike the Carr Rambler / Deluxe Reverb models which drop the reverb control entirely.
  This amp's identity is built around it ("Single Channel Reverb"), and the task asks for it.
- **Power Select.** 60 W runs all four 6V6GTAs; 30 W lifts the second tube of EACH
  pair's cathode via 47k (the classic cathode-lift half-power wiring) -- the lifted
  pair self-cuts at idle, push-pull balance survives, and the supply sees roughly
  half the plate population on its own.
- **Documented drawing errata applied:** 10nF coupling cap after V1a, the missing cathodyne
  bias resistor, C20 = 0.0033uF family values, clamp placement on V1b's grid.
- Historic cab pairing: the open-back 1x12 Electro-Voice EVM-12L (stock cab voice;
  alternates: EV 1x12 profile, generic open 1x12, 4x12 Greenback).

## reducedOrder

Same boundary as the other amps: the whole preamp stays a real solved netlist; V4a, the
cathodyne PI, the four 6V6s, the OT and the physical speaker are replaced by the shared
`behavioralPowerStage` knee + `PowerStageTail`. `bmGain0`/`bmYmax`/`bmLevelTrim` are
placeholders pending this amp's own POWERCAL calibration (the JCM800 precedent).

## Controls

Page 1: Gain, Volume, Treble, Bass, Reverb, Presence (pull), Bright (pull), Mid Boost
(pull), Power Select (60W/30W). Page 2: Power Drive, Bias, Tube Feel, Speaker (4/8/16 ohm
on the 8 ohm tap), Output, Cab.

## Crackles at full Gain, and the category (2026-10-31)
User report: crackles at full Gain. The two series 1N914s each way on V1b's grid were built as four junctions with a
floating node between each pair. While every junction was reverse-biased, Newton had nothing to hold that node, so it
failed on hot plucks. The channel then recovered (state restore) ~8 times a second, in both paths, and each recovery was
a click. Each chain now uses one equivalent diode (`SeriesDiodes.h`: 2× nVt, same Is), the same fix as the Zendrive.
Hard plucks at full Gain give zero failures and zero recoveries, guarded by a test. Unity is unchanged (−0.32 dB on the
old trim). The amp moves from Clean to **Crunch** in the add-effect browser.
