#!/usr/bin/env bash
#
# ogmfx-update.sh — OGMFX on-device updater (Phase 6, workstream W4)
#
# Implements the update flow v1 from docs/Phase6Plan.md:
#
#   1. Query GitHub Releases for the newest release on the configured channel
#      (stable = /releases/latest; beta = newest non-draft prerelease).
#   2. Skip when releases/<version> is already installed or is `current`.
#   3. Download the tarball + .sha256 sidecar and verify the checksum.
#   4. Extract to /opt/ogmfx/releases/<version>/ and run its install.sh.
#   5. Point `previous` at the old release, swap `current` atomically
#      (prepare a symlink, then mv -T).
#   6. Restart ogmfx.service; health check = unit active after health_delay_s
#      and stays active through health_grace_s. On failure: repoint `current`
#      to `previous`, restart, and record the bad version.
#   7. Prune releases/, keeping the keep_releases newest (default 2).
#
# Manual rollback:  ogmfx-update.sh --rollback
#
# Dependencies: bash >= 4.1, curl, tar, sha256sum, flock, systemctl.
# No jq: latest.json and the GitHub Releases API payloads are parsed with
# sed/awk (they are flat key/value JSON — see json_field / parse_releases).
#
# Configuration: /etc/ogmfx/updater.conf (see updater/updater.conf template).
#
# Install location on device: /usr/local/sbin/ogmfx-update.sh
# (referenced by ogmfx-updater.service; installed by the image / install.sh).
#
# ---- Test hooks (env overrides; used by updater/test/run-tests.sh) ----
#   OGMFX_CONF        path to updater.conf         (default /etc/ogmfx/updater.conf)
#   OGMFX_SYSTEMCTL   systemctl binary or stub     (default systemctl)
#   OGMFX_UNAME_M     override `uname -m` for the tarball arch check
#
set -euo pipefail

readonly PROG="ogmfx-update"

usage() {
    cat <<EOF
Usage: $PROG [--check|--force|--rollback|--help]

  (no args) / --check   Run one update check, honouring check_interval throttling.
  --force               Run an update check ignoring the check_interval throttle.
  --rollback            Point 'current' back at 'previous' and restart the service.
  --help                This text.
EOF
}

log() {
    local line
    line="$(date -u '+%Y-%m-%dT%H:%M:%SZ') $PROG: $*"
    printf '%s\n' "$line" >&2
    # Best-effort file copy so a minimal image without persistent journald
    # still keeps an update history.
    if [[ -n ${state_dir:-} && -d ${state_dir:-} ]]; then
        printf '%s\n' "$line" >> "$state_dir/update.log" 2>/dev/null || true
    fi
}

die() {
    log "ERROR: $*"
    exit 1
}

# ---------------------------------------------------------------------------
# Args
# ---------------------------------------------------------------------------
mode="check"
while (($#)); do
    case "$1" in
        --check)    mode="check" ;;
        --force)    mode="force" ;;
        --rollback) mode="rollback" ;;
        --help|-h)  usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
    shift
done

# ---------------------------------------------------------------------------
# Config (defaults here may be overridden by updater.conf)
# ---------------------------------------------------------------------------
repo=""                          # owner/name on GitHub
channel="stable"                 # stable | beta
api_base=""                      # default: https://api.github.com/repos/$repo
github_token=""                  # optional; raises API rate limits
check_interval="6h"              # throttle between checks: Ns Nm Nh Nd Nw
install_root="/opt/ogmfx"
state_dir="/var/lib/ogmfx/state"
service_name="ogmfx.service"
keep_releases=2                  # contract: keep the two newest releases
health_delay_s=10                # wait after restart before first is-active probe
health_grace_s=30                # unit must stay active through this window
connect_timeout_s=15
max_time_s=600

CONF_FILE="${OGMFX_CONF:-/etc/ogmfx/updater.conf}"
SYSTEMCTL="${OGMFX_SYSTEMCTL:-systemctl}"

[[ -r "$CONF_FILE" ]] || die "config not found: $CONF_FILE"
# shellcheck disable=SC1090
. "$CONF_FILE"

