#!/usr/bin/env bash
#
# run-tests.sh — exercise ogmfx-update.sh end-to-end on x86, no hardware.
#
# Environment under test:
#   $T/opt/ogmfx           fake install root (releases/, current, previous)
#   $T/var/lib/ogmfx/state fake state dir
#   $T/etc/ogmfx/updater.conf  config with api_base -> local HTTP server
#   $T/bin/systemctl       stub: `restart` records the version `current`
#                          points at; `is-active` fails for versions listed
#                          in $T/crash_versions (i.e. the "app" only stays up
#                          if the running release isn't a known-crasher)
#   $T/www                 python3 -m http.server standing in for the GitHub
#                          Releases API (/releases/latest, /releases) plus
#                          latest.json / tarball / .sha256 download assets
#
# Scenarios: fresh install -> no-op when current -> update (health pass) ->
# manual --rollback -> update (health FAIL) -> auto-rollback -> throttle ->
# pruning -> beta channel.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SCRIPT="$REPO_ROOT/updater/ogmfx-update.sh"

T="$(mktemp -d /tmp/ogmfx-updater-test.XXXXXX)"
OPT="$T/opt/ogmfx"
STATE="$T/var/lib/ogmfx/state"
ETC="$T/etc/ogmfx"
WWW="$T/www"
BIN="$T/bin"
ARCH="$(uname -m)"
PORT=""
HTTP_PID=""

PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); printf '  PASS %s\n' "$1"; }
bad()  { FAIL=$((FAIL+1)); printf '  FAIL %s\n' "$1"; }
check(){ if eval "$2"; then ok "$1"; else bad "$1"; fi; }
say()  { printf '\n== %s ==\n' "$1"; }

cleanup() {
    [[ -n "$HTTP_PID" ]] && kill "$HTTP_PID" 2>/dev/null || true
    rm -rf "$T"
}
trap cleanup EXIT

mkdir -p "$OPT/releases" "$STATE" "$ETC" "$WWW/releases" "$BIN" "$T/pkg"

# --- stub systemctl ---------------------------------------------------------
cat > "$BIN/systemctl" <<EOF
#!/usr/bin/env bash
set -e
cmd=\${1:-}; shift || true
case "\$cmd" in
    restart|start)
        svc=\$1
        ver=""
        if [[ -L "$OPT/current" ]]; then
            ver=\$(basename "\$(readlink -f "$OPT/current")")
        fi
        printf '%s\n' "\$ver" > "$STATE/running-version"
        echo "restart \$svc -> \$ver" >> "$STATE/systemctl.log"
        ;;
    stop)
        printf 'stopped\n' > "$STATE/running-version"
        ;;
    is-active)
        if [[ "\${1:-}" == "--quiet" ]]; then shift; fi
        [[ -f "$STATE/running-version" ]] || exit 3
        ver=\$(cat "$STATE/running-version")
        [[ "\$ver" == "stopped" || -z "\$ver" ]] && exit 3
        if [[ -f "$T/crash_versions" ]] && grep -qxF "\$ver" "$T/crash_versions"; then
            exit 3
        fi
        exit 0
        ;;
    daemon-reload) exit 0 ;;
    *) echo "stub systemctl: unhandled \$cmd" >&2; exit 1 ;;
esac
EOF
chmod +x "$BIN/systemctl"

# --- config -----------------------------------------------------------------
cat > "$ETC/updater.conf" <<EOF
repo="LuhOli42/OpenGuitarMultiFx"
channel="stable"
check_interval="1h"
install_root="$OPT"
state_dir="$STATE"
service_name="ogmfx.service"
keep_releases=2
health_delay_s=1
health_grace_s=2
api_base="http://127.0.0.1:__PORT__"
EOF

run_updater() {
    OGMFX_CONF="$ETC/updater.conf" OGMFX_SYSTEMCTL="$BIN/systemctl" \
        bash "$SCRIPT" "$@"
}

# --- fake package / release helpers -----------------------------------------

# make_pkg <ver>: build ogmfx-<ver>-<arch>.tar.gz + .sha256 into $WWW.
# Package layout per the contract: install.sh, VERSION, bin/, systemd/.
make_pkg() {
    local v="$1"
    local pkg="$T/pkg/ogmfx-$v-$ARCH"
    rm -rf "$pkg"; mkdir -p "$pkg/bin" "$pkg/systemd"
    printf '#!/bin/sh\necho "ogmfx %s"\n' "$v" > "$pkg/bin/OpenGuitarMultiFx"
    chmod +x "$pkg/bin/OpenGuitarMultiFx"
    printf '%s\n' "$v" > "$pkg/VERSION"
    cp "$REPO_ROOT/updater/ogmfx-updater.service" "$REPO_ROOT/updater/ogmfx-updater.timer" \
        "$pkg/systemd/"
    cat > "$pkg/install.sh" <<'EOS'
#!/usr/bin/env bash
set -euo pipefail
: "${OGMFX_STATE_DIR:?install.sh needs OGMFX_STATE_DIR}"
cat VERSION >> "$OGMFX_STATE_DIR/install-ran.log"
EOS
    chmod +x "$pkg/install.sh"
    tar -C "$T/pkg" -czf "$WWW/ogmfx-$v-$ARCH.tar.gz" "ogmfx-$v-$ARCH"
    ( cd "$WWW" && sha256sum "ogmfx-$v-$ARCH.tar.gz" > "ogmfx-$v-$ARCH.tar.gz.sha256" )
}

