#include "AppUpdater.h"

#include <thread>

namespace openguitarmultifx
{

namespace
{
    constexpr int connectTimeoutMs = 15000;

    bool httpGetText (const juce::String& url, juce::String& body)
    {
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                           .withExtraHeaders ("Accept: application/vnd.github+json\n"
                                              "User-Agent: ogmfx-updater")
                           .withConnectionTimeoutMs (connectTimeoutMs)
                           .withNumRedirectsToFollow (5);
        if (auto stream = juce::URL (url).createInputStream (options))
        {
            body = stream->readEntireStreamAsString();
            return body.isNotEmpty();
        }
        return false;
    }

    bool httpGetFile (const juce::String& url, const juce::File& dest,
                      const std::function<void (double)>& progress)
    {
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                           .withExtraHeaders ("User-Agent: ogmfx-updater")
                           .withConnectionTimeoutMs (connectTimeoutMs)
                           .withNumRedirectsToFollow (5);
        auto stream = juce::URL (url).createInputStream (options);
        if (stream == nullptr)
            return false;

        dest.deleteFile();
        juce::FileOutputStream out (dest);
        if (! out.openedOk())
            return false;

        const auto total = stream->getTotalLength();
        juce::MemoryBlock buffer ((size_t) 64 * 1024);
        juce::int64 received = 0;

        while (! stream->isExhausted())
        {
            const auto got = stream->read (buffer.getData(), (int) buffer.getSize());
            if (got <= 0)
                break;
            out.write (buffer.getData(), (size_t) got);
            received += got;
            if (progress && total > 0)
                progress (juce::jlimit (0.0, 1.0, (double) received / (double) total));
        }

        out.flush();
        // getTotalLength() can report -1 on chunked responses; only treat
        // a known-length shortfall as an error, never an unknown one.
        return received > 0 && (total < 0 || received >= total);
    }
} // namespace

AppUpdater::AppUpdater (juce::String version)
    : currentVersion (std::move (version))
{
    loadUpdaterConf();
}

AppUpdater::~AppUpdater()
{
    aliveFlag->store (false);
}

void AppUpdater::loadUpdaterConf()
{
    // Same file ogmfx-update.sh sources: bash KEY="value" lines, parsed
    // here as plain text. Absent on unpackaged installs -> defaults stay.
    const auto conf = juce::File ("/etc/ogmfx/updater.conf").loadFileAsString();
    for (const auto& line : juce::StringArray::fromLines (conf))
    {
        auto unquote = [] (juce::String v)
        {
            v = v.trim();
            return v.startsWithChar ('\"') && v.endsWithChar ('\"')
                     ? v.substring (1, v.length() - 1) : v;
        };
        if (line.trimStart().startsWith ("repo="))
            repo = unquote (line.fromFirstOccurrenceOf ("=", false, false));
        else if (line.trimStart().startsWith ("channel="))
            channel = unquote (line.fromFirstOccurrenceOf ("=", false, false));
    }
    if (channel != "stable" && channel != "beta")
        channel = "stable";
}

updatecore::Fetcher AppUpdater::makeFetcher() const
{
    updatecore::Fetcher fetch;
    fetch.getText = [] (const juce::String& url, juce::String& body)
    {
        return httpGetText (url, body);
    };
    fetch.getFile = [] (const juce::String& url, const juce::File& dest,
                        const std::function<void (double)>& progress)
    {
        return httpGetFile (url, dest, progress);
    };
    return fetch;
}

bool AppUpdater::isPackagedInstall() const
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                         .getFullPathName();
    return exe.startsWith ("/opt/ogmfx/");
}

juce::File AppUpdater::getStagingDir() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("OpenGuitarMultiFx")
               .getChildFile ("update-staging");
}

AppUpdater::UiSnapshot AppUpdater::getUiSnapshot() const
{
    const juce::ScopedLock sl (snapshotLock);
    return snapshot;
}

void AppUpdater::updateSnapshot (const std::function<void (UiSnapshot&)>& fn)
{
    const juce::ScopedLock sl (snapshotLock);
    fn (snapshot);
}

