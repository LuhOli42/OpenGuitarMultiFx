# OGMFX image for the Radxa Cubie A7S

Minimal kiosk image for the Phase 6 target (Allwinner A733; see
[`docs/Phase6Plan.md`](../docs/Phase6Plan.md) W5 and
[`ARCHITECTURE.md`](../ARCHITECTURE.md) §B). It is the **official Radxa CLI
image with the vendor BSP kernel left exactly as shipped**, plus:

- user `ogmfx` (groups `audio video input render plugdev`, password locked);
  autologin on tty1 as a maintenance console;
- realtime setup from [`scripts/setup-target.sh`](../scripts/setup-target.sh):
  `performance` governor on every boot, `rtprio 95` / `memlock unlimited` for
  `@audio` and `@ogmfx`, USB-audio IRQs pinned to a Cortex-A76 core,
  `irqbalance` off, USB audio never runtime-suspended, ALSA `default` = the USB
  interface;
- kiosk: `ogmfx.target` is the default target; it pulls `ogmfx.service`, which
  starts rootless Xorg on vt7 (vendor BXM X server) and the app fullscreen via
  `/usr/local/bin/ogmfx-kiosk`. There is no window manager or desktop;
- `ogmfx-updater.timer` (every 6 h) → `ogmfx-updater.service`;
- the install layout from the plan (`/opt/ogmfx/releases/`, `/var/lib/ogmfx/`,
  `/etc/ogmfx/`), empty until a release is installed;
- removed: PulseAudio, Samba, Chromium/desktop leftovers, Intel/AMD firmware,
  man pages, docs, non-English locales, apt lists. `/etc/dpkg/dpkg.cfg.d/01-ogmfx-nodoc`
  keeps later installs lean too.

The image ships **without the app**: `ogmfx.service` has
`ConditionPathExists=/opt/ogmfx/current/bin/OpenGuitarMultiFx` and is skipped
until a release tarball (W3) is installed by its `install.sh` or the updater
(W4). Those deliveries replace the three baseline units in
`/etc/systemd/system/` with their own copies under the same names.

## Build

On an x86_64 Debian/Ubuntu host (≈ 8 GB free disk, network for apt):

```sh
sudo apt install qemu-user-static binfmt-support curl xz-utils e2fsprogs \
                 cloud-guest-utils gdisk fdisk zerofree rsync systemd
sudo image/build-image.sh            # → build/image/ogmfx-cubie-a7s-<date>-<git>.img.xz
```

It downloads the pinned Radxa image (`BASE_URL` / `BASE_SHA512` in
`build-image.sh`, cached in `build/image/cache/`), grows the rootfs, applies
`image/overlay/`, runs `image/customize.sh` in an aarch64 chroot through
qemu-user binfmt, shrinks the rootfs back, zeroes free space, runs the
built-in verification and compresses. Takes ~15–30 min, mostly qemu-emulated apt.

Options: `--base-xz FILE` (offline / pre-downloaded base, still SHA-checked),
`--out DIR`, `--keep-work` (keep the raw `.img`), `--grow-mib` / `--headroom-mib`.

Check any produced image without booting it:

```sh
sudo image/build-image.sh --verify build/image/ogmfx-cubie-a7s-*.img.xz
```

Moving to a newer Radxa base is documented step by step at the top of
[`customize.sh`](customize.sh).

## Flash

microSD (≥ 8 GB, A1/A2 class recommended):

```sh
xz -dc ogmfx-cubie-a7s-*.img.xz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
```

or flash the `.img.xz` directly with balenaEtcher / Raspberry Pi Imager
("Use custom"). Optional before first boot: edit `before.txt` on the small
`config` FAT partition (admin password, Wi-Fi via `connect_wi-fi`).

## First boot

1. Radxa's rsetup runs `/config/before.txt` once: grows the rootfs to the card,
   regenerates SSH host keys, creates the admin account **`radxa` / `radxa`
   (sudo — change the password immediately)** and enables SSH. Expect one
   extra minute and possibly an automatic reboot.
2. Reachable as `ogmfx.local` (avahi) or via the DHCP lease; `ssh radxa@ogmfx.local`.
3. Without an installed release the screen stays on the tty1 console
   (autologged-in as `ogmfx`); `ogmfx.service` is skipped by its condition.
   Install a release, then `sudo systemctl start ogmfx.service` (or reboot).
4. Plug the USB audio interface before or after boot; the udev rule re-pins its
   IRQ and refreshes the ALSA default. `sudo ogmfx-setup-target --check` shows
   governor, IRQ affinity, limits, enabled units and whether `SCHED_FIFO`
   works in the service's cgroup.

Maintenance: `Ctrl+Alt+F1` = console, `Ctrl+Alt+F7` = kiosk;
`sudo systemctl isolate multi-user.target` stops the kiosk,
`sudo systemctl isolate ogmfx.target` brings it back. Logs:
`journalctl -u ogmfx -b`.

## Manual install on stock Radxa OS

The same RT/audio setup without this image:

```sh
sudo scripts/setup-target.sh            # live system: config + applies tuning now
sudo scripts/setup-target.sh --kiosk    # also make ogmfx.target the default (needs the units)
```

It is idempotent, installs itself as `/usr/local/sbin/ogmfx-setup-target` (the
udev rule calls it with `--runtime`) and persists its choices in
`/etc/ogmfx/target.conf`. `--irq-cpu N` and `--alsa-card ID` override the
auto-detection.

## Notes

- **Kernel.** Vendor 5.15 BSP, `CONFIG_PREEMPT=y` (not PREEMPT_RT),
  `CONFIG_RT_GROUP_SCHED=y`. The latter is why no OGMFX unit sets
  `CPUWeight=` / `CPUQuota=`: enabling the cgroup cpu controller makes
  `SCHED_FIFO` fail with `EPERM` for the audio thread.
- **Debian sources.** Bullseye is EOL; `customize.sh` points the Debian apt
  lists at `archive.debian.org` (Radxa's repos unchanged). Security updates for
  the base therefore come only through Radxa until the base moves to a newer
  Debian.
- **Display alternative.** If Xorg misbehaves on the BXM stack, cage (wlroots)
  is the documented fallback: `apt install cage`, and change `ExecStart=` of
  `ogmfx.service` to `/usr/bin/cage -s -- /opt/ogmfx/current/bin/OpenGuitarMultiFx`
  (the app must then run on XWayland or JUCE's native backend). Not the default.
- **Benchmarks.** `rt-tests` (cyclictest) and `lm-sensors` are included for the
  ARCHITECTURE.md §G measurements.
