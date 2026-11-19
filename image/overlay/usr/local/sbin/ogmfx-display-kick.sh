#!/bin/sh
# Re-modesets the screen once, a few seconds after Xorg is up. Run in the background by the X client script
# (ogmfx-kiosk --client, or a board's hand-made ogmfx-client), so DISPLAY/XAUTHORITY are already set.
#
# Why: the DP-over-USB-C monitor on the Cubie A7S (EDID "CX101", RTK scaler) shows the boot console, then goes
# black for good once Xorg sets its mode, while the CRTC, the plane and the app all keep running (scrot of :0 shows
# the full UI). One mode change to a different resolution and back brings the picture back (2026-11-19). Radxa's
# own hdmi-toggle-once.service does exactly this, but it waits for an SDDM X cookie and our xinit kiosk has no SDDM,
# so it always exits "no XAUTH" and never runs.
#
# OGMFX_DISPLAY_KICK=0 turns it off; OGMFX_DISPLAY_KICK_DELAY sets the wait in seconds (default 4).
[ "${OGMFX_DISPLAY_KICK:-1}" = 0 ] && exit 0
command -v xrandr >/dev/null 2>&1 || exit 0
sleep "${OGMFX_DISPLAY_KICK_DELAY:-4}"

out="$(xrandr --query 2>/dev/null | awk '$2 == "connected" { print $1; exit }')"
[ -n "$out" ] || exit 0
# Modes of that output, current one marked '*'. Pick the first mode with another resolution (a same-size refresh
# change was not tried on this monitor; a resolution change is what Radxa does and what was verified).
modes="$(xrandr --query 2>/dev/null | awk -v o="$out" '
    $1 == o { in_out = 1; next }
    in_out && /^[^ ]/ { exit }
    in_out { print $1, ($0 ~ /\*/ ? "cur" : "") }')"
cur="$(printf '%s\n' "$modes" | awk '$2 == "cur" { print $1; exit }')"
alt="$(printf '%s\n' "$modes" | awk -v c="$cur" '$1 != c { print $1; exit }')"
[ -n "$alt" ] || exit 0

xrandr --output "$out" --mode "$alt" 2>/dev/null
sleep 0.3
xrandr --output "$out" --auto 2>/dev/null
xset s off s noblank -dpms 2>/dev/null
logger -t ogmfx-display-kick "re-modeset $out via $alt -> $(xrandr --query 2>/dev/null | awk -v o="$out" '$1 == o { for (i = 3; i <= NF; i++) if ($i ~ /^[0-9]+x[0-9]+\+/) { print $i; exit } }')" 2>/dev/null
exit 0
