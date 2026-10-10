# Acoustic 360

## What it is

The solid-state 200 W bass head Acoustic sold with the folded-horn 361 cab — the amp on every
70s funk and rock record you can't quite identify. No tubes at all: a ~24 V germanium/silicon
BJT preamp, a passive Bass/Treble network, the famous Variamp rotary EQ, a Volume control with
bright bleed, and a big complementary solid-state output stage driving the speaker directly
(no output transformer). Front panel: Volume, Bass, Treble, Variamp (5-position rotary) with
its Effect depth knob, Fuzz engage (fuzz level lives inside).

## What's modelled

- The preamp's real order: a BJT gain stage (4.7k collector / 2k+220 emitter, bootstrapped
  input bias) into the passive Fender-family Bass/Treble network, a second identical gain
  stage for recovery.
- The Variamp as its real topology: a selectable series LC trap (fixed ~0.5 H inductor against
  one of five capacitors, ~390/700/1230/2200/3900 Hz) coupled to the recovery output through
  the Effect pot — at zero Effect the trap is fully bypassed, at max it's a deep mid scoop.
  The rotary selects the capacitor, like the real Varitone-derived network.
- Volume with the bright bleed cap across its top leg.
- The output stage honestly: the real amp is a 200 W discrete Class-AB pair — modelled with the
  repo's `addSaturatingOpAmp` power stage (+-55 V rails into 4 ohm) rather than a fake OT, since
  the real amp has no output transformer at all. Speaker impedance model directly on the output.

## Honest simplifications

- The Fuzzrite-derived fuzz section is **not** modelled — it's an optional one-transistor fuzz
  that would double the model's size; the switch is left out and documented here rather than
  shipped as a generic clipper.
- Power-stage saturation is the saturating op-amp, not a discrete quasi-complementary model —
  the real amp's clipping character is dominated by rail limiting anyway.
- The actual transistors (2N3055-family outputs, germanium preamp devices) are modelled with the
  repo's generic NPN BJT; per-device gain/leakage variations are not tracked.
- Volume's taper is linear where the real pot is audio; the tonal difference at noon is minor.
