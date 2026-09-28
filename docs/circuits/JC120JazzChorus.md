# JC-120-Style Amplifier (Roland JC-120 Jazz Chorus)

Display name **"JC-120-Style Amplifier"**. Runs on [`NodalCircuit`](./NodalCircuitSolver.md), but unlike every other
modelled amp in this project it is **solid state** -- no tubes, none of `TubeModels.h`/`TubeAmpCommon.h` reused here.
Two `NodalCircuit` blocks per channel (`ch.pre`, `ch.power`), same convention as the tube amps, plus a chorus/vibrato
stage that is deliberately NOT on `NodalCircuit` (see its own section below).

## Source
Roland's own factory schematic, "JC-120 JC-160", dated Feb 1 1979, found via GitHub (`rshloosh/Roland_JC120`, a
repository of scanned Roland service documents) after the usual tube-amp schematic hosts (el34world, schematicheaven)
turned out not to carry solid-state Roland gear. Read directly from the rasterized drawing at 400 dpi, region by
region.

## Scope
- **Modelled**: both channels' op-amp preamp + tone stack (Treble/Bass/Middle), the master gain stage, the
  footswitch-selectable Distortion boost/clipper (part of the amp's own core signal path, not an add-on effect), the
  power amplifier, the speaker, and -- per explicit user request 2026-09-28 -- the amp's own **real BBD chorus/vibrato
  circuit**. This is a deliberate exception to this project's usual "never model a built-in modulation effect" rule
  (already applied to skip the Fender amps' reverb tanks and vibrato oscillators): on a JC-120 the chorus isn't a
  secondary feature, it's the entire reason the amp exists, unlike a Twin Reverb's reverb tank.
- **Not modelled**: the real amp's spring reverb tank (same "never model an effects loop" call as every other amp
  here; the schematic's "Effect Board ET-7" contains BOTH the reverb driver/recovery circuit -- IC5 (TA-7200P) +
  Q33/Q34 into a physical spring tank -- and the chorus BBD; only the BBD half is modelled). The chorus/vibrato LFO's
  own discrete-transistor oscillator (Q35-Q38 on the real board) is replaced by a plain math sine LFO -- its exact
  waveform shape doesn't materially change the chorus's character the way the BBD's own discreteness does, and this
  project doesn't model oscillators at the transistor level anywhere else either (Vibrato/Chorus/Phaser all use a
  math LFO).

