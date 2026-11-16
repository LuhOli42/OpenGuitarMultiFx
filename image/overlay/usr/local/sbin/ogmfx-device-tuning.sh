#!/bin/sh
# OGMFX device tuning for the Radxa Cubie A7S (big.LITTLE: A55 cpu0-5, A76 cpu6-7). Idempotent; run by the image
# build (customize.sh) and by every release's install.sh, so an update can never undo it. Does nothing on other
# machines (a desktop/x86 install keeps all its cores and services).
#   * irqbalance off (it moves the USB audio interrupt back to core 0)       -> ogmfx-audio-irq pins it to core 6
#   * mpris-proxy off (BlueZ media proxy; it kept dbus at ~80% on core 6)
#   * system/user managers on cores 0-5 (etc/systemd/*.conf.d/ogmfx-cores.conf), the app's audio thread alone on 6-7
#   * ogmfx-thermal: clock caps instead of the factory 60 C trips / the 110 C shutdown
# Pass --now to also apply it to the running system (no reboot needed).
[ "$(uname -m)" = aarch64 ] && [ -e /sys/devices/system/cpu/cpu7 ] || { echo "ogmfx-device-tuning: not a Cubie A7S, skipped"; exit 0; }

systemctl disable --now irqbalance.service >/dev/null 2>&1
systemctl mask irqbalance.service >/dev/null 2>&1
systemctl --global mask mpris-proxy.service >/dev/null 2>&1
systemctl enable ogmfx-audio-irq.service ogmfx-thermal.service >/dev/null 2>&1

if [ "${1:-}" = "--now" ]; then
    pkill -x mpris-proxy 2>/dev/null
    systemctl daemon-reexec 2>/dev/null
    systemctl restart ogmfx-audio-irq.service ogmfx-thermal.service 2>/dev/null
fi
echo "ogmfx-device-tuning: applied"
exit 0
