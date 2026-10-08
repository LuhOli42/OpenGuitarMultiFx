#!/usr/bin/env bash
# Build the minimal OGMFX image for the Radxa Cubie A7S (Allwinner A733) from the
# official Radxa CLI image. Keeps the vendor BSP kernel/bootloader untouched
# (ARCHITECTURE.md section B); see image/README.md and docs/Phase6Plan.md (W5).
#
#   sudo image/build-image.sh [--base-xz FILE] [--out DIR] [--work DIR]
#                             [--grow-mib N] [--headroom-mib N] [--keep-work]
#   sudo image/build-image.sh --verify FILE.img[.xz]
#
# Pipeline: fetch + SHA-512 check -> decompress -> grow rootfs -> loop-mount
# -> copy image/overlay/ -> chroot (qemu-aarch64 binfmt) image/customize.sh
# -> shrink rootfs -> zero free blocks -> xz -> --verify the result.
# Every step fails loudly; mounts/loop devices are always cleaned up.
#
# Host requirements (Debian/Ubuntu x86_64):
#   apt install qemu-user-static binfmt-support curl xz-utils e2fsprogs \
#               cloud-guest-utils gdisk fdisk zerofree rsync systemd
set -euo pipefail

# --- Pinned base image -------------------------------------------------------
# Validated 2026-10-08 from https://docs.radxa.com/en/cubie/a7s (Downloads ->
# "radxa-a733" rsdk-r6 release): Debian 11 bullseye CLI, vendor kernel
# 5.15.147-21-a733, u-boot 2018.07-17, RSDK build date 2026-04-30.
# Bump all three together; see the re-basing procedure in image/customize.sh.
BASE_RELEASE="rsdk-r6"
BASE_URL="https://github.com/radxa-build/radxa-a733/releases/download/rsdk-r6/radxa-a733_bullseye_cli_r6.output_512.img.xz"
BASE_SHA512="154e4bf5baec5901c1cb6a6e20fb448cedfa7b4b232f3fe34e8ca1a0e861feeba1ab7b1360f5d14637690bb65449d760cc65b66ee2ef015b5138f3a6eac3ad8b"
# Partition layout of the pinned base (GPT): p1 "config" vfat (rsetup
# first-boot files), p2 "efi" vfat, p3 rootfs ext4 (last partition; grown here,
# re-grown to the card size by rsetup resize_root on first boot).
ROOT_PART=3
CONFIG_PART=1
EFI_PART=2

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$REPO_DIR/build/image"
WORK_DIR=""
BASE_XZ=""
GROW_MIB=1024
HEADROOM_MIB=256
KEEP_WORK=0
VERIFY_ONLY=""
ORIG_ARGS=("$@")

log() { echo "build-image: $*"; }
die() { echo "build-image: ERROR: $*" >&2; exit 1; }
step() { echo; echo "##### build-image: $*"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --base-xz)      BASE_XZ="${2:?}"; shift ;;
        --out)          OUT_DIR="${2:?}"; shift ;;
        --work)         WORK_DIR="${2:?}"; shift ;;
        --grow-mib)     GROW_MIB="${2:?}"; shift ;;
        --headroom-mib) HEADROOM_MIB="${2:?}"; shift ;;
        --keep-work)    KEEP_WORK=1 ;;
        --verify)       VERIFY_ONLY="${2:?}"; shift ;;
        -h|--help)      sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; exit 0 ;;
        *) die "unknown option: $1 (see --help)" ;;
    esac
    shift
done

[[ $EUID -eq 0 ]] || die "needs root for losetup/mount/chroot: sudo $0 $*"
# Private mount namespace: none of our mounts can leak into the namespaces of
# services started meanwhile (they would pin the loop device), and the kernel
# drops every mount if we die.
if [[ -z "${OGMFX_IN_MNTNS:-}" ]] && command -v unshare >/dev/null; then
    exec unshare --mount --propagation private env OGMFX_IN_MNTNS=1 "$0" "${ORIG_ARGS[@]}"
fi

# --- cleanup ----------------------------------------------------------------
MNT=""
LOOP=""
cleanup() {
    local rc=$?
    set +e
    if [[ -n "$MNT" ]] && mountpoint -q "$MNT"; then
        sync
        umount -R "$MNT" 2>/dev/null || umount -Rl "$MNT"
    fi
    [[ -n "$MNT" ]] && rmdir "$MNT" 2>/dev/null
    [[ -n "$LOOP" ]] && release_loop "$LOOP"
    if (( rc != 0 )); then
        echo "build-image: FAILED (exit $rc) -- mounts and loop devices released" >&2
    fi
    exit "$rc"
}
trap cleanup EXIT INT TERM

