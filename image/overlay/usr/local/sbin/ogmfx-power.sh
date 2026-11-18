#!/bin/sh
# OGMFX power policy for the Cubie A7S (big.LITTLE: A55 cpu0-5 run the UI, A76 cpu6-7 run the audio).
# Rule: a block should work near 70% busy, so its clock follows the load down instead of sitting at the ceiling.
#   * ondemand (the board's CPU governor) raises the clock only above 70% busy and drops it right away
#     (the old tuning, 30% / hold x4, kept every cluster at its maximum with the machine 90% idle).
#   * little cluster: ceiling 1.0 GHz (ogmfx-thermal.sh keeps it); big cluster: full range, so an amp added to the chain
#     can pull it up to 2 GHz; its floor is 780 MHz so the audio callback never has to wake a core at 416 MHz.
#   * memory on demand with a 1200 MHz floor (see below); NPU asked for its lowest clock (it is idle -- the app does not use it; the driver may ignore it).
D=/sys/class/devfreq/a020000.dmcfreq
echo simple_ondemand > $D/governor 2>/dev/null
# ...with a floor. simple_ondemand never sees the GPU's traffic, so the DRAM sat at its 400 MHz minimum and every frame the
# GPU presents (9 MB at 1920x1200, copied for the display) waited for memory: a black window with ONE moving square cost
# 61% GPU at 60 fps. Floor 1200 MHz -> 19%; 2400 MHz -> 11% (measured). It can still rise above the floor on load.
echo 1200000000 > $D/min_freq 2>/dev/null
N=/sys/class/devfreq/3600000.npu
echo userspace > $N/governor 2>/dev/null && echo 492000000 > $N/userspace/set_freq 2>/dev/null
echo 492000000 > $N/max_freq 2>/dev/null
G=/sys/devices/system/cpu/cpufreq/ondemand
if [ -d $G ]; then
  echo 70 > $G/up_threshold
  echo 1  > $G/sampling_down_factor
fi
for c in 6 7; do echo 780000 > /sys/devices/system/cpu/cpu$c/cpufreq/scaling_min_freq 2>/dev/null; done
exit 0
