# Scanner Vibrato (Hammond, key `ScannerVibrato`)

Processor: `Source/Effects/Modulation/Vibrato/VibratoPedalProcessor.{h,cpp}` (one class; this model is a `case`).

The Hammond organ's vibrato/chorus: the signal runs down an **LC delay line** (~1.1 ms end to end, cut off around
5–6 kHz) whose taps are read in turn by a **rotating capacitive scanner** driven from the 60 Hz synchronous motor
(412 rpm → 6.87 Hz) — forward along the line and back, a triangle of delay. V1 / V2 / V3 scan a third / two thirds /
all of the line; C1 / C2 / C3 add the undelayed signal (the famous "chorus" — C3 is the Leslie-less gospel sound).
No Rate knob: the scanner's speed is the motor's.
