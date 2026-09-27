# Orange Squeezer-Style Compressor

Source: DryBell DM1044 (Kristijan Golub, 2022), the schematic with every value plus a written analysis. A **feedback FET compressor** with no
input buffer: a 2N5457 (Q2) is the lower leg of a divider with R2 82K, i.e. a voltage-controlled resistor at the pedal's input; a JRC4558
gain stage (x23 = 27 dB) makes up the level; a germanium half-wave rectifier (D1, R3, C6, R8: attack ~6 ms, release ~200-470 ms) feeds the JFET's
gate. Q1 is a 500 uA current source; the VR2 trimmer sets Q2's point near cut-off, i.e. the compression threshold. One NodalCircuit block.

Controls: Volume and Bias (VR2: light compression .. limiting; default 0.5, 0.4 over-attenuated). Attack 3.6 .. 6 ms, release 140 .. 265 ms as
measured. Cost 2.6%. Unity trim +3.86 dB. Tests: `Tests/SqueezerStyleCompressorProcessorTests.cpp`.
