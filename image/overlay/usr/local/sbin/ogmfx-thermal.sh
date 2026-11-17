#!/bin/sh
# OGMFX thermal guard. Never lets the board reach the 110 C hardware-protection shutdown, and never falls back to the
# factory 60 C trips (those pin every core at 416 MHz the whole time, audible as crackles). The kernel's own
# non-critical trips sit at 105 C; this loop caps BOTH clusters (big cpu6-7, little cpu0-5 -- the UI/GL thread runs
# there) in steps as the hottest zone climbs, and releases one step at a time with 4 C of hysteresis.
set_trips() {
  for z in /sys/class/thermal/thermal_zone[0-6]; do
    for t in $z/trip_point_*_temp; do
      ty=$(cat ${t%_temp}_type 2>/dev/null); [ "$ty" = critical ] && continue
      [ "$(cat $t)" -lt 105000 ] && echo 105000 > $t 2>/dev/null
    done
  done
}
# Settings > Audio > Slow cores clock writes 416 or 780 (MHz) here; anything else = auto (ondemand up to the ceiling below).
SLOW_CFG=/var/lib/ogmfx/.config/OpenGuitarMultiFx/slow_clock.txt
slow_cfg() { case "$(cat $SLOW_CFG 2>/dev/null)" in 416) echo 416000;; 780) echo 780000;; *) echo auto;; esac; }
cap() { # $1 big kHz, $2 little kHz (this thermal level's caps)
  for c in 6 7; do echo $1 > /sys/devices/system/cpu/cpu$c/cpufreq/scaling_max_freq 2>/dev/null; done
  cfg=$(slow_cfg)
  if [ "$cfg" = auto ]; then M=$2; m=416000
  else M=$cfg; [ $2 -lt $M ] && M=$2; m=$M # locked: floor = ceiling (a hot level may still pull the lock down)
  fi
  # the little cluster shares one policy; floor down first, then the ceiling, then the floor -- min must never exceed max
  for c in 0 1 2 3 4 5; do
    echo 416000 > /sys/devices/system/cpu/cpu$c/cpufreq/scaling_min_freq 2>/dev/null
    echo $M > /sys/devices/system/cpu/cpu$c/cpufreq/scaling_max_freq 2>/dev/null
    echo $m > /sys/devices/system/cpu/cpu$c/cpufreq/scaling_min_freq 2>/dev/null
  done
}
BIG=$(cat /sys/devices/system/cpu/cpu6/cpufreq/cpuinfo_max_freq)
LIT=1014000 # little cores (UI / GL thread): ceiling 1.0 GHz -- they should run near their limit, not idle at 1.8 GHz
# One die: the GPU, the little cores (the UI) and the fast cores (the audio) share the heat. The UI pays first, the audio last:
#   level 1 >=94 C: little cores 780 MHz        | level 2 >=98: little 416, big 1.4 GHz
#   level 3 >=102 C: little 416, big 1.0 GHz    | level 4 >=106: everything 416 MHz
apply() {
  case $1 in
    0) cap $BIG $LIT ;; 1) cap $BIG 780000 ;; 2) cap 1404000 416000 ;; 3) cap 1014000 416000 ;; 4) cap 416000 416000 ;;
  esac
}
up() { case $1 in 1) echo 94000;; 2) echo 98000;; 3) echo 102000;; 4) echo 106000;; esac; }
level=0; apply 0; lastcfg=$(slow_cfg)
while true; do
  set_trips
  cfgnow=$(slow_cfg); [ "$cfgnow" != "$lastcfg" ] && { lastcfg=$cfgnow; apply $level; echo "ogmfx-thermal: slow cores clock -> $cfgnow"; }
  m=0; for z in /sys/class/thermal/thermal_zone[0-6]/temp; do v=$(cat $z); [ $v -gt $m ] && m=$v; done
  new=$level
  while [ $new -lt 4 ] && [ $m -ge $(up $((new + 1))) ]; do new=$((new + 1)); done
  if [ $new -eq $level ] && [ $level -gt 0 ] && [ $m -lt $(( $(up $level) - 4000 )) ]; then new=$((level - 1)); fi
  if [ $new -ne $level ]; then level=$new; apply $level; echo "ogmfx-thermal: $m mC -> level $level"; fi
  sleep 1
done
