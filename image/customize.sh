#!/usr/bin/env bash
# OGMFX image customization -- runs INSIDE the aarch64 chroot of the official
# Radxa Cubie A7S (A733) CLI image. Invoked by image/build-image.sh after
# image/overlay/ has been copied onto the rootfs; not meant to run on a live board
# (for a live board use scripts/setup-target.sh directly).
#
# Re-basing onto a newer Radxa image (the known procedure):
#   1. Pin the new URL + SHA-512 in image/build-image.sh (BASE_* variables).
#   2. ./image/build-image.sh  -- every step below either succeeds or stops the
#      build with a message naming the step. The usual suspects on a new base:
#        * step 2 (apt sources): a newer Debian release may not need the
#          archive.debian.org rewrite -- set OGMFX_DEBIAN_ARCHIVE=0;
#        * step 3 (PURGE_PACKAGES): names that no longer exist are skipped with a
#          note; anything new and fat shows up in the size report of step 7;
#        * step 3 (PROTECTED_PACKAGES): if Radxa renamed the kernel / u-boot /
#          GPU packages, the protection check fails loudly -- update the list,
#          never drop the check (the vendor BSP kernel MUST stay, see
#          ARCHITECTURE.md section B).
#   3. Mount the result (build-image.sh --verify) and diff against the previous
#      image's report.
#
# Steps:
#   0. sanity checks (arch, Debian release, build inputs present)
#   1. block service starts inside the chroot (policy-rc.d)
#   2. apt sources: Debian bullseye moved to archive.debian.org
#   3. remove obvious fat (desktop/server leftovers, foreign firmware, docs tools,
#      PulseAudio) while protecting the vendor kernel/bootloader/GPU stack
#   4. install kiosk runtime deps (Xorg via the vendor X server, xinit, JUCE libs)
#   5. RT/audio target setup + user + units (scripts/setup-target.sh --image --kiosk)
#   6. image identity (hostname, machine-id, release file)
#   7. strip docs/man/locales/caches + size report
set -euo pipefail

BUILD_DIR="${OGMFX_BUILD_DIR:-/root/ogmfx-build}"
OGMFX_DEBIAN_ARCHIVE="${OGMFX_DEBIAN_ARCHIVE:-1}"
OGMFX_HOSTNAME="${OGMFX_HOSTNAME:-ogmfx}"
export DEBIAN_FRONTEND=noninteractive
APT=(apt-get -y -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold
     -o Acquire::Retries=3)

# Must survive: the vendor BSP kernel, its bootloader, the Imagination BXM GPU
# driver + the vendor X server built against it, board firmware/config, rsetup
# (first-boot config from /config/before.txt).
PROTECTED_PACKAGES=(
    linux-image-radxa-a733 linux-headers-radxa-a733
    u-boot-radxa-a733 u-boot-aw2501 u-boot-menu
    img-bxm-dkms xserver-xorg-img-bxm-1.21.1-2.deb
    radxa-firmware radxa-bootutils radxa-udev radxa-overlays-dkms
    radxa-system-config-allwinner radxa-system-config-common
    aic8800-usb-dkms aic8800-firmware firmware-realtek firmware-brcm80211
    firmware-misc-nonfree   # generic USB Wi-Fi/BT dongles; only pulled via a meta
    task-a733-xorg task-allwinner-sound alsa-ucm-conf
    rsetup rsetup-config-first-boot
)
# Kept as dependencies of the protected set even when nothing else needs them
# (dkms rebuilds need a compiler and the headers on every kernel update).
PROTECTED_GLOBS=('linux-image-*-a733' 'linux-headers-*-a733' 'radxa-*' 'task-a733*' 'gcc' 'dkms')

PURGE_PACKAGES=(
    # Desktop/browser pulled by the general-purpose image; the kiosk is Xorg+app.
    chromium gnome-shell
    # PulseAudio grabs the USB interface; the app talks to ALSA hw: directly.
    pulseaudio pulseaudio-module-bluetooth
    # Server/dev conveniences with no place on a pedalboard.
    samba samba-common-bin python3-samba python3-pip software-properties-common
    needrestart apt-listchanges local-apt-repository
    # Speech / docs tooling.
    pocketsphinx-en-us man-db manpages vim vim-runtime
    # Firmware for hardware the A7S cannot host (Intel PCIe Wi-Fi, AMD GPUs).
    firmware-iwlwifi firmware-amd-graphics firmware-linux-nonfree firmware-linux
)

