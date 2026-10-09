#!/usr/bin/env bash
# Phase 6 RT/audio setup for the target board (Radxa Cubie A7S, Allwinner A733).
# See docs/Phase6Plan.md (W5) and image/README.md.
#
# One script, two consumers:
#   * image/customize.sh runs it inside the image chroot with --image --kiosk
#     (writes config + enables units, touches nothing live);
#   * a manual install on stock Radxa OS runs it directly on the board
#     (same config, plus applies governor / IRQ affinity immediately).
# Idempotent: every file it owns is rewritten wholesale, every action is a
# "make it so" check, so re-running is always safe.
#
# Usage: sudo scripts/setup-target.sh [options]
#   --image              offline mode (chroot / image build): no sysfs writes,
#                        no systemctl start/stop, no live checks
#   --kiosk              boot into ogmfx.target (ogmfx.service owns the display
#                        on vt7); disables any display manager (sddm/lightdm/gdm)
#   --runtime            only (re)apply live tuning: governor, USB-audio IRQ
#                        affinity, ALSA default card. Called by the udev rule.
#   --check              report current state, change nothing
#   --user NAME          service user (default: ogmfx)
#   --irq-cpu N|auto     CPU for USB host-controller IRQs (default: auto =
#                        highest-numbered "big" core by cpu_capacity/max freq)
#   --alsa-card ID|auto  ALSA card id for the "default" device (default: auto =
#                        first USB-Audio card present; nothing written if none)
#   --no-enable          don't enable ogmfx.service / ogmfx-updater.timer
#
# Settings chosen here are persisted in /etc/ogmfx/target.conf so --runtime
# (udev, at every USB audio hotplug) reuses them.
set -euo pipefail

OGMFX_USER="${OGMFX_USER:-ogmfx}"
IRQ_CPU="${OGMFX_IRQ_CPU:-auto}"
ALSA_CARD="${OGMFX_ALSA_CARD:-auto}"
MODE=install
IMAGE=0
KIOSK=0
ENABLE=1

INSTALL_PATH=/usr/local/sbin/ogmfx-setup-target
TARGET_CONF=/etc/ogmfx/target.conf
LIMITS_FILE=/etc/security/limits.d/95-ogmfx-audio.conf
TMPFILES_FILE=/etc/tmpfiles.d/ogmfx-cpufreq.conf
UDEV_FILE=/etc/udev/rules.d/90-ogmfx-usb-audio.rules
ASOUND_FILE=/etc/asound.conf
OGMFX_UNITS=(ogmfx.service ogmfx-updater.timer)

log()  { echo "setup-target: $*"; }
warn() { echo "setup-target: WARNING: $*" >&2; }
die()  { echo "setup-target: ERROR: $*" >&2; exit 1; }

usage() { sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --image)     IMAGE=1 ;;
        --kiosk)     KIOSK=1 ;;
        --runtime)   MODE=runtime ;;
        --check)     MODE=check ;;
        --user)      OGMFX_USER="${2:?--user needs a value}"; shift ;;
        --irq-cpu)   IRQ_CPU="${2:?--irq-cpu needs a value}"; shift ;;
        --alsa-card) ALSA_CARD="${2:?--alsa-card needs a value}"; shift ;;
        --no-enable) ENABLE=0 ;;
        -h|--help)   usage; exit 0 ;;
        *) die "unknown option: $1 (see --help)" ;;
    esac
    shift
done

[[ $EUID -eq 0 ]] || die "must run as root (sudo $0 ...)"

# Write $1 with stdin content only if it differs (keeps mtimes stable on re-runs).
write_file() {
    local path="$1" mode="${2:-0644}" tmp
    tmp="$(mktemp)"
    cat > "$tmp"
    if [[ -f "$path" ]] && cmp -s "$tmp" "$path"; then
        rm -f "$tmp"
        return 0
    fi
    install -D -m "$mode" "$tmp" "$path"
    rm -f "$tmp"
    log "wrote $path"
}

