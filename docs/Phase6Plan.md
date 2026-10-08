# Phase 6 Plan — ARM port (Radxa Cubie A7S) + release/update pipeline

*Written 2026-10-08, agreed with the user the same day. This is the action plan for
Phase 6 plus the deploy pipeline that ships updates to a running pedalboard.*

## Decisions locked with the user

| # | Question | Decision |
|---|---|---|
| 1 | OS on the A7S | Custom **minimal image** — we build it ourselves, "just enough for it to work" (not full Radxa OS, not Yocto for v1) |
| 2 | Update mechanism | **Simple v1**: tarball + atomic `current` symlink swap + systemd, with health check + rollback. No RAUC/A-B for now |
| 3 | Audio path for Phase 6 | **USB class-2 interface** (same one as the dev bench) first; the instrument-level I2S codec stays as a later hardware task |
| 4 | ARM build strategy | **Cross-compile** on x86 runners — and the *same* toolchain/scripts must also do x86, one parametrized pipeline for both targets |

The board is already with the user. Everything in this plan is written so that
work that doesn't need the physical board proceeds without it.

## Contracts (fixed here so parallel sessions never block each other)

### Install layout on the device

```
/opt/ogmfx/
├── releases/<version>/      # one directory per installed version
├── current -> releases/vX.Y.Z   # atomic symlink swap (ln -sfn + mv -T)
└── previous -> releases/vX.Y.Z  # rollback target
/var/lib/ogmfx/              # survives updates: presets/, models/, cache/, state/
/etc/ogmfx/updater.conf      # repo, channel (stable|beta), check interval
```

### Package artifact

- `ogmfx-<version>-<arch>.tar.gz` (+ `.sha256` sidecar), `<arch>` in `{aarch64, x86_64}`.
- Inside: `bin/OpenGuitarMultiFx`, `systemd/ogmfx.service`, `systemd/ogmfx-updater.{service,timer}`,
  `install.sh` (idempotent: installs units, reloads systemd), `VERSION` file.
- Each GitHub Release also carries `latest.json`:
  `{version, tag, tarball_url, sha256, channel, published_at}`.

### Versioning and services

- Tags `vX.Y.Z` trigger releases; `git describe` produces the `VERSION` string
  (`0.0.0-dev+<sha>` for untagged builds).
- `ogmfx.service` — the app, runs as user `ogmfx`, `WantedBy=ogmfx.target`-style autostart.
- `ogmfx-updater.timer` + `ogmfx-updater.service` — periodic update check.

### Update flow (v1)

1. Query GitHub Releases (`/releases/latest` for stable; newest prerelease for beta per `updater.conf`).
2. Skip if `releases/<v>` already installed or `current` is that version.
3. Download tarball + `.sha256`, verify checksum.
4. Extract to `/opt/ogmfx/releases/<v>/`, run its `install.sh`.
5. Point `previous` at the old release, swap `current` atomically (`mv -T` a prepared symlink).
6. Restart `ogmfx.service`; health check = unit active after N s and stays up through a
   grace window. On failure: repoint `current` to `previous`, restart, record the bad version.
7. Keep the two newest releases; prune the rest.

Manual rollback: `ogmfx-update.sh --rollback` (same symlink swap to `previous`).

## Workstreams

### W1 — Unified cross toolchain + build scripts *(foundation, no hardware)*

- `tools/toolchain/Dockerfile` — Ubuntu 24.04 base, `gcc-aarch64-linux-gnu`, and an
  aarch64 sysroot built via multiarch apt (JUCE deps: `libasound2-dev`, `libx11-dev`+friends,
  `libfreetype-dev`, `libfontconfig-dev`, `libcurl4-openssl-dev`, `libgtk-3-dev`,
  `libwebkit2gtk-4.1-dev`). The same image also builds `x86_64` (`TARGET` arg) — that is
  the "usar pra x86 tbm" requirement: one pipeline, parametrized arch.
- `cmake/toolchains/aarch64-linux-gnu.cmake` — system name/processor/sysroot, pkg-config
  pointed at the sysroot's `.pc` files.
- `scripts/cross-build.sh <aarch64|x86_64>` — thin wrapper over cmake + toolchain file.
- Keep `scripts/build.sh` untouched for the dev bench.
- **Acceptance:** `scripts/cross-build.sh aarch64` produces a working aarch64 ELF of the
  app *and* of the Tests binary; same command with `x86_64` builds for the host.

### W2 — CI *(needs W1's toolchain; x86 job can start immediately)*

- `.github/workflows/ci.yml`: on PR/push, matrix `{x86_64 native, aarch64 cross via the
  W1 image}` → build + `ctest`. aarch64 tests run under `qemu-aarch64` + binfmt inside the
  container; if too flaky/slow, aarch64 tests run on `main` only.
- Cache `build/_deps` (JUCE, Eigen, nlohmann/json, NAM core fetch) and compiler cache —
  the first uncached JUCE build is the dominant cost.

### W3 — Packaging + release workflow *(needs W1)*

