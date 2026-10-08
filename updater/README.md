# OGMFX on-device updater (Phase 6 / W4)

Bash + curl implementation of the "Update flow (v1)" contract in
[`docs/Phase6Plan.md`](../docs/Phase6Plan.md). No jq.

## Files

| File | Installs to | Purpose |
|------|-------------|---------|
| `ogmfx-update.sh` | `/usr/local/sbin/ogmfx-update.sh` | Update check + `--rollback` (run as root) |
| `updater.conf` | `/etc/ogmfx/updater.conf` | repo, channel (`stable`/`beta`), check interval, paths |
| `ogmfx-updater.service` | `/etc/systemd/system/` | one-shot service wrapping the script |
| `ogmfx-updater.timer` | `/etc/systemd/system/` | periodic trigger (`timers.target`) |

The `systemd/ogmfx-updater.{service,timer}` payload inside release tarballs
(W3) and the image overlay (W5) should install these four files at the
locations above; `ExecStart` in the service assumes
`/usr/local/sbin/ogmfx-update.sh` and `ConditionPathExists` assumes
`/etc/ogmfx/updater.conf`.

## Behavior

1. Query `api_base/releases/latest` (stable) or scan `api_base/releases`
   for the newest non-draft prerelease (beta); fetch the release's
   `latest.json` asset for `version`/`tarball_url`/`sha256`.
2. Skip when `releases/<version>` exists or is `current`.
3. Download `ogmfx-<version>-<arch>.tar.gz` + `.sha256`; verify checksum
   (and cross-check the `sha256` field of `latest.json`).
4. Extract into `/opt/ogmfx/releases/<version>/` (via a staging dir, so a
   partial download never leaves a half-installed release), run the
   package's `install.sh`.
5. Point `previous` at the old release, swap `current` atomically
   (`ln -s` a prepared symlink, `mv -T` over `current`).
6. Restart `ogmfx.service`; the unit must be `active` after
   `health_delay_s` and stay active through `health_grace_s`. On failure:
   repoint `current` at `previous`, restart, record the bad version in
   `$state_dir/bad-versions` (`previous` then points at the failed release,
   so `--rollback` can roll forward again for a retry).
7. Prune `releases/` to the `keep_releases` (2) newest directories; the
   targets of `current`/`previous` are never deleted.

Manual rollback: `ogmfx-update.sh --rollback` swaps `current`/`previous`
back and restarts the service.

Every failure path exits non-zero with `current` still pointing at the
release that was live before the run. The script is idempotent and takes a
`flock` so a timer tick can't overlap a manual run.

## Testing (no hardware)

`test/run-tests.sh` exercises the full flow on x86: a fake `/opt/ogmfx`, a
stub `systemctl` (selected via the `OGMFX_SYSTEMCTL` env var), and a local
HTTP server standing in for the GitHub Releases API (`api_base` override in
`updater.conf`, plus `OGMFX_CONF`/`OGMFX_UNAME_M` hooks). See the top of
that script for the scenario list.
