# DS201-Style Noise Gate

A Drawmer DS201-style gate (one channel of the two), from the manual's own specifications: threshold -54 dB .. off; **attack 10 us .. 1 s; hold 2 ms
.. 2 s; decay 2 ms .. 4 s; range 0 .. -80 dB**; a side-chain low-cut (25 Hz .. 4 kHz) and high-cut (250 Hz .. 35 kHz), and key listen.

Envelope, as the manual describes it: the hold restarts while the key is above the threshold and starts running when the key falls below;
then the gain travels the whole range in the decay time; the attack is the time it takes to travel back. The threshold is in dBFS (0 dBu =
-18 dBFS, as in the other studio units). Not a circuit model: the DS201's gate is a VCA controlled by fast logic, and the manual gives no
component data. Test: `Tests/NoiseGateTests.cpp`.