# ---------------------------------------------------------------- CPU topology
# A733 = 6x Cortex-A55 + 2x Cortex-A76. cpu_capacity is the scheduler's own view
# of big/LITTLE; cpuinfo_max_freq is the fallback on kernels that don't expose it.
big_cpus() {
    local c best=0 v list=()
    for c in /sys/devices/system/cpu/cpu[0-9]*; do
        v="$(cat "$c/cpu_capacity" 2>/dev/null || cat "$c/cpufreq/cpuinfo_max_freq" 2>/dev/null || echo 0)"
        (( v > best )) && best=$v
    done
    for c in /sys/devices/system/cpu/cpu[0-9]*; do
        v="$(cat "$c/cpu_capacity" 2>/dev/null || cat "$c/cpufreq/cpuinfo_max_freq" 2>/dev/null || echo 0)"
        (( v == best )) && list+=("${c##*cpu}")
    done
    printf '%s\n' "${list[@]}" | sort -n
}

resolve_irq_cpu() {
    if [[ "$IRQ_CPU" == auto ]]; then
        big_cpus | tail -n1
    else
        echo "$IRQ_CPU"
    fi
}

# ---------------------------------------------------------------- ALSA helpers
usb_audio_cards() {
    local d
    for d in /proc/asound/card[0-9]*; do
        [[ -e "$d/usbid" ]] && cat "$d/id"
    done
    return 0
}

resolve_alsa_card() {
    if [[ "$ALSA_CARD" == auto ]]; then
        usb_audio_cards | head -n1
    else
        echo "$ALSA_CARD"
    fi
}

# USB bus numbers that carry an audio interface ("usb3" etc.).
usb_audio_buses() {
    local d dev
    for d in /proc/asound/card[0-9]*; do
        [[ -e "$d/usbbus" ]] || continue
        # usbbus is "BBB/DDD"
        dev="$(cut -d/ -f1 "$d/usbbus")"
        echo "usb$((10#$dev))"
    done | sort -u
}

# ------------------------------------------------------------- config writers
ensure_user() {
    local g groups=()
    getent group audio >/dev/null || groupadd --system audio
    for g in audio video input render plugdev; do
        getent group "$g" >/dev/null && groups+=("$g")
    done
    if ! id "$OGMFX_USER" >/dev/null 2>&1; then
        useradd --create-home --shell /bin/bash --user-group \
            --comment "OpenGuitarMultiFx kiosk" "$OGMFX_USER"
        # No password: the account is only entered via getty autologin / the
        # PAM session of ogmfx.service, never by password.
        passwd -l "$OGMFX_USER" >/dev/null
        log "created user $OGMFX_USER"
    fi
    usermod -aG "$(IFS=,; echo "${groups[*]}")" "$OGMFX_USER"
}

ensure_layout() {
    # Install layout contract from docs/Phase6Plan.md.
    install -d -m 0755 /opt/ogmfx /opt/ogmfx/releases /etc/ogmfx
    install -d -m 0755 -o "$OGMFX_USER" -g "$OGMFX_USER" /var/lib/ogmfx
}

write_target_conf() {
    write_file "$TARGET_CONF" <<CONF
# Managed by ogmfx-setup-target -- re-run it instead of editing by hand.
OGMFX_USER=$OGMFX_USER
OGMFX_IRQ_CPU=$IRQ_CPU
OGMFX_ALSA_CARD=$ALSA_CARD
CONF
}

write_limits() {
    # PAM limits cover login sessions (autologin console, ssh, ogmfx.service's
    # PAMName=login session). ogmfx.service also sets LimitRTPRIO/LimitMEMLOCK
    # itself so it doesn't depend on PAM. rtprio 95 leaves 96-99 for kernel
    # threads (watchdog, migration) that must always win.
    write_file "$LIMITS_FILE" <<CONF
# Managed by ogmfx-setup-target. Realtime audio limits.
@audio          -  rtprio   95
@audio          -  memlock  unlimited
@audio          -  nice     -19
@$OGMFX_USER    -  rtprio   95
@$OGMFX_USER    -  memlock  unlimited
@$OGMFX_USER    -  nice     -19
CONF
}

write_governor_tmpfiles() {
    # The A733 BSP kernel defaults to "ondemand": frequency ramps cause audible
    # xruns at 64/128-sample blocks. tmpfiles 'w' accepts globs and runs early
    # at every boot, so no extra unit is needed.
    write_file "$TMPFILES_FILE" <<'CONF'
# Managed by ogmfx-setup-target: pin every cpufreq policy to performance.
w /sys/devices/system/cpu/cpufreq/policy*/scaling_governor - - - - performance
CONF
}

