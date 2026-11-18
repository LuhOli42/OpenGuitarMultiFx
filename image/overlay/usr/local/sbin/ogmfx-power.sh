#!/bin/sh
# OGMFX power policy for the Cubie A7S (big.LITTLE: A55 cpu0-5 run the UI, A76 cpu6-7 run the audio).
# Rule: a block should work near 70% busy, so its clock follows the load down instead of sitting at the ceiling.
#   * ondemand (the board's CPU governor) raises the clock only above 70% busy and drops it right away
#     (the old tuning, 30% / hold x4, kept every cluster at its maximum with the machine 90% idle).
#   * little cluster: ceiling 1.0 GHz (ogmfx-thermal.sh keeps it); big cluster: full range, so an amp added to the chain
#     can pull it up to 2 GHz; its floor is 780 MHz so the audio callback never has to wake a core at 416 MHz.
#   * memory on demand; NPU asked for its lowest clock (it is idle -- the app does not use it; the driver may ignore it).
echo simple_ondemand > /sys/class/devfreq/a020000.dmcfreq/governor 2>/dev/null
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
