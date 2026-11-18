#!/bin/sh
# OGMFX device tuning for the Radxa Cubie A7S (big.LITTLE: A55 cpu0-5, A76 cpu6-7). Idempotent; run by the image
# build (customize.sh) and by every release's install.sh, so an update can never undo it. Does nothing on other
# machines (a desktop/x86 install keeps all its cores and services).
#   * irqbalance off (it moves the USB audio interrupt back to core 0)       -> ogmfx-audio-irq pins it to core 6
#   * mpris-proxy off (BlueZ media proxy; it kept dbus at ~80% on core 6)
#   * system/user managers on cores 0-5 (etc/systemd/*.conf.d/ogmfx-cores.conf), the app's audio thread alone on 6-7
#   * ogmfx-thermal: clock caps instead of the factory 60 C trips / the 110 C shutdown
#   * Bluetooth radio unblocked and powered at boot: a soft rfkill block saved by systemd-rfkill was restored on
#     every boot, so BLE MIDI controllers could never be found ("Failed to set mode: Failed (0x03)")
#   * Bluetooth pairing only from the pedalboard: never pairable on its own (no stranger can pair with it on stage);
#     the app's Bluetooth screen opens a pairing window only while it pairs, closed by PairableTimeout anyway
#   * BlueZ's midi plugin off: the app reads BLE MIDI itself (BleMidiInput.h)
# Pass --now to also apply it to the running system (no reboot needed).
[ "$(uname -m)" = aarch64 ] && [ -e /sys/devices/system/cpu/cpu7 ] || { echo "ogmfx-device-tuning: not a Cubie A7S, skipped"; exit 0; }

systemctl disable --now irqbalance.service >/dev/null 2>&1
systemctl mask irqbalance.service >/dev/null 2>&1
systemctl --global mask mpris-proxy.service >/dev/null 2>&1
systemctl enable ogmfx-audio-irq.service ogmfx-thermal.service ogmfx-power.service >/dev/null 2>&1

# Sets "key = value" in one [section] of an ini file (BlueZ main.conf), replacing an active or commented-out line.
set_ini() { # file section key value
    awk -v sec="[$2]" -v key="$3" -v val="$4" '
        function emit() { if (insec && !done) { print key " = " val; done = 1 } }
        /^\[/ { emit(); insec = ($0 == sec) }
        insec && !done && $0 ~ "^#?[ \t]*" key "[ \t]*=" { print key " = " val; done = 1; next }
        { print }
        END { emit(); if (!done) { print ""; print sec; print key " = " val } }' "$1" > "$1.ogmfx" \
    && if cmp -s "$1" "$1.ogmfx"; then rm -f "$1.ogmfx"; else mv "$1.ogmfx" "$1"; bluez_changed=1; fi
}
bluez_changed=0
conf=/etc/bluetooth/main.conf
if [ -f "$conf" ]; then
    set_ini "$conf" General AlwaysPairable false
    set_ini "$conf" General PairableTimeout 60
    set_ini "$conf" General JustWorksRepairing never
    set_ini "$conf" Policy AutoEnable true
fi
for f in /var/lib/systemd/rfkill/*:bluetooth; do [ -f "$f" ] && printf 0 > "$f"; done

# BlueZ's own MIDI plugin claims a controller's BLE MIDI service and then fails (it needs the ALSA sequencer this
# kernel lacks), and a claimed service is never exported on D-Bus, so the app's BleMidiInput could not see it.
dropin=/etc/systemd/system/bluetooth.service.d/ogmfx-no-midi-plugin.conf
bluetoothd=/usr/libexec/bluetooth/bluetoothd
[ -x "$bluetoothd" ] || bluetoothd=$(command -v bluetoothd)
want="[Service]
ExecStart=
ExecStart=$bluetoothd -P midi"
if [ "$(cat "$dropin" 2>/dev/null)" != "$want" ]; then
    mkdir -p "${dropin%/*}" && printf '%s\n' "$want" > "$dropin" && systemctl daemon-reload 2>/dev/null
    bluez_changed=1
fi

if [ "${1:-}" = "--now" ]; then
    pkill -x mpris-proxy 2>/dev/null
    systemctl daemon-reexec 2>/dev/null
    systemctl restart ogmfx-audio-irq.service ogmfx-thermal.service ogmfx-power.service 2>/dev/null
    for r in /sys/class/rfkill/rfkill*; do [ "$(cat "$r/type" 2>/dev/null)" = bluetooth ] && echo 0 > "$r/soft"; done
    [ "$bluez_changed" = 1 ] && systemctl try-restart bluetooth.service 2>/dev/null
    command -v bluetoothctl >/dev/null && { bluetoothctl power on; bluetoothctl pairable off; } >/dev/null 2>&1
fi
echo "ogmfx-device-tuning: applied"
exit 0
