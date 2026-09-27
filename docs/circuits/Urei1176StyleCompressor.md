# 1176-Style Compressor

Sources: UREI 1176LN manual (theory of operation, controls, specs) and the scanned schematics Rev A-K in the Sowter PDF (page 54 = Rev F).

## Circuit as modelled
* **L section**: R6 27K in series, a JFET (Q1) as the voltage-variable **shunt** resistor. Level solved in closed form: `(vin - v)/R6 = beta v (2u + (2c-1) v)`,
  u = FET gate overdrive from the side-chain, beta = 1.5 mA/V^2, c = the gate feedback of half the signal that linearises the FET (2c-1 = 0.002; the
  residue is the 1176's distortion). Up to 3 V of control = ~48 dB.
* Preamp/line amp: a fixed x4 here, Input control -20..+20 dB in front, Output -20..+20 dB after.
* **Feedback side-chain** from the preamp output: full-wave peak rectified, biased by the threshold, filtered by C27 with the **attack tau 20 us .. 800 us**
  and **release tau 50 ms .. 1.1 s**; the DC lowers the FET's bias.
* Ratio buttons 4 / 8 / 12 / 20 : the detector sensitivity is tuned numerically so the static slope 8-20 dB above the threshold is the button's ratio
  (test measures 3.6 / 8.6 / 13.9 / 24:1), and the thresholds are the manual's table (-30 / -26 / -25 / -24 dB re 0.775 V at maximum Input; 0 dBu = -18 dBFS).
* **All buttons** ("British mode"): the manual does not describe it. *Estimates*: sensitivity x, larger linearity error (more distortion), timing x0.7. The
  measured slope is ~21:1 (capped by the FET's 48 dB reduction range).

## Behaviour worth knowing
A feedback compressor reacts one sample late: the first samples of a burst always pass, then the loop clamps. The attack setting decides how far it
overshoots: at 20 us the front of a burst is 27 dB down within 1.5 ms, at 800 us only 5 dB (test). The 48 kHz sample period (21 us) is the limit for the 20 us attack.

Controls: Input, Output, Attack, Release, Ratio (4 / 8 / 12 / 20 / All). Test: `Tests/StudioCompressorTests.cpp`.
