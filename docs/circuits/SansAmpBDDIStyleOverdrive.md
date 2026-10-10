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
- **C**: Drive -- the same non-inverting saturating stage, gain ~6x..100x; clipping is the op-amp's own
  rail saturation (the V1-early has no discrete clipper; the 3.3 V zener pair of later revisions is
  approximated by the macro's ~ +/-3 V swing, documented below).
- **D**: cabinet sim -- bridged-T notch @ ~450 Hz + two cascaded RC poles ~5 kHz + follower.
- **E**: Blend (dry taken at the input buffer, before the notch, as on the real unit -- Blend at 0
  leaves only Bass/Treble/Level) + Baxandall Bass (gyrator ~60 Hz shelf) / Treble (~4 kHz series-RC
  shelf) + Level divider.

## Deliberate simplifications

- The zener clip is approximated by the op-amp macro's rail swing rather than two ideal-zener branches
  (the NodalCircuit diode model is Shockley, not a zener knee); the audible result -- a soft ~3 V clip
  threshold on the emulated path -- is the same mechanism.
- The real cab-sim section is two second-order LPFs + a bridged-T; the model keeps the 450 Hz notch and
  approximates the LP stack with two passive RC poles (~-12 dB/oct effective vs -24).
- TLC2264 modelled by the project's TL072-class macro shape with a lower GBW (0.7 MHz).