# publish_stable <ver>: point the fake "latest" release + latest.json at <ver>.
publish_stable() {
    local v="$1" sha
    sha="$(awk '{print $1}' "$WWW/ogmfx-$v-$ARCH.tar.gz.sha256")"
    cat > "$WWW/latest.json" <<EOF
{
  "version": "$v",
  "tag": "$v",
  "tarball_url": "http://127.0.0.1:$PORT/ogmfx-$v-$ARCH.tar.gz",
  "sha256": "$sha",
  "channel": "stable",
  "published_at": "2026-10-08T00:00:00Z"
}
EOF
    cat > "$WWW/releases/latest" <<EOF
{
  "tag_name": "$v",
  "draft": false,
  "prerelease": false,
  "assets": [
    { "name": "latest.json",
      "browser_download_url": "http://127.0.0.1:$PORT/latest.json" },
    { "name": "ogmfx-$v-$ARCH.tar.gz",
      "browser_download_url": "http://127.0.0.1:$PORT/ogmfx-$v-$ARCH.tar.gz" }
  ]
}
EOF
}

current_ver() { [[ -L "$OPT/current" ]] && basename "$(readlink -f "$OPT/current")" || true; }
previous_ver(){ [[ -L "$OPT/previous" ]] && basename "$(readlink -f "$OPT/previous")" || true; }
running_ver() { cat "$STATE/running-version" 2>/dev/null || true; }
install_count(){ wc -l < "$STATE/install-ran.log" 2>/dev/null || echo 0; }

# --- start HTTP server -------------------------------------------------------
PORT="$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')"
sed -i "s/__PORT__/$PORT/" "$ETC/updater.conf"
python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$WWW" \
    > "$T/http.log" 2>&1 &
HTTP_PID=$!
sleep 0.5

# ---------------------------------------------------------------------------
say "1. fresh install of v1.0.0"
make_pkg v1.0.0; publish_stable v1.0.0
run_updater --force
check "exit ok, current -> v1.0.0"        '[[ "$(current_ver)" == v1.0.0 ]]'
check "install.sh ran once"               '[[ "$(install_count)" -eq 1 ]]'
check "service running v1.0.0"            '[[ "$(running_ver)" == v1.0.0 ]]'
check "no previous yet"                   '[[ ! -L "$OPT/previous" ]]'

say "2. no-op when already current"
run_updater --force
check "still current v1.0.0"              '[[ "$(current_ver)" == v1.0.0 ]]'
check "install.sh not re-run"             '[[ "$(install_count)" -eq 1 ]]'

say "3. update to v1.1.0 (health check passes)"
make_pkg v1.1.0; publish_stable v1.1.0
run_updater --force
check "current -> v1.1.0"                 '[[ "$(current_ver)" == v1.1.0 ]]'
check "previous -> v1.0.0"                '[[ "$(previous_ver)" == v1.0.0 ]]'
check "service running v1.1.0"            '[[ "$(running_ver)" == v1.1.0 ]]'

say "4. manual --rollback to v1.0.0"
run_updater --rollback
check "current -> v1.0.0"                 '[[ "$(current_ver)" == v1.0.0 ]]'
check "previous -> v1.1.0 (roll-forward)" '[[ "$(previous_ver)" == v1.1.0 ]]'
check "service running v1.0.0"            '[[ "$(running_ver)" == v1.0.0 ]]'

say "5. update to crashing v1.2.0 -> automatic rollback"
make_pkg v1.2.0; publish_stable v1.2.0
echo "v1.2.0" >> "$T/crash_versions"
if run_updater --force; then bad "updater exits non-zero on failed health check"; else ok "updater exits non-zero on failed health check"; fi
check "current restored -> v1.0.0"        '[[ "$(current_ver)" == v1.0.0 ]]'
check "service running v1.0.0 again"      '[[ "$(running_ver)" == v1.0.0 ]]'
check "v1.2.0 recorded as bad"            'grep -qx v1.2.0 "$STATE/bad-versions"'

say "6. check_interval throttles repeated --check"
before="$(grep -c 'GET /releases' "$T/http.log" || true)"
run_updater          # plain check, 1h interval, last check seconds ago
after="$(grep -c 'GET /releases' "$T/http.log" || true)"
check "no network hit while throttled"    '[[ "$after" == "$before" ]]'
run_updater --force >/dev/null 2>&1 || true
after="$(grep -c 'GET /releases' "$T/http.log" || true)"
check "--force bypasses throttle"         '[[ "$after" -gt "$before" ]]'