attach() {   # attach <img> -> sets LOOP
    LOOP="$(losetup -fP --show "$1")"
    udevadm settle 2>/dev/null || true
    [[ -b "${LOOP}p$ROOT_PART" ]] || die "no partition $ROOT_PART on $1"
}
# losetup -d on a device that udev is still probing only sets autoclear, and
# that sometimes never fires -- settle first and retry until it is really gone.
release_loop() {
    local dev="$1"
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        sync
        udevadm settle 2>/dev/null || true
        losetup -d "$dev" 2>/dev/null || true
        losetup -l -n -O NAME | grep -qx "$dev" || return 0
        sleep 1
    done
    echo "build-image: WARNING: could not release $dev (sudo losetup -d $dev)" >&2
}
detach() { release_loop "$LOOP"; LOOP=""; }

mount_image() {   # mount_image <ro|rw>
    MNT="$(mktemp -d /tmp/ogmfx-root.XXXXXX)"
    mount -o "$1" "${LOOP}p$ROOT_PART" "$MNT"
    mount -o "$1" "${LOOP}p$CONFIG_PART" "$MNT/config"
    mount -o "$1" "${LOOP}p$EFI_PART" "$MNT/boot/efi"
}
umount_image() { sync; umount -R "$MNT"; rmdir "$MNT"; MNT=""; }

# --- verify -----------------------------------------------------------------
verify_units() {
    local out rc=0
    mount --bind /proc "$MNT/proc"
    mount --rbind /sys "$MNT/sys"; mount --make-rslave "$MNT/sys"
    mount --bind /dev "$MNT/dev"
    mount -t tmpfs tmpfs "$MNT/run"
    mount -t tmpfs tmpfs "$MNT/tmp"
    out="$(chroot "$MNT" /usr/bin/env SYSTEMD_COLORS=0 systemd-analyze verify /etc/systemd/system/ogmfx.service \
            /etc/systemd/system/ogmfx.target /etc/systemd/system/ogmfx-updater.service \
            /etc/systemd/system/ogmfx-updater.timer 2>&1)" || rc=$?
    umount "$MNT/tmp" "$MNT/run" "$MNT/dev" "$MNT/proc"; umount -R "$MNT/sys"
    # ogmfx-update.sh ships with W4 (updater/); its absence is expected here.
    out="$(grep -v 'ogmfx-update.sh is not executable' <<<"$out" || true)"
    if [[ -n "$out" ]]; then
        printf '       %s\n' "${out//$'\n'/$'\n'       }"
        return 1
    fi
    (( rc == 0 )) || echo "       (only the expected W4 ogmfx-update.sh is missing)"
}

# Checks a produced image without booting it: partitions mount, the vendor
# kernel is intact, the overlay landed, the user/limits/units are in place and
# systemd-analyze verify accepts the OGMFX units against the image's own root.
verify_image() {
    local img="$1" tmp="" f rel fail=0
    step "verify $img"
    if [[ "$img" == *.xz ]]; then
        tmp="$(mktemp -p "$(dirname "$img")" verify.XXXXXX.img)"
        xz -dc "$img" > "$tmp"
        img="$tmp"
    fi
    sgdisk -v "$img" | grep -q 'No problems found' || { sgdisk -v "$img"; die "GPT not clean"; }
    attach "$img"
    mount_image ro
    check() { if eval "$2"; then echo "  ok   $1"; else echo "  FAIL $1"; fail=1; fi; }
    check "vendor kernel present"       "compgen -G '$MNT/boot/vmlinuz-*-a733' >/dev/null"
    check "extlinux boots vendor kernel" "grep -q 'vmlinuz-.*-a733' '$MNT/boot/extlinux/extlinux.conf'"
    check "root UUID matches extlinux"  "grep -q \"root=UUID=\$(blkid -o value -s UUID ${LOOP}p$ROOT_PART)\" '$MNT/boot/extlinux/extlinux.conf'"
    while IFS= read -r -d '' f; do
        rel="${f#"$SCRIPT_DIR/overlay"}"
        check "overlay $rel" "cmp -s '$f' '$MNT$rel'"
    done < <(find "$SCRIPT_DIR/overlay" -type f -print0)
    check "ogmfx user in audio group"   "grep -qE '^audio:.*\bogmfx\b' '$MNT/etc/group'"
    check "rtprio/memlock limits"       "grep -q 'rtprio' '$MNT/etc/security/limits.d/95-ogmfx-audio.conf'"
    check "performance governor tmpfile" "grep -q performance '$MNT/etc/tmpfiles.d/ogmfx-cpufreq.conf'"
    check "ogmfx.service enabled"       "[[ -L '$MNT/etc/systemd/system/ogmfx.target.wants/ogmfx.service' ]]"
    check "ogmfx-updater.timer enabled" "[[ -L '$MNT/etc/systemd/system/timers.target.wants/ogmfx-updater.timer' ]]"
    check "default target ogmfx.target" "[[ \$(readlink '$MNT/etc/systemd/system/default.target') == */ogmfx.target ]]"
    check "vendor Xorg diversion"       "grep -q 'usr/bin/Xorg' '$MNT/var/lib/dpkg/diversions'"
    check "no pulseaudio"               "[[ ! -e '$MNT/usr/bin/pulseaudio' ]]"
    check "no man pages"                "[[ -z \$(ls -A '$MNT/usr/share/man') ]]"
    check "install layout /opt/ogmfx/releases" "[[ -d '$MNT/opt/ogmfx/releases' && -d '$MNT/var/lib/ogmfx' ]]"
    # The image's own systemd (247) checks the units against the image's root,
    # so ExecStart= paths and Wants=/WantedBy= targets resolve as on the board.
    check "systemd-analyze verify (in image)" verify_units
    du -sh "$MNT" 2>/dev/null | sed 's/^/  rootfs used: /'
    umount_image
    detach
    [[ -n "$tmp" ]] && rm -f "$tmp"
    (( fail == 0 )) || die "verification failed"
    log "verify OK"
}