[[ -n "$repo" ]] || die "repo is not set in $CONF_FILE"
[[ "$channel" == "stable" || "$channel" == "beta" ]] \
    || die "channel must be 'stable' or 'beta' in $CONF_FILE"
[[ "$keep_releases" =~ ^[0-9]+$ && "$keep_releases" -ge 1 ]] \
    || die "keep_releases must be a positive integer"
[[ "$health_delay_s" =~ ^[0-9]+$ && "$health_grace_s" =~ ^[0-9]+$ ]] \
    || die "health_delay_s/health_grace_s must be non-negative integers"

api_base="${api_base:-https://api.github.com/repos/$repo}"
api_base="${api_base%/}"

releases_dir="$install_root/releases"
current_link="$install_root/current"
previous_link="$install_root/previous"
staging_dir="$install_root/.staging"
bad_versions_file="$state_dir/bad-versions"
last_check_file="$state_dir/last-check"
work=""                            # scratch dir; cleaned by the EXIT trap

mkdir -p "$releases_dir" "$state_dir" "$staging_dir"

# Serialize concurrent invocations (manual + timer).
exec {LOCK_FD}>"$state_dir/.update.lock"
if ! flock -n "$LOCK_FD"; then
    log "another $PROG instance is running; exiting"
    exit 0
fi

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# Extract the string value of a flat JSON key:  "key": "value"
json_field() {  # json_field <file> <key>
    sed -n "s/.*\"$2\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p" "$1" | head -n 1
}

# Extract the first browser_download_url ending in /<name>.
asset_url() {  # asset_url <release.json> <asset-name>
    sed -n "s/.*\"browser_download_url\"[[:space:]]*:[[:space:]]*\"\([^\"]*\/$2\)\".*/\1/p" "$1" \
        | head -n 1
}

# From a `GET /releases` array, find the NEWEST non-draft prerelease and print
# "tag<TAB>latest.json-asset-url". The API returns releases newest-first;
# within one release object, tag_name/draft/prerelease precede the assets list,
# so a single forward pass is enough.
find_beta_release() {  # find_beta_release <releases.json>
    awk '
        /"tag_name"[[:space:]]*:/ {
            line = $0
            sub(/.*"tag_name"[[:space:]]*:[[:space:]]*"/, "", line)
            sub(/".*/, "", line)
            tag = line; is_draft = 1; want = 0
            next
        }
        /"draft"[[:space:]]*:/ {
            is_draft = ($0 ~ /"draft"[[:space:]]*:[[:space:]]*false/) ? 0 : 1
            next
        }
        /"prerelease"[[:space:]]*:/ {
            want = (is_draft == 0 && $0 ~ /"prerelease"[[:space:]]*:[[:space:]]*true/) ? 1 : 0
            next
        }
        want == 1 && /"browser_download_url"[[:space:]]*:/ && $0 ~ /latest\.json/ {
            line = $0
            sub(/.*"browser_download_url"[[:space:]]*:[[:space:]]*"/, "", line)
            sub(/".*/, "", line)
            print tag "\t" line
            exit
        }
    ' "$1"
}

gh_api() {  # gh_api <url> <out-file>
    local -a hdr=(-fsSL
        --connect-timeout "$connect_timeout_s" --max-time 120 --retry 3
        -H "Accept: application/vnd.github+json"
        -H "User-Agent: ogmfx-updater")
    if [[ -n "$github_token" ]]; then
        hdr+=(-H "Authorization: Bearer $github_token")
    fi
    curl "${hdr[@]}" -o "$2" "$1"
}

download() {  # download <url> <out-file>
    curl -fL --connect-timeout "$connect_timeout_s" --max-time "$max_time_s" \
        --retry 3 -o "$2" "$1"
}

parse_interval() {  # parse_interval "6h" -> seconds
    local v="$1" n mult=1
    case "$v" in
        *s) n=${v%s}; mult=1 ;;
        *m) n=${v%m}; mult=60 ;;
        *h) n=${v%h}; mult=3600 ;;
        *d) n=${v%d}; mult=86400 ;;
        *w) n=${v%w}; mult=604800 ;;
        *)  n=$v;    mult=1 ;;
    esac
    [[ "$n" =~ ^[0-9]+$ ]] || die "bad check_interval: $v"
    echo $((n * mult))
}

