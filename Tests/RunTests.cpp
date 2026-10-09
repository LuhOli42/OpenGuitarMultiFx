#include <juce_core/juce_core.h>

#include <algorithm>
#include <vector>

// Each test file registers its juce::UnitTest classes through a static
// global instance (the pattern JUCE's own test framework expects). This
// main() just needs to trigger the runner.
// Usage: OpenGuitarMultiFx_Tests [--skip-category <cat>...] [--shard i/n] [name-substring]
//
//   (no args)                    run every registered suite (takes minutes)
//   <name-substring>             run only suites whose name contains it
//   --skip-category <cat>        skip all suites in a juce::UnitTest category;
//                                repeatable. Added for W2's CI: the wall-clock
//                                perf suites (categories "Bench" and "Probe")
//                                cannot hold their real-time bounds under
//                                qemu-aarch64 emulation, so the aarch64 CI job
//                                runs the suite with them skipped.
//   --shard <i>/<n>              run only the i-th slice of n roughly-equal
//                                shards over the name-sorted suite list — the
//                                runner is serial, so CI parallelizes by
//                                launching one process per shard (ctest
//                                --parallel or parallel qemu invocations).
int main (int argc, char* argv[])
{
    juce::UnitTestRunner runner;
    runner.setPassesAreLogged (false);

    juce::StringArray skipCategories;
    juce::String nameFilter;
    int shardIndex = 0, numShards = 1;

    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--skip-category" && i + 1 < argc)
        {
            skipCategories.add (argv[++i]);
        }
        else if (juce::String (argv[i]) == "--shard" && i + 1 < argc)
        {
            const auto parts = juce::StringArray::fromTokens (juce::String (argv[++i]), "/", "");
            if (parts.size() == 2)
            {
                shardIndex = parts[0].getIntValue();
                numShards = parts[1].getIntValue();
            }
            else
            {
                numShards = 0; // malformed value: fail the range check below
            }
        }
        else
        {
            nameFilter = argv[i];
        }
    }

    if (shardIndex < 0 || numShards < 1 || shardIndex >= numShards)
    {
        juce::Logger::writeToLog ("Invalid --shard value (expected <i>/<n> with 0 <= i < n).");
        return 2;
    }

    juce::Array<juce::UnitTest*> selected;
    for (auto* test : juce::UnitTest::getAllTests())
    {
        if (skipCategories.contains (test->getCategory()))
            continue;
        if (nameFilter.isNotEmpty() && ! test->getName().containsIgnoreCase (nameFilter))
            continue;
        selected.add (test);
    }

    if (numShards > 1)
    {
        // Sort by name first: registration order is link-order dependent,
        // so a fixed ordering is the only way to keep a suite on the same
        // shard across builds.
        std::vector<juce::UnitTest*> sorted (selected.begin(), selected.end());
        std::sort (sorted.begin(), sorted.end(),
                   [] (const juce::UnitTest* a, const juce::UnitTest* b)
                   { return a->getName() < b->getName(); });
        selected.clearQuick();
        for (size_t i = (size_t) shardIndex; i < sorted.size(); i += (size_t) numShards)
            selected.add (sorted[i]);
    }

    runner.runTests (selected);

    int numFailures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        numFailures += runner.getResult (i)->failures;

    if (numFailures > 0)
    {
        juce::Logger::writeToLog (juce::String (numFailures) + " test failure(s).");
        return 1;
    }

    juce::Logger::writeToLog ("All tests passed.");
    return 0;
}
