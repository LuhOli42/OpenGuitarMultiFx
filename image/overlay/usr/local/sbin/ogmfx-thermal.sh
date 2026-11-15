#!/bin/sh
# OGMFX: nunca trava a CPU no minimo. Limites do kernel em 105 C (critico 110 C fica, desliga sozinho). Em vez de fechar o
# app, a guarda so reduz o clock maximo dos cores rapidos (1,4 GHz a partir de 96 C, 1,0 GHz a partir de 102 C) e solta de novo
# abaixo de 90 C. Os limites de fabrica (60 C, que prendem em 416 MHz) nunca sao devolvidos.
set_trips() {
  for z in /sys/class/thermal/thermal_zone[0-6]; do
    for t in $z/trip_point_*_temp; do
      ty=$(cat ${t%_temp}_type 2>/dev/null); [ "$ty" = critical ] && continue
      [ "$(cat $t)" -lt 105000 ] && echo 105000 > $t 2>/dev/null
    done
  done
}
cap() { for c in 6 7; do echo $1 > /sys/devices/system/cpu/cpu$c/cpufreq/scaling_max_freq 2>/dev/null; done; }
max=$(cat /sys/devices/system/cpu/cpu6/cpufreq/cpuinfo_max_freq)
state=0
while true; do
  set_trips
  m=0; for z in /sys/class/thermal/thermal_zone[0-6]/temp; do v=$(cat $z); [ $v -gt $m ] && m=$v; done
  if   [ $m -ge 102000 ] && [ $state -lt 2 ]; then cap 1014000; state=2
  elif [ $m -ge 96000 ]  && [ $state -lt 1 ]; then cap 1404000; state=1
  elif [ $m -lt 90000 ]  && [ $state -gt 0 ]; then cap $max; state=0
  fi
  sleep 2
done
