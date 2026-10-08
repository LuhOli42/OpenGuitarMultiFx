#!/usr/bin/env bash
# OGMFX release install script (Phase 6, workstream W3).
#
# Shipped at the root of every ogmfx-<version>-<arch>.tar.gz. The updater
# (updater/ogmfx-update.sh) extracts a release into
# $OGMFX_INSTALL_ROOT/releases/<version>/ and invokes this with the release
# dir as cwd and OGMFX_INSTALL_ROOT / OGMFX_STATE_DIR in the environment.
# It can also be run by hand on a fresh device.
#
# What it does (all steps idempotent — safe to re-run):
#   * installs the systemd units into /etc/systemd/system/
#   * installs sbin/ogmfx-update.sh to /usr/local/sbin/ogmfx-update.sh
#   * installs etc/ogmfx/updater.conf to /etc/ogmfx/updater.conf — only when
#     the file does not exist yet, so an update never clobbers the device's
#     configured channel/repo
#   * makes sure the install root / state dirs exist
#   * reloads systemd and (re)enables the updater timer and the app unit
#
# Deliberately does NOT restart ogmfx.service or touch the `current` symlink:
# in the update flow both happen afterwards, atomically, in
# ogmfx-update.sh steps 5–6 (swap -> restart -> health check -> rollback).
set -euo pipefail

cd "$(dirname "$0")"

install_root="${OGMFX_INSTALL_ROOT:-/opt/ogmfx}"
state_dir="${OGMFX_STATE_DIR:-/var/lib/ogmfx/state}"
# Escape hatch for off-device testing (same convention as ogmfx-update.sh).
systemctl="${OGMFX_SYSTEMCTL:-systemctl}"

log() { printf 'install.sh: %s\n' "$*"; }
die() { printf 'install.sh: ERROR: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Payload sanity — a malformed tarball must fail here, before the updater
# swaps `current`, so the previous release stays live.
# ---------------------------------------------------------------------------
for f in \
    systemd/ogmfx.service \
    systemd/ogmfx-updater.service \
    systemd/ogmfx-updater.timer \
    sbin/ogmfx-update.sh \
    etc/ogmfx/updater.conf \
    bin/OpenGuitarMultiFx \
; do
    [ -f "$f" ] || die "release payload is missing $f"
done

# ---------------------------------------------------------------------------
# Units + updater script + default config
# ---------------------------------------------------------------------------
install -D -m 0644 systemd/ogmfx.service         /etc/systemd/system/ogmfx.service
install -D -m 0644 systemd/ogmfx-updater.service /etc/systemd/system/ogmfx-updater.service
install -D -m 0644 systemd/ogmfx-updater.timer   /etc/systemd/system/ogmfx-updater.timer
install -D -m 0755 sbin/ogmfx-update.sh          /usr/local/sbin/ogmfx-update.sh

if [ ! -f /etc/ogmfx/updater.conf ]; then
    install -D -m 0644 etc/ogmfx/updater.conf /etc/ogmfx/updater.conf
    log "installed default /etc/ogmfx/updater.conf"
else
    log "/etc/ogmfx/updater.conf already exists; leaving it untouched"
fi

# ---------------------------------------------------------------------------
# Layout dirs (docs/Phase6Plan.md "Install layout"). /var/lib/ogmfx survives
# updates: presets/, models/, cache/, state/.
# ---------------------------------------------------------------------------
mkdir -p "$install_root/releases" "$state_dir" \
    "$(dirname "$state_dir")/presets" \
    "$(dirname "$state_dir")/models" \
    "$(dirname "$state_dir")/cache"

# ---------------------------------------------------------------------------
# Reload + enable. A failed daemon-reload or enable must abort the install:
# the updater then leaves `current` on the previous release instead of
# booting into a release whose units never registered.
# ---------------------------------------------------------------------------
"$systemctl" daemon-reload || die "systemctl daemon-reload failed"
"$systemctl" enable --now ogmfx-updater.timer \
    || die "failed to enable ogmfx-updater.timer"
# enable only (no --now): starting/restarting the app is the updater's job,
# and it has to happen after the `current` swap, not before.
"$systemctl" enable ogmfx.service || die "failed to enable ogmfx.service"

log "installed $(cat VERSION 2>/dev/null || echo unknown release)"