if [[ -n "$VERIFY_ONLY" ]]; then
    verify_image "$(readlink -f "$VERIFY_ONLY")"
    exit 0
fi

# --- host checks ------------------------------------------------------------
step "host checks"
missing=()
for t in curl xz sha512sum losetup sgdisk sfdisk growpart e2fsck resize2fs \
         dumpe2fs zerofree rsync chroot blkid udevadm; do
    command -v "$t" >/dev/null || missing+=("$t")
done
(( ${#missing[@]} == 0 )) || die "missing host tools: ${missing[*]} (see header for the apt line)"
if [[ "$(uname -m)" != aarch64 ]]; then
    if ! grep -qs '^enabled' /proc/sys/fs/binfmt_misc/qemu-aarch64; then
        die "qemu-aarch64 binfmt not registered (apt install qemu-user-static binfmt-support)"
    fi
    grep -q '^flags:.*F' /proc/sys/fs/binfmt_misc/qemu-aarch64 \
        || die "qemu-aarch64 binfmt lacks the F (fix-binary) flag; use the qemu-user-static package's registration"
fi

GIT_REV="$(git -C "$REPO_DIR" describe --always --dirty 2>/dev/null || echo unknown)"
STAMP="$(date -u +%Y%m%d)"
OUT_NAME="ogmfx-cubie-a7s-${STAMP}-${GIT_REV}"
mkdir -p "$OUT_DIR"
[[ -n "$WORK_DIR" ]] || WORK_DIR="$OUT_DIR/work"
mkdir -p "$WORK_DIR"
IMG="$WORK_DIR/$OUT_NAME.img"

# --- fetch ------------------------------------------------------------------
step "fetch base image ($BASE_RELEASE)"
if [[ -z "$BASE_XZ" ]]; then
    BASE_XZ="$OUT_DIR/cache/$(basename "$BASE_URL")"
    mkdir -p "$(dirname "$BASE_XZ")"
    if [[ ! -f "$BASE_XZ" ]]; then
        log "downloading $BASE_URL"
        curl -fL --retry 3 --connect-timeout 30 -o "$BASE_XZ.part" "$BASE_URL" \
            || die "download failed: $BASE_URL (offline? pass --base-xz FILE)"
        mv "$BASE_XZ.part" "$BASE_XZ"
    fi
fi
[[ -f "$BASE_XZ" ]] || die "base image not found: $BASE_XZ"
log "checking SHA-512 of $BASE_XZ"
echo "$BASE_SHA512  $BASE_XZ" | sha512sum -c --quiet - \
    || die "SHA-512 mismatch for $BASE_XZ -- corrupted download or not the pinned $BASE_RELEASE image"

# --- decompress + grow ------------------------------------------------------
step "decompress + grow rootfs by ${GROW_MIB} MiB"
xz -dcT0 "$BASE_XZ" > "$IMG"
truncate -s "+${GROW_MIB}M" "$IMG"
sgdisk -e "$IMG" >/dev/null                      # move backup GPT to the new end
[[ "$(sfdisk -d "$IMG" | grep -c '^/')" -eq "$ROOT_PART" ]] \
    || die "unexpected partition count -- base layout changed, update *_PART"
growpart "$IMG" "$ROOT_PART"
attach "$IMG"
e2fsck -pf "${LOOP}p$ROOT_PART"
resize2fs "${LOOP}p$ROOT_PART"

# --- overlay + chroot -------------------------------------------------------
step "mount + overlay"
mount_image rw
for d in dev dev/pts proc sys; do mount --bind "/$d" "$MNT/$d"; done
mount -t tmpfs tmpfs "$MNT/run"
# Overlay: rootfs files root-owned; overlay/config goes to the FAT config
# partition (rsetup reads /config/before.txt on first boot).
rsync -rlt --chown=root:root --chmod=D0755,F0644 --exclude=/config/ "$SCRIPT_DIR/overlay/" "$MNT/"
find "$SCRIPT_DIR/overlay/usr/local/bin" -type f -printf '%P\n' | while read -r f; do
    chmod 0755 "$MNT/usr/local/bin/$f"
done
cp -r --no-preserve=ownership,mode "$SCRIPT_DIR/overlay/config/." "$MNT/config/"
BUILD_IN_ROOT="$MNT/root/ogmfx-build"
install -d "$BUILD_IN_ROOT"
install -m 0755 "$SCRIPT_DIR/customize.sh" "$REPO_DIR/scripts/setup-target.sh" "$BUILD_IN_ROOT/"
# The base ships without /etc/resolv.conf (NetworkManager creates it at boot);
# provide the host's for apt and remove it again afterwards.
resolv_state=absent
if [[ -e "$MNT/etc/resolv.conf" || -L "$MNT/etc/resolv.conf" ]]; then
    mv "$MNT/etc/resolv.conf" "$MNT/etc/resolv.conf.ogmfx-orig"; resolv_state=moved
fi
cat /etc/resolv.conf > "$MNT/etc/resolv.conf"

step "chroot: customize.sh"
chroot "$MNT" /usr/bin/env -i HOME=/root PATH=/usr/sbin:/usr/bin:/sbin:/bin \
    LANG=C.UTF-8 OGMFX_IMAGE_GIT="$GIT_REV" OGMFX_IMAGE_BASE="$BASE_RELEASE $(basename "$BASE_URL")" \
    /root/ogmfx-build/customize.sh

rm -rf "$BUILD_IN_ROOT"
rm -f "$MNT/etc/resolv.conf"
[[ "$resolv_state" == moved ]] && mv "$MNT/etc/resolv.conf.ogmfx-orig" "$MNT/etc/resolv.conf"
umount_image

# --- shrink -----------------------------------------------------------------
step "shrink rootfs (+${HEADROOM_MIB} MiB headroom)"
part="${LOOP}p$ROOT_PART"
e2fsck -pf "$part"
resize2fs -M "$part"
bs="$(dumpe2fs -h "$part" 2>/dev/null | awk -F: '/^Block size/ {gsub(/ /,"",$2); print $2}')"
blocks="$(dumpe2fs -h "$part" 2>/dev/null | awk -F: '/^Block count/ {gsub(/ /,"",$2); print $2}')"
fs_bytes=$(( blocks * bs + HEADROOM_MIB * 1024 * 1024 ))
fs_bytes=$(( (fs_bytes + 4194303) / 4194304 * 4194304 ))     # 4 MiB aligned
resize2fs "$part" "$(( fs_bytes / 1024 ))K"
log "zeroing free blocks"
zerofree "$part"
detach

start="$(sfdisk -d "$IMG" | awk -v p="${ROOT_PART}" '$1 ~ "p?"p"$" || $1 ~ p"$" {sub(/,/,"",$4); print $4; exit}')"
[[ "$start" =~ ^[0-9]+$ ]] || die "could not read start of partition $ROOT_PART"
sectors=$(( fs_bytes / 512 ))
# Rewrite only the size of the root partition; UUIDs/names/attrs stay as dumped.
sfdisk -d "$IMG" | grep -v '^last-lba:' \
    | sed -E "/^[^ ]*${ROOT_PART} :/ s/size= *[0-9]+/size=${sectors}/" \
    | sfdisk --quiet --no-reread --no-tell-kernel "$IMG"
truncate -s $(( (start + sectors) * 512 + 1024 * 1024 )) "$IMG"
# The truncate cuts off the backup GPT; sgdisk -e rebuilds it (its CRC
# warnings about the old backup are expected) and -v below must come out clean.
sgdisk -e "$IMG" >/dev/null 2>&1
sgdisk -v "$IMG" | grep -q 'No problems found' || { sgdisk -v "$IMG"; die "GPT damaged after shrink"; }
log "image: $(du -h --apparent-size "$IMG" | cut -f1)"

# --- compress + verify ------------------------------------------------------
step "verify + compress"
verify_image "$IMG"
xz -T0 -6 -f -c "$IMG" > "$OUT_DIR/$OUT_NAME.img.xz"
( cd "$OUT_DIR" && sha512sum "$OUT_NAME.img.xz" > "$OUT_NAME.img.xz.sha512" )
(( KEEP_WORK )) || rm -f "$IMG"
log "done: $OUT_DIR/$OUT_NAME.img.xz ($(du -h "$OUT_DIR/$OUT_NAME.img.xz" | cut -f1))"
