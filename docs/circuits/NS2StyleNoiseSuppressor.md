# NS-2-Style Noise Suppressor

Boss NS-2: what is public is "a high-quality VCA and high-speed envelope-detecting circuits", an **expander** ("starts its function when the
volume falls below the threshold"), Threshold and Decay knobs and a Reduction / Mute selector that decides what the foot switch does. No
ratio, depth or timing is published, so these are **estimates**: downward expander, ratio 1:3 (10 dB below the threshold is turned down by 20 dB
more), at most -60 dB, opening in ~1 ms and closing in 40 ms .. 2 s (Decay).

Controls: Threshold, Decay, Mute (engaged = the switch's Mute position: the range goes to the maximum). The ISP Decimator was on the list of
candidates and dropped: no reliable public data. Test: `Tests/NoiseGateTests.cpp`.
