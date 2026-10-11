# SansAmp BDDI-Style Overdrive (Tech 21 SansAmp Bass Driver DI, V1)

Bass preamp/DI with the famous mid-scoop "tube amp emulation": input buffer -> 750 Hz bridged-T notch +
72 Hz cabinet HPF -> Presence gain stage -> Drive stage (clipped into op-amp saturation) ->
cabinet-sim filters -> Blend of dry and emulated -> Baxandall Bass/Treble -> Level.

## Sources

- kanengomibako's teardown of the V1 (early): the "tube amp emulation" block lives on the
  PRESENCE+DRIVE side; its simulation shows a large mid-cut built from notch + low-pass sections; the
  blend then mixes this with the dry signal before a shared Baxandall BASS/TREBLE and LEVEL.
- fpgacpu.ca's analysis of the BDI21 (the documented V1-early clone): the 750 Hz bridged-differentiator
  notch (~-10 dB insertion loss), the 72 Hz first-order cabinet HPF, Presence as a gain+knee pair moving
  ~12.5..40.9 dB / 154 Hz..4.8 kHz limited by a 2.2 kHz feedback LP, Drive the same gain range into
  clipping, then cab-sim sections (~450 Hz scoop, -24 dB/oct from ~6 kHz), then the blend.
- kanengomibako's V2 analysis: clipping is via back-to-back 3.3 V zeners in a potted module.

## What is modelled (block A..E netlists)

- **A**: buffer + twin-T notch @ ~750 Hz (21.2k/10n) + 72 Hz first-order HPF.
- **B**: Presence -- non-inverting macro gain stage with the pot straddling (+)/(-) into an RC leg so
  gain and the high-pass knee move together; 68n feedback cap = the ~2.2 kHz LP.
- **C**: Drive -- the same non-inverting saturating stage, gain ~6x..100x; clipping is a discrete
  asymmetric diode pair across the feedback network (one Si forward vs two in series, riding the tap
  of a 100k/22k divider so the pair clips ~5.5x above its ~0.6/1.2 V knees). The earlier rail-only
  model made the Drive knob move only feedback resistance into a saturation the solver almost never
  reached -- ~0% THD at every setting, flagged by the Oct-11 audit. The real V1-early's clipping
  detail is undocumented (the potted module wasn't fully mapped); the DS-1-style feedback clipper is
  the standard interpretation for this stage's gain sweep and reproduces the unit's clean->growl
  sweep, so the diode branch replaced the rail-only assumption rather than coexisting with it.
- **D**: cabinet sim -- bridged-T notch @ ~450 Hz + two cascaded RC poles ~5 kHz + follower.
- **E**: Blend (dry taken at the input buffer, before the notch, as on the real unit -- Blend at 0
  leaves only Bass/Treble/Level) + Baxandall Bass (gyrator ~60 Hz shelf) / Treble (~4 kHz series-RC
  shelf) + Level divider.

## Deliberate simplifications

- The discrete clipper uses plain Shockley Si diodes rather than zeners (the NodalCircuit diode model
  has no zener knee); asymmetry comes from the 1-vs-2 series count, and the divider tap lifts the
  effective threshold to the several-volt swing the emulated path actually produces at noon -- a raw
  0.6 V knee on the output node squashed the stage ~10 dB before it could growl.
- The real cab-sim section is two second-order LPFs + a bridged-T; the model keeps the 450 Hz notch and
  approximates the LP stack with two passive RC poles (~-12 dB/oct effective vs -24).
- TLC2264 modelled by the project's TL072-class macro shape with a lower GBW (0.7 MHz).