# current_version: version dir name that `current` resolves to (empty if none)
link_target_ver() {  # link_target_ver <symlink>
    if [[ -L "$1" ]]; then
        basename "$(readlink -f "$1")"
    fi
}

# swap_current <new-ver>: point `previous` at the old release (if any) and
# repoint `current` at <new-ver> — each swap is a prepared symlink + mv -T.
# Prints the old current version (possibly empty) on stdout.
swap_current() {
    local new_ver="$1" old_ver=""
    old_ver="$(link_target_ver "$current_link")"

    local tmp_cur tmp_prev
    tmp_cur="$(mktemp -u "$install_root/.current.XXXXXX")"
    ln -s "releases/$new_ver" "$tmp_cur"

    if [[ -n "$old_ver" && "$old_ver" != "$new_ver" ]]; then
        tmp_prev="$(mktemp -u "$install_root/.previous.XXXXXX")"
        ln -s "releases/$old_ver" "$tmp_prev"
        mv -T "$tmp_prev" "$previous_link"
    fi
    mv -T "$tmp_cur" "$current_link"
    printf '%s' "$old_ver"
}

sc() { "$SYSTEMCTL" "$@"; }

# health_check: active after health_delay_s, still active through health_grace_s.
health_check() {
    sleep "$health_delay_s"
    sc is-active --quiet "$service_name" || return 1
    local remaining="$health_grace_s"
    while (( remaining > 0 )); do
        sleep 2
        sc is-active --quiet "$service_name" || return 1
        remaining=$((remaining - 2))
    done
    return 0
}

record_bad_version() {  # record_bad_version <ver>
    printf '%s\n' "$1" >> "$bad_versions_file"
    log "recorded bad version $1 in $bad_versions_file"
}

# roll_back_to_previous <bad-ver>: put `previous`'s target back on `current`.
# `previous` is then pointed at the failed release so --rollback can roll
# forward again if an operator wants to retry it.
roll_back_to_previous() {
    local failed_ver="$1"
    local prev_ver
    prev_ver="$(link_target_ver "$previous_link")"

    if [[ -z "$prev_ver" || ! -d "$releases_dir/$prev_ver" ]]; then
        # Nothing to roll back to (e.g. the very first install failed).
        rm -f "$current_link"
        sc stop "$service_name" 2>/dev/null || true
        log "no previous release to roll back to; 'current' removed, service stopped"
        return 1
    fi

    local tmp_cur tmp_prev
    tmp_cur="$(mktemp -u "$install_root/.current.XXXXXX")"
    ln -s "releases/$prev_ver" "$tmp_cur"
    mv -T "$tmp_cur" "$current_link"
    log "rolled back: current -> releases/$prev_ver"

    tmp_prev="$(mktemp -u "$install_root/.previous.XXXXXX")"
    ln -s "releases/$failed_ver" "$tmp_prev"
    mv -T "$tmp_prev" "$previous_link"

    sc restart "$service_name" || true
    if health_check; then
        log "rollback healthy: $service_name is active on $prev_ver"
        return 0
    fi
    log "ERROR: rollback restart did not pass the health check either"
    return 1
}