say "7. pruning keeps newest 2 + protected symlink targets"
make_pkg v1.3.0; publish_stable v1.3.0
run_updater --force
check "current -> v1.3.0"                 '[[ "$(current_ver)" == v1.3.0 ]]'
check "v1.1.0 pruned"                     '[[ ! -d "$OPT/releases/v1.1.0" ]]'
check "v1.0.0 kept (previous target)"     '[[ -d "$OPT/releases/v1.0.0" ]]'
check "v1.2.0 kept (2nd newest)"          '[[ -d "$OPT/releases/v1.2.0" ]]'
check "v1.3.0 kept (newest)"              '[[ -d "$OPT/releases/v1.3.0" ]]'

say "8. beta channel picks newest non-draft prerelease"
# v9.9.9-beta is a draft (must be skipped); v1.4.0-beta.1 is the pick.
# Per the contract every release's index asset is named exactly latest.json —
# the fake server gives the beta one its own directory.
make_pkg v1.4.0-beta.1
mkdir -p "$WWW/beta"
sha="$(awk '{print $1}' "$WWW/ogmfx-v1.4.0-beta.1-$ARCH.tar.gz.sha256")"
cat > "$WWW/beta/latest.json" <<EOF
{
  "version": "v1.4.0-beta.1",
  "tag": "v1.4.0-beta.1",
  "tarball_url": "http://127.0.0.1:$PORT/ogmfx-v1.4.0-beta.1-$ARCH.tar.gz",
  "sha256": "$sha",
  "channel": "beta",
  "published_at": "2026-10-08T00:00:00Z"
}
EOF
# The releases LIST lives at a separate api_base: http.server can't serve
# both a `releases` file (beta) and a `releases/` dir (stable's /latest).
mkdir -p "$WWW/beta-api"
cat > "$WWW/beta-api/releases" <<EOF
[
  {
    "tag_name": "v9.9.9-beta",
    "name": "draft release",
    "draft": true,
    "prerelease": true,
    "assets": [
      { "name": "latest.json",
        "browser_download_url": "http://127.0.0.1:$PORT/beta/latest.json" }
    ]
  },
  {
    "tag_name": "v1.4.0-beta.1",
    "name": "beta 1",
    "draft": false,
    "prerelease": true,
    "assets": [
      { "name": "latest.json",
        "browser_download_url": "http://127.0.0.1:$PORT/beta/latest.json" }
    ]
  },
  {
    "tag_name": "v1.3.0",
    "name": "stable",
    "draft": false,
    "prerelease": false,
    "assets": [
      { "name": "latest.json",
        "browser_download_url": "http://127.0.0.1:$PORT/latest.json" }
    ]
  }
]
EOF
sed -i 's/^channel="stable"$/channel="beta"/' "$ETC/updater.conf"
sed -i "s|^api_base=.*|api_base=\"http://127.0.0.1:$PORT/beta-api\"|" "$ETC/updater.conf"
run_updater --force
check "picked v1.4.0-beta.1 not the draft" '[[ "$(current_ver)" == v1.4.0-beta.1 ]]'
check "service running v1.4.0-beta.1"      '[[ "$(running_ver)" == v1.4.0-beta.1 ]]'

say "9. wrong-arch tarball is rejected"
make_pkg v1.5.0
# Rewrite the release to advertise a foreign-arch tarball name (server will
# 404 the download — but the updater must reject it before even fetching).
cat > "$WWW/latest.json" <<EOF
{
  "version": "v1.5.0",
  "tag": "v1.5.0",
  "tarball_url": "http://127.0.0.1:$PORT/ogmfx-v1.5.0-otherarch.tar.gz",
  "sha256": "deadbeef",
  "channel": "stable",
  "published_at": "2026-10-08T00:00:00Z"
}
EOF
cat > "$WWW/releases/latest" <<EOF
{ "tag_name": "v1.5.0", "draft": false, "prerelease": false,
  "assets": [ { "name": "latest.json",
    "browser_download_url": "http://127.0.0.1:$PORT/latest.json" } ] }
EOF
sed -i 's/^channel="beta"$/channel="stable"/' "$ETC/updater.conf"
sed -i "s|^api_base=.*|api_base=\"http://127.0.0.1:$PORT\"|" "$ETC/updater.conf"
if run_updater --force 2>/dev/null; then bad "rejects wrong-arch tarball"; else ok "rejects wrong-arch tarball"; fi
check "current still v1.4.0-beta.1"       '[[ "$(current_ver)" == v1.4.0-beta.1 ]]'
check "no v1.5.0 dir created"             '[[ ! -d "$OPT/releases/v1.5.0" ]]'

printf '\n=================================\n'
printf 'RESULT: %d passed, %d failed\n' "$PASS" "$FAIL"
exit "$FAIL"
