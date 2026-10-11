#pragma once

#include "UpdaterCore.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace openguitarmultifx
{

/**
    In-app updater: checks the project's GitHub releases from Settings and
    applies an update, driven entirely from the control side -- it never
    touches the audio path (see AGENTS.md's realtime boundary; network and
    file I/O all happen on one detached worker thread like
    Tone3000Manager's runInBackground, and results come back through
    MessageManager::callAsync).

    Two apply paths, picked by where the running binary lives:

    - Packaged install (the A7S image, or any Linux install done through
      the release tarball's install.sh): the binary sits under
      /opt/ogmfx/releases/. The whole check->download->verify->swap->
      health-check->rollback flow already exists as the root-side
      updater/ogmfx-update.sh + systemd units, so the app just runs
      `sudo -n ogmfx-update.sh --force` (packaging/ogmfx-updater.sudoers
      grants the ogmfx user exactly that one script) and shows its log
      lines as progress. On success the script restarts ogmfx.service
      itself -- meaning this app exits and comes back on the new version.
      Falls back to `systemctl start ogmfx-updater.service` when sudo is
      unavailable.

    - Unpackaged binary (developer desktop, build tree): a self-updating
      binary can't rewrite itself safely mid-execution, so the flow is
      download -> verify sha256 -> extract -> stageBinarySwap() next to
      the running exe (atomic rename, old binary kept as .bak) -> the UI
      offers "Restart now".

    repo/channel come from /etc/ogmfx/updater.conf when it exists (same
    file the shell updater reads), with defaults matching
    packaging/updater.conf otherwise.
*/
class AppUpdater
{
public:
    explicit AppUpdater (juce::String currentVersion);
    ~AppUpdater();

    struct CheckResult
    {
        enum class Status { upToDate, updateAvailable, failed };
        Status status = Status::failed;
        juce::String message;
        updatecore::UpdateCandidate candidate; // set when updateAvailable
    };

    juce::String getCurrentVersion() const { return currentVersion; }
    bool isBusy() const { return busy.load(); }

    /** True when this binary runs from the packaged /opt/ogmfx layout. */
    bool isPackagedInstall() const;

    /** Where downloads/checksums/extractions are staged. */
    juce::File getStagingDir() const;

    /** Asynchronous: onDone arrives on the message thread (guarded by an
        alive flag, like Tone3000Manager). One operation at a time; a
        second request while busy reports Status::failed "busy". */
    void checkForUpdate (std::function<void (const CheckResult&)> onDone);

    /** Download + apply the candidate. onStatus gets human-readable lines
        ("downloading", script output...), onProgress goes 0..1 for the
        download phase (-1 = indeterminate apply), onDone reports success,
        a message, and whether restartNow() should be offered. All on the
        message thread. On a packaged install success typically kills this
        process (the script restarts ogmfx.service) before onDone runs. */
    void downloadAndInstall (const updatecore::UpdateCandidate& candidate,
                             std::function<void (const juce::String&)> onStatus,
                             std::function<void (double)> onProgress,
                             std::function<void (bool ok, juce::String message, bool restartRequired)> onDone);

    /** Dev path: relaunch this executable (the staged binary already sits
        at its path) and quit. */
    void restartNow();

    /** Everything the Settings card needs to redraw the update section.
        Lives here (not in the panel) because the card is rebuilt every
        time Settings opens while the update keeps running -- a new card
        reads this to recover status text, the pending candidate (retry
        after a failed install) and the restart prompt instead of
        starting blank. */
    struct UiSnapshot
    {
        bool checkInFlight = false;
        bool installInFlight = false;
        bool restartRequired = false;
        std::optional<updatecore::UpdateCandidate> candidate;
        juce::String statusText;
        // 0..1 while a dev-binary download runs (packaged installs have
        // no measurable progress -- the card shows indeterminate there).
        double downloadProgress = 0.0;
    };
    UiSnapshot getUiSnapshot() const;

private:
    void updateSnapshot (const std::function<void (UiSnapshot&)>&);
    updatecore::Fetcher makeFetcher() const;
    void loadUpdaterConf();
    void runPackagedApply (std::function<void (const juce::String&)> onStatus,
                           std::function<void (bool, juce::String, bool)> onDone);

    juce::String currentVersion;
    juce::String repo = "LuhOli42/OpenGuitarMultiFx";
    juce::String channel = "stable";

    std::atomic<bool> busy { false };
    std::shared_ptr<std::atomic<bool>> aliveFlag = std::make_shared<std::atomic<bool>> (true);

    // Written from the worker thread and read on the message thread --
    // always under snapshotLock (a lock the audio path never touches).
    mutable juce::CriticalSection snapshotLock;
    UiSnapshot snapshot;
};

} // namespace openguitarmultifx