RUNTIME_PACKAGES=(
    # Audio
    alsa-utils libasound2
    # Display: Xorg core + input + xinit. /usr/bin/Xorg stays the vendor BXM
    # binary (dpkg diversion by xserver-xorg-img-bxm); the Debian core package
    # provides the module tree / modesetting driver it loads.
    xserver-xorg-core xserver-xorg-input-libinput xinit x11-xserver-utils
    dbus libpam-systemd
    # JUCE runtime (juce_gui_basics/extra, juce_audio_devices ALSA, network,
    # juce_opengl, WebBrowserComponent -- see docs/Phase6Plan.md risks: build
    # with JUCE_WEB_BROWSER=0 drops the webkit line).
    libfreetype6 libfontconfig1 fonts-dejavu-core
    libx11-6 libxext6 libxinerama1 libxrandr2 libxcursor1 libxrender1 libxcomposite1
    libgl1 libcurl4 libgtk-3-0 libwebkit2gtk-4.0-37
    # Bring-up / benchmark helpers (ARCHITECTURE.md section G): cyclictest, temps.
    rt-tests lm-sensors usbutils
)

step() { echo; echo "=== customize: $*"; }
die()  { echo "customize: ERROR: $*" >&2; exit 1; }
pkg_installed() { dpkg-query -Wf '${Status}' "$1" 2>/dev/null | grep -q '^install ok installed'; }

# ---------------------------------------------------------------- 0. sanity
step "0. sanity checks"
[[ "$(uname -m)" == aarch64 ]] || die "not running under aarch64 (qemu binfmt missing?)"
grep -q '^11\.' /etc/debian_version || echo "customize: NOTE: base is Debian $(cat /etc/debian_version), validated on 11 (bullseye)"
[[ -x "$BUILD_DIR/setup-target.sh" ]] || die "$BUILD_DIR/setup-target.sh missing (build-image.sh copies it)"
compgen -G '/lib/modules/*-a733' >/dev/null || die "no vendor A733 kernel modules in /lib/modules -- wrong base image?"
echo "vendor kernel: $(basename /lib/modules/*-a733)"

# --------------------------------------------------------- 1. policy-rc.d
step "1. block service starts in the chroot"
printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d
chmod 0755 /usr/sbin/policy-rc.d
trap 'rm -f /usr/sbin/policy-rc.d' EXIT

