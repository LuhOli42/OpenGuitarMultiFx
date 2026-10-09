#!/usr/bin/env bash
# Phase 6 W3: assemble the release tarball described by the "Package
# artifact" contract in docs/Phase6Plan.md.
#
#   scripts/package.sh <aarch64|x86_64> [version]
#
# Requires a finished build tree (scripts/cross-build.sh <arch>) and emits,
# into $OUT_DIR:
#
#   ogmfx-<version>-<arch>.tar.gz   payload (layout below)
#   ogmfx-<version>-<arch>.tar.gz.sha256
#   latest.json                    {version, tag, tarball_url, sha256,
#                                   channel, published_at}
#
# Tarball layout (the updater extracts this into releases/<version>/):
#
#   bin/OpenGuitarMultiFx          the app binary
#   systemd/ogmfx.service          app unit (kiosk)
#   systemd/ogmfx-updater.service  one-shot update check
#   systemd/ogmfx-updater.timer    periodic trigger
#   sbin/ogmfx-update.sh           installed to /usr/local/sbin by install.sh
#   etc/ogmfx/updater.conf         default config (installed only if absent)
#   install.sh                     idempotent device-side installer
#   VERSION                        the version string
#
# The systemd units and updater.conf under packaging/ mirror the canonical
# copies in updater/ (W4 owns that tree) — keep them in sync when W4's
# change. ogmfx-update.sh itself is taken from updater/ at packaging time:
# the script is W4's file and must not be duplicated.
#
# Version: defaults to `git describe --tags --match 'v*' --dirty`, i.e. the
# tag on tagged builds; untagged builds get 0.0.0-dev+<sha>.
#
# Env overrides (used by release.yml and for local testing):
#   BUILD_DIR    build tree to pick the binary from   (default build-<arch>)
#   OUT_DIR      output directory                     (default dist/<arch>)
#   UPDATER_DIR  where ogmfx-update.sh lives          (default updater)
#   TAG          release tag for latest.json          (default = version)
#   CHANNEL      latest.json channel                  (default stable)
#   REPO         owner/name for the asset URL         (default GITHUB_REPOSITORY,
#                                                     else LuhOli42/OpenGuitarMultiFx)
#   RELEASES_URL base URL for tarball_url             (default github releases)
set -euo pipefail
cd "$(dirname "$0")/.."

ARCH="${1:-}"
case "$ARCH" in
  aarch64|x86_64) ;;
  *) echo "usage: $0 <aarch64|x86_64> [version]" >&2; exit 2 ;;
esac

VERSION="${2:-}"
if [ -z "$VERSION" ]; then
  VERSION="$(git describe --tags --match 'v*' --dirty 2>/dev/null \
             || echo "0.0.0-dev+$(git rev-parse --short HEAD)")"
fi

BUILD_DIR="${BUILD_DIR:-build-$ARCH}"
OUT_DIR="${OUT_DIR:-dist/$ARCH}"
UPDATER_DIR="${UPDATER_DIR:-updater}"
TAG="${TAG:-$VERSION}"
CHANNEL="${CHANNEL:-stable}"
REPO="${REPO:-${GITHUB_REPOSITORY:-LuhOli42/OpenGuitarMultiFx}}"
RELEASES_URL="${RELEASES_URL:-https://github.com/$REPO/releases/download}"

die() { echo "package.sh: ERROR: $*" >&2; exit 1; }

# The app binary inside the JUCE artefacts tree. Match the exact name so the
# Tests binary (OpenGuitarMultiFx_Tests) can't be picked up.
app_bin="$(find "$BUILD_DIR" -name OpenGuitarMultiFx -type f -print -quit 2>/dev/null)"
[ -n "$app_bin" ] || die "no OpenGuitarMultiFx binary under $BUILD_DIR — run scripts/cross-build.sh $ARCH first"
[ -f "$UPDATER_DIR/ogmfx-update.sh" ] || die "missing $UPDATER_DIR/ogmfx-update.sh (the W4 updater script)"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

# Payload — mirrors the install() rules at the bottom of CMakeLists.txt.
install -D -m 0755 "$app_bin" "$stage/bin/OpenGuitarMultiFx"
install -D -m 0644 packaging/ogmfx.service         "$stage/systemd/ogmfx.service"
install -D -m 0644 packaging/ogmfx-updater.service "$stage/systemd/ogmfx-updater.service"
install -D -m 0644 packaging/ogmfx-updater.timer   "$stage/systemd/ogmfx-updater.timer"
install -D -m 0755 "$UPDATER_DIR/ogmfx-update.sh"  "$stage/sbin/ogmfx-update.sh"
install -D -m 0644 packaging/updater.conf          "$stage/etc/ogmfx/updater.conf"
install -D -m 0755 packaging/install.sh            "$stage/install.sh"
printf '%s\n' "$VERSION" > "$stage/VERSION"

fname="ogmfx-$VERSION-$ARCH.tar.gz"
mkdir -p "$OUT_DIR"
tar -C "$stage" -czf "$OUT_DIR/$fname" \
    bin systemd sbin etc install.sh VERSION
( cd "$OUT_DIR" && sha256sum "$fname" > "$fname.sha256" )

sha="$(awk '{print $1}' "$OUT_DIR/$fname.sha256")"
published_at="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"

# Flat key/value JSON — the updater parses it with sed/awk, no jq (see
# json_field in updater/ogmfx-update.sh). Keep it flat.
cat > "$OUT_DIR/latest.json" <<EOF
{
  "version": "$VERSION",
  "tag": "$TAG",
  "tarball_url": "$RELEASES_URL/$TAG/$fname",
  "sha256": "$sha",
  "channel": "$CHANNEL",
  "published_at": "$published_at"
}
EOF

echo "package.sh: wrote $OUT_DIR/$fname (+ .sha256, latest.json)"
