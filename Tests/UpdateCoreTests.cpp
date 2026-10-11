#include "Updater/UpdaterCore.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class UpdateCoreTests : public juce::UnitTest
{
public:
    UpdateCoreTests() : juce::UnitTest ("UpdateCore", "Updater") {}

    void runTest() override
    {
        using updatecore::compareVersions;

        beginTest ("version comparison: base ordering, v prefix, missing parts");
        {
            expect (compareVersions ("v1.2.3", "v1.2.4") < 0);
            expect (compareVersions ("1.2.4", "v1.2.3") > 0);
            expect (compareVersions ("v1.2.3", "v1.2.3") == 0);
            expect (compareVersions ("v1.2", "v1.2.0") == 0);
            expect (compareVersions ("v2.0.0", "v1.9.9") > 0);
            expect (compareVersions ("v0.1.0", "v1.0.0") < 0);
        }

        beginTest ("version comparison: prerelease < clean < ahead-of-tag, dev lowest");
        {
            expect (compareVersions ("v1.2.3-beta.1", "v1.2.3") < 0);
            expect (compareVersions ("v1.2.3-beta.2", "v1.2.3-beta.1") > 0);
            // Semver-style suffix ordering: numeric identifiers compare
            // numerically (not lexically), more identifiers = later.
            expect (compareVersions ("v1.2.3-beta.10", "v1.2.3-beta.9") > 0);
            expect (compareVersions ("v1.2.3-beta", "v1.2.3-beta.1") < 0);
            expect (compareVersions ("v1.2.3-alpha", "v1.2.3-beta") < 0);
            // git describe "-N-g<sha>" already contains the tag -> newer.
            expect (compareVersions ("v1.2.3-4-gdeadbeef", "v1.2.3") > 0);
            expect (compareVersions ("v1.2.3-dirty", "v1.2.3") > 0);
            expect (compareVersions ("v1.2.3-4-gdeadbeef-dirty", "v1.2.3") > 0);
            expect (compareVersions ("v1.2.3-5-gabc", "v1.2.3-4-gabc") > 0);
            // ...but still older than the next release.
            expect (compareVersions ("v1.2.3-4-gdeadbeef", "v1.2.4") < 0);
            // Untagged dev builds sort below everything real.
            expect (compareVersions ("0.0.0-dev+abc1234", "v0.0.1") < 0);
            expect (compareVersions ("0.0.0-dev+abc1234", "0.0.0-dev+def5678") == 0);
        }

        beginTest ("archName returns a contract arch");
        {
            const auto arch = updatecore::archName();
            expect (arch == "x86_64" || arch == "aarch64");
        }

        const auto releaseJson = R"({
            "tag_name": "v1.4.0",
            "draft": false,
            "prerelease": false,
            "body": "release notes",
            "assets": [
                { "name": "ogmfx-v1.4.0-aarch64.tar.gz",
                  "browser_download_url": "https://x/dl/ogmfx-v1.4.0-aarch64.tar.gz",
                  "size": 111 },
                { "name": "ogmfx-v1.4.0-aarch64.tar.gz.sha256",
                  "browser_download_url": "https://x/dl/ogmfx-v1.4.0-aarch64.tar.gz.sha256" },
                { "name": "ogmfx-v1.4.0-x86_64.tar.gz",
                  "browser_download_url": "https://x/dl/ogmfx-v1.4.0-x86_64.tar.gz",
                  "size": 222 },
                { "name": "ogmfx-v1.4.0-x86_64.tar.gz.sha256",
                  "browser_download_url": "https://x/dl/ogmfx-v1.4.0-x86_64.tar.gz.sha256" },
                { "name": "latest.json",
                  "browser_download_url": "https://x/dl/latest.json" }
            ]
        })";

        beginTest ("parseLatestRelease picks the arch-matched tarball and its sidecar");
        {
            const auto parsed = juce::JSON::parse (releaseJson);
            const auto cand = updatecore::parseLatestRelease (parsed, "x86_64");
            expect (cand.has_value());
            expectEquals (cand->version, juce::String ("v1.4.0"));
            expect (cand->tarballUrl.endsWith ("x86_64.tar.gz"));
            expect (cand->sha256Url.endsWith ("x86_64.tar.gz.sha256"));
            expectEquals (cand->tarballSize, (juce::int64) 222);

            const auto candArm = updatecore::parseLatestRelease (parsed, "aarch64");
            expect (candArm.has_value());
            expect (candArm->tarballUrl.endsWith ("aarch64.tar.gz"));
        }

        beginTest ("parseLatestRelease: no matching arch asset -> nullopt, bad json -> nullopt");
        {
            const auto parsed = juce::JSON::parse (releaseJson);
            expect (! updatecore::parseLatestRelease (parsed, "riscv64").has_value());
            expect (! updatecore::parseLatestRelease (juce::var(), "x86_64").has_value());
            // Missing the .sha256 sidecar -> nullopt (checksum is mandatory).
            const auto noSidecar = juce::JSON::parse (R"({
                "tag_name": "v9.9.9",
                "assets": [ { "name": "ogmfx-v9.9.9-x86_64.tar.gz",
                              "browser_download_url": "https://x/t.gz" } ]
            })");
            expect (! updatecore::parseLatestRelease (noSidecar, "x86_64").has_value());
        }

        beginTest ("findBetaRelease skips drafts and non-prereleases, newest-first");
        {
            const auto releasesJson = juce::JSON::parse (R"([
                { "tag_name": "v2.0.0-rc.1", "draft": true, "prerelease": true,
                  "assets": [ { "name": "ogmfx-v2.0.0-rc.1-x86_64.tar.gz",
                                "browser_download_url": "https://x/draft.gz" } ] },
                { "tag_name": "v1.9.0-beta.1", "draft": false, "prerelease": true,
                  "assets": [
                      { "name": "ogmfx-v1.9.0-beta.1-x86_64.tar.gz",
                        "browser_download_url": "https://x/beta.gz" },
                      { "name": "ogmfx-v1.9.0-beta.1-x86_64.tar.gz.sha256",
                        "browser_download_url": "https://x/beta.gz.sha256" }
                  ] },
                { "tag_name": "v1.8.0", "draft": false, "prerelease": false,
                  "assets": [ { "name": "ogmfx-v1.8.0-x86_64.tar.gz",
                                "browser_download_url": "https://x/stable.gz" } ] }
            ])");
            const auto cand = updatecore::findBetaRelease (releasesJson, "x86_64");
            expect (cand.has_value());
            expectEquals (cand->version, juce::String ("v1.9.0-beta.1"));
            expect (cand->tarballUrl.endsWith ("beta.gz"));
        }

        beginTest ("findUpdate drives the check flow through an injected fetcher");
        {
            updatecore::Fetcher fake;
            fake.getText = [&] (const juce::String& url, juce::String& body)
            {
                if (url.endsWith ("/releases/latest"))
                {
                    body = releaseJson;
                    return true;
                }
                return false;
            };

            juce::String error;
            const auto cand = updatecore::findUpdate (fake, "https://api.test/repos/o/r",
                                                      "stable", "x86_64", error);
            expect (cand.has_value());
            expectEquals (cand->version, juce::String ("v1.4.0"));

            fake.getText = [] (const juce::String&, juce::String&) { return false; };
            error.clear();
            expect (! updatecore::findUpdate (fake, "https://api.test", "stable",
                                              "x86_64", error).has_value());
            expect (error.isNotEmpty());
        }

        const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("OGMFxUpdateCoreTests_"
                                                + juce::String (juce::Random::getSystemRandom().nextInt64()));

        beginTest ("stageFromUrls downloads, verifies the checksum, extracts bin/");
        {
            updatecore::Fetcher fake;
            fake.getText = [] (const juce::String&, juce::String&) { return false; };

            // Build a real tarball through the packaging contract layout.
            const auto tarDir = tempDir.getChildFile ("pkgroot");
            tarDir.getChildFile ("bin").createDirectory();
            const auto appBin = tarDir.getChildFile ("bin/OpenGuitarMultiFx");
            appBin.replaceWithData ("#!/bin/fake-binary\n", 18);
            tarDir.getChildFile ("VERSION").replaceWithText ("v9.9.9-test\n");
            // Decoy member: extraction must pull ONLY bin/OpenGuitarMultiFx.
            tarDir.getChildFile ("bin/evil.sh").replaceWithText ("#!/bin/sh\n");

            const auto tarball = tempDir.getChildFile ("ogmfx-v9.9.9-test-x86_64.tar.gz");
            juce::ChildProcess tar;
            expect (tar.start ({ "tar", "-czf", tarball.getFullPathName(),
                                "-C", tarDir.getFullPathName(), "." }));
            tar.waitForProcessToFinish (30000);
            expectEquals ((int) tar.getExitCode(), 0);

            // Sidecar the same way package.sh writes it.
            const auto sha = updatecore::sha256HexOfFile (tarball);
            tempDir.getChildFile (tarball.getFileName() + ".sha256")
                   .replaceWithText (sha + "  " + tarball.getFileName() + "\n");

            fake.getFile = [&] (const juce::String& url, const juce::File& dest,
                                const std::function<void (double)>& progress)
            {
                // The fetch contract is URL -> file; the fake maps URL
                // suffixes onto the canned files above.
                const juce::File src = url.endsWith (".sha256")
                                         ? tempDir.getChildFile (tarball.getFileName() + ".sha256")
                                         : tarball;
                if (progress)
                    progress (1.0);
                return src.copyFileTo (dest);
            };

            updatecore::UpdateCandidate cand;
            cand.version = "v9.9.9-test";
            cand.tarballUrl = "https://fake/ogmfx-v9.9.9-test-x86_64.tar.gz";
            cand.sha256Url = "https://fake/ogmfx-v9.9.9-test-x86_64.tar.gz.sha256";

            juce::String error;
            double lastProgress = 0.0;
            const auto binary = updatecore::stageFromUrls (fake, cand,
                                                         tempDir.getChildFile ("staging"),
                                                         [&] (double p) { lastProgress = p; },
                                                         error);
            expect (binary.existsAsFile(), error);
            expect (binary.getFullPathName().contains ("bin/OpenGuitarMultiFx"));
            expect (binary.loadFileAsString().startsWith ("#!/bin/fake-binary"));
            expect (lastProgress > 0.0);
            // Nothing else from the archive may land in the staging dir.
            const auto extracted = tempDir.getChildFile ("staging/extracted");
            expect (! extracted.getChildFile ("VERSION").exists());
            expect (! extracted.getChildFile ("bin/evil.sh").exists());

            // Corrupt the sidecar -> checksum gate must refuse the install.
            tempDir.getChildFile (tarball.getFileName() + ".sha256")
                   .replaceWithText ("0000000000000000000000000000000000000000000000000000000000000000  x\n");
            error.clear();
            expect (! updatecore::stageFromUrls (fake, cand,
                                               tempDir.getChildFile ("staging2"), {}, error)
                         .existsAsFile());
            expect (error.contains ("checksum"));
        }

        beginTest ("stageBinarySwap atomically replaces the exe and keeps a .bak");
        {
            const auto dir = tempDir.getChildFile ("swapdir");
            dir.createDirectory();
            const auto exe = dir.getChildFile ("OpenGuitarMultiFx");
            exe.replaceWithData ("old-binary", 10);
            const auto staged = tempDir.getChildFile ("newbin");
            staged.replaceWithData ("new-binary", 10);

            juce::String error;
            expect (updatecore::stageBinarySwap (staged, exe, error), error);
            expectEquals (exe.loadFileAsString(), juce::String ("new-binary"));
            expectEquals (dir.getChildFile ("OpenGuitarMultiFx.bak").loadFileAsString(),
                          juce::String ("old-binary"));
            expect (! dir.getChildFile ("OpenGuitarMultiFx.new").existsAsFile());
            expect (exe.hasWriteAccess());

            // Failure path: missing staged binary -> exe untouched.
            error.clear();
            expect (! updatecore::stageBinarySwap (dir.getChildFile ("nope"), exe, error));
            expectEquals (exe.loadFileAsString(), juce::String ("new-binary"));
        }

        tempDir.deleteRecursively();
    }
};

static UpdateCoreTests updateCoreTests;

} // namespace openguitarmultifx
