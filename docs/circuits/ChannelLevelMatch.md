# Channel level match

User request (2026-11-02): several amps jumped in level on a channel switch, or when a boost was engaged. Each listed
amp now scales its output per channel / switch. The gain is eased over ~10 ms so a switch does not click.

## How the amounts are measured
`AmpSwitchLevelSurvey` measures every stepped control at the default knobs:

- `SWITCH_SURVEY=1` uses the E3 / E1 test tones;
- `SWITCH_DI=1` uses the repo's guitar DI fixture, the level a real player hits the amp with.

A clean channel and a saturated one differ by more on the DI than on the hot test tone, because the saturated one
compresses. No single gain matches both. The stored amount is the dB midpoint of the two, so neither picking strength
is off by more than ~6 dB.

## Amounts

| Amp | Switch | Level match |
|---|---|---|
| Mark IIC+ | Rhythm | +27.3 dB |
| Fryette Deliverance | Less | +25.7 dB |
| Dual Rectifier | Orange Normal / Orange Clean / Orange to Modern / Red to Vintage, vs Red Modern | +13.6 / +11.3 / +0.2 / +13.3 dB (the Vintage loop takes ~13 dB off the power amp) |
| Dumble ODS | Overdrive | −14.2 dB |
| Rivera Knucklehead | Crunch / Lead | −6.8 / −13.4 dB |
| Rivera Knucklehead | Boost | −12.4 dB (it keeps ~3 dB of lift) |
| Matchless HC-30 | Ch1 | +5.2 dB |
| Bogner Uberschall | Clean | +54.8 dB (output trim; ~16 dB is the OD Volume normalised at noon, see BognerUberschall.md) |

## HC-30: a dead EF86 channel, not a level problem
The HC-30's EF86 channel (Ch2) sat ~50 dB under where it should, and a +51.92 dB registry trim hid it. That trim then
slammed Ch1 into the output limiter.

The cause was `trackEf86Screen()`. It derived the screen current from the plate current. With the screen starting at
the rail, the EF86 bottomed out, that formula read zero, and the screen never left the rail: a self-locking dead
operating point. Now:

- it reads `pentodeScreenCurrent()`, which comes straight from the grid / screen voltages;
- `prepare()` relaxes the 2M2 dropper over 40 passes.

The channel works again. Reduced level 6.2 → 9.96, registry trim +51.92 → −1.5 dB, AFVR band bound 5.0 → 5.5 (the
live EF86 channel's treble). The EF86 builder no longer overwrites the `preampPlate` probe, which reads `ch.pre`.