## What is modelled
1. **Preamp** (`ch.pre`, per channel): a TA-7122AP op-amp (modelled as an ideal `addOpAmp` inverting stage, the same
   technique this project's Centaur/DS-1/OD-1 already use for their own op-amps) with ~100x gain from its own
   feedback ratio, followed by a passive 3-band Treble/Bass/Middle tone stack. The tone stack's pot VALUES are
   legible on the schematic (Treble 50k-B linear, Bass 100k-A audio, Middle 5k-B linear -- note the JC-120 mixes
   linear and audio taper pots on the SAME panel, confirmed from the schematic's own "B"/"A"/"C" suffixes, not
   assumed uniform like most of this project's other pots) but the exact trace between them was not fully legible at
   scan resolution, so the network itself is a plausible period-correct TMB topology using those real pot values,
   not a literal trace -- flagged here as an assumption, same honesty standard as the tube amps' unprinted OT ratios.
   Volume is a 100k-C (reverse-log) pot, also read directly off the schematic.
2. **Single-supply op-amp modelling**: the TA-7122AP runs on a single 34 V rail (no negative supply), which this
   project already has a proven technique for (`CentaurStyleOverdriveProcessor`'s own 9 V single-supply pedal): an
   explicit virtual-ground node at half rail (17 V), with the whole preamp built around it as if it were a
   split-supply op-amp. The output/power section, by contrast, is modelled around a TRUE 0 V ground (see the power
   amplifier section below for why), with a coupling capacitor stripping the preamp's 17 V DC bias off before it
   reaches that different reference frame.
3. **Master gain + Distortion boost**: a resistive divider (the real VR5 master pot) into a second recovery op-amp
   stage restores level lost in the passive tone stack and master pot. The Distortion footswitch clipper (a small
   NPN boost transistor into a symmetric silicon diode clipper, matching the schematic's own D15/D16) is modelled as
   a genuine second signal path, CROSSFADED in by the continuous Distortion knob (rather than a hard on/off bypass)
   by trading two coupling resistors' values off against each other -- avoids a discontinuity in the Newton solve
   that a literal switch would cause, and turns what was a real footswitch into a more useful continuous control.
4. **Power amplifier -- the biggest simplification here, and the hardest-won part of this build.** The real amp's
   power section is a multi-transistor quasi-complementary (both-NPN, to save the cost of a matched PNP power
   transistor) Class AB driver/output chain (2SA841GR/2SC1681GR drivers into 2SD425-O outputs). Two full attempts at
   a transistor-level model were made and abandoned before landing on the shipped design:
   - **Attempt 1**: a true complementary (NPN+PNP, simpler than the real quasi-complementary trick) output pair with
     a driver transistor and a 2-diode Vbe-multiplier bias spreader, all DC-coupled directly from the master gain
     op-amp's own output, with that op-amp's global feedback closed all the way around the whole chain back to its
     own inverting input (the "textbook" way a real solid-state power amp works, and the same "close the loop around
     the whole power stage" idea the tube amps use for their own phase-inverter feedback). This never converged to a
     usable DC operating point -- the Newton solve either diverged to non-physical rail-spanning voltages, or (once
     the reference frame mismatch between the single-supply preamp and the dual-rail output stage was fixed)
     converged to a technically-valid but severely IMBALANCED bias point where one output transistor carried a huge
     idle current and the other carried none, and in that state the stage's own AC small-signal gain came out at
     effectively zero -- confirmed by tracing `debugVoltage()` at every node along the signal path and finding the
     preamp and master-gain stages both carrying a healthy AC signal right up to the point where the output pair's
     own transistors sat.
   - **Attempt 2**: separating DC bias (a local resistor divider off the output rails, independent of the signal
     path) from AC signal (coupled in through a capacitor) fixed the DC operating point -- a real, valid, low-current
     Class AB bias point, matching the general real-amplifier technique of never asking one feedback loop to
     simultaneously solve for both "the right junction voltages" and "the right AC transfer function." But it still
     converged to zero net AC gain through the output pair itself, and root-causing exactly why (a per-transistor
     small-signal analysis of a 2-junction Class AB stage inside a global feedback loop, done by hand) was not
     completed in the time available.
   - **Shipped**: the driver + Class AB output pair is replaced by a single `NodalCircuit::addSaturatingOpAmp`
     (real, rail-limited [-31 V, +31 V] gain, no output-transistor Newton port at all) -- the same primitive this
     project already ships for the DS-1/HM-2/BD-2/Klon's own op-amps. This is an HONEST, bigger simplification than
     the "true complementary pair instead of quasi-complementary" one first planned: it gives up individual
     crossover-distortion character entirely in exchange for a power stage that is numerically solid and actually
     passes signal, which a from-scratch transistor-level model could not be made to do here. **Flagged as an open
     simplification** a future pass with more time should revisit, not presented as a transistor-level model.
5. **Speaker**: reuses `tubeamp::speakerModel` (the same generic guitar-speaker cone model the tube amps use) at the
   real amp's own 8 ohm internal speakers.

## Assumptions and known differences from the drawing
- Every transistor's Ebers-Moll parameters (`Is`, `betaF`) are generic small-signal silicon values, not fitted
  against any published curve for the specific 2SA841GR/2SC1681GR/2SD425-O parts -- same documented limitation as
  several of this project's from-datasheet tube fits.
- The tone stack's exact resistor/capacitor topology (not just its pot values) is inferred, not traced -- see above.
- The power amplifier is NOT a transistor-level model at all (see above) -- this is the single biggest fidelity gap
  in this processor relative to the rest of the project's circuit-modelled amps.
- The chorus/vibrato LFO is a plain math sine, not the real discrete-transistor oscillator.
- The real amp's spring reverb is out of scope entirely (matches every other amp on this project's roadmap).

## Verification (`Tests/JC120StyleAmplifierProcessorTests.cpp`)
DC operating point converges cleanly (a true 0 V speaker idle point, unlike the earlier failed attempts). Silence
settles with zero drift/failures. A plucked note through each Input (Channel 1 / Channel 2 / Both) and through both
Chorus and Vibrato modes stays finite and bounded. `PedalUnityLevelTests` passes at 0.00 dB with a measured registry
trim of **+14.10 dB**.

## BBD chorus/vibrato
The real circuit is an MN3002 bucket-brigade device (a 1024-total-stage BBD chip, one 512-stage half per channel)
clocked by a modulated oscillator, with simple companding around it to keep its own noise floor down. Simulating 512
individual bucket-brigade sample-and-hold stages is both unnecessary and far outside this project's per-effect CPU
budget (research confirmed no existing BBD model anywhere in this codebase to build on -- this is the first one).

**The technique**: rather than literally stepping 512 S&H cells, the delay TAP itself is quantized to the BBD's own
effective clock rate -- the LFO-modulated target delay length is held constant for a block of samples (an
approximation of "a real clock edge only moves the bucket chain this often"), then read back with ordinary linear
interpolation between held values. This reproduces the characteristic BBD "stepping"/mild aliasing shimmer at
essentially zero extra CPU (just a counter), rather than a perfectly smooth digital fractional delay (which is what
this project's OTHER modulation effects -- `ChorusProcessor`, `FlangerProcessor` -- already use, and sound
noticeably cleaner/more "digital" than a real BBD by design). A one-pole lowpass filter both before writing into the
delay line and after reading it back stands in for the MN3002's own limited bandwidth (the real chip's anti-aliasing
and reconstruction filters). Chorus mode mixes dry and wet; Vibrato mode (the real amp's other position on the same
switch) is 100% wet, matching how a real vibrato effect works (pure pitch modulation, no dry reference to beat
against).

**Known simplification**: no explicit compander (compressor before / expander after the BBD) is modelled -- the real
circuit's own noise-reduction companding was judged lower priority than getting the core stepped-delay character
right in the time available, and is a candidate for a future pass (this project already has the building blocks --
`EnvelopeFollower.h`, `CompressorCommon.h` -- that every other dynamics-based pedal here reuses for exactly this).

## Cost
Not yet separately profiled against the ~5%/core-per-effect budget this project targets; the circuit is
proportionally larger than any single tube amp stage (two full op-amp/tone-stack preamp channels, a boost/clipper,
one saturating-op-amp power stage, plus the BBD chorus DSP layer) but shares no expensive Newton solve in the output
stage now that it's a saturating op-amp rather than discrete transistors.
