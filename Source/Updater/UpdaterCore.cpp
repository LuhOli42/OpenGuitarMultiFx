#include "UpdaterCore.h"

#include <juce_cryptography/juce_cryptography.h>

namespace openguitarmultifx
{

namespace updatecore
{

namespace
{

struct ParsedVersion
{
    juce::Array<int> parts;
    // dev (untagged "0.0.0-dev+sha") < pre (prerelease tag) < clean release
    // < ahead (git-describe "-N-g<sha>" post-tag build, contains the tag).
    int rank = 2;
    int aheadCount = 0;
    juce::String suffix;
};

ParsedVersion parseVersion (juce::String v)
{
    ParsedVersion out;
    v = v.trim();
    if (v.startsWithChar ('v') || v.startsWithChar ('V'))
        v = v.substring (1);

    const int cut = v.indexOfAnyOf ("-+");
    const juce::String base = cut < 0 ? v : v.substring (0, cut);
    juce::String suffix = cut < 0 ? juce::String() : v.substring (cut);

    for (auto& p : juce::StringArray::fromTokens (base, ".", ""))
        out.parts.add (p.getIntValue());
    if (out.parts.isEmpty())
        out.parts.add (0);

    if (suffix.isNotEmpty())
    {
        // A trailing -dirty (uncommitted changes on top of whatever the
        // describe string names) is always "past" its base, like the
        // -N-g<sha> ahead-of-tag suffix; strip it before classifying.
        suffix = suffix.upToLastOccurrenceOf ("-dirty", false, false);
        if (suffix.endsWithChar ('-'))
            suffix = suffix.dropLastCharacters (1);

        if (suffix.isEmpty())
        {
            out.rank = 3;                           // bare "-dirty" on a tagged build
        }
        else if (suffix.startsWith ("-dev") || suffix.startsWith ("+dev"))
        {
            out.rank = 0;
            out.suffix = suffix;
        }
        else if (suffix.startsWithChar ('-'))
        {
            const auto tokens = juce::StringArray::fromTokens (suffix.substring (1), "-", "");
            if (tokens.size() >= 2
                && tokens[0].containsOnly ("0123456789")
                && tokens[1].startsWithChar ('g'))
            {
                out.rank = 3;                       // -N-g<sha>
                out.aheadCount = tokens[0].getIntValue();
            }
            else
            {
                out.rank = 1;                       // prerelease, e.g. -beta.1
                out.suffix = suffix;
            }
        }
        else
        {
            out.rank = 1;
            out.suffix = suffix;
        }
    }
    return out;
}

int compareBase (const ParsedVersion& a, const ParsedVersion& b)
{
    const int n = juce::jmax (a.parts.size(), b.parts.size());
    for (int i = 0; i < n; ++i)
    {
        const int x = i < a.parts.size() ? a.parts[i] : 0;
        const int y = i < b.parts.size() ? b.parts[i] : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

// Extract {name, url, size} from a release's assets array for the asset
// whose name ends with `suffix` ("-aarch64.tar.gz" etc).
struct FoundAsset
{
    juce::String name, url;
    juce::int64 size = 0;
};

std::optional<FoundAsset> findAssetEndingIn (const juce::var& assets, const juce::String& suffix)
{
    const auto* arr = assets.getArray();
    if (arr == nullptr)
        return std::nullopt;

    for (const auto& asset : *arr)
    {
        const auto name = asset["name"].toString();
        if (name.endsWith (suffix))
            return FoundAsset { name,
                                asset["browser_download_url"].toString(),
                                (juce::int64) asset["size"] };
    }
    return std::nullopt;
}

} // namespace

int compareVersions (const juce::String& a, const juce::String& b)
{
    const auto va = parseVersion (a);
    const auto vb = parseVersion (b);

    const int base = compareBase (va, vb);
    if (base != 0)
        return base;

    if (va.rank != vb.rank)
        return va.rank < vb.rank ? -1 : 1;

    switch (va.rank)
    {
        case 3:  return va.aheadCount == vb.aheadCount ? 0
                     : (va.aheadCount < vb.aheadCount ? -1 : 1);
        case 1:  return va.suffix.compare (vb.suffix);
        default: return 0;
    }
}

juce::String archName()
{
#if defined (__x86_64__) || defined (_M_X64)
    return "x86_64";
#elif defined (__aarch64__) || defined (_M_ARM64)
    return "aarch64";
#else
    return {};
#endif
}

std::optional<UpdateCandidate> parseLatestRelease (const juce::var& releaseJson,
                                                   const juce::String& arch)
{
    const auto* obj = releaseJson.getDynamicObject();
    if (obj == nullptr)
        return std::nullopt;

    const auto tarballSuffix = "-" + arch + ".tar.gz";
    const auto tarball = findAssetEndingIn (obj->getProperty ("assets"), tarballSuffix);
    if (! tarball || tarball->url.isEmpty())
        return std::nullopt;

    auto sidecar = findAssetEndingIn (obj->getProperty ("assets"), tarballSuffix + ".sha256");
    if (! sidecar)
        return std::nullopt;

    UpdateCandidate out;
    // Version comes from the tarball name itself (ogmfx-<version>-<arch>),
    // not tag_name: package.sh derives it from `git describe`, which the
    // tag only equals when the tag is exactly the HEAD being packaged.
    out.version = tarball->name.fromFirstOccurrenceOf ("ogmfx-", false, false)
                              .upToLastOccurrenceOf (tarballSuffix, false, false);
    if (out.version.isEmpty())
        out.version = obj->getProperty ("tag_name").toString();

    out.notes = obj->getProperty ("body").toString();
    out.tarballUrl = tarball->url;
    out.sha256Url = sidecar->url;
    out.tarballSize = tarball->size;
    return out;
}

std::optional<UpdateCandidate> findBetaRelease (const juce::var& releasesJson,
                                                const juce::String& arch)
{
    const auto* arr = releasesJson.getArray();
    if (arr == nullptr)
        return std::nullopt;

    // Newest-first per the GitHub API: the first non-draft prerelease
    // that actually ships a tarball for this arch wins.
    for (const auto& release : *arr)
    {
        if ((bool) release["draft"])
            continue;
        if (! (bool) release["prerelease"])
            continue;
        if (auto candidate = parseLatestRelease (release, arch))
            return candidate;
    }
    return std::nullopt;
}

std::optional<UpdateCandidate> findUpdate (const Fetcher& fetch,
                                           const juce::String& apiBase,
                                           const juce::String& channel,
                                           const juce::String& arch,
                                           juce::String& error)
{
    if (arch.isEmpty())
    {
        error = "unsupported machine architecture for OGMFX releases";
        return std::nullopt;
    }

    juce::String body;
    const auto base = apiBase.trimCharactersAtEnd ("/");

    if (channel == "beta")
    {
        if (! fetch.getText (base + "/releases?per_page=30", body))
        {
            error = "could not reach the update server";
            return std::nullopt;
        }
        auto json = juce::JSON::parse (body);
        auto candidate = findBetaRelease (json, arch);
        if (! candidate)
        {
            error = "no prerelease with a package for " + arch + " was found";
            return std::nullopt;
        }
        return candidate;
    }

    if (! fetch.getText (base + "/releases/latest", body))
    {
        error = "could not reach the update server";
        return std::nullopt;
    }

    auto json = juce::JSON::parse (body);
    if (auto candidate = parseLatestRelease (json, arch))
        return candidate;

    // GitHub error payloads carry {"message": "..."} -- surface it rather
    // than a generic "bad response" (covers "Not Found" when the repo has
    // no release yet).
    if (auto* obj = json.getDynamicObject())
        if (auto msg = obj->getProperty ("message").toString(); msg.isNotEmpty())
        {
            error = "update server said: " + msg;
            return std::nullopt;
        }

    error = "no release with a package for " + arch + " was found";
    return std::nullopt;
}

juce::String sha256HexOfFile (const juce::File& f)
{
    if (! f.existsAsFile())
        return {};
    return juce::SHA256 (f).toHexString();
}

juce::String expectedShaFromSidecar (const juce::File& sidecar)
{
    if (! sidecar.existsAsFile())
        return {};
    return sidecar.loadFileAsString().upToFirstOccurrenceOf (" ", false, false)
                  .trim().toLowerCase();
}

juce::File extractAppBinary (const juce::File& tarGz, const juce::File& destDir,
                             juce::String& error)
{
    if (! tarGz.existsAsFile())
    {
        error = "downloaded package is missing";
        return {};
    }
    if (! destDir.createDirectory())
    {
        error = "could not create " + destDir.getFullPathName();
        return {};
    }

#if JUCE_LINUX
    // Plain `tar` subprocess, same as ogmfx-update.sh: JUCE has a ZipFile
    // but no tar reader, and the release artifact is a .tar.gz.
    juce::ChildProcess proc;
    if (! proc.start ({ "tar", "-xzf", tarGz.getFullPathName(),
                        "-C", destDir.getFullPathName(), "--no-same-owner" }))
    {
        error = "could not run tar to extract the update package";
        return {};
    }
    proc.waitForProcessToFinish (60000);
    if (proc.getExitCode() != 0)
    {
        error = "tar failed extracting the update package";
        return {};
    }
#else
    error = "update extraction is only supported on Linux builds";
    return {};
#endif

    // Contract layout: bin/OpenGuitarMultiFx at the package root, possibly
    // wrapped in one top-level directory (same tolerance as the bash
    // updater's step 4).
    auto bin = destDir.getChildFile ("bin/OpenGuitarMultiFx");
    if (! bin.existsAsFile())
    {
        juce::Array<juce::File> subs;
        destDir.findChildFiles (subs, juce::File::findDirectories, false);
        if (subs.size() == 1)
            bin = subs[0].getChildFile ("bin/OpenGuitarMultiFx");
    }
    if (! bin.existsAsFile())
    {
        error = "package has no bin/OpenGuitarMultiFx";
        return {};
    }
    return bin;
}

juce::File stageFromUrls (const Fetcher& fetch, const UpdateCandidate& candidate,
                          const juce::File& stagingDir,
                          const std::function<void (double)>& progress,
                          juce::String& error)
{
    stagingDir.deleteRecursively();
    if (! stagingDir.createDirectory())
    {
        error = "could not create staging dir " + stagingDir.getFullPathName();
        return {};
    }

    const auto fname = candidate.tarballUrl.fromLastOccurrenceOf ("/", false, false);
    const auto tarball = stagingDir.getChildFile (fname);
    const auto sidecar = stagingDir.getChildFile (fname + ".sha256");

    if (progress) progress (0.02);
    if (! fetch.getFile (candidate.tarballUrl, tarball,
                         [&] (double p) { if (progress) progress (0.02 + p * 0.88); }))
    {
        error = "download failed";
        return {};
    }
    if (progress) progress (0.92);
    if (! fetch.getFile (candidate.sha256Url, sidecar, {}))
    {
        error = "could not download the checksum file";
        return {};
    }

    const auto expected = expectedShaFromSidecar (sidecar);
    const auto actual = sha256HexOfFile (tarball);
    if (expected.isEmpty() || actual.isEmpty() || expected != actual)
    {
        error = "checksum mismatch -- refusing to install";
        return {};
    }
    if (progress) progress (0.95);

    auto binary = extractAppBinary (tarball, stagingDir.getChildFile ("extracted"), error);
    if (progress) progress (1.0);
    return binary;
}

bool stageBinarySwap (const juce::File& newBinary, const juce::File& exe,
                      juce::String& error)
{
    if (! newBinary.existsAsFile())
    {
        error = "staged update binary is missing";
        return false;
    }
    if (! exe.existsAsFile())
    {
        error = "cannot find the running executable to replace";
        return false;
    }

    const auto dir = exe.getParentDirectory();
    const auto staged = dir.getChildFile (exe.getFileName() + ".new");
    const auto backup = dir.getChildFile (exe.getFileName() + ".bak");

    // Keep a rollback copy of the current binary, then land the new one
    // via rename(): atomic on POSIX, and the running process keeps the
    // old inode so the swap is safe while the app is live.
    staged.deleteFile();
    if (! newBinary.copyFileTo (staged))
    {
        error = "could not stage the update next to " + exe.getFullPathName()
              + " (check the directory is writable)";
        return false;
    }
    staged.setExecutePermission (true);

    backup.deleteFile();
    if (! exe.copyFileTo (backup))
    {
        staged.deleteFile();
        error = "could not keep a backup of the current version";
        return false;
    }

    if (! staged.moveFileTo (exe))
    {
        staged.deleteFile();
        backup.deleteFile();
        error = "could not replace " + exe.getFullPathName();
        return false;
    }
    exe.setExecutePermission (true);
    return true;
}

} // namespace updatecore

} // namespace openguitarmultifx