# prune: keep the keep_releases newest release dirs; never delete the targets
# of `current` or `previous` (they are protected even if not among the newest).
prune() {
    local -A protect=()
    local t
    if [[ -L "$current_link" ]]; then
        t="$(readlink -f "$current_link")"
        [[ -n "$t" ]] && protect["$t"]=1
    fi
    if [[ -L "$previous_link" ]]; then
        t="$(readlink -f "$previous_link")"
        [[ -n "$t" ]] && protect["$t"]=1
    fi

    local -a by_mtime=()
    local d abs
    for d in "$releases_dir"/*/; do
        [[ -d "$d" && ! -L "${d%/}" ]] || continue
        by_mtime+=("$(stat -c '%Y %n' "${d%/}")")
    done
    ((${#by_mtime[@]} <= keep_releases)) && return 0

    local kept=0 line path
    while IFS= read -r line; do
        path="${line#* }"
        if (( kept < keep_releases )); then
            kept=$((kept + 1))
            continue
        fi
        abs="$(readlink -f "$path")"
        if [[ -n ${protect["$abs"]:-} ]]; then
            log "prune: keeping $path (pointed at by current/previous)"
            continue
        fi
        log "prune: removing $path"
        rm -rf -- "$path"
    done < <(printf '%s\n' "${by_mtime[@]}" | sort -rn)
}

valid_version() {  # guard against path traversal / garbage from the index
    [[ "$1" =~ ^[A-Za-z0-9][A-Za-z0-9._+-]*$ ]]
}

# ---------------------------------------------------------------------------
# Manual rollback
# ---------------------------------------------------------------------------
do_rollback() {
    local prev_ver cur_ver
    prev_ver="$(link_target_ver "$previous_link")"
    [[ -n "$prev_ver" ]] || die "no previous release recorded; nothing to roll back to"
    [[ -d "$releases_dir/$prev_ver" ]] \
        || die "previous release dir missing: $releases_dir/$prev_ver"
    cur_ver="$(link_target_ver "$current_link")"

    log "manual rollback: current $cur_ver -> $prev_ver"
    swap_current "$prev_ver" >/dev/null

    sc restart "$service_name" || true
    if health_check; then
        log "rollback complete: $service_name active on $prev_ver"
        return 0
    fi
    log "ERROR: $service_name failed health check after rollback to $prev_ver"
    # Try to restore the pre-rollback state so we don't leave the box worse off.
    if [[ -n "$cur_ver" && -d "$releases_dir/$cur_ver" ]]; then
        log "restoring previous state: current -> $cur_ver"
        swap_current "$cur_ver" >/dev/null
        sc restart "$service_name" || true
    fi
    return 1
}

# ---------------------------------------------------------------------------
# Update check
# ---------------------------------------------------------------------------
do_update() {
    # Throttle: the .timer fires often; updater.conf's check_interval decides
    # how often we actually hit the network.
    if [[ "$mode" == "check" && -f "$last_check_file" ]]; then
        local interval last now
        interval="$(parse_interval "$check_interval")" || die "bad check_interval"
        last="$(stat -c %Y "$last_check_file")"
        now="$(date +%s)"
        if (( now - last < interval )); then
            exit 0
        fi
    fi
    touch "$last_check_file"

    work="$(mktemp -d "$staging_dir/work.XXXXXX")" || die "mktemp failed"
    trap 'rm -rf "$work"' EXIT

    # -- 1. Query releases for the configured channel -------------------------
    local tag="" latest_json_url=""
    if [[ "$channel" == "stable" ]]; then
        gh_api "$api_base/releases/latest" "$work/release.json" \
            || die "failed to query $api_base/releases/latest"
        tag="$(json_field "$work/release.json" tag_name)"
        latest_json_url="$(asset_url "$work/release.json" 'latest\.json')"
    else
        gh_api "$api_base/releases?per_page=30" "$work/releases.json" \
            || die "failed to query $api_base/releases"
        local out
        out="$(find_beta_release "$work/releases.json")" || die "release list parse failed"
        [[ -n "$out" ]] || die "no non-draft prerelease with a latest.json asset found"
        tag="${out%%$'\t'*}"
        latest_json_url="${out##*$'\t'}"
    fi
    [[ -n "$latest_json_url" ]] || die "release $tag has no latest.json asset"

    log "channel=$channel latest release tag: ${tag:-unknown}"

    download "$latest_json_url" "$work/latest.json" \
        || die "failed to download $latest_json_url"

    local version tarball_url expected_sha
    version="$(json_field "$work/latest.json" version)"
    [[ -z "$version" ]] && version="$tag"      # tolerate missing field; tag is vX.Y.Z
    tarball_url="$(json_field "$work/latest.json" tarball_url)"
    expected_sha="$(json_field "$work/latest.json" sha256)"

    [[ -n "$version" ]] || die "could not determine release version"
    valid_version "$version" || die "refusing unsafe version string: $version"
    [[ -n "$tarball_url" ]] || die "latest.json for $version has no tarball_url"

    # Arch sanity: contract names tarballs ogmfx-<version>-<arch>.tar.gz.
    local arch fname
    arch="${OGMFX_UNAME_M:-$(uname -m)}"
    fname="${tarball_url##*/}"
    [[ "$fname" == *"-${arch}.tar.gz" ]] \
        || die "tarball '$fname' does not match this machine's arch '$arch'"

    # -- 2. Skip if already installed / already current -----------------------
    local cur_ver
    cur_ver="$(link_target_ver "$current_link")"
    if [[ "$cur_ver" == "$version" ]]; then
        log "$version is already current; nothing to do"
        exit 0
    fi
    if [[ -d "$releases_dir/$version" ]]; then
        # Covers a previously-failed release dir kept for forensics — do not
        # reinstall in a loop. Remove the dir manually to force a retry.
        log "$version is already installed (releases/$version exists); skipping"
        exit 0
    fi

    # -- 3. Download tarball + .sha256, verify --------------------------------
    log "downloading $tarball_url"
    download "$tarball_url" "$work/$fname" || die "tarball download failed"
    download "${tarball_url}.sha256" "$work/$fname.sha256" \
        || die "checksum sidecar download failed"

    local sidecar_sha actual_sha
    sidecar_sha="$(awk '{print $1; exit}' "$work/$fname.sha256")"
    actual_sha="$(sha256sum "$work/$fname" | awk '{print $1}')"
    [[ "$sidecar_sha" == "$actual_sha" ]] \
        || die "sha256 mismatch for $fname (expected $sidecar_sha, got $actual_sha)"
    if [[ -n "$expected_sha" && "$expected_sha" != "$actual_sha" ]]; then
        die "latest.json sha256 disagrees with .sha256 sidecar for $fname"
    fi
    log "checksum verified: $actual_sha"

    # -- 4. Extract to releases/<v>/ and run install.sh ------------------------
    local ex root
    ex="$(mktemp -d "$staging_dir/extract.XXXXXX")"
    tar -xzf "$work/$fname" -C "$ex" --no-same-owner || die "tar extract failed"
    if [[ -f "$ex/install.sh" ]]; then
        root="$ex"
    else
        # Package wrapped in a single top-level directory (e.g. ogmfx-v1-aarch64/).
        local -a subs=("$ex"/*/)
        if [[ ${#subs[@]} -eq 1 && -f "${subs[0]%/}/install.sh" ]]; then
            root="${subs[0]%/}"
        else
            die "package has no install.sh at its root"
        fi
    fi
    [[ -f "$root/VERSION" ]] || log "warning: package has no VERSION file"

    mv -T "$root" "$releases_dir/$version"
    [[ "$root" == "$ex" ]] || rm -rf "$ex"
    log "extracted to $releases_dir/$version"

    ( cd "$releases_dir/$version" && \
        OGMFX_INSTALL_ROOT="$install_root" OGMFX_STATE_DIR="$state_dir" \
        bash ./install.sh ) \
        || die "install.sh failed for $version (previous release still active)"
    log "install.sh completed for $version"

    # -- 5. previous <- old release; current <- new release (atomic) ----------
    local old_ver
    old_ver="$(swap_current "$version")"
    log "symlinks: current -> releases/$version${old_ver:+, previous -> releases/$old_ver}"

    # -- 6. Restart + health check, roll back on failure -----------------------
    sc restart "$service_name" || true
    if ! health_check; then
        log "health check FAILED for $version; rolling back"
        record_bad_version "$version"
        roll_back_to_previous "$version" || true
        exit 1
    fi
    log "health check passed; $version is live"

    # -- 7. Prune to the newest keep_releases ----------------------------------
    prune
    log "update to $version complete"
}

case "$mode" in
    check|force) do_update ;;
    rollback)    do_rollback ;;
esac