write_udev_rules() {
    write_file "$UDEV_FILE" <<CONF
# Managed by ogmfx-setup-target.
# USB audio class interfaces: never runtime-suspend the device (a resume costs
# milliseconds and drops samples).
ACTION=="add", SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_interface", ATTR{bInterfaceClass}=="01", RUN+="/bin/sh -c 'echo on > /sys%p/../power/control'"
# Once the ALSA card is registered: re-pin the host-controller IRQ and refresh
# the ALSA default card (see $INSTALL_PATH --runtime).
ACTION=="add", SUBSYSTEM=="sound", KERNEL=="controlC[0-9]*", SUBSYSTEMS=="usb", RUN+="$INSTALL_PATH --runtime"
CONF
}

write_asound_conf() {
    local card="$1"
    if [[ -z "$card" ]]; then
        [[ "$MODE" == runtime ]] || log "no USB audio card present; ALSA default left as-is (udev sets it on hotplug)"
        return 0
    fi
    # The app picks its device explicitly; this only makes "default" sane for
    # aplay/arecord/speaker-test and the benchmark scripts.
    write_file "$ASOUND_FILE" <<CONF
# Managed by ogmfx-setup-target: default ALSA device = the USB audio interface.
defaults.pcm.card "$card"
defaults.ctl.card "$card"
CONF
}

self_install() {
    local self
    self="$(readlink -f "$0")"
    if [[ "$self" != "$INSTALL_PATH" ]]; then
        install -D -m 0755 "$self" "$INSTALL_PATH"
        log "installed $INSTALL_PATH"
    fi
}

unit_exists() {
    [[ -f "/etc/systemd/system/$1" || -f "/lib/systemd/system/$1" || -f "/usr/lib/systemd/system/$1" ]]
}

configure_systemd() {
    if (( ! IMAGE )); then
        systemctl daemon-reload
    fi
    # irqbalance would migrate the pinned USB IRQ away again; with 8 cores and
    # a handful of busy IRQs explicit pinning is the better trade.
    if unit_exists irqbalance.service; then
        systemctl disable irqbalance.service >/dev/null 2>&1 || true
        (( IMAGE )) || systemctl stop irqbalance.service 2>/dev/null || true
    fi
    if (( KIOSK )); then
        local dm
        for dm in display-manager.service sddm.service lightdm.service gdm3.service; do
            systemctl disable "$dm" >/dev/null 2>&1 || true
        done
        if unit_exists ogmfx.target; then
            systemctl set-default ogmfx.target >/dev/null
            log "default target: ogmfx.target (ogmfx.service owns the display)"
        else
            warn "ogmfx.target not installed (image/overlay ships it); default target unchanged"
        fi
    fi
    if (( ENABLE )); then
        local u
        for u in "${OGMFX_UNITS[@]}"; do
            if unit_exists "$u"; then
                systemctl enable "$u" >/dev/null 2>&1 && log "enabled $u"
            else
                warn "$u not installed yet -- it ships with the release tarball (W3/W4); re-run after installing it"
            fi
        done
    fi
}

# --------------------------------------------------------------- live tuning
apply_governor() {
    local p
    for p in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor; do
        [[ -w "$p" ]] || continue
        [[ "$(cat "$p")" == performance ]] || echo performance > "$p"
    done
}

apply_irq_affinity() {
    local cpu bus buses pattern irq name
    cpu="$(resolve_irq_cpu)"
    [[ -n "$cpu" ]] || { warn "could not determine an IRQ CPU"; return 0; }
    buses="$(usb_audio_buses)"
    if [[ -n "$buses" ]]; then
        # Host-controller IRQ names end in ":usbN" (xhci-hcd:usb1, ehci_hcd:usb3).
        pattern="$(for bus in $buses; do printf '[:_ ]%s$|' "$bus"; done)"
        pattern="${pattern%|}"
    else
        # No interface plugged yet: pin every USB host controller.
        pattern='xhci|ehci|ohci|dwc|musb'
    fi
    while read -r irq name; do
        [[ -w "/proc/irq/$irq/smp_affinity_list" ]] || continue
        if [[ "$(cat "/proc/irq/$irq/smp_affinity_list")" != "$cpu" ]]; then
            if echo "$cpu" > "/proc/irq/$irq/smp_affinity_list" 2>/dev/null; then
                log "IRQ $irq ($name) -> CPU $cpu"
            else
                warn "kernel refused affinity for IRQ $irq ($name)"
            fi
        fi
    done < <(awk 'NR>1 && $1 ~ /^[0-9]+:$/ { sub(":","",$1); print $1, $NF }' /proc/interrupts | grep -E " ($pattern)" || true)
}

