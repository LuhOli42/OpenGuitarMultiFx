#!/bin/sh
# OGMFX thermal guard. Never lets the board reach the 110 C hardware-protection shutdown, and never falls back to the
# factory 60 C trips (those pin every core at 416 MHz the whole time, audible as crackles). The kernel's own
# non-critical trips sit at 105 C; this loop caps BOTH clusters (big cpu6-7, little cpu0-5 -- the UI/GL thread runs
# there) in steps as the hottest zone climbs, and releases one step at a time with 4 C of hysteresis.
#   level 0 <96 C: full speed | 1 >=96: big 1.4 GHz | 2 >=100: big 1.0, little 1.2 | 3 >=104: both 780 MHz | 4 >=107: both 416 MHz
set_trips() {
  for z in /sys/class/thermal/thermal_zone[0-6]; do
    for t in $z/trip_point_*_temp; do
      ty=$(cat ${t%_temp}_type 2>/dev/null); [ "$ty" = critical ] && continue
      [ "$(cat $t)" -lt 105000 ] && echo 105000 > $t 2>/dev/null
    done
  done
}
cap() { # $1 big kHz, $2 little kHz
  for c in 6 7; do echo $1 > /sys/devices/system/cpu/cpu$c/cpufreq/scaling_max_freq 2>/dev/null; done
  for c in 0 1 2 3 4 5; do echo $2 > /sys/devices/system/cpu/cpu$c/cpufreq/scaling_max_freq 2>/dev/null; done
}
BIG=$(cat /sys/devices/system/cpu/cpu6/cpufreq/cpuinfo_max_freq)
LIT=1014000 # little cores (UI / GL thread): ceiling 1.0 GHz -- they should run near their limit, not idle at 1.8 GHz
apply() {
  case $1 in
    0) cap $BIG $LIT ;; 1) cap 1404000 $LIT ;; 2) cap 1014000 $LIT ;; 3) cap 780000 780000 ;; 4) cap 416000 416000 ;;
  esac
}
up() { case $1 in 1) echo 96000;; 2) echo 100000;; 3) echo 104000;; 4) echo 107000;; esac; }
level=0; apply 0
while true; do
  set_trips
  m=0; for z in /sys/class/thermal/thermal_zone[0-6]/temp; do v=$(cat $z); [ $v -gt $m ] && m=$v; done
  new=$level
  while [ $new -lt 4 ] && [ $m -ge $(up $((new + 1))) ]; do new=$((new + 1)); done
  if [ $new -eq $level ] && [ $level -gt 0 ] && [ $m -lt $(( $(up $level) - 4000 )) ]; then new=$((level - 1)); fi
  if [ $new -ne $level ]; then level=$new; apply $level; echo "ogmfx-thermal: $m mC -> level $level"; fi
  sleep 1
done
