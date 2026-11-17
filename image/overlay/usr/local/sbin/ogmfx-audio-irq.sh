#!/bin/sh
# OGMFX: the USB controller interrupt (the XTONE's audio packets) is served on fast core 7 -- the audio callback owns core 6
# alone (CpuAffinity.h: slot 0) -- instead of slow core 0 where it shares time with the UI, the GL renderer and the rest of the system. A late USB
# interrupt corrupts a few samples -- inaudible clean, a pop once an amp's gain and distortion get hold of it.
# irqbalance must stay off (it moves this interrupt back); see ogmfx-device-tuning in install.sh / customize.sh.
for i in $(awk '/xhci-hcd:usb/{print $1}' /proc/interrupts | tr -d :); do
    echo 7 > /proc/irq/$i/smp_affinity_list 2>/dev/null
done
exit 0
