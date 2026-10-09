#include <juce_core/juce_core.h>

// Each test file registers its juce::UnitTest classes through a static
// global instance (the pattern JUCE's own test framework expects). This
// main() just needs to trigger the runner.
// Usage: OpenGuitarMultiFx_Tests [--skip-category <cat>...] [name-substring]
//
//   (no args)                    run every registered suite (takes minutes)
//   <name-substring>             run only suites whose name contains it
//   --skip-category <cat>        skip all suites in a juce::UnitTest category;
//                                repeatable. Added for W2's CI: the wall-clock
//                                perf suites (categories "Bench" and "Probe")
//                                cannot hold their real-time bounds under
//                                qemu-aarch64 emulation, so the aarch64 CI job
//                                runs the suite with them skipped.
int main (int argc, char* argv[])
{
    juce::UnitTestRunner runner;
    runner.setPassesAreLogged (false);

    juce::StringArray skipCategories;
    juce::String nameFilter;

    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--skip-category" && i + 1 < argc)
            skipCategories.add (argv[++i]);
        else
            nameFilter = argv[i];
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