do_runtime() {
    apply_governor
    apply_irq_affinity
    write_asound_conf "$(resolve_alsa_card)"
}

# --------------------------------------------------------------------- check
do_check() {
    local p irq name cpu
    echo "== cpufreq governors"
    for p in /sys/devices/system/cpu/cpufreq/policy*; do
        [[ -e "$p/scaling_governor" ]] && echo "  ${p##*/}: $(cat "$p/scaling_governor") ($(cat "$p/related_cpus" 2>/dev/null))"
    done
    echo "== big cores: $(big_cpus | tr '\n' ' ')(IRQ target: $(resolve_irq_cpu))"
    echo "== USB audio cards: $(usb_audio_cards | tr '\n' ' ')"
    echo "== USB host-controller IRQs"
    awk 'NR>1 && $1 ~ /^[0-9]+:$/ { sub(":","",$1); print $1, $NF }' /proc/interrupts \
        | grep -E ' .*(xhci|ehci|ohci|dwc|musb|usb)' | while read -r irq name; do
            echo "  IRQ $irq $name -> CPU $(cat "/proc/irq/$irq/smp_affinity_list" 2>/dev/null)"
        done
    echo "== irqbalance: $(systemctl is-enabled irqbalance.service 2>/dev/null || echo absent)"
    echo "== user $OGMFX_USER: $(id "$OGMFX_USER" 2>/dev/null || echo missing)"
    echo "== limits file: $([[ -f $LIMITS_FILE ]] && echo present || echo missing)"
    echo "== ALSA default: $(grep -h '^defaults.pcm.card' "$ASOUND_FILE" 2>/dev/null || echo unset)"
    for p in "${OGMFX_UNITS[@]}"; do
        echo "== $p: $(systemctl is-enabled "$p" 2>/dev/null || echo not-installed)"
    done
    echo "== kernel: $(uname -r) preempt: $(uname -v | grep -oE 'PREEMPT[_A-Z]*' || echo none)"
    # Exactly the context ogmfx.service runs in. With CONFIG_RT_GROUP_SCHED=y
    # (the A733 BSP has it) SCHED_FIFO fails with EPERM inside a cgroup whose
    # cpu controller is enabled -- never give the ogmfx units CPUWeight=/CPUQuota=.
    if systemd-run --quiet --wait --pipe -p User="$OGMFX_USER" -p LimitRTPRIO=95 \
            -p LimitMEMLOCK=infinity chrt -f 80 true 2>/dev/null; then
        echo "== SCHED_FIFO 80 as $OGMFX_USER in a service cgroup: OK"
    else
        echo "== SCHED_FIFO 80 as $OGMFX_USER in a service cgroup: FAILED (check cgroup cpu controller / RT_GROUP_SCHED)"
    fi
}

# ---------------------------------------------------------------------- main
case "$MODE" in
    check)
        do_check
        ;;
    runtime)
        # Re-read the persisted choices (udev runs us without arguments).
        # shellcheck disable=SC1090
        [[ -f "$TARGET_CONF" ]] && . "$TARGET_CONF"
        IRQ_CPU="${OGMFX_IRQ_CPU:-$IRQ_CPU}"
        ALSA_CARD="${OGMFX_ALSA_CARD:-$ALSA_CARD}"
        do_runtime
        ;;
    install)
        self_install
        ensure_user
        ensure_layout
        write_target_conf
        write_limits
        write_governor_tmpfiles
        write_udev_rules
        if (( IMAGE )); then
            [[ "$ALSA_CARD" != auto ]] && write_asound_conf "$ALSA_CARD"
        else
            udevadm control --reload 2>/dev/null || true
            do_runtime
        fi
        configure_systemd
        log "done$( (( IMAGE )) && echo ' (image mode: live tuning applies at first boot)')"
        ;;
esac