# --------------------------------------------------------- 2. apt sources
step "2. apt sources"
if [[ "$OGMFX_DEBIAN_ARCHIVE" == 1 ]]; then
    # Bullseye is end-of-life: deb.debian.org still serves stale indices whose
    # packages 404 (seen on xserver-xorg-core 1.20.11-1+deb11u18). Point only
    # the Debian lists at the archive; Radxa's repos are left untouched.
    for f in /etc/apt/sources.list /etc/apt/sources.list.d/*.list; do
        [[ -f "$f" ]] || continue
        sed -i -E \
            -e 's#https?://deb\.debian\.org/debian-security#http://archive.debian.org/debian-security#' \
            -e 's#https?://(deb|security)\.debian\.org/debian( |$)#http://archive.debian.org/debian\2#' \
            "$f"
    done
    echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf.d/90ogmfx-archive
fi
grep -h '^deb ' /etc/apt/sources.list /etc/apt/sources.list.d/*.list 2>/dev/null || true
"${APT[@]}" update

# ------------------------------------------------------------- 3. remove fat
step "3. remove fat (protecting the vendor stack)"
for p in "${PROTECTED_PACKAGES[@]}"; do
    pkg_installed "$p" || die "protected package $p is not in the base image -- Radxa renamed it? update PROTECTED_PACKAGES"
done
mapfile -t keep < <(dpkg-query -Wf '${Package}\n' "${PROTECTED_GLOBS[@]}" 2>/dev/null | sort -u)
apt-mark manual "${PROTECTED_PACKAGES[@]}" "${keep[@]}" >/dev/null
purge=()
for p in "${PURGE_PACKAGES[@]}"; do
    if pkg_installed "$p"; then purge+=("$p"); else echo "skip (not installed): $p"; fi
done
if (( ${#purge[@]} )); then
    "${APT[@]}" purge --autoremove "${purge[@]}"
fi
for p in "${PROTECTED_PACKAGES[@]}"; do
    pkg_installed "$p" || die "removing fat took out protected package $p -- fix PURGE_PACKAGES"
done

# --------------------------------------------------------- 4. runtime deps
step "4. kiosk runtime dependencies"
xorg_before="$(sha256sum /usr/bin/Xorg | cut -d' ' -f1)"
"${APT[@]}" install --no-install-recommends "${RUNTIME_PACKAGES[@]}"
dpkg-divert --list /usr/bin/Xorg | grep -q xserver-xorg-img-bxm \
    || die "vendor Xorg diversion lost"
[[ "$(sha256sum /usr/bin/Xorg | cut -d' ' -f1)" == "$xorg_before" ]] \
    || die "/usr/bin/Xorg changed -- the vendor BXM X server must stay"
[[ -e /usr/lib/xorg/modules/drivers/modesetting_drv.so ]] || die "modesetting driver missing"
[[ -e /usr/lib/xorg/modules/input/libinput_drv.so ]] || die "libinput driver missing"

# -------------------------------------------------------- 5. target setup
step "5. RT/audio target setup"
# Creates the ogmfx user/groups, limits, governor tmpfile, udev rules, install
# layout, enables ogmfx.service + ogmfx-updater.timer, default ogmfx.target.
"$BUILD_DIR/setup-target.sh" --image --kiosk
# Overlay files must be root-owned; the launcher must be executable.
chmod 0755 /usr/local/bin/ogmfx-kiosk
systemctl is-enabled ogmfx.service ogmfx-updater.timer
[[ "$(systemctl get-default)" == ogmfx.target ]] || die "default target is not ogmfx.target"
systemctl is-enabled getty@tty1.service >/dev/null || systemctl enable getty@tty1.service

# ---------------------------------------------------------- 6. identity
step "6. image identity"
echo "$OGMFX_HOSTNAME" > /etc/hostname
sed -i -E "s/^127\.0\.1\.1\s.*/127.0.1.1\t$OGMFX_HOSTNAME/" /etc/hosts
grep -q '^127\.0\.1\.1' /etc/hosts || printf '127.0.1.1\t%s\n' "$OGMFX_HOSTNAME" >> /etc/hosts
# Empty machine-id => systemd generates a unique one on first boot.
: > /etc/machine-id
rm -f /var/lib/dbus/machine-id
{
    echo "OGMFX_IMAGE_BUILD_DATE='$(date -u +%Y-%m-%dT%H:%M:%SZ)'"
    echo "OGMFX_IMAGE_GIT='${OGMFX_IMAGE_GIT:-unknown}'"
    echo "OGMFX_IMAGE_BASE='${OGMFX_IMAGE_BASE:-unknown}'"
    echo "OGMFX_IMAGE_KERNEL='$(basename /lib/modules/*-a733)'"
} > /etc/ogmfx/image-release

# ------------------------------------------------------------- 7. strip
step "7. strip docs / man / locales / caches"
"${APT[@]}" clean
find /usr/share/doc -mindepth 1 ! -name copyright ! -type d -delete
find /usr/share/doc -mindepth 1 -type d -empty -delete
rm -rf /usr/share/man/* /usr/share/info/* /usr/share/lintian/* /usr/share/groff/*
find /usr/share/locale -mindepth 1 -maxdepth 1 ! -name 'en' ! -name 'en_US' ! -name 'locale.alias' -exec rm -rf {} +
rm -rf /var/lib/apt/lists/* /var/cache/apt/*.bin /var/cache/debconf/*-old
rm -rf /var/log/journal/* /var/tmp/* /tmp/* /root/.cache
find /var/log -type f -exec truncate -s 0 {} +
echo "largest remaining packages (MiB):"
dpkg-query -Wf '${Installed-Size}\t${Package}\n' | sort -rn | head -15 | awk '{printf "  %6.1f %s\n", $1/1024, $2}'
echo "=== customize: done"