void AppUpdater::checkForUpdate (std::function<void (const CheckResult&)> onDone)
{
    if (busy.exchange (true))
    {
        CheckResult res;
        res.message = "an update is already in progress";
        onDone (res);
        return;
    }

    updateSnapshot ([] (UiSnapshot& s)
    {
        s.checkInFlight = true;
        s.statusText = "checking for updates...";
        // A previously offered release is only confirmed by a fresh
        // check succeeding -- don't keep offering it while rechecking.
        s.candidate.reset();
    });

    const auto fetch = makeFetcher();
    const auto apiBase = "https://api.github.com/repos/" + repo;
    const auto updateChannel = channel;
    const auto current = currentVersion;
    const auto arch = updatecore::archName();
    const auto alive = aliveFlag;

    std::thread ([=, this]() mutable
    {
        CheckResult res;
        juce::String error;
        auto candidate = updatecore::findUpdate (fetch, apiBase, updateChannel, arch, error);

        if (! candidate)
        {
            res.status = CheckResult::Status::failed;
            res.message = error;
        }
        else if (updatecore::compareVersions (candidate->version, current) > 0)
        {
            res.status = CheckResult::Status::updateAvailable;
            res.candidate = *candidate;
            res.message = "update available: " + candidate->version;
        }
        else
        {
            res.status = CheckResult::Status::upToDate;
            res.message = "up to date (latest is " + candidate->version + ")";
        }

        updateSnapshot ([&] (UiSnapshot& s)
        {
            s.checkInFlight = false;
            s.statusText = res.message;
            if (res.status == CheckResult::Status::updateAvailable)
                s.candidate = res.candidate;
        });
        busy.store (false);
        juce::MessageManager::callAsync ([alive, res = std::move (res),
                                        onDone = std::move (onDone)]() mutable
        {
            if (alive->load() && onDone)
                onDone (res);
        });
    }).detach();
}

void AppUpdater::downloadAndInstall (
    const updatecore::UpdateCandidate& candidate,
    std::function<void (const juce::String&)> onStatus,
    std::function<void (double)> onProgress,
    std::function<void (bool, juce::String, bool)> onDone)
{
    if (busy.exchange (true))
    {
        if (onDone)
            onDone (false, "an update is already in progress", false);
        return;
    }

    const auto fetch = makeFetcher();
    const auto staging = getStagingDir();
    const auto alive = aliveFlag;
    const bool packaged = isPackagedInstall();

    updateSnapshot ([&] (UiSnapshot& s)
    {
        s.installInFlight = true;
        s.restartRequired = false;
        s.candidate = candidate;
        s.statusText = "starting update to " + candidate.version + "...";
        s.downloadProgress = 0.0;
    });

    // Every status line also lands in the snapshot so a rebuilt Settings
    // card shows the same text; the caller's callback still runs. The
    // snapshot write happens at emit time on the worker -- posting it
    // would let a queued progress line overwrite the terminal result.
    std::function<void (const juce::String&)> onStatusRecorded = [this, onStatus, alive] (const juce::String& line)
    {
        updateSnapshot ([&] (UiSnapshot& s) { s.statusText = line; });
        if (onStatus)
            juce::MessageManager::callAsync ([alive, onStatus, line]
            {
                if (alive->load())
                    onStatus (line);
            });
    };

    if (packaged)
    {
        std::thread ([=, this]() mutable
        {
            runPackagedApply (onStatusRecorded, onDone);
            busy.store (false);
        }).detach();
        return;
    }

    std::thread ([=, this]() mutable
    {
        // Defined inside the thread body: it must reference THIS lambda's
        // captured copies of the callbacks, not the caller's stack.
        auto post = [&] (auto&& cb, auto&&... args)
        {
            juce::MessageManager::callAsync ([alive, cb,
                                            ... capturedArgs = std::forward<decltype (args)> (args)]() mutable
            {
                if (alive->load() && cb)
                    cb (std::move (capturedArgs)...);
            });
        };

        onStatusRecorded ("downloading " + candidate.version + "...");

        juce::String error;
        auto binary = updatecore::stageFromUrls (fetch, candidate, staging,
                                                 [&] (double p)
                                                 {
                                                     updateSnapshot ([&] (UiSnapshot& s) { s.downloadProgress = p; });
                                                     if (onProgress)
                                                         post (onProgress, p);
                                                 },
                                                 error);

        if (binary == juce::File())
        {
            updateSnapshot ([&] (UiSnapshot& s)
            {
                s.installInFlight = false;
                s.statusText = "update failed: " + error; // candidate kept for retry
            });
            busy.store (false);
            if (onDone)
                post (onDone, false, "update failed: " + error, false);
            return;
        }

        onStatusRecorded ("installing...");

        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        const bool ok = updatecore::stageBinarySwap (binary, exe, error);

        updateSnapshot ([&] (UiSnapshot& s)
        {
            s.installInFlight = false;
            s.restartRequired = ok;
            s.statusText = ok ? candidate.version + " staged -- restart to finish the update"
                              : "update failed: " + error; // candidate kept for retry
        });
        busy.store (false);
        if (onDone)
        {
            if (ok)
                post (onDone, true,
                      candidate.version + " staged -- restart to finish the update", true);
            else
                post (onDone, false, "update failed: " + error, false);
        }
    }).detach();
}

