# Dyna Comp-Style and Ross-Style Compressors

Sources: ElectroSmash's Dyna Comp analysis and schematic (R2 1M, R5 470K, C9 50 nF, 2N3904, VR = 9 V * 22K / 78K); Aion FX's Ross / Dyna
table for the Ross (R19 56K / R20 27K -> 2.93 V; R2 || R3 470K / 470K; the OTA feed R8 + R7 = 220K + 220K; C13 100 nF; C1 150 pF; 2N5088). One
processor, `Model::dynaComp` / `Model::ross`, "Dyna Comp-Style Compressor" and "Ross-Style Compressor" (menu: Dynamics).

## Circuit (two NodalCircuit blocks per channel)

* Block A: emitter-follower input and the two OTA input nets (the CA3080's inputs see the guitar through different filters, so it amplifies the difference).
* OTA: `Iout = Iabc * tanh (Vd / 2 Vt)` computed outside the solver from block A's input-pair voltage and the previous sample's bias current
  (the envelope moves over milliseconds), injected into block B with `addCurrentSource`.
* Block B: Q2 phase splitter, two transistor rectifiers discharging the 10 uF, the envelope, Q5 follower to the OTA's bias-current pin, output.
* Feedback: the louder the guitar, the lower the OTA's bias current and the lower its transconductance.

## Measured

Linear gain 7.9 .. 17 dB over the Sustain range; compression from ~30 .. 100 mV; attack ~8 ms, release ~480 ms. Cost 5.5% of a core (inside
the 5% budget rule the effect was simplified until it was within reach; see `pedal-cpu-budget`). Unity trims: Dyna -2.21 dB, Ross -0.55 dB.
Tests: `Tests/DynaCompStyleCompressorProcessorTests.cpp`.
