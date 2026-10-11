#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>

namespace openguitarmultifx
{

/**
    Pure, network-free core of the in-app updater: version comparison,
    GitHub Releases JSON parsing, asset selection, checksum/extract and the
    staged-binary swap used by the desktop apply path. Everything here is
    deterministic and unit-tested in Tests/UpdateCoreTests.cpp -- the live
    network + subprocess orchestration lives in AppUpdater (app target
    only), which supplies the Fetcher implementation below.

    Contract kept with the shell-side updater (updater/ogmfx-update.sh and
    docs/Phase6Plan.md "Update flow v1"): tarballs are named
    ogmfx-<version>-<arch>.tar.gz with a .sha256 sidecar, <arch> is
    `uname -m` (x86_64 / aarch64), and staging+verify always complete
    before anything the running binary depends on is touched.
*/
namespace updatecore
{
    /** strcmp-style version comparison (<0, 0, >0).

        Accepts the strings this project's builds and tags actually
        produce: "v1.2.3", "1.2.3", git-describe ahead-of-tag
        "v1.2.3-4-gdeadbeef" (and its "-dirty" variant), prerelease tags
        "v1.2.3-beta.1", and untagged "0.0.0-dev+<sha>".

        Ordering: compare the numeric base first; on a tie,
        dev < prerelease < clean release < ahead-of-tag (a git-describe
        "-N-g<sha>" build already contains the tagged release plus N
        commits, so it counts as newer, not older). */
    int compareVersions (const juce::String& a, const juce::String& b);

    /** The tarball-arch string for the machine this binary runs on
        ("x86_64" or "aarch64"); empty on an unexpected arch. */
    juce::String archName();

    struct UpdateCandidate
    {
        juce::String version;      // release version string, e.g. "v1.2.3"
        juce::String notes;        // release body, may be empty
        juce::String tarballUrl;   // browser_download_url of ogmfx-<v>-<arch>.tar.gz
        juce::String sha256Url;    // browser_download_url of the .sha256 sidecar
        juce::int64 tarballSize = 0;
    };

    /** Everything network-touching is injected so the whole check +
        download + verify + extract flow is testable against canned
        payloads: AppUpdater supplies a juce::URL/libcurl implementation,
        the tests supply one that reads local files. Both return false on
        transport failure; progress goes 0.0 -> 1.0 during getFile. */
    struct Fetcher
    {
        std::function<bool (const juce::String& url, juce::String& bodyOut)> getText;
        std::function<bool (const juce::String& url, const juce::File& dest,
                            const std::function<void (double)>& progress)> getFile;
    };

    /** Extract the UpdateCandidate for `arch` from one parsed
        `GET /repos/<r>/releases/<tag|latest>` payload. nullopt when the
        release carries no matching tarball (or the JSON isn't a release
        object at all). */
    std::optional<UpdateCandidate> parseLatestRelease (const juce::var& releaseJson,
                                                       const juce::String& arch);

    /** Beta channel: from a parsed `GET /repos/<r>/releases` array
        (newest-first per the API), the newest non-draft prerelease that
        carries a tarball for `arch`. */
    std::optional<UpdateCandidate> findBetaRelease (const juce::var& releasesJson,
                                                    const juce::String& arch);

    /** Full check flow, network-free given a Fetcher: queries the
        channel's release endpoint and returns the candidate for `arch`.
        `error` is filled on transport or parse failure (a repo with no
        release yet reports through `error`, not through an empty
        optional). */
    std::optional<UpdateCandidate> findUpdate (const Fetcher& fetch,
                                               const juce::String& apiBase,
                                               const juce::String& channel,
                                               const juce::String& arch,
                                               juce::String& error);

    juce::String sha256HexOfFile (const juce::File& f);

    /** First field of a .sha256 sidecar ("<hex>  <name>"), lowercase. */
    juce::String expectedShaFromSidecar (const juce::File& sidecar);

    /** Download tarball + .sha256 sidecar, verify the checksum, extract
        into `stagingDir`, return the extracted bin/OpenGuitarMultiFx
        (empty File + `error` on any failure; partial downloads never
        leave a usable-looking binary behind because extraction only runs
        after the checksum verifies). */
    juce::File stageFromUrls (const Fetcher& fetch, const UpdateCandidate& candidate,
                              const juce::File& stagingDir,
                              const std::function<void (double)>& progress,
                              juce::String& error);

    /** Extract the ogmfx tarball (`.tar.gz` per the packaging contract),
        return the extracted bin/OpenGuitarMultiFx inside `destDir`. */
    juce::File extractAppBinary (const juce::File& tarGz, const juce::File& destDir,
                                 juce::String& error);

    /** Desktop self-update: rename `exe` to "<name>.bak", then atomically
        rename `newBinary` (staged, checksum-verified) over `exe`.
        rename() is atomic and the running process keeps the old inode,
        so the swap is safe while the app is live; the .bak stays next to
        the exe as the manual-fallback copy. `error` filled on failure --
        the exe is left untouched whenever that happens. */
    bool stageBinarySwap (const juce::File& newBinary, const juce::File& exe,
                          juce::String& error);

} // namespace updatecore

} // namespace openguitarmultifx