void AppUpdater::runPackagedApply (std::function<void (const juce::String&)> onStatus,
                                   std::function<void (bool, juce::String, bool)> onDone)
{
    const auto alive = aliveFlag;
    auto post = [&] (auto&& cb, auto&&... args)
    {
        juce::MessageManager::callAsync ([alive, cb, ... capturedArgs = std::forward<decltype (args)> (args)]() mutable
        {
            if (alive->load() && cb)
                cb (std::move (capturedArgs)...);
        });
    };
    auto finish = [&] (bool ok, juce::String msg)
    {
        updateSnapshot ([&] (UiSnapshot& s)
        {
            s.installInFlight = false;
            s.statusText = msg;
        });
        if (onDone)
            post (onDone, ok, std::move (msg), false);
    };

    // The root-side updater owns the whole swap/health-check/rollback
    // flow. It needs root, which the ogmfx user reaches through the
    // packaging/ogmfx-updater.sudoers rule (NOPASSWD on this one script).
    juce::StringArray cmd;
    const juce::File sudoBin ("/usr/bin/sudo");
    const juce::File updateScript ("/usr/local/sbin/ogmfx-update.sh");

    if (sudoBin.existsAsFile() && updateScript.existsAsFile())
        cmd = { sudoBin.getFullPathName(), "-n", updateScript.getFullPathName(), "--force" };
    else
        cmd = { "systemctl", "start", "ogmfx-updater.service" };

    // onStatus is the emit-time recording emitter above -- call it
    // synchronously so snapshot ordering is preserved (see its comment).
    if (onStatus)
        onStatus (juce::String ("installing via the system updater..."));

    juce::ChildProcess proc;
    if (! proc.start (cmd))
    {
        finish (false, juce::String ("could not start the system updater"));
        return;
    }

    juce::String output;
    char buffer[4096];
    while (proc.isRunning())
    {
        const auto n = proc.readProcessOutput (buffer, sizeof (buffer));
        if (n > 0)
            output += juce::String::fromUTF8 (buffer, (int) n);
        if (onStatus && output.isNotEmpty())
        {
            const auto lines = juce::StringArray::fromLines (output.trimEnd());
            if (! lines.isEmpty())
                onStatus (lines[lines.size() - 1]);
        }
        juce::Thread::sleep (200);
    }
    // Drain whatever the child printed in its last moments.
    while (true)
    {
        const auto n = proc.readProcessOutput (buffer, sizeof (buffer));
        if (n <= 0)
            break;
        output += juce::String::fromUTF8 (buffer, (int) n);
    }

    const int exitCode = (int) proc.getExitCode();
    if (exitCode == 0)
    {
        // On success the script restarts ogmfx.service -- usually killing
        // this app before this callback lands. If we are still here (e.g.
        // throttled check), say so plainly.
        finish (true,
                juce::String ("system updater finished -- the app will restart on the new version"));
        return;
    }

    auto lines = juce::StringArray::fromLines (output);
    while (lines.size() > 3)
        lines.remove (0);
    finish (false,
            "update failed (the previous version stays live): " + lines.joinIntoString (" / "));
}

void AppUpdater::restartNow()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    if (exe.startAsProcess())
    {
        updateSnapshot ([] (UiSnapshot& s) { s.restartRequired = false; });
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->systemRequestedQuit();
    }
    else
    {
        // Keep restartRequired so the prompt survives -- the staged
        // binary still waits at the executable path.
        updateSnapshot ([] (UiSnapshot& s)
        {
            s.statusText = "could not relaunch the app -- restart it manually to finish the update";
        });
    }
}

} // namespace openguitarmultifx