- `install()` rules in `CMakeLists.txt` (binary + `packaging/` payload).
- `packaging/`: `ogmfx.service`, `install.sh`, tarball assembly
  (`scripts/package.sh <arch>` emits the contract tarball + `.sha256` + `latest.json`).
- `.github/workflows/release.yml`: tag `v*` → cross-build aarch64 + x86 via W1 →
  artifacts → GitHub Release.
- **File-overlap note:** this touches the bottom of `CMakeLists.txt` (install rules) while
  W6 touches `cmake/NAMCore.cmake` and `Source/` — safe to run in parallel.

### W4 — On-device updater *(fully independent; only needs the contracts above)*

- `updater/ogmfx-update.sh` — the 7-step flow, bash + curl, no jq dependency
  (parse `latest.json` minimally; document if jq becomes required).
- `updater/ogmfx-updater.service` + `.timer`, `updater.conf` template.
- Health check + automatic rollback to `previous`; keep-2 pruning.
- Develop and test entirely on x86 with a fake `/opt/ogmfx` and a local HTTP server —
  no board needed.

### W5 — Minimal custom image for the A7S *(fully independent)*

- `image/build-image.sh` — fetch the official **Radxa minimal CLI** image for the Cubie
  A7S (vendor BSP kernel — mainline/PREEMPT_RT is still community bring-up per
  `ARCHITECTURE.md` §B), loop-mount it, chroot via `qemu-aarch64`, apply `image/overlay/`:
  - `ogmfx` user, audio/realtime groups, autologin;
  - runtime deps only (ALSA utils, minimal display stack per the kiosk decision);
  - our systemd units enabled; docs/locales/man-pages stripped;
  - repack as flashable `.img.xz`.
- `scripts/setup-target.sh` — the RT/audio setup reused by both the image build and a
  manual install on stock Radxa OS: `performance` governor, rtprio+memlock limits,
  IRQ affinity for the USB audio IRQs, ALSA defaults, systemd enablement.
- `image/customize.sh` documents each step so the same script is re-runnable when Radxa
  ships a newer base image.
- **Kiosk display — default for v1:** minimal Xorg + the JUCE app fullscreen (JUCE's Linux
  GUI path is most proven on X11; its Wayland support is newer). Cage/wlroots is the
  documented alternative if X proves problematic on the BSP.

### W6 — ARM code fixes + benchmarks *(needs W1; on-board part needs the hardware)*

- aarch64 build fixes as they surface. Known one already: `OGMFX_NATIVE_TUNING` uses
  `-mcpu=native`, which under a *cross* compiler means the build host — add an explicit
  `OGMFX_TARGET_CPU` (e.g. `cortex-a76`) used when cross-compiling. A733 is
  2×A76 + 6×A55; `-mcpu=cortex-a76` + NEON is the right tuning for cross-built binaries.
- Run the unit-test binary under qemu-aarch64 in CI (W2's job), fix whatever fails.
- On-board (user-run or a session with board access): `EffectCostBench`,
  `ChainSpikeBench`, `cyclictest`, 30-min soak per `ARCHITECTURE.md` §G → results into
  `docs/arm-benchmarks.md`. This is where PREEMPT_RT reality gets measured.

## Parallel session map

| Session | Workstream | Depends on | Needs board? | Files owned |
|---|---|---|---|---|
| S1 | W1 toolchain | — | no | `tools/`, `cmake/toolchains/`, `scripts/cross-build.sh` |
| S2 | W4 updater | contracts only | no | `updater/` |
| S3 | W5 image + target setup | contracts only | no* | `image/`, `scripts/setup-target.sh` |
| S4 | W2 CI + W3 release | W1 | no | `.github/`, `packaging/`, `scripts/package.sh`, `CMakeLists.txt` (install rules) |
| S5 | W6 ARM fixes | W1 | partially | `cmake/NAMCore.cmake`, `Source/`, `docs/arm-benchmarks.md` |

\* S3 produces a flashable image without the board; actually booting it is the user's call.

**Recommended opening order:** S1, S2, S3 in parallel right now. S4 and S5 start once S1's
toolchain merges (S4 may start earlier with just the x86 CI job).

## Open risks / watch items

- **PREEMPT_RT on the A733 BSP kernel is unconfirmed** (`ARCHITECTURE.md` §I) — W6's
  cyclictest/soak numbers decide whether 64-sample blocks are viable or we ship 128.
- **WebKitGTK in the aarch64 sysroot is the heaviest cross-compile dep.** Escape hatch if
  it turns out brittle: build the target image with `JUCE_WEB_BROWSER=0` — TONE3000 login
  then happens off-device; the module is optional by design and `.nam` files still load
  locally. Decide only if W1/S4 actually hits trouble.
- **I2S instrument-level codec** is a purchase + electrical-validation item, not software —
  tracked in `ARCHITECTURE.md` §H, deliberately out of this plan.
- **Radxa image churn:** the build script pins the exact base-image URL/version it was
  validated against; bump deliberately.
