#!/bin/sh
# OGMFX thermal guard. Never lets the board reach the 110 C hardware-protection shutdown, and never falls back to the
# factory 60 C trips (those pin every core at 416 MHz the whole time, audible as crackles). The kernel's own
# non-critical trips sit at 105 C; this loop caps BOTH clusters (big cpu6-7, little cpu0-5 -- the UI/GL thread runs
# there) in steps as the hottest zone climbs, and releases one step at a time with 4 C of hysteresis.
#   level 0 <96 C: full speed | 1 >=96: big 1.4 GHz | 2 >=100: big 1.0, little 1.2 | 3 >=104: both 780 MHz | 4 >=107: both 416 MHz
# The user's CPU setting (below) only chooses within those ceilings: no setting can switch the guard off.
set_trips() {
  for z in /sys/class/thermal/thermal_zone[0-6]; do
    for t in $z/trip_point_*_temp; do
      ty=$(cat ${t%_temp}_type 2>/dev/null); [ "$ty" = critical ] && continue
      [ "$(cat $t)" -lt 105000 ] && echo 105000 > $t 2>/dev/null
    done
  done
}
# Settings > Audio > CPU writes one of these here:
#   auto (default) - slow cores follow the load up to 1.0 GHz; they should run near their limit, not idle at 1.8 GHz
#   full           - slow cores follow the load up to the hardware maximum (1.8 GHz)
#   max            - every core locked at its maximum (slow 1.8 GHz, fast 2.0 GHz): for a well-cooled board
#   416 / 780      - slow cores locked at that clock (MHz)
# The fast cores (audio) always follow the load over their full range, floor 780 MHz (ogmfx-power.sh), except in max.
SLOW_CFG=/var/lib/ogmfx/.config/OpenGuitarMultiFx/slow_clock.txt
slow_cfg() { case "$(cat $SLOW_CFG 2>/dev/null)" in 416) echo 416000;; 780) echo 780000;; full) echo full;; max) echo max;; *) echo auto;; esac; }
LIT_AUTO=1014000
min_of() { [ $1 -lt $2 ] && echo $1 || echo $2; }
set_cluster() { # $1 floor kHz, $2 ceiling kHz, cpus... -- floor down first, then the ceiling, then the floor: min must never exceed max
  lo=$1; hi=$2; shift 2
  for c in "$@"; do
    echo 416000 > /sys/devices/system/cpu/cpu$c/cpufreq/scaling_min_freq 2>/dev/null
    echo $hi > /sys/devices/system/cpu/cpu$c/cpufreq/scaling_max_freq 2>/dev/null
    echo $lo > /sys/devices/system/cpu/cpu$c/cpufreq/scaling_min_freq 2>/dev/null
  done
}
cap() { # $1 big kHz, $2 little kHz (this thermal level's ceilings)
  cfg=$(slow_cfg)
  case $cfg in
    auto) M=$(min_of $2 $LIT_AUTO); m=416000 ;;
    full) M=$2; m=416000 ;;
    max)  M=$2; m=$2 ;;
    *)    M=$(min_of $2 $cfg); m=$M ;; # locked: floor = ceiling (a hot level may still pull the lock down)
  esac
  set_cluster $m $M 0 1 2 3 4 5
  if [ "$cfg" = max ]; then bm=$1; else bm=$(min_of 780000 $1); fi
  set_cluster $bm $1 6 7
}
BIG=$(cat /sys/devices/system/cpu/cpu6/cpufreq/cpuinfo_max_freq)
LIT=$(cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq)
apply() {
  case $1 in
    0) cap $BIG $LIT ;; 1) cap 1404000 $LIT ;; 2) cap 1014000 1196000 ;; 3) cap 780000 780000 ;; 4) cap 416000 416000 ;;
  esac
}
up() { case $1 in 1) echo 96000;; 2) echo 100000;; 3) echo 104000;; 4) echo 107000;; esac; }
level=0; apply 0; lastcfg=$(slow_cfg)
while true; do
  set_trips
  cfgnow=$(slow_cfg); [ "$cfgnow" != "$lastcfg" ] && { lastcfg=$cfgnow; apply $level; echo "ogmfx-thermal: CPU setting -> $cfgnow"; }
  m=0; for z in /sys/class/thermal/thermal_zone[0-6]/temp; do v=$(cat $z); [ $v -gt $m ] && m=$v; done
  new=$level
  while [ $new -lt 4 ] && [ $m -ge $(up $((new + 1))) ]; do new=$((new + 1)); done
  if [ $new -eq $level ] && [ $level -gt 0 ] && [ $m -lt $(( $(up $level) - 4000 )) ]; then new=$((level - 1)); fi
  if [ $new -ne $level ]; then level=$new; apply $level; echo "ogmfx-thermal: $m mC -> level $level"; fi
  sleep 1
done
